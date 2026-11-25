#!/usr/bin/env python3
"""
Empaqueta el firmware multi-archivo de lilyxm7/ en un paquete listo para Wokwi Web
(navegador Chrome/Edge, sin VS Code ni licencia):
  - wokwi_web/sketch.ino        (todos los .h/.cpp/.ino unidos en un solo archivo)
  - wokwi_web/diagram.json      (circuito completo ESP32-S3 + ILI9341 + botones + pot + switch)
  - wokwi_web/libraries.txt     (Adafruit GFX Library + Adafruit ILI9341)
  - wokwi_web/abrir_wokwi.html  (panel Matrix local con botones de copiado en 1 clic)
"""

from __future__ import annotations

import html
import json
import re
import sys
import webbrowser
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
from tools.run_logged import log_line

LOG_COMPONENT = "wokwi_web"
SRC_DIR = PROJECT_ROOT / "lilyxm7"
OUT_DIR = PROJECT_ROOT / "wokwi_web"

LOCAL_HEADERS = {
    "config.h",
    "data_types.h",
    "obd_protocol.h",
    "buttons.h",
    "data_source.h",
    "assistant_ascii.h",
    "ui_tft.h",
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"\s*$')
PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\s*$")
SYS_INCLUDE_RE = re.compile(r"^\s*#\s*include\s*<([^>]+)>\s*$")

ORDER = [
    "config.h",
    "data_types.h",
    "obd_protocol.h",
    "buttons.h",
    "data_source.h",
    "assistant_ascii.h",
    "ui_tft.h",
    "buttons.cpp",
    "data_source.cpp",
    "assistant_ascii.cpp",
    "ui_tft.cpp",
    "lilyxm7.ino",
]


def strip_and_prepare(filename: str, text: str) -> str:
    out_lines: list[str] = []
    for line in text.splitlines():
        if PRAGMA_ONCE_RE.match(line):
            continue
        m_loc = INCLUDE_RE.match(line)
        if m_loc and m_loc.group(1) in LOCAL_HEADERS:
            continue
        m_sys = SYS_INCLUDE_RE.match(line)
        if m_sys:
            # Se colocan una sola vez al inicio del archivo unificado
            continue
        out_lines.append(line)
    return "\n".join(out_lines).strip()


def build_single_sketch() -> str:
    header = """// ============================================================================
// lilyxm7 v1.0 - Sketch unificado generado automaticamente para Wokwi Web
// Fuente original: C:\\Git\\lilyxm7\\lilyxm7\\
// ============================================================================

#define LILYXM7_SIMULATION_MODE 1

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
"""
    sections = [header]
    for name in ORDER:
        src_path = SRC_DIR / name
        raw = src_path.read_text(encoding="utf-8")
        cleaned = strip_and_prepare(name, raw)
        sections.append(
            f"\n// ----------------------------------------------------------------------------\n"
            f"// Archivo origen: lilyxm7/{name}\n"
            f"// ----------------------------------------------------------------------------\n"
            f"{cleaned}\n"
        )
    return "\n".join(sections)


def build_diagram_json() -> str:
    src_diag = PROJECT_ROOT / "wokwi" / "diagram.json"
    data = json.loads(src_diag.read_text(encoding="utf-8"))
    data["serialMonitor"] = {"display": "always"}
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def build_libraries_txt() -> str:
    return (PROJECT_ROOT / "wokwi" / "libraries.txt").read_text(encoding="utf-8").strip() + "\n"


def build_helper_html(sketch: str, diagram: str, libraries: str) -> str:
    sk_js = json.dumps(sketch)
    dg_js = json.dumps(diagram)
    lb_js = json.dumps(libraries)
    return f"""<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>lilyxm7 — Lanzador Wokwi Web</title>
  <style>
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      padding: 28px;
      background: #020802;
      color: #00ff41;
      font-family: Consolas, "Courier New", monospace;
    }}
    .wrap {{ max-width: 900px; margin: 0 auto; }}
    h1 {{ margin: 0 0 6px; font-size: 26px; letter-spacing: 1px; }}
    .sub {{ color: #00b82e; margin-bottom: 22px; }}
    .card {{
      border: 1px solid #007a1f;
      background: #041004;
      border-radius: 8px;
      padding: 18px 20px;
      margin-bottom: 16px;
    }}
    .step-title {{ font-size: 16px; font-weight: bold; margin: 0 0 8px; color: #a8ffb8; }}
    .hint {{ color: #6bd982; font-size: 13px; margin: 0 0 12px; line-height: 1.45; }}
    .row {{ display: flex; gap: 12px; flex-wrap: wrap; align-items: center; }}
    button, a.btn {{
      background: #00ff41;
      color: #000;
      border: none;
      border-radius: 6px;
      padding: 11px 18px;
      font-family: inherit;
      font-size: 14px;
      font-weight: bold;
      cursor: pointer;
      text-decoration: none;
      display: inline-block;
    }}
    button:hover, a.btn:hover {{ background: #a8ffb8; }}
    button.sec {{
      background: #082408;
      color: #00ff41;
      border: 1px solid #00ff41;
    }}
    button.sec:hover {{ background: #0d3a0d; }}
    .status {{ color: #a8ffb8; font-size: 13px; font-weight: bold; }}
    details {{ margin-top: 10px; }}
    summary {{ cursor: pointer; color: #00b82e; font-size: 13px; }}
    pre {{
      background: #000;
      border: 1px solid #003b00;
      padding: 10px;
      max-height: 220px;
      overflow: auto;
      font-size: 12px;
      color: #6bd982;
      white-space: pre;
    }}
    code {{ color: #a8ffb8; }}
  </style>
</head>
<body>
  <div class="wrap">
    <h1>LILYXM7 v1.0 // Wokwi Web (Navegador)</h1>
    <div class="sub">Paquete generado automaticamente desde C:\\Git\\lilyxm7\\lilyxm7\\ — 100% gratis, sin licencia ni VS Code</div>

    <div class="card">
      <div class="step-title">Paso 1 — Abre un proyecto nuevo de ESP32-S3 en Wokwi</div>
      <p class="hint">Se abrira en otra pestana de tu navegador (Chrome / Edge).</p>
      <div class="row">
        <a class="btn" href="https://wokwi.com/projects/new/esp32-s3" target="_blank" rel="noopener">
          Abrir Wokwi Web (ESP32-S3) ↗
        </a>
      </div>
    </div>

    <div class="card">
      <div class="step-title">Paso 2 — Pega los 3 archivos en Wokwi (1 clic cada uno)</div>
      <p class="hint">
        En Wokwi Web haz clic en cada pestana, selecciona todo (<code>Ctrl+A</code>) y pega (<code>Ctrl+V</code>):<br />
        • <code>sketch.ino</code>: todo tu firmware C++ unificado (menu, Lily ASCII Matrix, OBD-II 01 05, modo SIMULACION).<br />
        • <code>diagram.json</code>: ESP32-S3 + TFT 2.4" ILI9341 + botones ↑ ↓ A B + potenciometro + switch de senal.<br />
        • <code>libraries.txt</code>: en Wokwi dale al boton <b>+</b> (o pestana Library Manager) y agrega <b>Adafruit GFX Library</b> y <b>Adafruit ILI9341</b>, o crea la pestana <code>libraries.txt</code> y pega esto.
      </p>
      <div class="row">
        <button onclick="copyText('sketch', 'st1')">1. Copiar sketch.ino</button>
        <button onclick="copyText('diagram', 'st1')">2. Copiar diagram.json</button>
        <button onclick="copyText('libs', 'st1')">3. Copiar libraries.txt</button>
        <span id="st1" class="status"></span>
      </div>
      <details>
        <summary>Ver vista previa de libraries.txt y diagram.json</summary>
        <pre>{html.escape(libraries)}</pre>
        <pre>{html.escape(diagram)}</pre>
      </details>
    </div>

    <div class="card">
      <div class="step-title">Paso 3 — Dale al boton verde Play ▶ en Wokwi</div>
      <p class="hint">
        • Usa los botones fisicos (o teclas <code>↑</code>, <code>↓</code>, <code>a</code>, <code>b</code>) para entrar a <b>Temperatura</b> y <b>Estado de conexion</b>.<br />
        • Gira el <b>potenciometro</b> para variar entre 20 °C y 120 °C y ver reaccionar a Lily.<br />
        • Mueve el <b>switch deslizante</b> a la derecha para probar perdida de senal (tras 3 s aparece <b>Sin datos del auto</b> + <b>Ultima lectura</b>).
      </p>
    </div>
  </div>

  <script>
    const PAYLOADS = {{
      sketch: {sk_js},
      diagram: {dg_js},
      libs: {lb_js}
    }};

    async function copyText(key, statusId) {{
      const el = document.getElementById(statusId);
      try {{
        await navigator.clipboard.writeText(PAYLOADS[key]);
        el.textContent = "✓ Copiado (" + key + ")! Ahora presiona Ctrl+A y Ctrl+V en Wokwi.";
      }} catch (err) {{
        const ta = document.createElement("textarea");
        ta.value = PAYLOADS[key];
        document.body.appendChild(ta);
        ta.select();
        document.execCommand("copy");
        document.body.removeChild(ta);
        el.textContent = "✓ Copiado (" + key + ")! Ahora presiona Ctrl+A y Ctrl+V en Wokwi.";
      }}
    }}
  </script>
</body>
</html>
"""


def main() -> int:
    open_browser = "--no-open" not in sys.argv
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    sketch = build_single_sketch()
    diagram = build_diagram_json()
    libraries = build_libraries_txt()
    helper_html = build_helper_html(sketch, diagram, libraries)

    (OUT_DIR / "sketch.ino").write_text(sketch, encoding="utf-8")
    (OUT_DIR / "diagram.json").write_text(diagram, encoding="utf-8")
    (OUT_DIR / "libraries.txt").write_text(libraries, encoding="utf-8")
    html_path = OUT_DIR / "abrir_wokwi.html"
    html_path.write_text(helper_html, encoding="utf-8")

    msg = f"Paquete Wokwi Web generado en {OUT_DIR} (sketch.ino={len(sketch)} bytes)"
    print(f"[OK] {msg}")
    log_line(LOG_COMPONENT, msg)

    if open_browser:
        url = html_path.resolve().as_uri()
        print(f"[lilyxm7] Abriendo panel Wokwi Web en navegador: {html_path}")
        webbrowser.open(url)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
