#pragma once

#include "font5x7.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Emulador de Adafruit_GFX con framebuffer RGB565 de 320x240 para sim_web
// ============================================================================

namespace sim_tft {

constexpr int TFT_W = 320;
constexpr int TFT_H = 240;

extern uint16_t g_fb[TFT_H][TFT_W];

inline void putPixel(int16_t x, int16_t y, uint16_t color) {
  if (x >= 0 && x < TFT_W && y >= 0 && y < TFT_H) {
    g_fb[y][x] = color;
  }
}

} // namespace sim_tft

class Adafruit_GFX {
public:
  Adafruit_GFX(int16_t w = sim_tft::TFT_W, int16_t h = sim_tft::TFT_H)
      : width_(w), height_(h) {}

  void setRotation(uint8_t r) {
    rotation_ = r & 3;
    if (rotation_ == 1 || rotation_ == 3) {
      width_ = sim_tft::TFT_W;
      height_ = sim_tft::TFT_H;
    } else {
      width_ = sim_tft::TFT_H;
      height_ = sim_tft::TFT_W;
    }
  }

  void cp437(bool) {}
  void setTextWrap(bool w) { wrap_ = w; }
  void setCursor(int16_t x, int16_t y) { cursorX_ = x; cursorY_ = y; }
  int16_t getCursorX() const { return cursorX_; }
  int16_t getCursorY() const { return cursorY_; }
  void setTextColor(uint16_t c) { textColor_ = c; }
  void setTextSize(uint8_t s) { textSize_ = (s > 0) ? s : 1; }
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  void drawPixel(int16_t x, int16_t y, uint16_t color) {
    sim_tft::putPixel(x, y, color);
  }

  void fillScreen(uint16_t color) {
    fillRect(0, 0, sim_tft::TFT_W, sim_tft::TFT_H, color);
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t yy = y; yy < y + h; ++yy) {
      for (int16_t xx = x; xx < x + w; ++xx) {
        sim_tft::putPixel(xx, yy, color);
      }
    }
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    fillRect(x, y, 1, h, color);
  }

  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
  }

  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t dx = std::abs(x1 - x0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = -std::abs(y1 - y0);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;
    while (true) {
      sim_tft::putPixel(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      int16_t e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  size_t write(uint8_t c) {
    if (c == '\n') {
      cursorX_ = 0;
      cursorY_ += textSize_ * 8;
      return 1;
    }
    if (c == '\r') {
      return 1;
    }
    uint8_t glyph[5];
    sim_font::getGlyph5x7(c, glyph);
    for (int col = 0; col < 5; ++col) {
      uint8_t bits = glyph[col];
      for (int row = 0; row < 7; ++row) {
        if (bits & (1u << row)) {
          fillRect(cursorX_ + col * textSize_, cursorY_ + row * textSize_, textSize_, textSize_, textColor_);
        }
      }
    }
    cursorX_ += 6 * textSize_;
    if (wrap_ && (cursorX_ + 6 * textSize_ > width_)) {
      cursorX_ = 0;
      cursorY_ += 8 * textSize_;
    }
    return 1;
  }

private:
  int16_t width_;
  int16_t height_;
  uint8_t rotation_ = 1;
  bool wrap_ = false;
  int16_t cursorX_ = 0;
  int16_t cursorY_ = 0;
  uint16_t textColor_ = 0x07E0;
  uint8_t textSize_ = 1;
};
