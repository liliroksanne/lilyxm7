#!/usr/bin/env python3
r"""
Módulo y ejecutor centralizado de logs para lilyxm7.
Todos los componentes escriben aquí: C:\Git\lilyxm7\logs\

Uso como CLI desde .bat o terminal:
    python tools/run_logged.py <componente> -- <comando> [args...]

Ejemplo:
    python tools/run_logged.py build_sim -- arduino-cli compile ...
"""

from __future__ import annotations

import datetime as dt
import os
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
LOGS_DIR = PROJECT_ROOT / "logs"
ALL_LOG_PATH = LOGS_DIR / "all.log"


def ensure_logs_dir() -> Path:
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
    return LOGS_DIR


def _timestamp() -> str:
    return dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def log_line(component: str, message: str, level: str = "INFO") -> None:
    """Escribe una línea con timestamp en logs/<component>.log y en logs/all.log."""
    ensure_logs_dir()
    comp_safe = "".join(c if (c.isalnum() or c in ("_", "-")) else "_" for c in component)
    line = f"[{_timestamp()}][{comp_safe}][{level}] {message}\n"

    for path in (LOGS_DIR / f"{comp_safe}.log", ALL_LOG_PATH):
        try:
            with path.open("a", encoding="utf-8", errors="replace") as fh:
                fh.write(line)
        except OSError:
            pass


def run_command_logged(component: str, cmd: list[str]) -> int:
    """Ejecuta un comando mostrando salida en vivo y guardando todo en logs/<component>.log y all.log."""
    ensure_logs_dir()
    log_line(component, f"===== INICIO COMANDO: {' '.join(cmd)} =====")
    log_line(component, f"CWD: {os.getcwd()}")

    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
    except FileNotFoundError as exc:
        msg = f"Ejecutable no encontrado: {cmd[0]} ({exc})"
        print(f"[ERROR] {msg}", flush=True)
        log_line(component, msg, level="ERROR")
        return 127
    except OSError as exc:
        msg = f"No se pudo lanzar el comando: {exc}"
        print(f"[ERROR] {msg}", flush=True)
        log_line(component, msg, level="ERROR")
        return 1

    assert proc.stdout is not None
    for raw_line in proc.stdout:
        clean = raw_line.rstrip("\r\n")
        print(clean, flush=True)
        log_line(component, clean, level="OUT")

    rc = proc.wait()
    level = "INFO" if rc == 0 else "ERROR"
    log_line(component, f"===== FIN COMANDO (exit={rc}) =====", level=level)
    return rc


def main() -> int:
    if len(sys.argv) < 4 or "--" not in sys.argv:
        print("Uso: run_logged.py <componente> -- <comando> [args...]", file=sys.stderr)
        return 2

    sep = sys.argv.index("--")
    component = sys.argv[1]
    cmd = sys.argv[sep + 1 :]
    if not cmd:
        print("Error: falta el comando despues de '--'", file=sys.stderr)
        return 2

    return run_command_logged(component, cmd)


if __name__ == "__main__":
    raise SystemExit(main())
