#!/usr/bin/env python3
"""
Servidor Web local (100% offline, solo stdlib de Python) para el simulador propio de lilyxm7.

- Si detecta g++ (MinGW/WinLibs) o clang++ en Windows, compila TU CODIGO C++ REAL
  de lilyxm7/ en dos DLLs nativas (modo SIMULACION y modo OBD-II CAN real) y transmite
  en vivo el framebuffer real de 320x240 pixeles + Serial.printf + tramas CAN a la web.
- Si aun no hay g++ instalado, arranca inmediatamente en modo PREVIEW interactivo
  (mientras te indica como activar el motor C++ real con 1 comando).
"""

from __future__ import annotations

import ctypes
import json
import os
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


class NativeDllWrapper:
    """Carga y controla una instancia DLL compilada desde sim_bridge.cpp + lilyxm7/."""

    def __init__(self, dll_path: Path) -> None:
        self.dll_path = dll_path
        # En Windows con Python 3.8+, agregar el directorio del compilador por si depende de libstdc++/libgcc
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
    """Motor ligero de respaldo si aun no se instala g++ (permite usar la web de inmediato)."""

    def __init__(self) -> None:
        self.reset("sim")

    def reset(self, mode: str) -> None:
        self.mode = mode
        self.screen = "MAIN_MENU"
        self.selected_idx = 0
        self.temp_c = 86
        self.signal_on = True
        self.has_live = False
        self.has_last = False
        self.last_temp = 86
        self.prev_temp = 86
        self.delta_c = 0
        self.trend = "NONE"
        self.valid_count = 0
        self.tx_count = 0
        self.last_valid_ts = 0.0
        self.last_poll_ts = 0.0
        self.anim_tick = 0
        self.last_anim_ts = time.monotonic()
        self.serial_lines: list[str] = [
            "========================================",
            "  lilyxm7 v1.0 - Motor PREVIEW Web",
            "  (Instala g++ con opcion [4] para ejecutar el C++ real)",
            "========================================",
        ]
        self.can_lines: list[str] = [
            "[PREVIEW] Modo simulado listo. Instala g++ para compilar lilyxm7/*.cpp real."
        ]

    def press_button(self, pin: int) -> None:
        if self.screen == "MAIN_MENU":
            if pin == 4:
                self.selected_idx = (self.selected_idx - 1) % 2
                self.serial_lines.append(f"[LOG][BTN] UP -> opcion={self.selected_idx}")
            elif pin == 5:
                self.selected_idx = (self.selected_idx + 1) % 2
                self.serial_lines.append(f"[LOG][BTN] DOWN -> opcion={self.selected_idx}")
            elif pin == 6:
                self.screen = "TEMPERATURE" if self.selected_idx == 0 else "CONNECTION_STATUS"
                self.serial_lines.append(f"[LOG][UI] pantalla={self.screen}")
        elif pin == 7:
            self.screen = "MAIN_MENU"
            self.serial_lines.append("[LOG][UI] regreso a MAIN_MENU")

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
                self.can_lines.append("TX 0x7DF [8] 02 01 05 00 00 00 00 00")
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
                    self.can_lines.append(f"RX 0x7E8 [8] 03 41 05 {byte_a:02X} 00 00 00 00  (A={byte_a} -> {self.temp_c} °C)")
                if was_off or self.temp_c != old_t:
                    self.serial_lines.append(f"[LOG][DATA] temp={self.temp_c}C delta={self.delta_c:+d}C trend={self.trend}")
            else:
                if self.has_live and (now - self.last_valid_ts) >= 3.0:
                    self.has_live = False
                    self.serial_lines.append(f"[LOG][DATA] SIN DATOS DEL AUTO | ultima_lectura={self.last_temp}C")

        if len(self.serial_lines) > 150:
            self.serial_lines = self.serial_lines[-150:]
        if len(self.can_lines) > 150:
            self.can_lines = self.can_lines[-150:]

    def snapshot(self) -> dict[str, Any]:
        age = int(time.monotonic() - self.last_valid_ts) if self.has_last else 0
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
        }


class SimManager:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.mode = "sim"  # "sim" | "obd"
        self.temp_c = 86
        self.signal_on = True
        self.compiler = find_cpp_compiler()
        self.build_log: list[str] = []
        self.build_error: str | None = None
        self.dll: NativeDllWrapper | None = None
        self.dll_paths: dict[str, Path] = {}
        self.dll_generation = 0
        self.frame_bytes: bytes = bytes(320 * 240 * 3)
        self.gpio_mask: int = 0
        self.serial_lines: list[str] = []
        self.can_lines: list[str] = []
        self.preview = PreviewFallbackEngine()
        self._running = True

        self.compile_and_load()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _append_serial_text(self, chunk: str) -> None:
        if not chunk:
            return
        for line in chunk.splitlines():
            self.serial_lines.append(line)
            log_line(LOG_COMPONENT, f"SERIAL: {line}")
        if len(self.serial_lines) > 200:
            self.serial_lines = self.serial_lines[-200:]

    def _append_can_text(self, chunk: str) -> None:
        if not chunk:
            return
        for line in chunk.splitlines():
            self.can_lines.append(line)
            log_line(LOG_COMPONENT, f"CAN: {line}")
        if len(self.can_lines) > 200:
            self.can_lines = self.can_lines[-200:]

    def compile_and_load(self) -> tuple[bool, str]:
        with self.lock:
            self.compiler = find_cpp_compiler()
            self.build_log = []
            self.build_error = None

            if not self.compiler:
                msg = "No se encontro g++/clang++ en Windows. Usando modo PREVIEW (ejecuta opcion [4] en el .bat para instalar g++ y activar motor C++ real)."
                self.build_log.append(msg)
                log_line(LOG_COMPONENT, msg, level="WARN")
                self.dll = None
                self.preview.reset(self.mode)
                return False, msg

            BUILD_DIR.mkdir(parents=True, exist_ok=True)
            self.dll_generation += 1
            gen = self.dll_generation

            # Limpiar DLLs viejas si ya no están bloqueadas
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
                self.build_log.append(f"$ {' '.join(cmd)}")
                log_line(LOG_COMPONENT, f"Compilando {out_dll.name} (sim={sim_flag})...")
                res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
                if res.stdout:
                    self.build_log.extend(res.stdout.splitlines())
                if res.stderr:
                    self.build_log.extend(res.stderr.splitlines())
                if res.returncode != 0:
                    err_msg = f"Error compilando modo {key} (exit={res.returncode})"
                    self.build_error = err_msg
                    self.build_log.append(err_msg)
                    log_line(LOG_COMPONENT, err_msg + "\n" + res.stderr, level="ERROR")
                    self.dll = None
                    return False, err_msg
                self.dll_paths[key] = out_dll

            self.build_log.append("[OK] Compilacion C++ real exitosa (sim + obd).")
            log_line(LOG_COMPONENT, "Compilacion C++ real exitosa (sim + obd).")
            self._load_mode_locked(self.mode)
            return True, "Compilado y recargado en C++ real."

    def _load_mode_locked(self, mode: str) -> None:
        self.mode = mode
        self.serial_lines = []
        self.can_lines = []
        if mode in self.dll_paths and self.dll_paths[mode].exists():
            self.dll = NativeDllWrapper(self.dll_paths[mode])
            self.dll.init()
            self.dll.set_temp_c(self.temp_c)
            self.dll.set_signal(self.signal_on)
            self.dll.tick(40)
            self.frame_bytes = self.dll.get_frame_bytes()
            self.gpio_mask = self.dll.get_gpio_mask()
            self._append_serial_text(self.dll.pop_serial())
            self._append_can_text(self.dll.pop_can_log())
        else:
            self.dll = None
            self.preview.reset(mode)
            self.preview.temp_c = self.temp_c
            self.preview.signal_on = self.signal_on

    def _loop(self) -> None:
        while self._running:
            with self.lock:
                if self.dll is not None:
                    self.dll.tick(40)
                    self.frame_bytes = self.dll.get_frame_bytes()
                    self.gpio_mask = self.dll.get_gpio_mask()
                    self._append_serial_text(self.dll.pop_serial())
                    self._append_can_text(self.dll.pop_can_log())
                else:
                    self.preview.tick()
            time.sleep(0.04)

    def set_mode(self, mode: str) -> None:
        if mode not in ("sim", "obd"):
            return
        with self.lock:
            log_line(LOG_COMPONENT, f"Cambio de modo -> {mode}")
            self._load_mode_locked(mode)

    def reset_board(self) -> None:
        with self.lock:
            log_line(LOG_COMPONENT, "Reset de ESP32-S3 virtual")
            self._load_mode_locked(self.mode)

    def set_inputs(self, temp_c: int | None, signal_on: bool | None) -> None:
        with self.lock:
            if temp_c is not None:
                self.temp_c = max(20, min(120, int(temp_c)))
            if signal_on is not None:
                self.signal_on = bool(signal_on)
            if self.dll is not None:
                self.dll.set_temp_c(self.temp_c)
                self.dll.set_signal(self.signal_on)
            else:
                self.preview.temp_c = self.temp_c
                self.preview.signal_on = self.signal_on

    def pulse_button(self, pin: int) -> None:
        with self.lock:
            if self.dll is not None:
                # Simula pulsacion fisica real superando el antirrebote de 45 ms en buttons.cpp
                self.dll.set_button(pin, True)
                self.dll.tick(25)
                self.dll.tick(35)
                self.dll.tick(20)
                self.dll.set_button(pin, False)
                self.dll.tick(55)
                self.frame_bytes = self.dll.get_frame_bytes()
                self.gpio_mask = self.dll.get_gpio_mask()
                self._append_serial_text(self.dll.pop_serial())
                self._append_can_text(self.dll.pop_can_log())
            else:
                self.preview.press_button(pin)

    def trigger_bus_off(self) -> None:
        with self.lock:
            if self.dll is not None:
                self.dll.trigger_bus_off()
            else:
                self.preview.can_lines.append("[PREVIEW] BUS_OFF solo activo en motor C++ real.")

    def get_state_dict(self) -> dict[str, Any]:
        with self.lock:
            is_cpp = self.dll is not None
            return {
                "engine": "cpp" if is_cpp else "preview",
                "compiler": self.compiler,
                "mode": self.mode,
                "tempC": self.temp_c,
                "signalOn": self.signal_on,
                "gpioMask": self.gpio_mask,
                "serialLines": self.serial_lines if is_cpp else self.preview.serial_lines,
                "canLines": self.can_lines if is_cpp else self.preview.can_lines,
                "buildLog": self.build_log,
                "buildError": self.build_error,
                "preview": None if is_cpp else self.preview.snapshot(),
            }

    def get_frame(self) -> bytes:
        with self.lock:
            return self.frame_bytes


SIM = SimManager()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: Any) -> None:
        return  # Silencia spam HTTP en consola

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
            pin = int(body.get("pin", -1))
            SIM.pulse_button(pin)
            self._send_json({"ok": True})
            return

        if path == "/api/inputs":
            SIM.set_inputs(body.get("tempC"), body.get("signalOn"))
            self._send_json({"ok": True})
            return

        if path == "/api/mode":
            SIM.set_mode(str(body.get("mode", "sim")))
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
