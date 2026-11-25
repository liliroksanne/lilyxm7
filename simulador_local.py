#!/usr/bin/env python3
"""
lilyxm7 — Simulador interactivo local para Windows (sin dependencias externas).
Replica la pantalla TFT 2.4" ILI9341 (320x240) en estética Matrix, la chica ASCII ("Lily"),
el menú físico (↑, ↓, A, B), el potenciómetro virtual (20..120 °C), las tramas OBD-II 01 05
y la pérdida de señal ("Sin datos del auto" + "Última lectura").
"""

import time
import tkinter as tk
from dataclasses import dataclass

# Paleta Matrix
BG = "#000000"
PANEL_BG = "#030A03"
M_DARK = "#003B00"
M_DIM = "#007A1F"
M_MID = "#00B82E"
M_BRIGHT = "#00FF41"
M_GLOW = "#A8FFB8"

# Umbrales idénticos a lilyxm7/config.h
TEMP_COLD_MAX = 59
TEMP_WARMING_MAX = 79
TEMP_OPTIMAL_MAX = 97
TEMP_WARN_MAX = 107
SIGNAL_TIMEOUT_SEC = 3.0
POLL_INTERVAL_MS = 350


@dataclass
class TempReading:
    has_live_signal: bool = False
    has_last_value: bool = False
    temp_c: int = 0
    previous_temp_c: int = 0
    delta_c: int = 0
    trend: str = "NONE"  # NONE | STABLE | RISING | FALLING
    last_valid_ts: float = 0.0
    valid_count: int = 0


def get_mood(screen: str, r: TempReading) -> tuple[str, str, str, str, bool]:
    """Devuelve (mood, mood_tag, line1, line2, is_alert) igual que assistant_ascii.cpp."""
    if screen == "MAIN_MENU":
        if r.has_live_signal:
            return ("MENU_IDLE", "EN LINEA", "Modo simulación listo.", "Usa ↑/↓ y A para entrar.", False)
        return ("NO_SIGNAL", "EN ESPERA", "Modo simulación listo.", "Usa ↑/↓ y A para entrar.", False)

    if screen == "CONNECTION_STATUS":
        if r.has_live_signal:
            return ("OPTIMAL", "ENLACE OK", "Enlace virtual activo.", "Pulsa B para volver.", False)
        l2 = "Última lectura guardada." if r.has_last_value else "Esperando respuesta ECU."
        return ("NO_SIGNAL", "SIN SEÑAL", "Sin datos del auto.", l2, True)

    # Pantalla TEMPERATURE
    if not r.has_live_signal:
        l1 = "Sin datos. Guardé última." if r.has_last_value else "Sin datos del auto..."
        l2 = f"Conservando última: {r.temp_c}°C" if r.has_last_value else "Revisa ignición o cable OBD"
        return ("NO_SIGNAL", "SIN SEÑAL", l1, l2, True)

    if r.trend == "RISING":
        l2 = f"Subió +{r.delta_c}°C desde {r.previous_temp_c}°C"
    elif r.trend == "FALLING":
        l2 = f"Bajó {r.delta_c}°C desde {r.previous_temp_c}°C"
    elif r.trend == "STABLE":
        l2 = f"Estable respecto a {r.previous_temp_c}°C"
    else:
        l2 = "Primera lectura válida"

    if r.temp_c <= TEMP_COLD_MAX:
        if r.trend == "RISING":
            l1 = "Subiendo, buen arranque."
        elif r.trend == "FALLING":
            l1 = "Bajó, motor aún frío."
        else:
            l1 = "Motor frío, acelera suave."
        return ("COLD", "MOTOR FRIO", l1, l2, False)

    if r.temp_c <= TEMP_WARMING_MAX:
        if r.trend == "RISING":
            l1 = "Subiendo a punto ideal."
        elif r.trend == "FALLING":
            l1 = "Bajó un poco, atenta."
        else:
            l1 = "Tomando temperatura."
        return ("WARMING", "CALENTANDO", l1, l2, False)

    if r.temp_c <= TEMP_OPTIMAL_MAX:
        if r.trend == "RISING":
            l1 = "Subió, pero sigue ideal."
        elif r.trend == "FALLING":
            l1 = "Bajó, todo bajo control."
        else:
            l1 = "Vitara en temp óptima."
        return ("OPTIMAL", "OPTIMO", l1, l2, False)

    if r.temp_c <= TEMP_WARN_MAX:
        if r.trend == "RISING":
            l1 = "¡Subió! Vigila el motor."
        elif r.trend == "FALLING":
            l1 = "Está bajando, bien."
        else:
            l1 = "Temperatura alta, alerta."
        return ("WARM_WARN", "TEMP ALTA", l1, l2, True)

    if r.trend == "RISING":
        l1 = "¡ALERTA! Sigue subiendo."
    elif r.trend == "FALLING":
        l1 = "Aún crítica, detente."
    else:
        l1 = "¡PELIGRO! Sobrecalent."
    return ("OVERHEAT", "PELIGRO!", l1, l2, True)


def get_ascii_lines(mood: str, blink_closed: bool) -> list[str]:
    top = " ! .---=======---. ! " if mood == "OVERHEAT" else "   .---=======---.   "
    lines = [
        top,
        "  / .-/|||||||\\-. \\  ",
        " | / /  _   _  \\ \\ | ",
    ]
    if mood == "MENU_IDLE":
        lines += [
            " || |  -     -  | || " if blink_closed else " || |  ^     ^  | || ",
            " || |     .     | || ",
            " | \\ \\   \\_/   / / | ",
            " |  \\ '-------' /  | ",
        ]
    elif mood == "COLD":
        lines += [
            " || |  -     -  | || " if blink_closed else " || |  >     <  | || ",
            " || |     .     | || ",
            " | \\ \\   ~~~   / / | ",
            " |  \\ '-------' /  | ",
        ]
    elif mood == "WARMING":
        lines += [
            " || |  -     -  | || " if blink_closed else " || |  o     o  | || ",
            " || |     .     | || ",
            " | \\ \\    v    / / | ",
            " |  \\ '-------' /  | ",
        ]
    elif mood == "OPTIMAL":
        lines += [
            " || |  -     -  | || " if blink_closed else " || |  ^     ^  | || ",
            " || |   * . *   | || ",
            " | \\ \\  \\___/  / / | ",
            " |  \\ '-------' /  | ",
        ]
    elif mood == "WARM_WARN":
        lines += [
            " || |  -    -'  | || " if blink_closed else " || |  O    O'  | || ",
            " || |     .     | || ",
            " | \\ \\    o    / / | ",
            " |  \\ '-------' /  | ",
        ]
    elif mood == "OVERHEAT":
        lines += [
            " || |  >     <  | || " if blink_closed else " || |  X     X  | || ",
            " || |     !     | || ",
            " | \\ \\   ===   / / | ",
            " !  \\ '-------' /  ! ",
        ]
    else:
        lines += [
            " || |  -     -  | || " if blink_closed else " || |  ?     ?  | || ",
            " || |     .     | || ",
            " | \\ \\   ___   / / | ",
            " |  \\ '-------' /  | ",
        ]

    lines += [
        " |   '.__   __.'   | ",
        " |  .--/ | | \\--.  | ",
        " \\_/  | [010] |  \\_/ ",
        "      |  ___  |      ",
        "     /  /   \\  \\     ",
    ]
    return lines


class LilySimApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("lilyxm7 v1.0 — Simulador Local (Suzuki Grand Vitara 2008 V6 2.7L)")
        self.root.configure(bg="#0B0F0B")
        self.root.resizable(False, False)

        self.screen = "MAIN_MENU"
        self.selected_idx = 0
        self.reading = TempReading()
        self.tx_requests = 0
        self.anim_tick = 0
        self.pot_temp = tk.IntVar(value=86)
        self.signal_on = tk.BooleanVar(value=True)
        self.last_can_log = "TX 7DF#0201050000000000 | esperando..."

        # Pantalla TFT escalada 2x (640x480 representa 320x240)
        tft_frame = tk.Frame(root, bg=M_DIM, padx=3, pady=3)
        tft_frame.pack(padx=14, pady=(14, 8))

        self.canvas = tk.Canvas(tft_frame, width=640, height=480, bg=BG, highlightthickness=0)
        self.canvas.pack()

        # Controles físicos simulados (botones ↑ ↓ A B + potenciómetro + switch ECU)
        ctrl = tk.Frame(root, bg="#101810", padx=12, pady=10)
        ctrl.pack(fill="x", padx=14, pady=(0, 8))

        btn_box = tk.LabelFrame(ctrl, text=" Botones físicos (o teclado: ↑ ↓ A B) ", fg=M_MID, bg="#101810", font=("Consolas", 9, "bold"))
        btn_box.pack(side="left", padx=(0, 12))

        btn_cfg = {"bg": "#082408", "fg": M_BRIGHT, "activebackground": M_BRIGHT, "activeforeground": BG, "font": ("Consolas", 11, "bold"), "width": 5, "relief": "ridge", "bd": 2}
        tk.Button(btn_box, text="↑", command=self.on_up, **btn_cfg).grid(row=0, column=0, padx=4, pady=6)
        tk.Button(btn_box, text="↓", command=self.on_down, **btn_cfg).grid(row=0, column=1, padx=4, pady=6)
        tk.Button(btn_box, text="A", command=self.on_a, **btn_cfg).grid(row=0, column=2, padx=4, pady=6)
        tk.Button(btn_box, text="B", command=self.on_b, **btn_cfg).grid(row=0, column=3, padx=4, pady=6)

        pot_box = tk.LabelFrame(ctrl, text=" Potenciómetro virtual (20–120 °C) ", fg=M_MID, bg="#101810", font=("Consolas", 9, "bold"))
        pot_box.pack(side="left", fill="x", expand=True, padx=(0, 12))

        self.slider = tk.Scale(
            pot_box, from_=20, to=120, orient="horizontal", variable=self.pot_temp,
            bg="#101810", fg=M_BRIGHT, troughcolor="#041504", activebackground=M_BRIGHT,
            highlightthickness=0, font=("Consolas", 9, "bold"), length=210
        )
        self.slider.pack(padx=6, pady=2)

        ecu_box = tk.LabelFrame(ctrl, text=" Señal ECU ", fg=M_MID, bg="#101810", font=("Consolas", 9, "bold"))
        ecu_box.pack(side="left")

        self.chk_signal = tk.Checkbutton(
            ecu_box, text="ECU activa", variable=self.signal_on,
            bg="#101810", fg=M_BRIGHT, selectcolor="#062006",
            activebackground="#101810", activeforeground=M_GLOW,
            font=("Consolas", 10, "bold")
        )
        self.chk_signal.pack(padx=8, pady=10)

        # Monitor de trama CAN OBD-II (Servicio 01, PID 05)
        self.can_label = tk.Label(
            root, text=self.last_can_log, bg="#050A05", fg=M_MID,
            font=("Consolas", 9), anchor="w", padx=10, pady=5
        )
        self.can_label.pack(fill="x", padx=14, pady=(0, 12))

        # Atajos de teclado
        root.bind("<Up>", lambda _e: self.on_up())
        root.bind("<Down>", lambda _e: self.on_down())
        root.bind("a", lambda _e: self.on_a())
        root.bind("A", lambda _e: self.on_a())
        root.bind("<Return>", lambda _e: self.on_a())
        root.bind("b", lambda _e: self.on_b())
        root.bind("B", lambda _e: self.on_b())
        root.bind("<Escape>", lambda _e: self.on_b())

        self.poll_data()
        self.tick_anim()

    def on_up(self) -> None:
        if self.screen == "MAIN_MENU":
            self.selected_idx = (self.selected_idx - 1) % 2
            self.render()

    def on_down(self) -> None:
        if self.screen == "MAIN_MENU":
            self.selected_idx = (self.selected_idx + 1) % 2
            self.render()

    def on_a(self) -> None:
        if self.screen == "MAIN_MENU":
            self.screen = "TEMPERATURE" if self.selected_idx == 0 else "CONNECTION_STATUS"
            self.render()

    def on_b(self) -> None:
        if self.screen != "MAIN_MENU":
            self.screen = "MAIN_MENU"
            self.render()

    def poll_data(self) -> None:
        now = time.monotonic()
        self.tx_requests += 1

        if self.signal_on.get():
            new_temp = int(self.pot_temp.get())
            if not self.reading.has_last_value:
                self.reading.previous_temp_c = new_temp
                self.reading.temp_c = new_temp
                self.reading.delta_c = 0
                self.reading.trend = "NONE"
                self.reading.has_last_value = True
            else:
                diff = new_temp - self.reading.temp_c
                self.reading.previous_temp_c = self.reading.temp_c
                self.reading.temp_c = new_temp
                self.reading.delta_c = diff
                if diff >= 1:
                    self.reading.trend = "RISING"
                elif diff <= -1:
                    self.reading.trend = "FALLING"
                else:
                    self.reading.trend = "STABLE"
            self.reading.has_live_signal = True
            self.reading.last_valid_ts = now
            self.reading.valid_count += 1

            byte_a = max(0, min(255, new_temp + 40))
            self.last_can_log = (
                f"OBD-II 01 05 | TX 7DF#0201050000000000  -->  "
                f"RX 7E8#034105{byte_a:02X}00000000  (A={byte_a} -> {new_temp} °C)"
            )
        else:
            elapsed = now - self.reading.last_valid_ts if self.reading.has_last_value else SIGNAL_TIMEOUT_SEC
            if self.reading.has_live_signal and elapsed >= SIGNAL_TIMEOUT_SEC:
                self.reading.has_live_signal = False
            rem = max(0.0, SIGNAL_TIMEOUT_SEC - elapsed)
            if self.reading.has_live_signal:
                self.last_can_log = f"OBD-II 01 05 | TX 7DF#020105... sin respuesta (timeout en {rem:.1f}s)"
            else:
                self.last_can_log = "OBD-II 01 05 | SIN RESPUESTA DE LA ECU -> Mostrando 'Sin datos del auto'"

        self.can_label.config(text=self.last_can_log)
        self.render()
        self.root.after(POLL_INTERVAL_MS, self.poll_data)

    def tick_anim(self) -> None:
        self.anim_tick += 1
        self.render()
        self.root.after(450, self.tick_anim)

    def render(self) -> None:
        c = self.canvas
        c.delete("all")
        s = 2  # Factor de escala 320x240 -> 640x480

        mood, mood_tag, line1, line2, is_alert = get_mood(self.screen, self.reading)
        blink_closed = (self.anim_tick % 8) == 7

        # Barra superior
        c.create_line(0, 21 * s, 320 * s, 21 * s, fill=M_DIM, width=2)
        titles = {
            "MAIN_MENU": "LILYXM7 // VITARA 2.7",
            "TEMPERATURE": "LILYXM7 // TEMPERATURA",
            "CONNECTION_STATUS": "LILYXM7 // CONEXIÓN",
        }
        c.create_text(12, 20, text=titles[self.screen], fill=M_BRIGHT, font=("Consolas", 11, "bold"), anchor="w")

        # Insignia SIMULACIÓN
        c.create_rectangle(222 * s, 3 * s, 316 * s, 18 * s, fill=M_BRIGHT, outline=M_BRIGHT)
        c.create_text(269 * s, 10.5 * s, text="SIMULACIÓN", fill=BG, font=("Consolas", 10, "bold"))

        # Panel izquierdo: Chica ASCII Matrix
        border_col = M_GLOW if is_alert else M_DIM
        c.create_rectangle(4 * s, 26 * s, 148 * s, 182 * s, fill=PANEL_BG, outline=border_col, width=2)
        rain = "01 10 LILY.ASCII 01" if (self.anim_tick & 1) else "10 01 LILY.ASCII 10"
        c.create_text(76 * s, 34 * s, text=rain, fill=M_DARK, font=("Consolas", 9, "bold"))

        ascii_lines = get_ascii_lines(mood, blink_closed)
        for i, line in enumerate(ascii_lines):
            col = M_MID
            if 3 <= i <= 6:
                col = M_GLOW if is_alert else M_BRIGHT
            elif i == 9:
                col = M_BRIGHT
            c.create_text(76 * s, (46 + i * 9.5) * s, text=line, fill=col, font=("Consolas", 9, "bold"))

        c.create_line(8 * s, 164 * s, 144 * s, 164 * s, fill=M_DARK)
        c.create_text(16 * s, 173 * s, text="ESTADO: ", fill=M_DIM, font=("Consolas", 9, "bold"), anchor="w")
        c.create_text(64 * s, 173 * s, text=mood_tag, fill=M_GLOW if is_alert else M_BRIGHT, font=("Consolas", 9, "bold"), anchor="w")

        # Panel derecho según pantalla
        rx, ry, rw, rh = 154, 26, 162, 156
        r_border = M_DIM if (self.screen == "MAIN_MENU" or self.reading.has_live_signal) else M_GLOW
        c.create_rectangle(rx * s, ry * s, (rx + rw) * s, (ry + rh) * s, fill=PANEL_BG, outline=r_border, width=2)

        if self.screen == "MAIN_MENU":
            c.create_text((rx + 10) * s, (ry + 12) * s, text="MENÚ PRINCIPAL", fill=M_BRIGHT, font=("Consolas", 11, "bold"), anchor="w")
            c.create_line((rx + 8) * s, (ry + 22) * s, (rx + rw - 8) * s, (ry + 22) * s, fill=M_DARK)

            for idx, label in enumerate(["Temperatura", "Estado de conexión"]):
                iy = ry + 36 + idx * 38
                if idx == self.selected_idx:
                    c.create_rectangle((rx + 8) * s, iy * s, (rx + rw - 8) * s, (iy + 28) * s, fill=M_BRIGHT, outline=M_BRIGHT)
                    c.create_text((rx + 14) * s, (iy + 14) * s, text=f"> {label}", fill=BG, font=("Consolas", 11, "bold"), anchor="w")
                else:
                    c.create_rectangle((rx + 8) * s, iy * s, (rx + rw - 8) * s, (iy + 28) * s, fill=BG, outline=M_DIM)
                    c.create_text((rx + 14) * s, (iy + 14) * s, text=f"  {label}", fill=M_MID, font=("Consolas", 11), anchor="w")

            c.create_line((rx + 8) * s, (ry + 122) * s, (rx + rw - 8) * s, (ry + 122) * s, fill=M_DARK)
            c.create_text((rx + 10) * s, (ry + 134) * s, text="Vitara 2008 V6 2.7L", fill=M_DIM, font=("Consolas", 9), anchor="w")
            c.create_text((rx + 10) * s, (ry + 146) * s, text="OBD-II 01 05 // v1.0", fill=M_DARK, font=("Consolas", 9), anchor="w")

        elif self.screen == "TEMPERATURE":
            c.create_text((rx + 8) * s, (ry + 11) * s, text="REFRIGERANTE (01 05)", fill=M_MID, font=("Consolas", 10, "bold"), anchor="w")
            c.create_line((rx + 6) * s, (ry + 20) * s, (rx + rw - 6) * s, (ry + 20) * s, fill=M_DARK)

            if self.reading.has_live_signal:
                t_col = M_GLOW if self.reading.temp_c > TEMP_WARN_MAX else M_BRIGHT
                c.create_text((rx + rw / 2) * s, (ry + 52) * s, text=f"{self.reading.temp_c:3d}°C", fill=t_col, font=("Consolas", 34, "bold"))

                if self.reading.trend == "RISING":
                    trend_txt = f"↑ Subió (+{self.reading.delta_c}°C)"
                elif self.reading.trend == "FALLING":
                    trend_txt = f"↓ Bajó ({self.reading.delta_c}°C)"
                elif self.reading.trend == "STABLE":
                    trend_txt = "= Estable"
                else:
                    trend_txt = "* Lectura inicial"
                c.create_text((rx + 12) * s, (ry + 86) * s, text=trend_txt, fill=M_BRIGHT, font=("Consolas", 10, "bold"), anchor="w")

                self._draw_bar(rx + 10, ry + 102, rw - 20, 14, self.reading.temp_c, True)
                c.create_text((rx + 10) * s, (ry + 126) * s, text="20°C", fill=M_DARK, font=("Consolas", 8), anchor="w")
                c.create_text((rx + 74) * s, (ry + 126) * s, text="85°C", fill=M_DIM, font=("Consolas", 8), anchor="w")
                c.create_text((rx + 118) * s, (ry + 126) * s, text="120°C", fill=M_DARK, font=("Consolas", 8), anchor="w")
                c.create_text((rx + 10) * s, (ry + 144) * s, text=f"Lecturas OK: {self.reading.valid_count}", fill=M_DIM, font=("Consolas", 9), anchor="w")
            else:
                c.create_rectangle((rx + 8) * s, (ry + 30) * s, (rx + rw - 8) * s, (ry + 56) * s, fill=M_DARK, outline=M_GLOW, width=2)
                c.create_text((rx + rw / 2) * s, (ry + 43) * s, text="Sin datos del auto", fill=M_GLOW, font=("Consolas", 11, "bold"))

                if self.reading.has_last_value:
                    c.create_text((rx + 12) * s, (ry + 72) * s, text="Última lectura:", fill=M_MID, font=("Consolas", 10, "bold"), anchor="w")
                    c.create_text((rx + rw / 2) * s, (ry + 98) * s, text=f"{self.reading.temp_c} °C", fill=M_DIM, font=("Consolas", 26, "bold"))
                    self._draw_bar(rx + 10, ry + 118, rw - 20, 12, self.reading.temp_c, False)
                    c.create_text((rx + 10) * s, (ry + 142) * s, text="Valor congelado", fill=M_DARK, font=("Consolas", 9), anchor="w")
                else:
                    c.create_text((rx + rw / 2) * s, (ry + 90) * s, text="-- °C", fill=M_DIM, font=("Consolas", 26, "bold"))
                    c.create_text((rx + 12) * s, (ry + 120) * s, text="Sin lecturas previas", fill=M_DIM, font=("Consolas", 9), anchor="w")
                    c.create_text((rx + 12) * s, (ry + 136) * s, text="Esperando ECU 01 05", fill=M_DARK, font=("Consolas", 9), anchor="w")

        else:  # CONNECTION_STATUS
            c.create_text((rx + 8) * s, (ry + 11) * s, text="DIAGNÓSTICO ENLACE", fill=M_BRIGHT, font=("Consolas", 10, "bold"), anchor="w")
            c.create_line((rx + 6) * s, (ry + 20) * s, (rx + rw - 6) * s, (ry + 20) * s, fill=M_DARK)

            age = int(time.monotonic() - self.reading.last_valid_ts) if self.reading.has_last_value else 0
            last_str = f"{self.reading.temp_c}°C ({age}s)" if self.reading.has_last_value else "ninguna"
            rows = [
                ("Modo: ", "SIMULACIÓN", M_BRIGHT),
                ("ECU:  ", "CONECTADA" if self.reading.has_live_signal else "Sin datos", M_BRIGHT if self.reading.has_live_signal else M_GLOW),
                ("Bus:  ", "Local virtual", M_MID),
                ("PID:  ", "01 05 (Refrig.)", M_MID),
                ("TX/RX:", f" {self.tx_requests} / {self.reading.valid_count}", M_MID),
                ("Última:", f" {last_str}", M_DIM),
                ("Errores:", " 0", M_DARK),
            ]
            for idx, (k, v, col) in enumerate(rows):
                yy = (ry + 32 + idx * 17) * s
                c.create_text((rx + 8) * s, yy, text=k, fill=M_DIM, font=("Consolas", 9, "bold"), anchor="w")
                c.create_text((rx + 50) * s, yy, text=v, fill=col, font=("Consolas", 9, "bold"), anchor="w")

        # Caja de diálogo inferior de Lily
        c.create_rectangle(4 * s, 186 * s, 316 * s, 222 * s, fill=PANEL_BG, outline=M_GLOW if is_alert else M_DIM, width=2)
        c.create_text(12 * s, 197 * s, text="> LILY: ", fill=M_MID, font=("Consolas", 10, "bold"), anchor="w")
        c.create_text(58 * s, 197 * s, text=line1, fill=M_GLOW if is_alert else M_BRIGHT, font=("Consolas", 10, "bold"), anchor="w")
        c.create_text(22 * s, 212 * s, text=line2, fill=M_MID, font=("Consolas", 9), anchor="w")

        # Pie de ayuda
        c.create_line(0, 225 * s, 320 * s, 225 * s, fill=M_DARK)
        hint = "[↑/↓] Mover   [A] Seleccionar" if self.screen == "MAIN_MENU" else "[B] Volver al menú principal"
        c.create_text(16, 233 * s, text=hint, fill=M_DIM, font=("Consolas", 9), anchor="w")

    def _draw_bar(self, x: int, y: int, w: int, h: int, temp_c: int, live: bool) -> None:
        s = 2
        c = self.canvas
        c.create_rectangle(x * s, y * s, (x + w) * s, (y + h) * s, fill=BG, outline=M_DIM, width=2)
        clamped = max(20, min(120, temp_c))
        fill_w = ((clamped - 20) * (w - 4)) / 100.0
        if fill_w > 0:
            col = (M_GLOW if temp_c > TEMP_WARN_MAX else M_BRIGHT) if live else M_DIM
            c.create_rectangle((x + 2) * s, (y + 2) * s, (x + 2 + fill_w) * s, (y + h - 2) * s, fill=col, outline="")
        for mark_pct in (0.4, 0.8):
            mx = (x + 2 + mark_pct * (w - 4)) * s
            c.create_line(mx, y * s, mx, (y + h) * s, fill=BG, width=2)


def main() -> None:
    root = tk.Tk()
    LilySimApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
