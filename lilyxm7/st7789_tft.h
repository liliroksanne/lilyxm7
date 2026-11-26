#pragma once

// ============================================================================
// Driver nativo ST7789 (240x320 SPI) con la secuencia oficial de LAFVIN
// Retro Game Emulator (esp32-s3-devkit/config.h) sobre Adafruit_SPITFT.
// ============================================================================

#if defined(LILYXM7_NATIVE_SIM)

#include <Adafruit_ILI9341.h>
using St7789Tft = Adafruit_ILI9341;

#else

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>

class St7789Tft : public Adafruit_SPITFT {
public:
  static constexpr uint16_t NATIVE_WIDTH  = 240;
  static constexpr uint16_t NATIVE_HEIGHT = 320;

  St7789Tft(int16_t cs, int16_t dc, int16_t rst = -1)
      : Adafruit_SPITFT(NATIVE_WIDTH, NATIVE_HEIGHT, cs, dc, rst) {}

  void begin(uint32_t freq = 24000000) override {
    if (!freq) freq = 24000000;
    initSPI(freq, SPI_MODE0);

    // Secuencia oficial LAFVIN Retro Game Kit (ST7789 240x320)
    sendCommand(0x01); // SWRESET
    delay(150);
    sendCommand(0x11); // SLPOUT
    delay(120);

    const uint8_t colmod = 0x55; // 16-bit RGB565
    sendCommand(0x3A, &colmod, 1);
    delay(10);

    invertOnCommand  = 0x21;
    invertOffCommand = 0x20;
    sendCommand(invertOffCommand);

    const uint8_t madctl = 0x60; // Horizontal 320x240 RGB
    sendCommand(0x36, &madctl, 1);

    static const uint8_t porch[]  = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    static const uint8_t gate[]   = { 0x35 };
    static const uint8_t vcom[]   = { 0x19 };
    static const uint8_t lcm[]    = { 0x2C };
    static const uint8_t vdvEn[]  = { 0x01 };
    static const uint8_t vrh[]    = { 0x12 };
    static const uint8_t vdv[]    = { 0x20 };
    static const uint8_t frate[]  = { 0x0F };
    static const uint8_t pwctrl[] = { 0xA4, 0xA1 };
    static const uint8_t pgamma[] = { 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                                      0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23 };
    static const uint8_t ngamma[] = { 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                                      0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23 };

    sendCommand(0xB2, porch, sizeof(porch));
    sendCommand(0xB7, gate, sizeof(gate));
    sendCommand(0xBB, vcom, sizeof(vcom));
    sendCommand(0xC0, lcm, sizeof(lcm));
    sendCommand(0xC2, vdvEn, sizeof(vdvEn));
    sendCommand(0xC3, vrh, sizeof(vrh));
    sendCommand(0xC4, vdv, sizeof(vdv));
    sendCommand(0xC6, frate, sizeof(frate));
    sendCommand(0xD0, pwctrl, sizeof(pwctrl));
    sendCommand(0xE0, pgamma, sizeof(pgamma));
    sendCommand(0xE1, ngamma, sizeof(ngamma));

    sendCommand(0x13); // NORON
    delay(10);
    sendCommand(0x29); // DISPON
    delay(100);

    setRotation(1);
  }

  void setRotation(uint8_t m) override {
    rotation = m & 3;
    uint8_t madctl = 0x60;
    switch (rotation) {
      case 0: madctl = 0x00; _width = NATIVE_WIDTH;  _height = NATIVE_HEIGHT; break;
      case 1: madctl = 0x60; _width = NATIVE_HEIGHT; _height = NATIVE_WIDTH;  break;
      case 2: madctl = 0xC0; _width = NATIVE_WIDTH;  _height = NATIVE_HEIGHT; break;
      case 3: madctl = 0xA0; _width = NATIVE_HEIGHT; _height = NATIVE_WIDTH;  break;
    }
    _xstart = 0;
    _ystart = 0;
    sendCommand(0x36, &madctl, 1);
  }

  void setAddrWindow(uint16_t x1, uint16_t y1, uint16_t w, uint16_t h) override {
    x1 += _xstart;
    y1 += _ystart;
    const uint16_t x2 = x1 + w - 1;
    const uint16_t y2 = y1 + h - 1;
    writeCommand(0x2A); SPI_WRITE16(x1); SPI_WRITE16(x2);
    writeCommand(0x2B); SPI_WRITE16(y1); SPI_WRITE16(y2);
    writeCommand(0x2C);
  }
};

#endif // LILYXM7_NATIVE_SIM
