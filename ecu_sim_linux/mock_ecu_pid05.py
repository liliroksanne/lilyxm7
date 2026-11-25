#!/usr/bin/env python3
"""
Emulador ligero de respaldo para PID 05 (temperatura del refrigerante) sobre Linux SocketCAN (vcan0).
Útil si quieres validar rápidamente la trama 01 05 o simular pérdida de señal sin instalar dependencias externas,
como complemento a ecu-simulator.
"""

import socket
import struct
import sys
import threading

CAN_FRAME_FMT = "=IB3x8s"
OBD_REQ_ID = 0x7DF
OBD_RESP_ID = 0x7E8


def encode_temp(temp_c: int) -> int:
    clamped = max(-40, min(215, temp_c))
    return clamped + 40


def main() -> int:
    iface = sys.argv[1] if len(sys.argv) >= 2 else "vcan0"
    state = {"temp_c": 88, "silent": False, "running": True}

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((iface,))
    sock.settimeout(0.2)

    print(f"[mock-ecu] Escuchando en {iface} (PID 01 05). Temp inicial: {state['temp_c']} °C")
    print("[mock-ecu] Comandos: escribe un número (ej. 95) para cambiar °C, 'off' para cortar señal, 'on' para reanudar, 'q' para salir.")

    def input_loop() -> None:
        while state["running"]:
            try:
                cmd = input().strip().lower()
            except EOFError:
                state["running"] = False
                break
            if cmd in ("q", "quit", "exit"):
                state["running"] = False
                break
            elif cmd == "off":
                state["silent"] = True
                print("[mock-ecu] Señal cortada (la ECU no responderá).")
            elif cmd == "on":
                state["silent"] = False
                print(f"[mock-ecu] Señal activa ({state['temp_c']} °C).")
            else:
                try:
                    val = int(cmd)
                    state["temp_c"] = max(-40, min(215, val))
                    state["silent"] = False
                    print(f"[mock-ecu] Temperatura actualizada a {state['temp_c']} °C (byte A=0x{encode_temp(state['temp_c']):02X}).")
                except ValueError:
                    print("[mock-ecu] Comando no reconocido. Usa un entero (-40..215), 'off', 'on' o 'q'.")

    t = threading.Thread(target=input_loop, daemon=True)
    t.start()

    try:
        while state["running"]:
            try:
                raw = sock.recv(16)
            except socket.timeout:
                continue
            can_id, dlc, data = struct.unpack(CAN_FRAME_FMT, raw)
            can_id &= 0x7FF
            if state["silent"]:
                continue
            if can_id in (OBD_REQ_ID, 0x7E0) and dlc >= 3 and data[1] == 0x01 and data[2] == 0x05:
                byte_a = encode_temp(state["temp_c"])
                resp_data = bytes([0x03, 0x41, 0x05, byte_a, 0x00, 0x00, 0x00, 0x00])
                frame = struct.pack(CAN_FRAME_FMT, OBD_RESP_ID, 8, resp_data)
                sock.send(frame)
    except KeyboardInterrupt:
        pass
    finally:
        state["running"] = False
        sock.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
