# lilyxm7 — Escáner OBD-II para Suzuki Grand Vitara 2008 V6 2.7 L

Escáner OBD-II v1.0 programado en **C++ (Arduino IDE)** para **ESP32-S3** con pantalla física **TFT 2.4" (ILI9341)**, menú navegado con 4 botones (**↑, ↓, A, B**) y una **asistente virtual ASCII ("Lily")** en verde Matrix sobre fondo negro cuyas expresiones y mensajes cambian dinámicamente según la temperatura del refrigerante del motor.

## Qué incluye esta primera versión (v1.0)

- **Asistente ASCII Matrix (`assistant_ascii.*`)**: chica dibujada con caracteres ASCII en tonos verde fósforo sobre negro, con parpadeo suave y expresiones controladas por código (*en espera*, *motor frío*, *calentando*, *óptimo*, *temperatura alta*, *peligro/sobrecalentamiento* y *sin señal*).
- **Menú físico (`buttons.*`, `ui_tft.*`)**: navegación con **↑** y **↓**, selección con **A** y regreso con **B**. Opciones iniciales:
  1. **Temperatura** (lectura en vivo en °C, tendencia *subió/bajó/estable*, barra térmica y reacción de la asistente).
  2. **Estado de conexión** (modo activo, estado de la ECU, estado del bus CAN/TWAI, tramas TX/RX y última lectura).
- **Temperatura en vivo (`obd_protocol.h`, `data_source.*`)**: consulta OBD-II estándar **ISO 15765-4 CAN (11-bit, 500 kbps)**, **Servicio 01, PID 05** (`0x7DF → 02 01 05`), decodifica `A - 40` en °C y actualiza la interfaz con cada respuesta válida.
- **Pérdida de señal**: si la ECU deja de responder durante 3 segundos, muestra **«Sin datos del auto»** e identifica el último valor válido como **«Última lectura»**.
- **Función común de temperatura (`getCoolantTemperature()`)**: tanto el menú como la asistente consumen exactamente la misma función, cambiando entre datos simulados (Wokwi) y datos reales (OBD-II) con un único flag (`LILYXM7_SIMULATION_MODE` en `lilyxm7/config.h`).
- **Simulación del tablero en Wokwi (`wokwi/`)**: circuito completo con ESP32-S3, ILI9341, 4 botones, potenciómetro virtual (20–120 °C), switch de corte de señal e indicador **`SIMULACIÓN`** en pantalla.
- **Simulación de la ECU en Linux (`ecu_sim_linux/`)**: flujo separado para probar el protocolo real `lilyxm7/obd_protocol.h` sobre SocketCAN (`vcan0`) contra `ecu-simulator` (incluye pruebas unitarias en `tests/test_obd_protocol.cpp`).

## Estructura del proyecto

```text
C:\Git\lilyxm7\
├── lilyxm7/                     # Sketch principal para Arduino IDE (abrir lilyxm7.ino)
│   ├── lilyxm7.ino              # Loop principal y máquina de pantallas
│   ├── config.h                 # Selector SIMULACIÓN/REAL, pines ESP32-S3 y umbrales
│   ├── data_types.h             # Estructuras compartidas (TempReading, ConnectionInfo, etc.)
│   ├── obd_protocol.h           # Protocolo OBD-II 01 05 y TempTracker (C++ puro reutilizable)
│   ├── data_source.h / .cpp     # Función común getCoolantTemperature() (Wokwi y TWAI real)
│   ├── buttons.h / .cpp         # Lectura con antirrebote de ↑, ↓, A y B
│   ├── assistant_ascii.h / .cpp # Arte ASCII Matrix y mensajes dinámicos de Lily
│   └── ui_tft.h / .cpp          # Renderizado ILI9341 sin parpadeos y soporte UTF-8 español
├── wokwi/                       # Simulación de tablero, pantalla, botones y potenciómetro
│   ├── diagram.json
│   ├── libraries.txt
│   ├── wokwi.toml
│   └── README.md
├── ecu_sim_linux/               # Prueba separada en Linux con ecu-simulator + vcan0
│   ├── setup_vcan.sh
│   ├── lilyxm7_obd_tester.cpp
│   ├── mock_ecu_pid05.py
│   ├── Makefile
│   └── README.md
├── tests/
│   └── test_obd_protocol.cpp    # Pruebas unitarias de protocolo OBD-II y pérdida de señal
└── docs/
    └── wiring_vitara_2008.md    # Guía de cableado físico para la Vitara 2008 y SN65HVD230
```

## Cómo compilar en Arduino IDE

1. Instala el paquete de placas **esp32 by Espressif Systems** en Arduino IDE (*Boards Manager*).
2. Instala estas dos librerías desde *Library Manager*:
   - **Adafruit GFX Library**
   - **Adafruit ILI9341**
3. Abre [`lilyxm7/lilyxm7.ino`](lilyxm7/lilyxm7.ino) en Arduino IDE.
4. Selecciona la placa **ESP32S3 Dev Module**.
5. En [`lilyxm7/config.h`](lilyxm7/config.h):
   - Usa `#define LILYXM7_SIMULATION_MODE 1` para probar en Wokwi o en banco con potenciómetro.
   - Usa `#define LILYXM7_SIMULATION_MODE 0` para cargar el firmware real y conectarlo a la Suzuki Grand Vitara mediante el transceptor `SN65HVD230`.

## Checklist de aceptación de la v1.0

1. **Wokwi**: probar menú (`↑`, `↓`, `A`, `B`), indicador **`SIMULACIÓN`**, variación del potenciómetro (expresiones + sube/baja) y switch de pérdida de señal (`Sin datos del auto` + `Última lectura`). Ver [`wokwi/README.md`](wokwi/README.md).
2. **Linux (`ecu-simulator`)**: levantar `vcan0`, correr `ecu-simulator` y validar `01 05` con `./lilyxm7_obd_tester vcan0`. Ver [`ecu_sim_linux/README.md`](ecu_sim_linux/README.md).
3. **Vitara 2008**: cambiar `LILYXM7_SIMULATION_MODE` a `0`, conectar según [`docs/wiring_vitara_2008.md`](docs/wiring_vitara_2008.md), entrar a **Temperatura** con los botones, confirmar lectura real en `°C` junto a Lily y comprobar **«Sin datos del auto»** al desconectar o apagar ignición.

## Hoja de ruta (próximas versiones)
- Lectura de **RPM** (`Servicio 01, PID 0C`) y más PIDs.
- Historial de temperaturas máximas/mínimas.
- Avisos de voz / alertas sonoras con la bocina del kit LAFVIN.
- Pruebas de relevadores en banco usando el **ULN2003AN**.
