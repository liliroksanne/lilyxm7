# Simulación de la ECU en Linux con `ecu-simulator` (`vcan0`)

Esta prueba se ejecuta **por separado de Wokwi** en Linux y verifica que la lógica OBD-II exacta de `lilyxm7` (`lilyxm7/obd_protocol.h`) consulta correctamente el **Servicio 01, PID 05** (temperatura del refrigerante), decodifica la respuesta `A - 40` en °C, detecta subidas/bajadas y maneja la pérdida de señal (**«Sin datos del auto»** + **«Última lectura»**).

## 1. Preparar SocketCAN (`vcan0`) y herramientas

```bash
sudo apt update
sudo apt install -y build-essential can-utils python3 python3-pip git

cd ecu_sim_linux
chmod +x setup_vcan.sh
./setup_vcan.sh vcan0
```

## 2. Compilar el cliente OBD-II de `lilyxm7` y las pruebas unitarias

```bash
make test
make lilyxm7_obd_tester
```

## 3. Ejecutar `ecu-simulator` en una terminal

Clona e inicia `ecu-simulator` apuntando a `vcan0`:

```bash
git clone https://github.com/lbenthins/ecu-simulator.git
cd ecu-simulator
pip3 install -r requirements.txt
python3 ecu_simulator.py
```

*(Asegúrate de que en su configuración use la interfaz `vcan0` y tenga habilitado el PID `0x05` de temperatura de refrigerante).*

> **Alternativa rápida sin dependencias:** también incluimos `./mock_ecu_pid05.py vcan0`, que responde en `0x7E8` a `01 05` y permite cambiar la temperatura desde consola o escribir `off` para probar la pérdida de señal al instante.

## 4. Ejecutar el tester de `lilyxm7` en otra terminal

```bash
./lilyxm7_obd_tester vcan0
```

En una tercera terminal puedes observar el intercambio real de tramas CAN:

```bash
candump vcan0
```

Verás algo como:
- Consulta de `lilyxm7`: `vcan0  7DF   [8]  02 01 05 00 00 00 00 00`
- Respuesta de la ECU (ej. 90 °C → `90 + 40 = 130 = 0x82`): `vcan0  7E8   [8]  03 41 05 82 00 00 00 00`

## 5. Verificar pérdida de señal

Detén `ecu-simulator` con `Ctrl+C` (o escribe `off` en `mock_ecu_pid05.py`). Tras 3 segundos, `lilyxm7_obd_tester` imprimirá:

```text
[ALERTA] Sin datos del auto | Última lectura: 90 °C
```

Al reiniciar `ecu-simulator`, volverá a reportar lecturas `[OK]` automáticamente.
