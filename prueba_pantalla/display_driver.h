#pragma once

#include "board_config.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>

// ============================================================================
// Driver ST7789 240x320 (LAFVIN Retro Game Emulator) + texto español CP437
// ============================================================================

class LafvinST7789 : public Adafruit_SPITFT {
public:
  LafvinST7789(int16_t cs, int16_t dc, int16_t rst);
  void begin(uint32_t freq = 24000000) override;
  void setRotation(uint8_t m) override;
  void setAddrWindow(uint16_t x1, uint16_t y1, uint16_t w, uint16_t h) override;
};

extern LafvinST7789 g_tft;
extern Theme g_theme;
extern bool g_usePinkTheme;
extern uint8_t g_rotation;

void displayInit();
void displayToggleTheme();
void displayFlipRotation();
void printEs(const char* text, uint16_t color, uint8_t size = 1);
