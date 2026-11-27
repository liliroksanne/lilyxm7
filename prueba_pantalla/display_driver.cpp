#include "display_driver.h"

LafvinST7789 g_tft(TFT_CS, TFT_DC, TFT_RST);
Theme g_theme = THEME_PINK;
bool g_usePinkTheme = true;
uint8_t g_rotation = 1;

LafvinST7789::LafvinST7789(int16_t cs, int16_t dc, int16_t rst)
    : Adafruit_SPITFT(240, 320, cs, dc, rst) {}

void LafvinST7789::begin(uint32_t freq) {
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

void LafvinST7789::setRotation(uint8_t m) {
  rotation = m & 3;
  uint8_t madctl = 0x60;
  switch (rotation) {
    case 0: madctl = 0x00; _width = 240; _height = 320; break;
    case 1: madctl = 0x60; _width = 320; _height = 240; break;
    case 2: madctl = 0xC0; _width = 240; _height = 320; break;
    case 3: madctl = 0xA0; _width = 320; _height = 240; break;
  }
  _xstart = 0;
  _ystart = 0;
  sendCommand(0x36, &madctl, 1);
}

void LafvinST7789::setAddrWindow(uint16_t x1, uint16_t y1, uint16_t w, uint16_t h) {
  x1 += _xstart;
  y1 += _ystart;
  const uint16_t x2 = x1 + w - 1;
  const uint16_t y2 = y1 + h - 1;
  writeCommand(0x2A); SPI_WRITE16(x1); SPI_WRITE16(x2);
  writeCommand(0x2B); SPI_WRITE16(y1); SPI_WRITE16(y2);
  writeCommand(0x2C);
}

void displayInit() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  g_tft.begin();
  g_tft.setRotation(g_rotation);
  g_tft.cp437(true);
  g_tft.setTextWrap(false);
}

void displayToggleTheme() {
  g_usePinkTheme = !g_usePinkTheme;
  g_theme = g_usePinkTheme ? THEME_PINK : THEME_MATRIX;
}

void displayFlipRotation() {
  g_rotation = (g_rotation == 1) ? 3 : 1;
  g_tft.setRotation(g_rotation);
}

namespace {
void drawUpperAcuteAccent(int16_t x, int16_t y, uint8_t size, uint16_t color) {
  if (y < 2) return;
  const int16_t ax = x + (2 * size);
  const int16_t ay = y - 2;
  g_tft.drawLine(ax, ay + 1, ax + size + 1, ay, color);
}
} // namespace

void printEs(const char* text, uint16_t color, uint8_t size) {
  g_tft.setTextColor(color);
  g_tft.setTextSize(size);
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
  while (*p != '\0') {
    const uint8_t c = *p;
    if (c < 0x80) {
      g_tft.write(c);
      ++p;
      continue;
    }
    if (c == 0xC2 && p[1] != '\0') {
      const uint8_t c2 = p[1];
      if (c2 == 0xB0) g_tft.write(0xF8);      // °
      else if (c2 == 0xA1) g_tft.write(0xAD); // ¡
      else if (c2 == 0xBF) g_tft.write(0xA8); // ¿
      else if (c2 == 0xAB) g_tft.write(0xAE); // «
      else if (c2 == 0xBB) g_tft.write(0xAF); // »
      else g_tft.write(' ');
      p += 2;
      continue;
    }
    if (c == 0xC3 && p[1] != '\0') {
      const uint8_t c2 = p[1];
      const int16_t cx = g_tft.getCursorX();
      const int16_t cy = g_tft.getCursorY();
      switch (c2) {
        case 0xA1: g_tft.write(0xA0); break; // á
        case 0xA9: g_tft.write(0x82); break; // é
        case 0xAD: g_tft.write(0xA1); break; // í
        case 0xB3: g_tft.write(0xA2); break; // ó
        case 0xBA: g_tft.write(0xA3); break; // ú
        case 0xB1: g_tft.write(0xA4); break; // ñ
        case 0x91: g_tft.write(0xA5); break; // Ñ
        case 0x89: g_tft.write(0x90); break; // É
        case 0x81: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('A'); break;
        case 0x8D: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('I'); break;
        case 0x93: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('O'); break;
        case 0x9A: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('U'); break;
        default: g_tft.write('?'); break;
      }
      p += 2;
      continue;
    }
    if (c == 0xE2 && p[1] == 0x86 && p[2] != '\0') {
      if (p[2] == 0x90) g_tft.write(0x1B);      // ←
      else if (p[2] == 0x91) g_tft.write(0x18); // ↑
      else if (p[2] == 0x92) g_tft.write(0x1A); // →
      else if (p[2] == 0x93) g_tft.write(0x19); // ↓
      else g_tft.write('-');
      p += 3;
      continue;
    }
    ++p;
  }
}
