# Cableado físico — `lilyxm7` v1.0 (Suzuki Grand Vitara 2008 V6 2.7 L)

## 1. Mapa de pines en la ESP32-S3 (kit LAFVIN)

Los pines definidos en [`lilyxm7/config.h`](../lilyxm7/config.h) evitan pines de strapping (`0, 3, 45, 46`), USB nativo (`19, 20`) y PSRAM (`33-37`), y coinciden 1 a 1 con el circuito de Wokwi.

### Pantalla TFT 2.4" ILI9341 (SPI)
| Señal TFT | GPIO ESP32-S3 | Nota |
|---|---|---|
| `VCC` | `3.3V` | Alimentación lógica 3.3 V |
| `GND` | `GND` | Tierra común |
| `CS` | `GPIO 10` | Chip Select |
| `RESET / RST` | `GPIO 14` | Reset |
| `DC / RS` | `GPIO 9` | Data / Command |
| `SDI (MOSI)` | `GPIO 11` | SPI MOSI |
| `SCK` | `GPIO 12` | SPI Clock |
| `SDO (MISO)` | `GPIO 13` | SPI MISO |
| `LED / BL` | `3.3V` o `GPIO 21` | Backlight (`GPIO 21` se pone en `HIGH` al iniciar) |

> **Nota sobre kits LAFVIN integrados:** si tu placa LAFVIN ya trae la TFT soldada con otros pines fijos, solo cambia las constantes `PIN_TFT_*` en `lilyxm7/config.h` sin tocar el resto del código.

### Botones físicos del menú (`INPUT_PULLUP`, activo en `LOW`)
Cada botón conecta un lado a su GPIO y el otro lado a `GND`:

| Botón | Función | GPIO ESP32-S3 |
|---|---|---|
| `↑` | Subir en menú | `GPIO 4` |
| `↓` | Bajar en menú | `GPIO 5` |
| `A` | Seleccionar | `GPIO 6` |
| `B` | Volver | `GPIO 7` |

### Transceptor CAN `SN65HVD230` (modo OBD-II real)
| Pin SN65HVD230 | Conexión | Nota |
|---|---|---|
| `3V3 / VCC` | `3.3V` de la ESP32-S3 | **Nunca conectar a 5 V** (el SN65HVD230 es de 3.3 V) |
| `GND` | `GND` común | Compartido con ESP32-S3 y OBD-II Pin 4/5 |
| `CTX` (`D`) | `GPIO 17` | Salida TX del controlador TWAI de la ESP32-S3 |
| `CRX` (`R`) | `GPIO 18` | Entrada RX del controlador TWAI de la ESP32-S3 |
| `RS` (si está expuesto) | `GND` | Modo alta velocidad |
| `CANH` | OBD-II **Pin 6** | CAN High (500 kbps) |
| `CANL` | OBD-II **Pin 14** | CAN Low (500 kbps) |

## 2. Conector OBD-II (Suzuki Grand Vitara 2008 V6 2.7 L)

La Suzuki Grand Vitara 2008 V6 2.7 L (motor N27A, serie JT) utiliza **ISO 15765-4 CAN 11-bit a 500 kbps**:

- **Pin 4 / Pin 5**: Tierra de chasis / Tierra de señal → conectar a `GND` del proyecto.
- **Pin 6**: `CAN-H` → `CANH` del `SN65HVD230`.
- **Pin 14**: `CAN-L` → `CANL` del `SN65HVD230`.
- **Pin 16**: `+12 V` de batería (si decides alimentar la ESP32-S3 desde el puerto OBD-II, usa **siempre** un regulador buck 12 V → 5 V automotriz con fusible; **nunca** conectes +12 V directo a la ESP32-S3). Para las primeras pruebas puedes alimentar la ESP32-S3 por USB (powerbank o cargador 5 V) compartiendo `GND` con el OBD-II.

### Precauciones clave antes de conectar a la Vitara
1. **Resistencia de terminación de 120 Ω:** el bus CAN de la Vitara ya cuenta con sus dos resistencias de 120 Ω internas. Revisa si tu módulo `SN65HVD230` trae una resistencia de 120 Ω soldada entre `CANH` y `CANL`; en pruebas cortas suele funcionar, pero lo ideal para uso permanente en vehículo es que el nodo OBD-II **no** agregue una tercera resistencia de 120 Ω.
2. **Ignición en ON:** la ECU no responde con el switch apagado. Pon la llave en **ON** o arranca el motor para ver lecturas reales en °C.
3. **Verificación de pérdida de señal:** una vez veas la temperatura real en °C, apaga la ignición o desconecta la extensión en Y; en 3 segundos debe aparecer **«Sin datos del auto»** junto a la **«Última lectura»**.

### LED «Ojo de Lily»

Conecta **GPIO 16 → resistencia de 330 Ω → ánodo (+)** del LED indicador. Conecta el cátodo (−) a **GND** de la ESP32. Elige un LED indicador de baja tensión directa compatible con 3.3 V. El firmware lo enciende durante 450 ms por mensaje y 1.1 s por alerta.

## 3. Relés y ventiladores de la Vitara

El simulador reproduce los trayectos del [esquema de enfriamiento de la Grand Vitara 2008 V6 2.7 L 2WD](https://charm.li/Suzuki/2008/Grand%20Vitara%202WD%20V6-2.7L/Repair%20and%20Diagnosis/Diagrams/Electrical%20Diagrams/Cooling%20System/). Confirma la variante del vehículo antes de usar las referencias E29, E30 y E31.

| Modo | Relés cerrados o conmutados | Trayecto de potencia |
|---|---|---|
| Ambos en baja | 1 (E29) | Batería + → RDTR2 → E29 → ventilador 1 → contacto de reposo de E31 → ventilador 2 → masa |
| Alta del ventilador 1 | 1 (E29) y 3 (E31) | Batería + → RDTR2 → E29 → ventilador 1 → E31 conmutado → masa |
| Alta del ventilador 2 | 2 (E30) | Batería + → RDTR → E30 → ventilador 2 → masa |
| Ambos en alta | 1, 2 y 3 | Cada motor recibe su propia alimentación protegida |

En el vehículo, la ECM controla las bobinas y conserva el mando del circuito de enfriamiento. Los controles manuales del simulador actúan sobre la ECU virtual. La conexión física del escáner al vehículo sigue siendo el enlace OBD-II de la sección 2. El pin 16 del OBD-II se deja libre al alimentar la ESP32 por USB.

## 4. Componentes reservados para versiones futuras
- **Bocina del kit LAFVIN**: reservada para avisos sonoros / de voz en versiones posteriores.
- **ULN2003AN**: reservado para pruebas de relevadores en banco en una etapa posterior (no conectar en v1.0).
