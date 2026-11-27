#pragma once

#include <stdint.h>

// ============================================================================
// Configuración de hardware para placa LAFVIN "Retro Game Emulator" (ESP32-S3)
// ============================================================================

// Pantalla ST7789 240x320 (SPI2)
constexpr int TFT_BL   = 6;
constexpr int TFT_CS   = 7;
constexpr int TFT_DC   = 15;
constexpr int TFT_RST  = 16;
constexpr int TFT_MOSI = 17;
constexpr int TFT_SCK  = 18;
constexpr int TFT_MISO = -1;

// 10 botones físicos soldados en la placa (INPUT_PULLUP, activo LOW)
constexpr int BTN_UP     = 11;
constexpr int BTN_DOWN   = 12;
constexpr int BTN_LEFT   = 13;
constexpr int BTN_RIGHT  = 14;
constexpr int BTN_A      = 21;
constexpr int BTN_B      = 47;
constexpr int BTN_START  = 39;
constexpr int BTN_SELECT = 40;
constexpr int BTN_MENU   = 5;
constexpr int BTN_OPTION = 4;

constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;

// Umbrales térmicos Suzuki Grand Vitara 2008 V6 2.7 L
constexpr int16_t TEMP_COLD_MAX    = 59;
constexpr int16_t TEMP_WARMING_MAX = 79;
constexpr int16_t TEMP_OPTIMAL_MAX = 97;
constexpr int16_t TEMP_WARN_MAX    = 107;

// ============================================================================
// Paletas de color (Rosa suave por defecto / Verde Matrix con botón MENU)
// ============================================================================
struct Theme {
  uint16_t bg;
  uint16_t dark;
  uint16_t dim;
  uint16_t mid;
  uint16_t bright;
  uint16_t glow;
  uint16_t selBg;
};

constexpr Theme THEME_PINK = {
  0x0000, // Negro puro
  0x2864, // Rosa muy oscuro (separadores)
  0x5949, // Rosa apagado (marcos y etiquetas)
  0x9A90, // Rosa medio suave (texto secundario)
  0xC394, // Rosa pastel suave (principal, nada cegante)
  0xDC98, // Rosa claro suave (alerta / destaque)
  0x40A7  // Fondo suave de opción seleccionada
};

constexpr Theme THEME_MATRIX = {
  0x0000, // Negro puro
  0x0180, // Verde oscuro
  0x0360, // Verde tenue
  0x05A0, // Verde medio
  0x07E0, // Verde Matrix neón
  0x87F0, // Verde fósforo claro
  0x0240  // Fondo suave de opción seleccionada
};

enum class ScreenId : uint8_t {
  MAIN_MENU = 0,
  TEMPERATURE = 1,
  CONNECTION_STATUS = 2,
  SNAKE = 3,
  DOOM = 4
};

constexpr uint8_t MENU_COUNT = 4; // 0=Temperatura, 1=Conexión, 2=Snake, 3=Lily DOOM
