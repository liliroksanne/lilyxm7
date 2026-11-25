#!/usr/bin/env python3
"""
Servidor Web local (100% offline, solo stdlib de Python) para lilyxm7.

Incluye:
- Motor C++ REAL (compila lilyxm7/*.cpp en DLLs nativas si existe g++/clang++)
  o motor PREVIEW automatico si aun no se instala g++.
- Simulador termico realista del motor / ECU OBD-II de la Suzuki Grand Vitara 2008 V6 2.7L
  (se calienta poco a poco desde frio ~28 °C hasta temperatura de operacion ~89-92 °C,
  con escenarios de manejo, ignicion ON/OFF, falla de termostato y desconexion OBD-II).
- Logs estructurados POR COMPONENTE (ECU_OBD, DATA, CHAT, BTN, UI, BUILD) enviados en vivo
  al frontend y guardados en C:\\Git\\lilyxm7\\logs\\sim_web.log y logs\\all.log.
"""

from __future__ import annotations

import ctypes
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SIM_WEB_DIR = Path(__file__).resolve().parent
NATIVE_DIR = SIM_WEB_DIR / "native"
BUILD_DIR = SIM_WEB_DIR / "build"

sys.path.insert(0, str(PROJECT_ROOT))
from tools.run_logged import log_line

LOG_COMPONENT = "sim_web"
HOST = "127.0.0.1"
PORT = 8765

SERIAL_TAG_RE = re.compile(r"^\[LOG\]\[([A-Z0-9_]+)\]\s*(.*)$")


def find_cpp_compiler() -> str | None:
    """Busca g++ o clang++ en PATH y rutas comunes de WinLibs/MinGW/LLVM en Windows."""
    for name in ("g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found

    candidates: list[Path] = [
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


class VitaraEngineSim:
    """Modelo termico gradual de la Suzuki Grand Vitara 2008 V6 2.7 L (motor N27A)."""

    PROFILES = {
        "idle":    {"label": "Ralentí (Idle)",       "target": 89.0,  "rate": 1.1, "rpm": 750},
        "city":    {"label": "Ciudad",               "target": 93.0,  "rate": 1.8, "rpm": 2100},
        "highway": {"label": "Carretera / Subida",   "target": 98.0,  "rate": 2.6, "rpm": 3400},
        "fault":   {"label": "Falla termostato",     "target": 116.0, "rate": 3.4, "rpm": 2600},
    }

    def __init__(self) -> None:
        self.ambient_c = 26.0
        self.coolant_c = 28.0
        self.ignition = "RUNNING"  # OFF | ACC_ON | RUNNING
        self.profile = "idle"
        self.obd_connected = True
        self.auto_thermal = True
        self.fan_on = False
        self.thermostat_open = False
        self._last_ts = time.monotonic()
        self._last_logged_int = 28

    def reset_cold(self) -> None:
        self.coolant_c = 28.0
        self.fan_on = False
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

        if not self.auto_thermal:
            return events

        if self.ignition == "RUNNING":
            prof = self.PROFILES.get(self.profile, self.PROFILES["idle"])
            target = prof["target"]
            rate = prof["rate"]
            if self.coolant_c < target:
                # Calentamiento progresivo (mas rapido al inicio, mas suave cerca del termostato)
                factor = 1.0 if (target - self.coolant_c) > 10.0 else 0.45
                self.coolant_c = min(target, self.coolant_c + rate * factor * dt_s)
            elif self.coolant_c > target:
                self.coolant_c = max(target, self.coolant_c - 1.4 * dt_s)
        else:
            # Motor apagado -> enfriamiento gradual hacia temperatura ambiente
            if self.coolant_c > self.ambient_c:
                self.coolant_c = max(self.ambient_c, self.coolant_c - 1.2 * dt_s)

        # Histéresis del termostato (~82 °C) y electroventilador (~95 °C, salvo en falla)
        if not self.thermostat_open and self.coolant_c >= 82.0:
            self.thermostat_open = True
            events.append(("INFO", "Termostato abierto (~82 °C), regulando flujo de refrigerante"))
        elif self.thermostat_open and self.coolant_c <= 76.0:
            self.thermostat_open = False

        if self.profile != "fault":
            if not self.fan_on and self.coolant_c >= 94.0:
                self.fan_on = True
                events.append(("INFO", "Electroventilador ENCENDIDO (>= 94 °C)"))
            elif self.fan_on and self.coolant_c <= 89.0:
                self.fan_on = False
                events.append(("INFO", "Electroventilador APAGADO (<= 89 °C)"))
        else:
            self.fan_on = False

        cur_int = int(round(self.coolant_c))
        if abs(cur_int - self._last_logged_int) >= 5:
            self._last_logged_int = cur_int
            events.append(("INFO", f"Motor Vitara 2.7L -> refrigerante={self.coolant_c:.1f} °C ({self.ignition}/{self.profile})"))

        return events

    def to_dict(self) -> dict[str, Any]:
        prof = self.PROFILES.get(self.profile, self.PROFILES["idle"])
        return {
            "coolantC": round(self.coolant_c, 1),
            "coolantInt": int(round(self.coolant_c)),
            "ignition": self.ignition,
            "profile": self.profile,
            "profileLabel": prof["label"],
            "rpm": prof["rpm"] if self.ignition == "RUNNING" else 0,
            "obdConnected": self.obd_connected,
            "autoThermal": self.auto_thermal,
            "ecuResponding": self.is_ecu_responding(),
            "thermostatOpen": self.thermostat_open,
            "fanOn": self.fan_on,
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

    def get_gpio_mask(self) -> int:
        return int(self.lib.sim_get_gpio_mask())


class PreviewFallbackEngine:
    """Motor de respaldo con chatbot Lily cuando aun no se instala g++."""

    def __init__(self, emit_log) -> None:
        self._emit = emit_log
        self.reset("obd")

    def reset(self, mode: str) -> None:
        self.mode = mode
        self.screen = "TEMPERATURE"
        self.selected_idx = 0
        self.temp_c = 28
        self.signal_on = True
        self.has_live = False
        self.has_last = False
        self.last_temp = 28
        self.prev_temp = 28
        self.delta_c = 0
        self.trend = "NONE"
        self.valid_count = 0
        self.tx_count = 0
        self.last_valid_ts = 0.0
        self.last_poll_ts = 0.0
        self.anim_tick = 0
        self.last_anim_ts = time.monotonic()
        self.prev_mood = "MENU_IDLE"
        self.last_announced_temp = -999
        self.chat_messages: list[dict[str, Any]] = []
        self._push_chat("Hola! Soy Lily v1.0.", False)
        self._push_chat("Modo SIMULACIÓN activo." if mode == "sim" else "Lista p/ Vitara 2.7L.", False)

    def _push_chat(self, text: str, is_alert: bool) -> None:
        if self.chat_messages and self.chat_messages[-1]["text"] == text:
            return
        ts = dt.datetime.now().strftime("%H:%M:%S")
        self.chat_messages.append({"ts": ts, "text": text, "isAlert": is_alert})
        if len(self.chat_messages) > 25:
            self.chat_messages = self.chat_messages[-25:]
        self._emit("CHAT", "WARN" if is_alert else "INFO", f"LILY: {text}")

    def _mood_from_temp(self) -> tuple[str, str, bool]:
        if not self.has_live:
            return ("NO_SIGNAL", "SIN SENAL", True)
        if self.last_temp <= 59:
            return ("COLD", "MOTOR FRIO", False)
        if self.last_temp <= 79:
            return ("WARMING", "CALENTANDO", False)
        if self.last_temp <= 97:
            return ("OPTIMAL", "OPTIMO", False)
        if self.last_temp <= 107:
            return ("WARM_WARN", "TEMP ALTA", True)
        return ("OVERHEAT", "PELIGRO!", True)

    def press_button(self, pin: int) -> None:
        names = {4: "UP", 5: "DOWN", 6: "A", 7: "B"}
        self._emit("BTN", "INFO", f"Pulsado {names.get(pin, pin)} (GPIO {pin})")
        if self.screen == "MAIN_MENU":
            if pin == 4:
                self.selected_idx = (self.selected_idx - 1) % 2
                self._emit("UI", "INFO", f"Cursor menú -> {'Temperatura' if self.selected_idx == 0 else 'Estado de conexión'}")
            elif pin == 5:
                self.selected_idx = (self.selected_idx + 1) % 2
                self._emit("UI", "INFO", f"Cursor menú -> {'Temperatura' if self.selected_idx == 0 else 'Estado de conexión'}")
            elif pin == 6:
                self.screen = "TEMPERATURE" if self.selected_idx == 0 else "CONNECTION_STATUS"
                self._emit("UI", "INFO", f"Cambio de pantalla -> {self.screen}")
                self._push_chat("Abriendo Temperatura..." if self.screen == "TEMPERATURE" else "Revisando enlace ECU.", False)
        elif pin == 7:
            self.screen = "MAIN_MENU"
            self._emit("UI", "INFO", "Regreso a pantalla -> MAIN_MENU")
            self._push_chat("Menú principal listo.", False)

    def tick(self) -> None:
        now = time.monotonic()
        if now - self.last_anim_ts >= 0.45:
            self.last_anim_ts = now
            self.anim_tick += 1

        interval = 0.35 if self.mode == "sim" else 0.50
        if self.last_poll_ts == 0.0 or (now - self.last_poll_ts) >= interval:
            self.last_poll_ts = now
            self.tx_count += 1
            if self.mode == "obd":
                self._emit("ECU_OBD", "INFO", "TX 0x7DF [8] 02 01 05 00 00 00 00 00")

            if self.signal_on:
                was_off = not self.has_live
                old_t = self.last_temp
                if not self.has_last:
                    self.prev_temp = self.temp_c
                    self.last_temp = self.temp_c
                    self.delta_c = 0
                    self.trend = "NONE"
                    self.has_last = True
                else:
                    diff = self.temp_c - self.last_temp
                    self.prev_temp = self.last_temp
                    self.last_temp = self.temp_c
                    self.delta_c = diff
                    self.trend = "UP" if diff >= 1 else ("DOWN" if diff <= -1 else "STABLE")

                self.has_live = True
                self.last_valid_ts = now
                self.valid_count += 1
                byte_a = max(0, min(255, self.temp_c + 40))

                if self.mode == "obd":
                    self._emit("ECU_OBD", "INFO", f"RX 0x7E8 [8] 03 41 05 {byte_a:02X} 00 00 00 00 (A={byte_a} -> {self.temp_c} °C)")

                if was_off or self.temp_c != old_t:
                    self._emit("DATA", "INFO", f"Lectura válida: {self.temp_c} °C | tendencia={self.trend} ({self.delta_c:+d} °C)")

                mood, _, _ = self._mood_from_temp()
                mood_changed = mood != self.prev_mood
                big_step = self.last_announced_temp == -999 or abs(self.last_temp - self.last_announced_temp) >= 3
                if was_off or mood_changed or big_step:
                    if was_off and self.last_announced_temp != -999:
                        self._push_chat(f"Señal OK! Ahora {self.last_temp}°C", False)
                    elif mood == "COLD":
                        self._push_chat(f"Subió a {self.last_temp}°C, calent." if self.trend == "UP" else f"Motor frío: {self.last_temp}°C.", False)
                    elif mood == "WARMING":
                        self._push_chat(f"Calentando: {self.last_temp}°C (+)" if self.trend == "UP" else f"Tomando temp: {self.last_temp}°C.", False)
                    elif mood == "OPTIMAL":
                        if mood_changed:
                            self._push_chat(f"{self.last_temp}°C! Temp óptima :)", False)
                        elif self.trend == "UP":
                            self._push_chat(f"Subió a {self.last_temp}°C (ideal).", False)
                        else:
                            self._push_chat(f"Bajó a {self.last_temp}°C (ideal).", False)
                    elif mood == "WARM_WARN":
                        self._push_chat(f"Ojo! Temp alta: {self.last_temp}°C", True)
                    elif mood == "OVERHEAT":
                        self._push_chat(f"ALERTA! {self.last_temp}°C detente!", True)
                    self.last_announced_temp = self.last_temp
                    self.prev_mood = mood
            else:
                if self.has_live and (now - self.last_valid_ts) >= 3.0:
                    self.has_live = False
                    self.prev_mood = "NO_SIGNAL"
                    self._emit("DATA", "WARN", f"SIN DATOS DEL AUTO | última lectura conservada: {self.last_temp} °C")
                    self._push_chat("Sin datos del auto!", True)
                    if self.has_last:
                        self._push_chat(f"Última lectura: {self.last_temp}°C", True)

    def snapshot(self) -> dict[str, Any]:
        age = int(time.monotonic() - self.last_valid_ts) if self.has_last else 0
        mood, mood_tag, is_alert = self._mood_from_temp()
        return {
            "screen": self.screen,
            "selectedIdx": self.selected_idx,
            "hasLiveSignal": self.has_live,
            "hasLastValue": self.has_last,
            "tempC": self.last_temp,
            "previousTempC": self.prev_temp,
            "deltaC": self.delta_c,
            "trend": self.trend,
            "validCount": self.valid_count,
            "txCount": self.tx_count,
            "ageSec": age,
            "animTick": self.anim_tick,
            "isSimulated": self.mode == "sim",
            "mood": mood,
            "moodTag": mood_tag,
            "isAlert": is_alert,
            "chatMessages": self.chat_messages[-12:],
        }


class SimManager:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.mode = "obd"  # Arranca listo con el simulador OBD-II de la Vitara 2008
        self.engine_sim = VitaraEngineSim()
        self.compiler = find_cpp_compiler()
        self.build_error: str | None = None
        self.dll: NativeDllWrapper | None = None
        self.dll_paths: dict[str, Path] = {}
        self.dll_generation = 0
        self.frame_bytes: bytes = bytes(320 * 240 * 3)
        self.gpio_mask: int = 0
        self.log_seq = 0
        self.logs: list[dict[str, Any]] = []
        self.chat_feed: list[dict[str, Any]] = []
        self.preview = PreviewFallbackEngine(self.emit_log)
        self._running = True

        self.compile_and_load()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def emit_log(self, comp: str, level: str, msg: str) -> None:
        ts = dt.datetime.now().strftime("%H:%M:%S")
        self.log_seq += 1
        entry = {"id": self.log_seq, "ts": ts, "comp": comp, "level": level, "msg": msg}
        self.logs.append(entry)
        if len(self.logs) > 400:
            self.logs = self.logs[-400:]

        if comp == "CHAT":
            clean = msg[6:].strip() if msg.startswith("LILY:") else msg
            self.chat_feed.append({"ts": ts, "text": clean, "isAlert": level == "WARN"})
            if len(self.chat_feed) > 30:
                self.chat_feed = self.chat_feed[-30:]

        log_line(LOG_COMPONENT, f"[{comp}] {msg}", level=level)

    def _ingest_serial_chunk(self, chunk: str) -> None:
        if not chunk:
            return
        for raw in chunk.splitlines():
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
                self.emit_log(comp, lvl, rest)
            else:
                self.emit_log("UI", "INFO", line)

    def _ingest_can_chunk(self, chunk: str) -> None:
        if not chunk:
            return
        for raw in chunk.splitlines():
            line = raw.strip()
            if not line:
                continue
            lvl = "WARN" if "BUS_OFF" in line or "Fallo" in line else "INFO"
            self.emit_log("ECU_OBD", lvl, line)

    def compile_and_load(self) -> tuple[bool, str]:
        with self.lock:
            self.compiler = find_cpp_compiler()
            self.build_error = None

            if not self.compiler:
                msg = "No se encontro g++/clang++ en Windows. Usando motor PREVIEW (instala g++ con opcion [4] para C++ real)."
                self.emit_log("BUILD", "WARN", msg)
                self.dll = None
                self._load_mode_locked(self.mode)
                return False, msg

            BUILD_DIR.mkdir(parents=True, exist_ok=True)
            self.dll_generation += 1
            gen = self.dll_generation

            for old in BUILD_DIR.glob("lilyxm7_*.dll"):
                try:
                    old.unlink()
                except OSError:
                    pass

            compiler_dir = str(Path(self.compiler).parent)
            if hasattr(os, "add_dll_directory"):
                try:
                    os.add_dll_directory(compiler_dir)
                except OSError:
                    pass

            targets = [
                ("sim", 1, BUILD_DIR / f"lilyxm7_sim_{gen}.dll"),
                ("obd", 0, BUILD_DIR / f"lilyxm7_obd_{gen}.dll"),
            ]

            for key, sim_flag, out_dll in targets:
                cmd = [
                    self.compiler,
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
                self.emit_log("BUILD", "INFO", f"Compilando {out_dll.name} (LILYXM7_SIMULATION_MODE={sim_flag})...")
                res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
                if res.stderr:
                    for ln in res.stderr.splitlines():
                        self.emit_log("BUILD", "ERROR" if res.returncode != 0 else "WARN", ln)
                if res.returncode != 0:
                    err_msg = f"Error compilando modo {key} (exit={res.returncode})"
                    self.build_error = err_msg
                    self.emit_log("BUILD", "ERROR", err_msg)
                    self.dll = None
                    return False, err_msg
                self.dll_paths[key] = out_dll

            self.emit_log("BUILD", "INFO", "Compilación C++ real exitosa (modo SIMULACIÓN + modo OBD-II CAN).")
            self._load_mode_locked(self.mode)
            return True, "Compilado y recargado en C++ real."

    def _sync_hw_inputs_locked(self) -> None:
        t_int = int(round(self.engine_sim.coolant_c))
        ecu_ok = self.engine_sim.is_ecu_responding()
        if self.dll is not None:
            self.dll.set_temp_c(t_int)
            self.dll.set_signal(ecu_ok)
        else:
            self.preview.temp_c = t_int
            self.preview.signal_on = ecu_ok

    def _load_mode_locked(self, mode: str) -> None:
        self.mode = mode
        self.chat_feed = []
        if mode in self.dll_paths and self.dll_paths[mode].exists():
            self.dll = NativeDllWrapper(self.dll_paths[mode])
            self.dll.init()
            self._sync_hw_inputs_locked()
            self.dll.tick(40)
            self.frame_bytes = self.dll.get_frame_bytes()
            self.gpio_mask = self.dll.get_gpio_mask()
            self._ingest_serial_chunk(self.dll.pop_serial())
            self._ingest_can_chunk(self.dll.pop_can_log())
        else:
            self.dll = None
            self.preview.reset(mode)
            self._sync_hw_inputs_locked()

    def _loop(self) -> None:
        while self._running:
            with self.lock:
                for lvl, ev_msg in self.engine_sim.step():
                    self.emit_log("ECU_OBD", lvl, ev_msg)
                self._sync_hw_inputs_locked()

                if self.dll is not None:
                    self.dll.tick(40)
                    self.frame_bytes = self.dll.get_frame_bytes()
                    self.gpio_mask = self.dll.get_gpio_mask()
                    self._ingest_serial_chunk(self.dll.pop_serial())
                    self._ingest_can_chunk(self.dll.pop_can_log())
                else:
                    self.preview.tick()
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

    def pulse_button(self, pin: int) -> None:
        with self.lock:
            if self.dll is not None:
                self.dll.set_button(pin, True)
                self.dll.tick(25)
                self.dll.tick(35)
                self.dll.tick(20)
                self.dll.set_button(pin, False)
                self.dll.tick(55)
                self.frame_bytes = self.dll.get_frame_bytes()
                self.gpio_mask = self.dll.get_gpio_mask()
                self._ingest_serial_chunk(self.dll.pop_serial())
                self._ingest_can_chunk(self.dll.pop_can_log())
            else:
                self.preview.press_button(pin)

    def trigger_bus_off(self) -> None:
        with self.lock:
            if self.dll is not None:
                self.dll.trigger_bus_off()
            else:
                self.emit_log("ECU_OBD", "WARN", "BUS_OFF inducido (en motor C++ real prueba twai_initiate_recovery)")

    def clear_logs(self) -> None:
        with self.lock:
            self.logs = []

    def get_state_dict(self) -> dict[str, Any]:
        with self.lock:
            is_cpp = self.dll is not None
            return {
                "engine": "cpp" if is_cpp else "preview",
                "compiler": self.compiler,
                "mode": self.mode,
                "vehicle": self.engine_sim.to_dict(),
                "gpioMask": self.gpio_mask,
                "logs": self.logs[-250:],
                "chatFeed": self.chat_feed[-20:] if is_cpp else self.preview.chat_messages[-20:],
                "buildError": self.build_error,
                "preview": None if is_cpp else self.preview.snapshot(),
            }

    def get_frame(self) -> bytes:
        with self.lock:
            return self.frame_bytes


SIM = SimManager()


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
            html_bytes = (SIM_WEB_DIR / "index.html").read_bytes()
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

        if path == "/api/frame.rgb":
            data = SIM.get_frame()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/octet-stream")
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
            SIM.pulse_button(int(body.get("pin", -1)))
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

    server = ThreadingHTTPServer((HOST, port), Handler)
    url = f"http://{HOST}:{port}"
    engine_label = "C++ REAL (lilyxm7/*.cpp)" if SIM.dll is not None else "PREVIEW (sin g++ aun)"
    print("============================================================")
    print(f"  lilyxm7 Web Simulator — {url}")
    print(f"  Motor activo : {engine_label}")
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
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
