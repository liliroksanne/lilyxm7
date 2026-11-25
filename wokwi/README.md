# Simulación del tablero en Wokwi (lilyxm7)

Esta carpeta contiene el circuito virtual listo para probar la **ESP32-S3**, la pantalla **TFT 2.4" ILI9341**, los botones físicos (**↑, ↓, A, B**), el **potenciómetro de temperatura** y un **interruptor de pérdida de señal**.

## Controles en la simulación

- **Flechas `↑` / `↓`** (botones verdes o teclas `ArrowUp` / `ArrowDown`): navegan el menú.
- **Botón `A`** (azul o tecla `a`): selecciona la opción (**Temperatura** o **Estado de conexión**).
- **Botón `B`** (rojo o tecla `b`): regresa al menú principal.
- **Potenciómetro (`pot_temp`)**: varía la temperatura simulada entre **20 °C y 120 °C** para ver cambiar las expresiones y mensajes de Lily (frío, calentando, óptimo, alta, sobrecalentamiento y tendencia sube/baja).
- **Switch deslizante (`sw_signal`)**:
  - Izquierda (por defecto): ECU simulada respondiendo con normalidad.
  - Derecha: corta la señal; tras 3 segundos aparece **«Sin datos del auto»** conservando la **«Última lectura»**.

Durante toda la prueba, la barra superior muestra el indicador **`SIMULACIÓN`**.

## Cómo ejecutarlo en wokwi.com (sin instalar nada)

1. Abre un proyecto nuevo de **ESP32-S3 (Arduino)** en [wokwi.com](https://wokwi.com/).
2. Verifica que en `config.h` esté activo:
   ```cpp
   #define LILYXM7_SIMULATION_MODE 1
   ```
3. Copia el contenido de `../lilyxm7/lilyxm7.ino` en `sketch.ino`.
4. Crea pestañas en Wokwi con los demás archivos de `../lilyxm7/`:
   - `config.h`
   - `data_types.h`
   - `obd_protocol.h`
   - `buttons.h` y `buttons.cpp`
   - `data_source.h` y `data_source.cpp`
   - `assistant_ascii.h` y `assistant_ascii.cpp`
   - `ui_tft.h` y `ui_tft.cpp`
5. Reemplaza `diagram.json` por el archivo `wokwi/diagram.json` de este repositorio.
6. Reemplaza `libraries.txt` por `wokwi/libraries.txt` (o agrega **Adafruit GFX Library** y **Adafruit ILI9341** desde Library Manager).
7. Pulsa **Start Simulation** ▶.
