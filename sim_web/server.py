#!/usr/bin/env python3
"""
Servidor Web local (100% offline, solo stdlib de Python) para lilyxm7.

Incluye:
- Ejecuta el firmware lilyxm7/ en DLLs nativas compiladas con g++/clang++.
- Simulador termico realista del motor / ECU OBD-II de la Suzuki Grand Vitara 2008 V6 2.7L
  (se calienta poco a poco desde frio ~28 °C hasta temperatura de operacion ~89-92 °C,
  con escenarios de manejo, ignicion ON/OFF, falla de termostato y desconexion OBD-II).
- Logs estructurados POR COMPONENTE (ECU_OBD, DATA, CHAT, BTN, UI, BUILD) enviados en vivo
  al frontend y guardados en C:\\Git\\lilyxm7\\logs\\sim_web.log y logs\\all.log.
"""

from __future__ import annotations

import ctypes
import hashlib
import datetime as dt
import errno
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import uuid
import webbrowser
from collections import deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SIM_WEB_DIR = Path(__file__).resolve().parent
NATIVE_DIR = SIM_WEB_DIR / "native"
BUILD_DIR = SIM_WEB_DIR / "build"
BUILD_MANIFEST = BUILD_DIR / "native-build.json"
BUILD_RECIPE = Path(__file__).read_bytes()

sys.path.insert(0, str(PROJECT_ROOT))
from tools.run_logged import log_line

LOG_COMPONENT = "sim_web"
HOST = "127.0.0.1"
PORT = 8765

# Parse the literal GPIO constants used by the firmware.
PIN_CONFIG = PROJECT_ROOT / "lilyxm7" / "config.h"
PIN_PATTERN = re.compile(r"^constexpr\s+int\s+(PIN_\w+)\s*=\s*(\d+)\s*;", re.MULTILINE)


def read_pins() -> dict[str, int]:
    return {name: int(number) for name, number in PIN_PATTERN.findall(PIN_CONFIG.read_text(encoding="utf-8"))}


SERIAL_TAG_RE = re.compile(r"^\[LOG\]\[([A-Z0-9_]+)\]\s*(.*)$")


def find_cpp_compiler() -> str | None:
    """Busca g++ o clang++ en PATH y rutas comunes de WinLibs/MinGW/LLVM en Windows."""
    for name in ("g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found

    candidates: list[Path] = [
        BUILD_DIR / "w64devkit" / "bin" / "g++.exe",
        Path(r"C:\msys64\ucrt64\bin\g++.exe"),
        Path(r"C:\msys64\mingw64\bin\g++.exe"),
        Path(r"C:\mingw64\bin\g++.exe"),
        Path(r"C:\Program Files\LLVM\bin\clang++.exe"),
    ]
    local_app = os.environ.get("LOCALAPPDATA", "")
    if local_app:
        winget_pkgs = Path(local_app) / "Microsoft" / "WinGet" / "Packages"
        if winget_pkgs.exists():
            for match in winget_pkgs.glob("*WinLibs*/*/bin/g++.exe"):
                candidates.insert(0, match)
            for match in winget_pkgs.glob("*WinLibs*/*/*/bin/g++.exe"):
                candidates.insert(0, match)
            for match in winget_pkgs.glob("*MinGW*/*/bin/g++.exe"):
                candidates.insert(0, match)

    for cand in candidates:
        if cand.is_file():
            return str(cand)
    return None


DTC_SCENARIOS = {
    "none": (),
    "misfire": (0x0300,),
    "multiple": (0x0300, 0x0118, 0x0420),
}


def format_dtc(value: int) -> str:
    return f"{'PCBU'[value >> 14]}{value & 0x3FFF:04X}"


class VitaraEngineSim:
    """Modelo termico gradual de la Suzuki Grand Vitara 2008 V6 2.7 L (motor N27A)."""

    PROFILES = {
        "idle":    {"label": "Ralentí (Idle)",       "target": 89.0,  "rate": 1.1, "rpm": 750},
        "city":    {"label": "Ciudad",               "target": 93.0,  "rate": 1.8, "rpm": 2100},
        "highway": {"label": "Carretera / Subida",   "target": 98.0,  "rate": 2.6, "rpm": 3400},
        "fault":   {"label": "Falla termostato",     "target": 116.0, "rate": 3.4, "rpm": 2600},
    }

    def __init__(self) -> None:
        self.speed_kph = 0.0
        self.speed_target = 0
        self.dtc_scenario = "none"
        self.ambient_c = 26.0
        self.coolant_c = 28.0
        self.ignition = "RUNNING"  # OFF | ACC_ON | RUNNING
        self.profile = "idle"
        self.obd_connected = True
        self.auto_thermal = True
        self.relay_mode = "auto"
        self.relay_low = False
        self.relay_high = (False, False)
        self.thermostat_open = False
        self._last_ts = time.monotonic()
        self._last_logged_int = 28

    def reset_cold(self) -> None:
        self.coolant_c = 28.0
        self.relay_mode = "auto"
        self.relay_low = False
        self.relay_high = (False, False)
        self.thermostat_open = False
        self._last_logged_int = 28

    def is_ecu_responding(self) -> bool:
        # En la Vitara 2008 la ECU responde con ignicion en ON o motor encendido + cable conectado
        return self.obd_connected and self.ignition in ("ACC_ON", "RUNNING")

    def step(self) -> list[tuple[str, str]]:
        """Avanza la fisica termica y devuelve eventos importantes [(level, msg), ...]."""
        now = time.monotonic()
        dt_s = max(0.0, min(0.5, now - self._last_ts))
        self._last_ts = now
        events: list[tuple[str, str]] = []
        target = self.speed_target if self.ignition == "RUNNING" else 0
        delta = target - self.speed_kph
        self.speed_kph += max(-16 * dt_s, min(12 * dt_s, delta))

        # The thermostat fault models a valve stuck closed; the fans can still spin.
        if self.profile == "fault":
            self.thermostat_open = False
        elif self.coolant_c >= 82.0 and not self.thermostat_open:
            self.thermostat_open = True
            events.append(("INFO", "Termostato abierto (~82 °C)"))
        elif self.coolant_c <= 76.0:
            self.thermostat_open = False

        before = self.relay_states()
        if self.ignition == "OFF":
            self.relay_low = False
            self.relay_high = (False, False)
        elif self.relay_mode == "auto":
            self.relay_low = self.coolant_c >= (92 if self.relay_low else 95)
            self.relay_high = (
                self.coolant_c >= (97 if self.relay_high[0] else 100),
                self.coolant_c >= (102 if self.relay_high[1] else 105),
            )
        else:
            self.relay_low = self.relay_mode == "low"
            self.relay_high = (
                self.relay_mode in ("high1", "both"),
                self.relay_mode in ("high2", "both"),
            )

        for label, was_on, is_on in zip(
            ("N.º 1 · alimentación", "N.º 3 · conmutación", "N.º 2 · alta ventilador 2"),
            before, self.relay_states(),
        ):
            if was_on != is_on:
                events.append(("INFO", f"Relé {label}: {'ACTIVO' if is_on else 'ABIERTO'}"))

        if self.auto_thermal:
            if self.ignition == "RUNNING":
                profile = self.PROFILES[self.profile]
                gap = profile["target"] - self.coolant_c
                if gap > 0:
                    rate = profile["rate"] * (1.0 if gap > 10.0 else 0.45)
                    natural = self.coolant_c + min(gap, rate * dt_s)
                else:
                    natural = self.coolant_c + max(gap, -1.4 * dt_s)
            else:
                natural = max(self.ambient_c, self.coolant_c - 1.2 * dt_s)
            airflow = sum({"off": 0.0, "low": 0.85, "high": 2.3}[speed] for speed in self.fan_speeds())
            # Below the thermostat threshold the radiator has little effect on coolant.
            circulation = 1.0 if self.thermostat_open else 0.12
            self.coolant_c = max(self.ambient_c, natural - airflow * circulation * dt_s)

        cur_int = int(round(self.coolant_c))
        if abs(cur_int - self._last_logged_int) >= 5:
            self._last_logged_int = cur_int
            events.append(("INFO", f"Motor Vitara 2.7L -> refrigerante={self.coolant_c:.1f} °C ({self.ignition}/{self.profile})"))

        return events

    def relay_states(self) -> tuple[bool, bool, bool]:
        high1, high2 = self.relay_high
        return self.relay_low or high1, high1, high2  # OEM: relays 1, 3, 2

    def fan_speeds(self) -> tuple[str, str]:
        feed1, changeover, feed2 = self.relay_states()
        in_series = feed1 and not changeover and not feed2
        return (
            "high" if feed1 and changeover else "low" if in_series else "off",
            "high" if feed2 else "low" if in_series else "off",
        )

    def to_dict(self) -> dict[str, Any]:
        prof = self.PROFILES.get(self.profile, self.PROFILES["idle"])
        return {
            "coolantC": round(self.coolant_c, 1),
            "speedTarget": self.speed_target,
            "dtcScenario": self.dtc_scenario,
            "coolantInt": int(round(self.coolant_c)),
            "ignition": self.ignition,
            "profile": self.profile,
            "profileLabel": prof["label"],
            "rpm": prof["rpm"] if self.ignition == "RUNNING" else 0,
            "obdConnected": self.obd_connected,
            "autoThermal": self.auto_thermal,
            "ecuResponding": self.is_ecu_responding(),
            "thermostatOpen": self.thermostat_open,
            "fanOn": any(speed != "off" for speed in self.fan_speeds()),
            "fanSpeeds": self.fan_speeds(),
            "relayMode": self.relay_mode,
            "relays": dict(zip(("r1", "r3", "r2"), self.relay_states())),
        }


class NativeDllWrapper:
    """Carga y controla una DLL compilada desde sim_bridge.cpp + lilyxm7/."""

    def __init__(self, dll_path: Path) -> None:
        self.dll_path = dll_path
        self.lib = ctypes.CDLL(str(dll_path))
        self.lib.sim_init.argtypes = []
        self.lib.sim_tick.argtypes = [ctypes.c_uint32]
        self.lib.sim_set_button.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.sim_set_temp_c.argtypes = [ctypes.c_int]
        self.lib.sim_set_signal.argtypes = [ctypes.c_int]
        self.lib.sim_trigger_bus_off.argtypes = []
        self.lib.sim_get_framebuffer_rgb.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
        self.lib.sim_get_framebuffer_rgb.restype = ctypes.c_int
        self.lib.sim_pop_serial.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self.lib.sim_pop_serial.restype = ctypes.c_int
        self.lib.sim_pop_can_log.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self.lib.sim_pop_can_log.restype = ctypes.c_int
        self.lib.sim_get_gpio_mask.argtypes = []
        self.lib.sim_get_gpio_mask.restype = ctypes.c_uint32
        self.lib.sim_get_eye.argtypes = []
        self.lib.sim_get_eye.restype = ctypes.c_int
        self.lib.sim_get_board_snapshot.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        self.lib.sim_get_board_snapshot.restype = None
        self._board_status = (ctypes.c_uint32 * 3)()
        self.lib.sim_set_ecu_telemetry.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_uint16), ctypes.c_int]
        self.lib.sim_set_ecu_telemetry.restype = None
        self.lib.sim_read_vehicle_telemetry.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_int]
        self.lib.sim_read_vehicle_telemetry.restype = ctypes.c_int
        self._telemetry = (ctypes.c_int * 22)()

        self._fb_buf = (ctypes.c_uint8 * (320 * 240 * 3))()
        self._text_buf = ctypes.create_string_buffer(8192)

    def init(self) -> None:
        self.lib.sim_init()

    def tick(self, dt_ms: int) -> None:
        self.lib.sim_tick(dt_ms)

    def set_button(self, pin: int, pressed: bool) -> None:
        self.lib.sim_set_button(pin, 1 if pressed else 0)

    def set_temp_c(self, temp_c: int) -> None:
        self.lib.sim_set_temp_c(temp_c)

    def set_signal(self, enabled: bool) -> None:
        self.lib.sim_set_signal(1 if enabled else 0)

    def trigger_bus_off(self) -> None:
        self.lib.sim_trigger_bus_off()

    def get_frame_bytes(self) -> bytes:
        n = self.lib.sim_get_framebuffer_rgb(self._fb_buf, len(self._fb_buf))
        return bytes(self._fb_buf[:n])

    def pop_serial(self) -> str:
        n = self.lib.sim_pop_serial(self._text_buf, len(self._text_buf))
        if n <= 0:
            return ""
        return self._text_buf.raw[:n].decode("utf-8", errors="replace")

    def pop_can_log(self) -> str:
        n = self.lib.sim_pop_can_log(self._text_buf, len(self._text_buf))
        if n <= 0:
            return ""
        return self._text_buf.raw[:n].decode("utf-8", errors="replace")

    def set_ecu_telemetry(self, speed: int, codes: tuple[int, ...]) -> None:
        payload = (ctypes.c_uint16 * len(codes))(*codes)
        self.lib.sim_set_ecu_telemetry(speed, payload, len(codes))

    def telemetry(self) -> dict[str, Any]:
        size = self.lib.sim_read_vehicle_telemetry(self._telemetry, len(self._telemetry))
        speed, acceleration, mil, reported, total, bus = self._telemetry[:6]
        return {
            "speedKph": speed if speed >= 0 else None,
            "accelMs2": acceleration / 10 if acceleration != -32768 else None,
            "mil": bool(mil) if mil >= 0 else None,
            "reportedDtcCount": reported if reported >= 0 else None,
            "storedDtcCount": total if total >= 0 else None,
            "codes": [format_dtc(raw) for raw in self._telemetry[6:size]],
            "busRunning": bus == 1,
            "busRecovering": bus == 3,
        }

    def is_eye_on(self) -> bool:
        return bool(self.lib.sim_get_eye())

    def board_status(self) -> dict[str, Any]:
        self.lib.sim_get_board_snapshot(self._board_status)
        return {
            "uptimeMs": self._board_status[0],
            "screen": self._board_status[1],
            "liveCoolant": bool(self._board_status[2]),
        }

    def get_gpio_mask(self) -> int:
        return int(self.lib.sim_get_gpio_mask())


def firmware_signature(compiler: str | None) -> str:
    digest = hashlib.sha256()
    digest.update(BUILD_RECIPE)  # compile command and ABI of this server process
    if compiler:
        compiler_path = Path(compiler).resolve()
        stat = compiler_path.stat()
        digest.update(f"{compiler_path}:{stat.st_size}:{stat.st_mtime_ns}".encode())
    for root in (PROJECT_ROOT / "lilyxm7", NATIVE_DIR):
        for path in sorted(root.rglob("*")):
            if path.suffix.lower() in {".ino", ".cpp", ".c", ".h", ".hpp", ".inc", ".s"}:
                digest.update(str(path.relative_to(PROJECT_ROOT)).encode())
                digest.update(b"\x00")
                digest.update(path.read_bytes())
                digest.update(b"\x00")
    return digest.hexdigest()


class SimManager:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.mode = "obd"  # Arranca listo con el simulador OBD-II de la Vitara 2008
        self.engine_sim = VitaraEngineSim()
        self.compiler = find_cpp_compiler()
        self.build_error: str | None = None
        self.dll: NativeDllWrapper | None = None
        self.dll_paths: dict[str, Path] = {}
        self.last_attempt: str | None = None
        self.loaded_signature: str | None = None
        self.dll_search_dir = None
        self.pins = read_pins()
        self.button_queue: deque[int] = deque()
        self.pressed_pin: int | None = None
        self.next_button_ms = 0
        self.last_tick_ms = 0
        self.frame_bytes: bytes = b""
        self.frame_revision = 0
        self.gpio_mask: int = 0
        self.log_seq = 0
        self.logs: list[dict[str, Any]] = []
        self.chat_feed: list[dict[str, Any]] = []
        self.serial_rows: deque[dict[str, Any]] = deque(maxlen=240)
        self.serial_pending = ""
        self.serial_seq = 0
        self.serial_epoch = uuid.uuid4().hex
        self._running = True

        self._restore_cached_build()
        self.compile_and_load(force=False)
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def emit_log(self, comp: str, level: str, msg: str, source: str = "MODEL") -> None:
        ts = dt.datetime.now().strftime("%H:%M:%S")
        self.log_seq += 1
        entry = {"id": self.log_seq, "ts": ts, "comp": comp, "level": level, "msg": msg, "source": "BUILD" if comp == "BUILD" else source}
        self.logs.append(entry)
        if len(self.logs) > 400:
            self.logs = self.logs[-400:]

        if comp == "CHAT" and source == "FIRMWARE":
            clean = msg[6:].strip() if msg.startswith("LILY:") else msg
            self.chat_feed.append({"ts": ts, "text": clean, "isAlert": level == "WARN"})
            if len(self.chat_feed) > 30:
                self.chat_feed = self.chat_feed[-30:]

        log_line(LOG_COMPONENT, f"[{entry['source']}][{comp}] {msg}", level=level)

    def _ingest_serial_chunk(self, chunk: str) -> None:
        if not chunk:
            return
        lines = (self.serial_pending + chunk).split("\n")
        self.serial_pending = lines.pop()
        for raw in lines:
            raw = raw.removesuffix("\r")
            self.serial_seq += 1
            self.serial_rows.append({"id": self.serial_seq, "ts": dt.datetime.now().strftime("%H:%M:%S"), "line": raw})
            log_line("firmware_serial", raw, level="OUT")
            if "--serial-console" in sys.argv:
                print("SERIAL | " + raw, flush=True)
            line = raw.strip()
            if not line:
                continue
            m = SERIAL_TAG_RE.match(line)
            if m:
                tag, rest = m.group(1), m.group(2)
                comp_map = {
                    "OBD": "ECU_OBD",
                    "DATA": "DATA",
                    "CHAT": "CHAT",
                    "BTN": "BTN",
                    "UI": "UI",
                }
                comp = comp_map.get(tag, "DATA")
                lvl = "WARN" if ("SIN DATOS" in rest.upper() or "ERROR" in rest.upper() or "ALERTA" in rest.upper() or "OJO" in rest.upper() or "BUS_OFF" in rest.upper()) else "INFO"
                self.emit_log(comp, lvl, rest, source="FIRMWARE")
            else:
                self.emit_log("UI", "INFO", line, source="FIRMWARE")

    def _ingest_can_chunk(self, chunk: str) -> None:
        for raw in chunk.splitlines():
            line = raw.strip()
            if line:
                level = "WARN" if "BUS_OFF" in line or "Fallo" in line else "INFO"
                self.emit_log("ECU_OBD", level, line, source="BUS")

    def serial_snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {"epoch": self.serial_epoch, "sequence": self.serial_seq, "lines": list(self.serial_rows)}

    def _capture_board_locked(self) -> None:
        frame = self.dll.get_frame_bytes()
        if len(frame) == 320 * 240 * 3 and frame != self.frame_bytes:
            self.frame_bytes = frame
            self.frame_revision += 1
        self.gpio_mask = self.dll.get_gpio_mask()
        self._ingest_serial_chunk(self.dll.pop_serial())
        self._ingest_can_chunk(self.dll.pop_can_log())

    def _configure_dll_search(self, compiler: str | None) -> None:
        if compiler and hasattr(os, "add_dll_directory"):
            try:
                directory = os.add_dll_directory(str(Path(compiler).resolve().parent))
            except OSError:
                return
            if self.dll_search_dir is not None:
                self.dll_search_dir.close()
            self.dll_search_dir = directory

    def _restore_cached_build(self) -> None:
        try:
            manifest = json.loads(BUILD_MANIFEST.read_text(encoding="utf-8"))
            paths = {mode: BUILD_DIR / Path(manifest["files"][mode]).name for mode in ("sim", "obd")}
            pins = manifest["pins"]
            signature = manifest["signature"]
            if not isinstance(pins, dict) or not isinstance(signature, str):
                return
            self._configure_dll_search(self.compiler)
            for path in paths.values():
                NativeDllWrapper(path)
        except (OSError, AttributeError, TypeError, KeyError, ValueError):
            return
        self.dll_paths = paths
        self.pins = pins
        self.loaded_signature = signature
        self._load_mode_locked(self.mode)
        self.emit_log("BUILD", "INFO", "Firmware nativo cargado de la última compilación válida.")

    def _build_failed_locked(self, message: str) -> tuple[bool, str]:
        self.build_error = message
        self.emit_log("BUILD", "ERROR", message)
        if self.dll is None:
            self._load_mode_locked(self.mode)
        return False, message

    def watch_firmware(self) -> None:
        while self._running:
            time.sleep(1.0)
            self.compile_and_load(force=False)

    def compile_and_load(self, *, force: bool = True) -> tuple[bool, str]:
        with self.lock:
            compiler = find_cpp_compiler()
            try:
                signature = firmware_signature(compiler)
                if not force and signature == self.last_attempt:
                    return self.build_error is None, self.build_error or "Firmware sin cambios."
                self.last_attempt = signature
                self.compiler = compiler
                self.build_error = None
                if not force and signature == self.loaded_signature:
                    return True, "Firmware sin cambios."
                pins = read_pins()
                if not compiler:
                    if self.dll is None:
                        self.pins = pins
                    return self._build_failed_locked("No se encontró g++/clang++; instala el compilador con la opción [4].")

                self._configure_dll_search(compiler)
                BUILD_DIR.mkdir(parents=True, exist_ok=True)
                build_id = uuid.uuid4().hex[:16]
                targets = [
                    ("sim", 1, BUILD_DIR / f"lilyxm7_sim_{build_id}.dll"),
                    ("obd", 0, BUILD_DIR / f"lilyxm7_obd_{build_id}.dll"),
                ]
                new_paths = {}
                for key, sim_flag, out_dll in targets:
                    cmd = [
                        compiler,
                        "-shared",
                        "-std=c++17",
                        "-O2",
                        "-static-libgcc",
                        "-static-libstdc++",
                        f"-DLILYXM7_SIMULATION_MODE={sim_flag}",
                        f"-I{NATIVE_DIR}",
                        f"-I{PROJECT_ROOT / 'lilyxm7'}",
                        str(NATIVE_DIR / "sim_bridge.cpp"),
                        "-o",
                        str(out_dll),
                    ]
                    self.emit_log("BUILD", "INFO", f"Compilando firmware para el simulador ({key})...")
                    result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
                    for line in result.stderr.splitlines():
                        self.emit_log("BUILD", "ERROR" if result.returncode else "WARN", line)
                    if result.returncode:
                        return self._build_failed_locked(f"Error compilando modo {key} (exit={result.returncode}).")
                    NativeDllWrapper(out_dll)
                    new_paths[key] = out_dll

                if firmware_signature(compiler) != signature:
                    self.last_attempt = None
                    return self._build_failed_locked("El código cambió durante la compilación. Se reintentará con los archivos nuevos.")
            except (OSError, AttributeError) as exc:
                return self._build_failed_locked(f"Error de compilación o carga: {exc}")

            self.dll_paths = new_paths
            self.pins = pins
            self.loaded_signature = signature
            self._load_mode_locked(self.mode)
            manifest = {
                "signature": signature,
                "pins": pins,
                "files": {mode: path.name for mode, path in new_paths.items()},
            }
            try:
                temp = BUILD_DIR / f"native-build-{build_id}.tmp"
                temp.write_text(json.dumps(manifest), encoding="utf-8")
                temp.replace(BUILD_MANIFEST)
            except OSError as exc:
                self.emit_log("BUILD", "WARN", f"Firmware actualizado, pero no se pudo guardar la caché: {exc}")
            self.emit_log("BUILD", "INFO", "Firmware C++ actualizado y cargado en la ESP32 virtual.")
            return True, "Firmware C++ actualizado."

    def _sync_hw_inputs_locked(self) -> None:
        t_int = int(round(self.engine_sim.coolant_c))
        ecu_ok = self.engine_sim.is_ecu_responding()
        if self.dll is not None:
            self.dll.set_temp_c(t_int)
            self.dll.set_signal(ecu_ok)
            self.dll.set_ecu_telemetry(round(self.engine_sim.speed_kph), DTC_SCENARIOS[self.engine_sim.dtc_scenario])


    def _load_mode_locked(self, mode: str) -> None:
        self.mode = mode
        self.chat_feed = []
        self.serial_pending = ""
        self.button_queue.clear()
        self.pressed_pin = None
        self.next_button_ms = 0
        self.last_tick_ms = time.monotonic_ns() // 1_000_000
        if mode in self.dll_paths and self.dll_paths[mode].exists():
            self.dll = NativeDllWrapper(self.dll_paths[mode])
            self.dll.init()
            self._sync_hw_inputs_locked()
            self.dll.tick(40)
            self._capture_board_locked()
        else:
            self.dll = None
            self.frame_bytes = b""
            self.gpio_mask = 0

    def _loop(self) -> None:
        while self._running:
            with self.lock:
                now_ms = time.monotonic_ns() // 1_000_000
                elapsed_ms = now_ms - self.last_tick_ms
                self.last_tick_ms = now_ms
                for lvl, ev_msg in self.engine_sim.step():
                    self.emit_log("ECU_OBD", lvl, ev_msg)
                self._sync_hw_inputs_locked()

                if self.dll is not None:
                    # Keep each edge stable for several ticks so the real firmware debounces it.
                    if now_ms >= self.next_button_ms:
                        if self.pressed_pin is not None:
                            self.dll.set_button(self.pressed_pin, False)
                            self.pressed_pin = None
                            self.next_button_ms = now_ms + 120
                        elif self.button_queue:
                            self.pressed_pin = self.button_queue.popleft()
                            self.dll.set_button(self.pressed_pin, True)
                            self.next_button_ms = now_ms + 120
                    self.dll.tick(elapsed_ms)
                    self._capture_board_locked()

            time.sleep(0.05)

    def set_mode(self, mode: str) -> None:
        if mode not in ("sim", "obd"):
            return
        with self.lock:
            self.emit_log("UI", "INFO", f"Modo cambiado -> {'OBD-II Vitara 2008' if mode == 'obd' else 'SIMULACIÓN Potenciómetro'}")
            self._load_mode_locked(mode)

    def reset_board(self) -> None:
        with self.lock:
            self.emit_log("UI", "INFO", "Reinicio de ESP32-S3 virtual")
            self._load_mode_locked(self.mode)

    def update_vehicle(self, body: dict[str, Any]) -> None:
        with self.lock:
            if "ignition" in body:
                ign = str(body["ignition"])
                if ign in ("OFF", "ACC_ON", "RUNNING"):
                    self.engine_sim.ignition = ign
                    self.emit_log("ECU_OBD", "INFO", f"Llave/Ignición -> {ign}")
            if "profile" in body:
                prof = str(body["profile"])
                if prof in VitaraEngineSim.PROFILES:
                    self.engine_sim.profile = prof
                    self.engine_sim.auto_thermal = True
                    self.emit_log("ECU_OBD", "INFO", f"Escenario Vitara -> {VitaraEngineSim.PROFILES[prof]['label']}")
            if "obdConnected" in body:
                self.engine_sim.obd_connected = bool(body["obdConnected"])
                self.emit_log(
                    "ECU_OBD",
                    "INFO" if self.engine_sim.obd_connected else "WARN",
                    "Cable OBD-II CONECTADO" if self.engine_sim.obd_connected else "Cable OBD-II DESCONECTADO (sin respuesta ECU)",
                )
            speed = body.get("speedTarget")
            if type(speed) is int:
                self.engine_sim.speed_target = max(0, min(180, speed))
            scenario = body.get("dtcScenario")
            if isinstance(scenario, str) and scenario in DTC_SCENARIOS:
                self.engine_sim.dtc_scenario = scenario
            relay_mode = body.get("relayMode")
            if relay_mode in ("auto", "off", "low", "high1", "high2", "both"):
                self.engine_sim.relay_mode = relay_mode
            if "autoThermal" in body:
                self.engine_sim.auto_thermal = bool(body["autoThermal"])
                self.emit_log("ECU_OBD", "INFO", f"Calentamiento gradual automático -> {'ACTIVO' if self.engine_sim.auto_thermal else 'MANUAL'}")
            if "tempC" in body and body["tempC"] is not None:
                val = max(20.0, min(120.0, float(body["tempC"])))
                self.engine_sim.coolant_c = val
                self.engine_sim.auto_thermal = False
                self.emit_log("ECU_OBD", "INFO", f"Temperatura manual fijada en {int(round(val))} °C")
            if body.get("resetCold"):
                self.engine_sim.reset_cold()
                self.engine_sim.auto_thermal = True
                self.emit_log("ECU_OBD", "INFO", "Motor enfriado a 28 °C (arranque en frío)")
            self._sync_hw_inputs_locked()

    def pulse_button(self, pin: int) -> bool:
        with self.lock:
            if pin not in (number for name, number in self.pins.items() if name.startswith("PIN_BTN_")):
                return False
            if self.dll is not None:
                self.button_queue.append(pin)
            else:
                return False
            return True

    def trigger_bus_off(self) -> None:
        with self.lock:
            if self.dll is not None:
                self.dll.trigger_bus_off()


    def clear_logs(self) -> None:
        with self.lock:
            self.logs = []

    def get_state_dict(self) -> dict[str, Any]:
        with self.lock:
            is_cpp = self.dll is not None
            return {
                "engine": "cpp" if is_cpp else "unavailable",
                "session": self.serial_epoch,
                "serialSequence": self.serial_seq,
                "board": self.dll.board_status() if self.dll is not None else None,
                "frameRevision": self.frame_revision,
                "buildId": self.loaded_signature[:12] if self.loaded_signature else None,
                "compiler": self.compiler,
                "mode": self.mode,
                "vehicle": self.engine_sim.to_dict(),
                "telemetry": self.dll.telemetry() if self.dll is not None and self.mode == "obd" else None,
                "pins": self.pins,
                "gpioMask": self.gpio_mask,
                "eyeOn": self.dll.is_eye_on() if self.dll is not None else False,
                "logs": self.logs[-250:],
                "chatFeed": self.chat_feed[-20:],
                "buildError": self.build_error,
            }

    def get_frame(self) -> tuple[bytes, int]:
        with self.lock:
            return self.frame_bytes, self.frame_revision


if "--serial-console" in sys.argv:
    sys.stdout.reconfigure(errors="replace")

SIM = SimManager()


class SimulatorServer(ThreadingHTTPServer):
    allow_reuse_address = os.name != "nt"

    def __init__(self, port: int) -> None:
        self.page_html = (SIM_WEB_DIR / "index.html").read_bytes()
        super().__init__((HOST, port), Handler)

    def server_bind(self) -> None:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: Any) -> None:
        return

    def _send_json(self, obj: Any, status: int = 200) -> None:
        payload = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        return json.loads(raw.decode("utf-8"))

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            html_bytes = self.server.page_html
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(html_bytes)))
            self.end_headers()
            self.wfile.write(html_bytes)
            return

        if path == "/api/state":
            self._send_json(SIM.get_state_dict())
            return

        if path == "/api/serial":
            self._send_json(SIM.serial_snapshot())
            return

        if path == "/api/frame.rgb":
            data, revision = SIM.get_frame()
            if not data:
                self._send_json({"message": "Firmware C++ sin cargar"}, HTTPStatus.SERVICE_UNAVAILABLE)
                return
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("X-Frame-Revision", str(revision))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        path = self.path.split("?", 1)[0]
        body = self._read_json()

        if path == "/api/button":
            pin = body.get("pin")
            if not isinstance(pin, int) or not SIM.pulse_button(pin):
                self._send_json({"ok": False, "message": "Pulsador no disponible. Verifica el GPIO y la carga del firmware."}, 400)
                return
            self._send_json({"ok": True})
            return

        if path == "/api/vehicle":
            SIM.update_vehicle(body)
            self._send_json({"ok": True})
            return

        if path == "/api/mode":
            SIM.set_mode(str(body.get("mode", "obd")))
            self._send_json({"ok": True})
            return

        if path == "/api/reset":
            SIM.reset_board()
            self._send_json({"ok": True})
            return

        if path == "/api/busoff":
            SIM.trigger_bus_off()
            self._send_json({"ok": True})
            return

        if path == "/api/logs/clear":
            SIM.clear_logs()
            self._send_json({"ok": True})
            return

        if path == "/api/recompile":
            ok, msg = SIM.compile_and_load()
            self._send_json({"ok": ok, "message": msg})
            return

        self.send_error(HTTPStatus.NOT_FOUND)


def main() -> int:
    no_open = "--no-open" in sys.argv
    port = PORT
    for arg in sys.argv[1:]:
        if arg.startswith("--port="):
            port = int(arg.split("=", 1)[1])

    watch = "--watch-firmware" in sys.argv
    try:
        server = SimulatorServer(port)
    except OSError as exc:
        if not watch or exc.errno not in (errno.EADDRINUSE, errno.EACCES, 10048, 10013):
            raise
        original_port = port
        try:
            server = SimulatorServer(port + 1 if port < 65535 else 0)
        except OSError:
            server = SimulatorServer(0)
        print(f"El puerto {original_port} está ocupado. Instancia actual: http://{HOST}:{server.server_port}/")
    port = server.server_port
    if watch:
        threading.Thread(target=SIM.watch_firmware, daemon=True).start()
    url = f"http://{HOST}:{port}"
    engine_label = "C++ REAL (lilyxm7/*.cpp)" if SIM.dll is not None else "FIRMWARE NO CARGADO"
    print("============================================================")
    print(f"  lilyxm7 Web Simulator — {url}")
    print(f"  Motor activo : {engine_label}")
    if watch:
        print("  Actualización automática al guardar cambios del firmware.")
    print(f"  Serial C++   : {PROJECT_ROOT / 'logs' / 'firmware_serial.log'}")
    print(f"  Logs         : {PROJECT_ROOT / 'logs' / 'sim_web.log'}")
    print("  Presiona Ctrl+C en esta ventana para detener el servidor.")
    print("============================================================")
    log_line(LOG_COMPONENT, f"Servidor web iniciado en {url} | motor={engine_label}")

    if not no_open:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[lilyxm7] Servidor detenido.")
        log_line(LOG_COMPONENT, "Servidor web detenido por el usuario.")
    finally:
        SIM._running = False
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
