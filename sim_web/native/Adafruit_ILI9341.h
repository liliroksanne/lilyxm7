#pragma once

#include "Adafruit_GFX.h"

class Adafruit_ILI9341 : public Adafruit_GFX {
public:
  Adafruit_ILI9341(int8_t cs, int8_t dc, int8_t rst = -1)
      : Adafruit_GFX(320, 240) {
    (void)cs;
    (void)dc;
    (void)rst;
  }

  void begin(uint32_t freq = 0) {
    (void)freq;
    fillScreen(0x0000);
  }
};
