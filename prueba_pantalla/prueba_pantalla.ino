#include <Arduino.h>

#include "board_config.h"
#include "display_driver.h"
#include "input_buttons.h"
#include "ui_lily.h"
#include "game_snake.h"
#include "game_doom.h"

// ============================================================================
// prueba_pantalla.ino — Punto de entrada modular para LAFVIN Retro Game PCB
//
// Módulos en esta carpeta (Arduino IDE / arduino-cli los compila todos juntos):
//   - board_config.h          : Pines, umbrales térmicos y paletas de color
//   - display_driver.h / .cpp : Driver ST7789 320x240 y texto español CP437
//   - input_buttons.h / .cpp  : Lectura con antirrebote de los 10 botones
//   - ui_lily.h / .cpp        : Menú, Temperatura, Conexión y chatbot Lily
//   - game_snake.h / .cpp     : Minijuego Snake sin parpadeo
//   - game_doom.h / .cpp      : Motor 3D Raycasting Lily DOOM
// ============================================================================

namespace {

ScreenId g_screen = ScreenId::MAIN_MENU;
uint8_t g_menuIdx = 0; // Por defecto siempre seleccionada Temperatura (0)

void renderActiveScreen() {
  if (g_screen == ScreenId::SNAKE) {
    snakeRenderFull();
  } else if (g_screen == ScreenId::DOOM) {
    doomRenderFull();
  } else {
    uiLilyRenderFull(g_screen, g_menuIdx, true);
  }
}

void handleButtonEvent(int pin) {
  if (pin < 0) return;
  Serial.printf("[BTN] GPIO %d\n", pin);

  // Atajos globales en cualquier pantalla o juego
  if (pin == BTN_MENU) {
    displayToggleTheme();
    uiLilyPushChat(g_usePinkTheme ? "Tema: Rosa Lily." : "Tema: Verde Matrix.", false);
    renderActiveScreen();
    return;
  }
  if (pin == BTN_OPTION) {
    displayFlipRotation();
    renderActiveScreen();
    return;
  }

  // Enrutamiento por módulo activo
  if (g_screen == ScreenId::DOOM) {
    if (doomHandleButton(pin)) {
      g_screen = ScreenId::MAIN_MENU;
      renderActiveScreen();
    }
    return;
  }

  if (g_screen == ScreenId::SNAKE) {
    if (snakeHandleButton(pin)) {
      g_screen = ScreenId::MAIN_MENU;
      renderActiveScreen();
    }
    return;
  }

  const ScreenId prev = g_screen;
  uiLilyHandleButton(g_screen, g_menuIdx, pin);
  if (g_screen == ScreenId::SNAKE && prev != ScreenId::SNAKE) {
    snakeStart();
  } else if (g_screen == ScreenId::DOOM && prev != ScreenId::DOOM) {
    doomStart();
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("========================================="));
  Serial.println(F("  lilyxm7 v1.0 - Prueba modular pantalla"));
  Serial.println(F("========================================="));

  inputButtonsInit();
  displayInit();
  uiLilyShowSplash();
  uiLilyInit();
  renderActiveScreen();
}

void loop() {
  handleButtonEvent(inputButtonsPoll());

  if (g_screen == ScreenId::DOOM) {
    doomTick();
    return;
  }
  if (g_screen == ScreenId::SNAKE) {
    snakeTick();
    return;
  }

  uiLilyTick(g_screen, g_menuIdx);
  delay(15);
}
