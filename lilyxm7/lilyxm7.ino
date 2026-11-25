// ============================================================================
// lilyxm7 v1.0 - Escáner OBD-II con asistente ASCII estilo Matrix
// Vehículo objetivo: Suzuki Grand Vitara 2008 V6 2.7 L (ISO 15765-4 CAN 500k)
// Hardware: ESP32-S3 + TFT 2.4" ILI9341 + 4 botones (↑, ↓, A, B) + SN65HVD230
//
// Para cambiar entre simulación Wokwi y auto real, ajusta LILYXM7_SIMULATION_MODE
// en config.h (1 = SIMULACIÓN en Wokwi, 0 = OBD-II real en la Vitara).
// ============================================================================

#include "config.h"
#include "data_types.h"
#include "buttons.h"
#include "data_source.h"
#include "ui_tft.h"

namespace {

ScreenId g_currentScreen = ScreenId::MAIN_MENU;
MenuOption g_selectedOption = MenuOption::TEMPERATURE;

void handleButtonEvent(ButtonEvent ev) {
  if (ev == ButtonEvent::NONE) {
    return;
  }

  Serial.printf("[LOG][BTN] evento=%u pantalla=%u\n",
                static_cast<unsigned>(ev), static_cast<unsigned>(g_currentScreen));

  if (g_currentScreen == ScreenId::MAIN_MENU) {
    switch (ev) {
      case ButtonEvent::UP:
        g_selectedOption = (g_selectedOption == MenuOption::TEMPERATURE)
                               ? MenuOption::CONNECTION_STATUS
                               : MenuOption::TEMPERATURE;
        break;

      case ButtonEvent::DOWN:
        g_selectedOption = (g_selectedOption == MenuOption::CONNECTION_STATUS)
                               ? MenuOption::TEMPERATURE
                               : MenuOption::CONNECTION_STATUS;
        break;

      case ButtonEvent::SELECT_A:
        g_currentScreen = (g_selectedOption == MenuOption::TEMPERATURE)
                              ? ScreenId::TEMPERATURE
                              : ScreenId::CONNECTION_STATUS;
        Serial.printf("[LOG][UI] cambio a pantalla=%u\n", static_cast<unsigned>(g_currentScreen));
        uiForceRedraw();
        break;

      case ButtonEvent::BACK_B:
      case ButtonEvent::NONE:
      default:
        break;
    }
  } else {
    // En Temperatura o Estado de conexión, B regresa al menú principal
    if (ev == ButtonEvent::BACK_B) {
      g_currentScreen = ScreenId::MAIN_MENU;
      Serial.println(F("[LOG][UI] regreso a pantalla=MAIN_MENU"));
      uiForceRedraw();
    }
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("========================================"));
  Serial.println(F("  lilyxm7 " LILYXM7_VERSION " - OBD-II Vitara 2008"));
  Serial.println(F("========================================"));

  buttonsInit();
  dataSourceInit();
  uiInit();
}

void loop() {
  // 1. Leer botones físicos (↑, ↓, A, B)
  handleButtonEvent(buttonsPoll());

  // 2. Actualizar la fuente de datos (Wokwi o ECU real por TWAI)
  dataSourceUpdate();

  // 3. Leer temperatura mediante la función común (mismo flujo en sim y real)
  const TempReading tempReading = getCoolantTemperature();
  const ConnectionInfo connInfo = getConnectionInfo();

  // 4. Actualizar pantalla TFT y asistente ASCII Matrix
  uiRender(g_currentScreen, g_selectedOption, tempReading, connInfo);

  delay(15);
}
