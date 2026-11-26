# lilyxm7 — Escáner OBD-II para Suzuki Grand Vitara 2008 V6 2.7 L

COMANDOS 

cargar_placa.bat COM3 -ArduinoData "C:\Users\LilianaRoksanneDíaz\AppData\Local\Arduino15" -PruebaPantalla


**`lilyxm7`** es un escáner OBD-II programado en **C++ (Arduino IDE)** para **ESP32-S3** pensado específicamente para la **Suzuki Grand Vitara 2008 V6 2.7 L** (motor N27A, protocolo **ISO 15765-4 CAN 11-bit a 500 kbps**).

La primera versión (**v1.0**) lee en vivo la **temperatura del refrigerante** (`Servicio 01, PID 05`), la muestra en una **pantalla física TFT 2.4" (ILI9341)** con estética **Matrix verde sobre negro**, se navega con **4 botones físicos (`↑`, `↓`, `A`, `B`)** e incluye a **Lily**, una asistente tipo **chatbot simple** que envía mensajes breves según lo que ocurre con el motor y la señal del vehículo.

---

## El reto del proyecto: dejar Wokwi y construir nuestro propio simulador local real

Originalmente se consideró **Wokwi** para simular el tablero y una herramienta aparte para la ECU, pero **Wokwi se dejará de usar** en este proyecto porque depende de servidores en la nube, licencias y WebSockets (`wss://`) que se bloquean fácilmente en redes corporativas o sin internet, y además separa la simulación del tablero de la simulación de la camioneta.

**El reto ahora es construir y evolucionar un simulador local propio, 100 % offline (`sim_web/`), que integre en un solo banco de pruebas real:**

1. **La placa ESP32-S3 real (motor C++ nativo):** compila y ejecuta **el mismo código C++ de `lilyxm7/`** (`lilyxm7.ino`, `ui_tft.cpp`, `assistant_ascii.cpp`, `data_source.cpp`, `buttons.cpp`, `obd_protocol.h`) sin reescribir lógica.
2. **La pantalla física TFT 2.4" ILI9341 (`320×240`):** renderizado pixel a pixel de la interfaz Matrix, menú físico y mensajes del chatbot Lily.
3. **La ECU y el motor de la Suzuki Grand Vitara 2008 V6 2.7 L:** simulación térmica progresiva desde motor frío (~28 °C) hasta temperatura de operación (~89–93 °C), apertura de termostato (~82 °C), electroventilador (~94 °C), escenarios de manejo (*ralentí*, *ciudad*, *subida*, *falla de termostato*) e ignición `OFF / ON / ARRANCAR`.
4. **El bus OBD-II CAN (`500 kbps`, Servicio `01` PID `05`):** intercambio real de tramas `0x7DF → 02 01 05` y respuestas `0x7E8 → 03 41 05 XX`, desconexión de cable OBD-II, pérdida de señal (**«Sin datos del auto»** + **«Última lectura»**) y recuperación de **`BUS_OFF`**.
5. **Logs centralizados por componente:** visibles en vivo en el frontend (`ECU_OBD`, `DATA`, `CHAT`, `BTN`, `UI`, `BUILD`) y guardados automáticamente en `logs/`.

> *Nota:* la carpeta `wokwi/` y el empaquetador `tools/export_wokwi_web.py` se conservan únicamente como referencia heredada; el desarrollo y las pruebas principales del proyecto viven ahora en `sim_web/`.

---

## Qué incluye la v1.0

- **Chatbot simple Matrix (`lilyxm7/assistant_ascii.*`)**: panel `CHAT // LILY` con burbujas en verde fósforo sobre negro que informa cambios de temperatura (*motor frío*, *calentando*, *óptimo*, *temperatura alta*, *peligro/sobrecalentamiento*), tendencias (*subió/bajó*) y pérdida o recuperación de señal.
- **Menú físico (`lilyxm7/buttons.*`, `lilyxm7/ui_tft.*`)**: navegación con **↑** y **↓**, selección con **A** y regreso con **B**. Opciones actuales:
  1. **Temperatura**: lectura en vivo en `°C`, tendencia respecto a la lectura anterior, barra térmica y mensajes de Lily.
  2. **Estado de conexión**: estado de ECU y TWAI, velocidad (PID 0D), aceleración estimada en m/s², testigo Check Engine (PID 01) y códigos confirmados de la computadora del motor (servicio 03). La TFT alterna los códigos y conserva la temperatura como primera opción.
- **Función común de datos (`getCoolantTemperature()`)**: definida en `lilyxm7/data_source.*` para que el menú y el chatbot usen exactamente la misma entrada tanto con datos simulados como con el bus OBD-II real (`driver/twai.h` + `SN65HVD230`).
- **Pérdida de señal**: si la ECU deja de responder por 3 segundos, muestra **«Sin datos del auto»** y conserva el último valor válido claramente etiquetado como **«Última lectura»**.
- **Serial del firmware**. La opción [1] muestra la salida de `Serial` en la terminal de Windows, en el monitor junto al TFT y en `logs/firmware_serial.log`. El TFT web presenta el búfer RGB dibujado por el mismo C++ que se compila para la ESP32. El puente nativo modela sus periféricos.
- **Logs unificados (`logs/` y frontend)**: todos los componentes registran eventos en `C:\Git\lilyxm7\logs\` (ignorado por Git) y los muestran filtrables por componente en el Simulador Web.

---

## Estructura del proyecto

```text
C:\Git\lilyxm7\
├── lilyxm7/                      # Firmware C++ principal (abrir lilyxm7.ino en Arduino IDE)
│   ├── lilyxm7.ino               # Setup/loop y navegación entre pantallas
│   ├── config.h                  # Modo simulación/real, pines ESP32-S3 y umbrales térmicos
│   ├── data_types.h              # Estructuras compartidas (TempReading, ConnectionInfo, etc.)
│   ├── obd_protocol.h            # Protocolo OBD-II 01 05 y TempTracker (C++ puro reutilizable)
│   ├── data_source.h / .cpp      # Función común getCoolantTemperature() (simulación y TWAI real)
│   ├── buttons.h / .cpp          # Lectura con antirrebote de ↑, ↓, A y B
│   ├── assistant_ascii.h / .cpp  # Chatbot simple Lily (historial y mensajes dinámicos)
│   └── ui_tft.h / .cpp           # Interfaz TFT ILI9341 320x240 con soporte UTF-8 español
├── sim_web/                      # Simulador local propio 100% offline (placa + TFT + ECU + OBD-II)
│   ├── server.py                 # Servidor local HTTP + modelo térmico Vitara + puente DLL C++
│   ├── index.html                # Interfaz web: TFT en vivo, controles Vitara, chat y logs por componente
│   └── native/                   # Emulador C++ de Arduino.h, Adafruit_GFX/ILI9341, fuente CP437 y TWAI CAN
├── tools/
│   └── run_logged.py             # Ejecutor con logs centralizados en logs/
├── ecu_sim_linux/                # Pruebas opcionales por SocketCAN (vcan0) en Linux con ecu-simulator
├── tests/
│   └── test_obd_protocol.cpp     # Pruebas unitarias de tramas OBD-II 01 05 y pérdida de señal
├── docs/
│   └── wiring_vitara_2008.md     # Guía de cableado físico para la Grand Vitara 2008 y SN65HVD230
├── wokwi/                        # [Legado] Configuración anterior de Wokwi (en desuso)
├── simulador_local.py            # [Alternativo] Simulador clásico de escritorio en Tkinter
└── ejecutar_simulador.bat        # Lanzador todo-en-uno en Windows
```

---

## Inicio rápido en Windows

Haz doble clic o ejecuta en PowerShell:

```powershell
.\ejecutar_simulador.bat
```

Opciones principales:
- **`[1]` Simulador WEB propio de `lilyxm7`**: abre `http://127.0.0.1:8765` en Chrome/Edge, 100 % offline, con pantalla TFT 2.4", chatbot Lily, motor Vitara con calentamiento gradual, bus OBD-II y logs por componente. En **Camioneta completa** se ven juntos la ECU, el escáner, el radiador y los estados del cableado. Ajusta la velocidad objetivo o el caso de DTC para probar las lecturas y alertas del firmware.
- **`[4]` Instalar herramientas (una sola vez)**: instala `arduino-cli`, core `esp32:esp32`, librerías `Adafruit GFX` / `Adafruit ILI9341` y **`g++` (WinLibs)** para activar el **Motor C++ Real** dentro del simulador web.
- **`[5]` Compilar firmware REAL para la Vitara**: compila con `LILYXM7_SIMULATION_MODE=0` para subir a la placa física.
- **`[6]` Capturar logs USB de la ESP32-S3 real**: lee el puerto COM a 115200 baud y guarda todo en `logs/esp32_serial.log`.

---

## Hardware físico previsto (Suzuki Grand Vitara 2008 V6 2.7 L)

- Kit **LAFVIN ESP32-S3** con pantalla **TFT 2.4" (ILI9341)**, 4 botones físicos (`↑`, `↓`, `A`, `B`), bocina y protoboard.
- Transceptor CAN **SN65HVD230** (alimentado a **3.3 V**) + extensión OBD-II en Y adaptada (pines **6 CAN-H**, **14 CAN-L**, **4/5 GND**).
- **ULN2003AN** reservado para pruebas de relevadores en banco en una etapa posterior.

Consulta las conexiones exactas y precauciones en [`docs/wiring_vitara_2008.md`](docs/wiring_vitara_2008.md).

### Cómo cargar en la placa física desde Arduino IDE
1. Instala el soporte de placas **esp32 by Espressif Systems** y las librerías **Adafruit GFX Library** y **Adafruit ILI9341**.
2. Abre [`lilyxm7/lilyxm7.ino`](lilyxm7/lilyxm7.ino) y selecciona **ESP32S3 Dev Module**.
3. En [`lilyxm7/config.h`](lilyxm7/config.h) ajusta `#define LILYXM7_SIMULATION_MODE 0`.
4. Sube el firmware y conéctalo a la Grand Vitara con la ignición en **ON** (o motor encendido).

---

## Próximos retos y hoja de ruta

- Ampliar las consultas OBD-II con RPM (`01 0C`), carga del motor y voltaje de batería.
- Historial de temperaturas máximas/mínimas en pantalla.
- Avisos sonoros / de voz con la bocina del kit LAFVIN.
- Módulo de pruebas de relevadores en banco usando el **ULN2003AN** (también simulable en el banco local).
