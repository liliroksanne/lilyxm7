#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// prueba_pantalla.ino — Diseño visual completo de lilyxm7 v1.0 para probar
// directamente en la placa física LAFVIN "Retro Game Emulator" (ESP32-S3 + ST7789)
//
// Controles físicos en la placa:
//   UP (11) / DOWN (12) : Navegar menú (Temperatura / Estado de conexión)
//   A  (21)             : Seleccionar opción
//   B  (47)             : Volver al menú principal
//   RIGHT (14)          : Subir temperatura demo (+3 °C)
//   LEFT  (13)          : Bajar temperatura demo (-3 °C)
//   START (39)          : Activar/pausar calentamiento gradual automático
//   SELECT (40)         : Cortar/restaurar señal ("Sin datos del auto" + "Última lectura")
//   MENU (5)            : Cambiar paleta Verde Matrix <-> Rosa Lily en vivo
//   OPTION (4)          : Girar pantalla horizontal 180° (Rot 1 <-> 3)
// ============================================================================

// Pines oficiales LAFVIN Retro Game Emulator (ST7789 240x320)
constexpr int TFT_BL   = 6;
constexpr int TFT_CS   = 7;
constexpr int TFT_DC   = 15;
constexpr int TFT_RST  = 16;
constexpr int TFT_MOSI = 17;
constexpr int TFT_SCK  = 18;
constexpr int TFT_MISO = -1;

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
// Driver ST7789 240x320 probado en la placa LAFVIN Retro Game Emulator
// ============================================================================
class LafvinST7789 : public Adafruit_SPITFT {
public:
  LafvinST7789(int16_t cs, int16_t dc, int16_t rst)
      : Adafruit_SPITFT(240, 320, cs, dc, rst) {}

  void begin(uint32_t freq = 24000000) override {
    if (!freq) freq = 24000000;
    initSPI(freq, SPI_MODE0);

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
      case 0: madctl = 0x00; _width = 240; _height = 320; break;
      case 1: madctl = 0x60; _width = 320; _height = 240; break;
      case 2: madctl = 0xC0; _width = 240; _height = 320; break;
      case 3: madctl = 0xA0; _width = 320; _height = 240; break;
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

// ============================================================================
// Paletas de color (Verde Matrix por defecto / Rosa Lily con boton MENU)
// ============================================================================
struct Theme {
  uint16_t bg;
  uint16_t dark;
  uint16_t dim;
  uint16_t mid;
  uint16_t bright;
  uint16_t glow;
};

constexpr Theme THEME_MATRIX = {
  0x0000, // Negro puro
  0x0180, // Verde oscuro
  0x0360, // Verde tenue
  0x05A0, // Verde medio
  0x07E0, // Verde Matrix neón
  0x87F0  // Verde fósforo claro (alerta)
};

constexpr Theme THEME_PINK = {
  0x0000, // Negro puro
  0x2864, // Rosa muy oscuro (separadores)
  0x5949, // Rosa apagado (marcos y etiquetas)
  0x9A90, // Rosa medio suave (texto secundario)
  0xC394, // Rosa pastel suave (principal, nada cegante)
  0xDC98  // Rosa claro suave (alerta / destaque)
};

constexpr uint16_t COLOR_PINK_SEL_BG   = 0x40A7; // Fondo suave de opcion seleccionada (rosa)
constexpr uint16_t COLOR_MATRIX_SEL_BG = 0x0240; // Fondo suave de opcion seleccionada (verde)

// ============================================================================
// Estado de la interfaz demo lilyxm7
// ============================================================================
enum class ScreenId : uint8_t {
  MAIN_MENU = 0,
  TEMPERATURE = 1,
  CONNECTION_STATUS = 2,
  SNAKE = 3
};

enum class TrendId : uint8_t {
  TREND_NONE = 0,
  TREND_STABLE = 1,
  TREND_UP = 2,
  TREND_DOWN = 3
};

struct ChatItem {
  char text[34];
  bool isAlert;
};

struct DebouncedBtn {
  int pin;
  bool lastRaw;
  bool stable;
  uint32_t changedMs;
};

LafvinST7789 g_tft(TFT_CS, TFT_DC, TFT_RST);

Theme g_theme = THEME_PINK;
bool g_usePinkTheme = true;
uint8_t g_rotation = 1;

ScreenId g_screen = ScreenId::MAIN_MENU;
uint8_t g_menuIdx = 0; // Por default siempre seleccionada Temperatura (0)
constexpr uint8_t MENU_COUNT = 3; // 0=Temperatura, 1=Estado de conexión, 2=Snake

// ============================================================================
// Estado del minijuego Snake (misma estética rosa suave / Matrix, sin parpadeo)
// ============================================================================
constexpr int16_t SNAKE_CELL   = 10;
constexpr int16_t SNAKE_COLS   = 30;  // 30 * 10 = 300 px (x: 10..310)
constexpr int16_t SNAKE_ROWS   = 19;  // 19 * 10 = 190 px (y: 26..216)
constexpr int16_t SNAKE_OX     = 10;
constexpr int16_t SNAKE_OY     = 26;
constexpr int16_t SNAKE_MAXLEN = SNAKE_COLS * SNAKE_ROWS;

int8_t g_snakeX[SNAKE_MAXLEN];
int8_t g_snakeY[SNAKE_MAXLEN];
int16_t g_snakeLen = 0;
int8_t g_dirX = 1;
int8_t g_dirY = 0;
int8_t g_nextDirX = 1;
int8_t g_nextDirY = 0;
int8_t g_foodX = 15;
int8_t g_foodY = 9;
uint16_t g_snakeScore = 0;
uint16_t g_snakeBest = 0;
bool g_snakeGameOver = false;
bool g_snakePaused = false;
uint32_t g_lastSnakeStepMs = 0;

// Datos simulados de la Grand Vitara para probar la pantalla en mano
bool g_hasLiveSignal = true;
bool g_hasLastValue = true;
int16_t g_tempC = 86;
int16_t g_prevTempC = 86;
int16_t g_deltaC = 0;
TrendId g_trend = TrendId::TREND_NONE;
uint32_t g_validCount = 1;
uint32_t g_txCount = 1;
uint32_t g_lastValidMs = 0;
uint32_t g_lastShownAgeSec = 999999;
bool g_autoWarmup = false;
uint32_t g_lastAutoMs = 0;

// Historial del chatbot Lily
constexpr uint8_t MAX_CHAT = 5;
ChatItem g_chat[MAX_CHAT];
uint8_t g_chatCount = 0;
uint32_t g_chatVersion = 0;
int16_t g_lastChatTemp = -999;

// Caché de redibujado parcial
bool g_fullRedraw = true;
uint8_t g_animTick = 0;
uint32_t g_lastAnimMs = 0;

DebouncedBtn g_buttons[] = {
  { BTN_UP,     true, true, 0 },
  { BTN_DOWN,   true, true, 0 },
  { BTN_LEFT,   true, true, 0 },
  { BTN_RIGHT,  true, true, 0 },
  { BTN_A,      true, true, 0 },
  { BTN_B,      true, true, 0 },
  { BTN_START,  true, true, 0 },
  { BTN_SELECT, true, true, 0 },
  { BTN_MENU,   true, true, 0 },
  { BTN_OPTION, true, true, 0 },
};

// ============================================================================
// Soporte de texto UTF-8 español -> CP437
// ============================================================================
void drawUpperAcuteAccent(int16_t x, int16_t y, uint8_t size, uint16_t color) {
  if (y < 2) return;
  const int16_t ax = x + (2 * size);
  const int16_t ay = y - 2;
  g_tft.drawLine(ax, ay + 1, ax + size + 1, ay, color);
}

void printEs(const char* text, uint16_t color, uint8_t size = 1) {
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
      if (p[2] == 0x91) g_tft.write(0x18);      // ↑
      else if (p[2] == 0x93) g_tft.write(0x19); // ↓
      else g_tft.write('-');
      p += 3;
      continue;
    }
    ++p;
  }
}

// ============================================================================
// Lógica del chatbot Lily y estado térmico
// ============================================================================
void pushChat(const char* msg, bool isAlert = false) {
  if (!msg || !msg[0]) return;
  if (g_chatCount > 0 && strcmp(g_chat[g_chatCount - 1].text, msg) == 0) return;

  if (g_chatCount < MAX_CHAT) {
    snprintf(g_chat[g_chatCount].text, sizeof(g_chat[g_chatCount].text), "%s", msg);
    g_chat[g_chatCount].isAlert = isAlert;
    g_chatCount++;
  } else {
    for (uint8_t i = 1; i < MAX_CHAT; ++i) g_chat[i - 1] = g_chat[i];
    snprintf(g_chat[MAX_CHAT - 1].text, sizeof(g_chat[MAX_CHAT - 1].text), "%s", msg);
    g_chat[MAX_CHAT - 1].isAlert = isAlert;
  }
  g_chatVersion++;
  Serial.printf("[CHAT] LILY: %s\n", msg);
}

bool isAlertState() {
  return !g_hasLiveSignal || g_tempC > TEMP_OPTIMAL_MAX;
}

const char* currentMoodTag() {
  if (!g_hasLiveSignal) return "SIN SENAL";
  if (g_screen == ScreenId::MAIN_MENU) return "EN LINEA";
  if (g_tempC <= TEMP_COLD_MAX) return "MOTOR FRIO";
  if (g_tempC <= TEMP_WARMING_MAX) return "CALENTANDO";
  if (g_tempC <= TEMP_OPTIMAL_MAX) return "OPTIMO";
  if (g_tempC <= TEMP_WARN_MAX) return "TEMP ALTA";
  return "PELIGRO!";
}

void announceTempToChat() {
  if (!g_hasLiveSignal) return;
  char msg[34];
  if (g_tempC <= TEMP_COLD_MAX) {
    snprintf(msg, sizeof(msg), g_trend == TrendId::TREND_UP ? "Subió a %d°C, calent." : "Motor frío: %d°C.", g_tempC);
    pushChat(msg, false);
  } else if (g_tempC <= TEMP_WARMING_MAX) {
    snprintf(msg, sizeof(msg), g_trend == TrendId::TREND_UP ? "Calentando: %d°C (+)" : "Bajó a %d°C, atenta.", g_tempC);
    pushChat(msg, false);
  } else if (g_tempC <= TEMP_OPTIMAL_MAX) {
    if (g_lastChatTemp <= TEMP_WARMING_MAX || g_lastChatTemp > TEMP_OPTIMAL_MAX) {
      snprintf(msg, sizeof(msg), "%d°C! Temp óptima :)", g_tempC);
    } else if (g_trend == TrendId::TREND_UP) {
      snprintf(msg, sizeof(msg), "Subió a %d°C (ideal).", g_tempC);
    } else {
      snprintf(msg, sizeof(msg), "Bajó a %d°C (ideal).", g_tempC);
    }
    pushChat(msg, false);
  } else if (g_tempC <= TEMP_WARN_MAX) {
    snprintf(msg, sizeof(msg), "Ojo! Temp alta: %d°C", g_tempC);
    pushChat(msg, true);
  } else {
    snprintf(msg, sizeof(msg), "ALERTA! %d°C detente!", g_tempC);
    pushChat(msg, true);
  }
  g_lastChatTemp = g_tempC;
}

void applyNewTemp(int16_t newTemp) {
  if (newTemp < 20) newTemp = 20;
  if (newTemp > 120) newTemp = 120;
  g_prevTempC = g_tempC;
  g_tempC = newTemp;
  g_deltaC = static_cast<int16_t>(g_tempC - g_prevTempC);
  if (g_deltaC > 0) g_trend = TrendId::TREND_UP;
  else if (g_deltaC < 0) g_trend = TrendId::TREND_DOWN;
  else g_trend = TrendId::TREND_STABLE;

  g_hasLiveSignal = true;
  g_hasLastValue = true;
  g_validCount++;
  g_txCount++;
  g_lastValidMs = millis();
  announceTempToChat();
}

// ============================================================================
// Dibujo de pantallas lilyxm7 (320x240 horizontal)
// ============================================================================
void drawTopBar() {
  g_tft.fillRect(0, 0, SCREEN_W, 22, g_theme.bg);
  g_tft.drawFastHLine(0, 21, SCREEN_W, g_theme.dim);

  const char* title = "LILYXM7 // VITARA 2.7";
  if (g_screen == ScreenId::TEMPERATURE) title = "LILYXM7 // TEMPERATURA";
  else if (g_screen == ScreenId::CONNECTION_STATUS) title = "LILYXM7 // CONEXIÓN";

  g_tft.setCursor(6, 7);
  printEs(title, g_theme.bright, 1);

  // Insignia SIMULACIÓN (suave, sin brillo excesivo)
  const uint16_t badgeBg = g_usePinkTheme ? COLOR_PINK_SEL_BG : COLOR_MATRIX_SEL_BG;
  g_tft.fillRect(222, 3, 94, 15, badgeBg);
  g_tft.drawRect(222, 3, 94, 15, g_theme.bright);
  g_tft.setCursor(239, 7);
  printEs("SIMULACIÓN", g_theme.glow, 1);
}

void updateChatPulseDot() {
  // Solo actualiza el puntito sin borrar ni parpadear el panel completo
  constexpr int16_t px = 4;
  constexpr int16_t py = 26;
  constexpr int16_t pw = 144;
  g_tft.setTextColor(g_theme.mid, g_theme.bg);
  g_tft.setTextSize(1);
  g_tft.setCursor(px + pw - 16, py + 6);
  g_tft.write((g_animTick & 1) ? '*' : 'o');
}

void drawChatPanel() {
  constexpr int16_t px = 4;
  constexpr int16_t py = 26;
  constexpr int16_t pw = 144;
  constexpr int16_t ph = 156;
  const bool alert = isAlertState();

  g_tft.fillRect(px, py, pw, ph, g_theme.bg);
  g_tft.drawRect(px, py, pw, ph, alert ? g_theme.glow : g_theme.dim);

  g_tft.setCursor(px + 6, py + 6);
  printEs("CHAT // LILY", g_theme.bright, 1);
  g_tft.setCursor(px + pw - 16, py + 6);
  printEs((g_animTick & 1) ? "*" : "o", g_theme.mid, 1);
  g_tft.drawFastHLine(px + 4, py + 17, pw - 8, g_theme.dark);

  int16_t y = py + 23;
  for (uint8_t i = 0; i < g_chatCount; ++i) {
    const bool isNewest = (i == g_chatCount - 1);
    const uint16_t boxCol = isNewest ? (g_chat[i].isAlert ? g_theme.glow : g_theme.bright) : g_theme.dark;
    const uint16_t txtCol = isNewest ? (g_chat[i].isAlert ? g_theme.glow : g_theme.bright) : g_theme.mid;

    g_tft.drawRect(px + 5, y, pw - 10, 20, boxCol);

    // Recorte seguro por caracteres UTF-8 (máx. 20 glifos)
    char clipped[36];
    size_t si = 0, di = 0;
    uint8_t glyphs = 0;
    while (g_chat[i].text[si] != '\0' && glyphs < 20 && di < sizeof(clipped) - 4) {
      const uint8_t b = static_cast<uint8_t>(g_chat[i].text[si]);
      size_t len = 1;
      if ((b & 0xE0) == 0xC0) len = 2;
      else if ((b & 0xF0) == 0xE0) len = 3;
      for (size_t k = 0; k < len && g_chat[i].text[si] != '\0'; ++k) clipped[di++] = g_chat[i].text[si++];
      glyphs++;
    }
    clipped[di] = '\0';

    g_tft.setCursor(px + 9, y + 6);
    printEs(clipped, txtCol, 1);
    y += 23;
  }

  g_tft.drawFastHLine(px + 4, py + ph - 18, pw - 8, g_theme.dark);
  g_tft.setCursor(px + 8, py + ph - 13);
  printEs("ESTADO: ", g_theme.dim, 1);
  printEs(currentMoodTag(), alert ? g_theme.glow : g_theme.bright, 1);
}

void drawTempBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t tempC, bool live) {
  g_tft.drawRect(x, y, w, h, g_theme.dim);
  g_tft.fillRect(x + 2, y + 2, w - 4, h - 4, g_theme.bg);

  int16_t clamped = tempC;
  if (clamped < 20) clamped = 20;
  if (clamped > 120) clamped = 120;

  const int16_t fillW = static_cast<int16_t>(((clamped - 20) * (w - 4)) / 100);
  if (fillW > 0) {
    const uint16_t col = live ? (tempC > TEMP_WARN_MAX ? g_theme.glow : g_theme.bright) : g_theme.dim;
    g_tft.fillRect(x + 2, y + 2, fillW, h - 4, col);
  }
  const int16_t m60  = x + 2 + ((40 * (w - 4)) / 100);
  const int16_t m100 = x + 2 + ((80 * (w - 4)) / 100);
  g_tft.drawFastVLine(m60, y, h, g_theme.bg);
  g_tft.drawFastVLine(m100, y, h, g_theme.bg);
}

void drawMenuItemsOnly() {
  constexpr int16_t rx = 154, ry = 26, rw = 162;
  const uint16_t selBg = g_usePinkTheme ? COLOR_PINK_SEL_BG : COLOR_MATRIX_SEL_BG;
  const char* items[MENU_COUNT] = { "Temperatura", "Estado de conexión", "Snake" };

  for (uint8_t i = 0; i < MENU_COUNT; ++i) {
    const int16_t iy = ry + 26 + (i * 29);
    if (g_menuIdx == i) {
      g_tft.fillRect(rx + 8, iy, rw - 16, 24, selBg);
      g_tft.drawRect(rx + 8, iy, rw - 16, 24, g_theme.bright);
      g_tft.setCursor(rx + 14, iy + 8);
      printEs("> ", g_theme.glow, 1);
      printEs(items[i], g_theme.glow, 1);
    } else {
      g_tft.fillRect(rx + 8, iy, rw - 16, 24, g_theme.bg);
      g_tft.drawRect(rx + 8, iy, rw - 16, 24, g_theme.dark);
      g_tft.setCursor(rx + 14, iy + 8);
      printEs("  ", g_theme.dim, 1);
      printEs(items[i], g_theme.mid, 1);
    }
  }
}

void drawMenuRightPanel() {
  constexpr int16_t rx = 154, ry = 26, rw = 162, rh = 156;
  g_tft.fillRect(rx, ry, rw, rh, g_theme.bg);
  g_tft.drawRect(rx, ry, rw, rh, g_theme.dim);

  g_tft.setCursor(rx + 10, ry + 8);
  printEs("MENÚ PRINCIPAL", g_theme.bright, 1);
  g_tft.drawFastHLine(rx + 8, ry + 19, rw - 16, g_theme.dark);

  drawMenuItemsOnly();

  g_tft.drawFastHLine(rx + 8, ry + 118, rw - 16, g_theme.dark);
  g_tft.setCursor(rx + 10, ry + 126);
  printEs("Vitara 2008 V6 2.7L", g_theme.dim, 1);
  g_tft.setCursor(rx + 10, ry + 140);
  printEs("OBD-II 01 05 // v1.0", g_theme.dark, 1);
}

void drawTemperatureRightPanel() {
  constexpr int16_t rx = 154, ry = 26, rw = 162, rh = 156;
  g_tft.fillRect(rx, ry, rw, rh, g_theme.bg);
  g_tft.drawRect(rx, ry, rw, rh, g_hasLiveSignal ? g_theme.dim : g_theme.glow);

  g_tft.setCursor(rx + 8, ry + 8);
  printEs("REFRIGERANTE (01 05)", g_theme.mid, 1);
  g_tft.drawFastHLine(rx + 6, ry + 20, rw - 12, g_theme.dark);

  if (g_hasLiveSignal) {
    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%3d°C", g_tempC);
    g_tft.setCursor(rx + 14, ry + 36);
    printEs(tempBuf, (g_tempC > TEMP_WARN_MAX) ? g_theme.glow : g_theme.bright, 4);

    char trendBuf[28];
    if (g_trend == TrendId::TREND_UP) snprintf(trendBuf, sizeof(trendBuf), "↑ Subió (+%d°C)", g_deltaC);
    else if (g_trend == TrendId::TREND_DOWN) snprintf(trendBuf, sizeof(trendBuf), "↓ Bajó (%d°C)", g_deltaC);
    else if (g_trend == TrendId::TREND_STABLE) snprintf(trendBuf, sizeof(trendBuf), "= Estable");
    else snprintf(trendBuf, sizeof(trendBuf), "* Lectura inicial");

    g_tft.setCursor(rx + 12, ry + 82);
    printEs(trendBuf, g_theme.bright, 1);

    drawTempBar(rx + 10, ry + 102, rw - 20, 14, g_tempC, true);

    g_tft.setCursor(rx + 10, ry + 122); printEs("20°C", g_theme.dark, 1);
    g_tft.setCursor(rx + 68, ry + 122); printEs("85°C", g_theme.dim, 1);
    g_tft.setCursor(rx + 118, ry + 122); printEs("120°C", g_theme.dark, 1);

    char cntBuf[28];
    snprintf(cntBuf, sizeof(cntBuf), "%s | OK:%lu",
             g_autoWarmup ? "AUTO" : "<-/->",
             static_cast<unsigned long>(g_validCount));
    g_tft.setCursor(rx + 10, ry + 140);
    printEs(cntBuf, g_theme.dim, 1);
  } else {
    g_tft.fillRect(rx + 8, ry + 30, rw - 16, 26, g_theme.dark);
    g_tft.drawRect(rx + 8, ry + 30, rw - 16, 26, g_theme.glow);
    g_tft.setCursor(rx + 27, ry + 39);
    printEs("Sin datos del auto", g_theme.glow, 1);

    g_tft.setCursor(rx + 12, ry + 68);
    printEs("Última lectura:", g_theme.mid, 1);

    char lastBuf[16];
    snprintf(lastBuf, sizeof(lastBuf), "%d °C", g_tempC);
    g_tft.setCursor(rx + 22, ry + 84);
    printEs(lastBuf, g_theme.dim, 3);

    drawTempBar(rx + 10, ry + 118, rw - 20, 12, g_tempC, false);
    g_tft.setCursor(rx + 10, ry + 138);
    printEs("[SELECT] Reconectar", g_theme.dark, 1);
  }
}

void updateConnectionAgeLineOnly() {
  constexpr int16_t rx = 154, ry = 26;
  const uint32_t ageSec = (millis() >= g_lastValidMs) ? ((millis() - g_lastValidMs) / 1000) : 0;
  if (ageSec == g_lastShownAgeSec) return;
  g_lastShownAgeSec = ageSec;

  const int16_t y = ry + 28 + (5 * 16);
  g_tft.fillRect(rx + 8, y - 2, 146, 12, g_theme.bg);
  char buf[32];
  snprintf(buf, sizeof(buf), "Última: %d°C (%lus)", g_tempC, static_cast<unsigned long>(ageSec));
  g_tft.setCursor(rx + 8, y);
  printEs(buf, g_theme.dim, 1);
}

void drawConnectionRightPanel() {
  constexpr int16_t rx = 154, ry = 26, rw = 162, rh = 156;
  g_tft.fillRect(rx, ry, rw, rh, g_theme.bg);
  g_tft.drawRect(rx, ry, rw, rh, g_hasLiveSignal ? g_theme.dim : g_theme.glow);

  g_tft.setCursor(rx + 8, ry + 8);
  printEs("DIAGNÓSTICO ENLACE", g_theme.bright, 1);
  g_tft.drawFastHLine(rx + 6, ry + 20, rw - 12, g_theme.dark);

  int16_t y = ry + 28;
  constexpr int16_t step = 16;

  g_tft.setCursor(rx + 8, y); printEs("Modo: ", g_theme.dim, 1); printEs("SIMULACIÓN", g_theme.bright, 1); y += step;
  g_tft.setCursor(rx + 8, y); printEs("ECU:  ", g_theme.dim, 1);
  printEs(g_hasLiveSignal ? "CONECTADA" : "Sin datos", g_hasLiveSignal ? g_theme.bright : g_theme.glow, 1); y += step;
  g_tft.setCursor(rx + 8, y); printEs("Bus:  ", g_theme.dim, 1); printEs("Demo pantalla", g_theme.mid, 1); y += step;
  g_tft.setCursor(rx + 8, y); printEs("PID:  ", g_theme.dim, 1); printEs("01 05 (Refrig.)", g_theme.mid, 1); y += step;

  char buf[32];
  snprintf(buf, sizeof(buf), "TX: %lu  RX: %lu", static_cast<unsigned long>(g_txCount), static_cast<unsigned long>(g_validCount));
  g_tft.setCursor(rx + 8, y); printEs(buf, g_theme.mid, 1); y += step;

  g_lastShownAgeSec = 999999;
  updateConnectionAgeLineOnly();
  y += step;

  g_tft.setCursor(rx + 8, y);
  printEs(g_usePinkTheme ? "Tema: Rosa suave" : "Tema: Verde Matrix", g_theme.dark, 1);
}

void drawDialogueBox() {
  constexpr int16_t bx = 4, by = 186, bw = 312, bh = 36;
  const bool alert = isAlertState();

  g_tft.fillRect(bx, by, bw, bh, g_theme.bg);
  g_tft.drawRect(bx, by, bw, bh, alert ? g_theme.glow : g_theme.dim);

  const char* latest = (g_chatCount > 0) ? g_chat[g_chatCount - 1].text : "Lista.";
  g_tft.setCursor(bx + 6, by + 6);
  printEs("LILY> ", g_theme.mid, 1);
  printEs(latest, alert ? g_theme.glow : g_theme.bright, 1);

  char infoBuf[44];
  if (!g_hasLiveSignal) {
    snprintf(infoBuf, sizeof(infoBuf), "Última lectura: %d°C (sin señal)", g_tempC);
  } else if (g_trend == TrendId::TREND_UP) {
    snprintf(infoBuf, sizeof(infoBuf), "Subió +%d°C (de %d°C)", g_deltaC, g_prevTempC);
  } else if (g_trend == TrendId::TREND_DOWN) {
    snprintf(infoBuf, sizeof(infoBuf), "Bajó %d°C (de %d°C)", g_deltaC, g_prevTempC);
  } else {
    snprintf(infoBuf, sizeof(infoBuf), "Estable en %d°C // Vitara 2.7L", g_tempC);
  }
  g_tft.setCursor(bx + 6, by + 20);
  printEs("INFO> ", g_theme.dim, 1);
  printEs(infoBuf, g_theme.mid, 1);
}

void drawFooter() {
  g_tft.fillRect(0, 226, SCREEN_W, 14, g_theme.bg);
  g_tft.drawFastHLine(0, 225, SCREEN_W, g_theme.dark);
  g_tft.setCursor(6, 229);
  if (g_screen == ScreenId::MAIN_MENU) {
    printEs("[↑/↓]Mover [A]Abrir [MENU]Color [OPT]Girar", g_theme.dim, 1);
  } else if (g_screen == ScreenId::TEMPERATURE) {
    printEs("[B]Menú [←/→]Temp [ST]Auto [SEL]Sin señal", g_theme.dim, 1);
  } else if (g_screen == ScreenId::SNAKE) {
    printEs("[↑↓←→]Mover [ST]Pausa [B]Menú [MENU]Color", g_theme.dim, 1);
  } else {
    printEs("[B]Volver [SEL]Cortar señal [MENU]Color", g_theme.dim, 1);
  }
}

// ============================================================================
// Lógica y dibujo del juego Snake (sin parpadeo)
// ============================================================================
bool isSnakeOccupying(int8_t cx, int8_t cy, int16_t ignoreTail = 0) {
  const int16_t limit = g_snakeLen - ignoreTail;
  for (int16_t i = 0; i < limit; ++i) {
    if (g_snakeX[i] == cx && g_snakeY[i] == cy) return true;
  }
  return false;
}

void drawSnakeCell(int8_t cx, int8_t cy, uint16_t fillCol, uint16_t borderCol = 0xFFFF) {
  const int16_t px = SNAKE_OX + cx * SNAKE_CELL;
  const int16_t py = SNAKE_OY + cy * SNAKE_CELL;
  g_tft.fillRect(px + 1, py + 1, SNAKE_CELL - 2, SNAKE_CELL - 2, fillCol);
  if (borderCol != 0xFFFF) {
    g_tft.drawRect(px + 1, py + 1, SNAKE_CELL - 2, SNAKE_CELL - 2, borderCol);
  }
}

void eraseSnakeCell(int8_t cx, int8_t cy) {
  const int16_t px = SNAKE_OX + cx * SNAKE_CELL;
  const int16_t py = SNAKE_OY + cy * SNAKE_CELL;
  g_tft.fillRect(px, py, SNAKE_CELL, SNAKE_CELL, g_theme.bg);
}

void drawSnakeFood() {
  const int16_t px = SNAKE_OX + g_foodX * SNAKE_CELL;
  const int16_t py = SNAKE_OY + g_foodY * SNAKE_CELL;
  g_tft.fillRect(px + 2, py + 2, SNAKE_CELL - 4, SNAKE_CELL - 4, g_theme.glow);
  g_tft.drawRect(px + 1, py + 1, SNAKE_CELL - 2, SNAKE_CELL - 2, g_theme.bright);
}

void spawnSnakeFood() {
  for (int tries = 0; tries < 200; ++tries) {
    const int8_t fx = static_cast<int8_t>(random(0, SNAKE_COLS));
    const int8_t fy = static_cast<int8_t>(random(0, SNAKE_ROWS));
    if (!isSnakeOccupying(fx, fy)) {
      g_foodX = fx;
      g_foodY = fy;
      return;
    }
  }
  for (int8_t y = 0; y < SNAKE_ROWS; ++y) {
    for (int8_t x = 0; x < SNAKE_COLS; ++x) {
      if (!isSnakeOccupying(x, y)) {
        g_foodX = x;
        g_foodY = y;
        return;
      }
    }
  }
}

void drawSnakeHud() {
  g_tft.fillRect(0, 0, SCREEN_W, 22, g_theme.bg);
  g_tft.drawFastHLine(0, 21, SCREEN_W, g_theme.dim);

  g_tft.setCursor(8, 7);
  printEs("LILYXM7 // SNAKE", g_theme.bright, 1);

  char buf[40];
  snprintf(buf, sizeof(buf), "PTS:%u  MAX:%u", g_snakeScore, g_snakeBest);
  g_tft.setCursor(184, 7);
  printEs(buf, g_theme.glow, 1);
}

void drawSnakeGameOverBox() {
  constexpr int16_t bx = 56, by = 68, bw = 208, bh = 104;
  const uint16_t boxBg = g_usePinkTheme ? COLOR_PINK_SEL_BG : COLOR_MATRIX_SEL_BG;
  g_tft.fillRect(bx, by, bw, bh, g_theme.bg);
  g_tft.drawRect(bx, by, bw, bh, g_theme.glow);
  g_tft.fillRect(bx + 2, by + 2, bw - 4, 24, boxBg);

  g_tft.setCursor(bx + 50, by + 7);
  printEs("GAME OVER", g_theme.glow, 2);

  char buf[36];
  snprintf(buf, sizeof(buf), "Puntos: %u   Record: %u", g_snakeScore, g_snakeBest);
  g_tft.setCursor(bx + 28, by + 40);
  printEs(buf, g_theme.bright, 1);

  g_tft.setCursor(bx + 36, by + 64);
  printEs("[A] Jugar de nuevo", g_theme.glow, 1);
  g_tft.setCursor(bx + 36, by + 80);
  printEs("[B] Volver al menú", g_theme.mid, 1);
}

void drawSnakePauseBanner() {
  constexpr int16_t bx = 104, by = 102, bw = 112, bh = 32;
  g_tft.fillRect(bx, by, bw, bh, g_theme.bg);
  g_tft.drawRect(bx, by, bw, bh, g_theme.bright);
  g_tft.setCursor(bx + 26, by + 9);
  printEs("PAUSA", g_theme.glow, 2);
}

void drawSnakeBoardFull() {
  g_tft.fillScreen(g_theme.bg);
  drawSnakeHud();
  g_tft.drawRect(SNAKE_OX - 2, SNAKE_OY - 2, SNAKE_COLS * SNAKE_CELL + 4, SNAKE_ROWS * SNAKE_CELL + 4, g_theme.dim);

  for (int16_t i = g_snakeLen - 1; i >= 0; --i) {
    if (i == 0) drawSnakeCell(g_snakeX[0], g_snakeY[0], g_theme.glow, g_theme.bright);
    else drawSnakeCell(g_snakeX[i], g_snakeY[i], g_theme.mid, g_theme.bright);
  }
  drawSnakeFood();
  drawFooter();

  if (g_snakeGameOver) drawSnakeGameOverBox();
  else if (g_snakePaused) drawSnakePauseBanner();
}

void startNewSnakeGame() {
  randomSeed(micros());
  g_snakeLen = 4;
  for (int16_t i = 0; i < g_snakeLen; ++i) {
    g_snakeX[i] = static_cast<int8_t>(12 - i);
    g_snakeY[i] = 9;
  }
  g_dirX = 1;
  g_dirY = 0;
  g_nextDirX = 1;
  g_nextDirY = 0;
  g_snakeScore = 0;
  g_snakeGameOver = false;
  g_snakePaused = false;
  g_lastSnakeStepMs = millis();
  spawnSnakeFood();
  drawSnakeBoardFull();
  Serial.println(F("[SNAKE] Nueva partida iniciada."));
}

void stepSnake() {
  if (g_snakeGameOver || g_snakePaused) return;

  g_dirX = g_nextDirX;
  g_dirY = g_nextDirY;
  const int8_t nx = static_cast<int8_t>(g_snakeX[0] + g_dirX);
  const int8_t ny = static_cast<int8_t>(g_snakeY[0] + g_dirY);

  // Choque contra pared o contra su propio cuerpo
  if (nx < 0 || nx >= SNAKE_COLS || ny < 0 || ny >= SNAKE_ROWS || isSnakeOccupying(nx, ny, 1)) {
    g_snakeGameOver = true;
    if (g_snakeScore > g_snakeBest) g_snakeBest = g_snakeScore;
    drawSnakeHud();
    drawSnakeGameOverBox();
    Serial.printf("[SNAKE] GAME OVER | pts=%u max=%u\n", g_snakeScore, g_snakeBest);
    return;
  }

  const bool ate = (nx == g_foodX && ny == g_foodY);
  const int8_t oldTailX = g_snakeX[g_snakeLen - 1];
  const int8_t oldTailY = g_snakeY[g_snakeLen - 1];
  const int8_t oldHeadX = g_snakeX[0];
  const int8_t oldHeadY = g_snakeY[0];

  if (ate && g_snakeLen < SNAKE_MAXLEN) {
    g_snakeLen++;
    g_snakeScore++;
    if (g_snakeScore > g_snakeBest) g_snakeBest = g_snakeScore;
  } else {
    eraseSnakeCell(oldTailX, oldTailY);
  }

  for (int16_t i = g_snakeLen - 1; i > 0; --i) {
    g_snakeX[i] = g_snakeX[i - 1];
    g_snakeY[i] = g_snakeY[i - 1];
  }
  g_snakeX[0] = nx;
  g_snakeY[0] = ny;

  // Redibujar solo cabeza anterior, cabeza nueva y comida si comió (0 parpadeo)
  drawSnakeCell(oldHeadX, oldHeadY, g_theme.mid, g_theme.bright);
  drawSnakeCell(nx, ny, g_theme.glow, g_theme.bright);

  if (ate) {
    spawnSnakeFood();
    drawSnakeFood();
    drawSnakeHud();
    Serial.printf("[SNAKE] +1 punto! pts=%u\n", g_snakeScore);
  }
}

void renderAll() {
  if (g_screen == ScreenId::SNAKE) {
    drawSnakeBoardFull();
    g_fullRedraw = false;
    return;
  }
  if (g_fullRedraw) {
    g_tft.fillScreen(g_theme.bg);
  }
  drawTopBar();
  drawChatPanel();
  if (g_screen == ScreenId::MAIN_MENU) drawMenuRightPanel();
  else if (g_screen == ScreenId::TEMPERATURE) drawTemperatureRightPanel();
  else drawConnectionRightPanel();
  drawDialogueBox();
  drawFooter();
  g_fullRedraw = false;
}

void drawSplashScreen() {
  g_tft.fillScreen(g_theme.bg);
  g_tft.drawRect(8, 8, SCREEN_W - 16, SCREEN_H - 16, g_theme.dim);
  g_tft.setCursor(76, 76);
  printEs("LILYXM7", g_theme.bright, 4);
  g_tft.setCursor(54, 124);
  printEs("OBD-II // SUZUKI GRAND VITARA", g_theme.mid, 1);
  g_tft.setCursor(98, 142);
  printEs("2008 V6 2.7L  v1.0", g_theme.dim, 1);
  g_tft.setCursor(86, 172);
  printEs("[ PRUEBA DE PANTALLA ]", g_theme.glow, 1);
  delay(1000);
}

// ============================================================================
// Manejo de botones físicos
// ============================================================================
int pollButtons() {
  const uint32_t now = millis();
  for (auto& b : g_buttons) {
    const bool rawHigh = (digitalRead(b.pin) == HIGH);
    if (rawHigh != b.lastRaw) {
      b.lastRaw = rawHigh;
      b.changedMs = now;
    }
    if ((now - b.changedMs) >= 40) {
      if (rawHigh != b.stable) {
        b.stable = rawHigh;
        if (!b.stable) return b.pin;
      }
    }
  }
  return -1;
}

void handleButton(int pin) {
  if (pin < 0) return;
  Serial.printf("[BTN] GPIO %d\n", pin);

  if (pin == BTN_MENU) {
    g_usePinkTheme = !g_usePinkTheme;
    g_theme = g_usePinkTheme ? THEME_PINK : THEME_MATRIX;
    pushChat(g_usePinkTheme ? "Tema: Rosa Lily." : "Tema: Verde Matrix.", false);
    g_fullRedraw = true;
    renderAll();
    return;
  }

  if (pin == BTN_OPTION) {
    g_rotation = (g_rotation == 1) ? 3 : 1;
    g_tft.setRotation(g_rotation);
    g_fullRedraw = true;
    renderAll();
    return;
  }

  // Controles exclusivos dentro de Snake
  if (g_screen == ScreenId::SNAKE) {
    if (pin == BTN_B) {
      g_screen = ScreenId::MAIN_MENU;
      g_fullRedraw = true;
      renderAll();
      return;
    }
    if (g_snakeGameOver) {
      if (pin == BTN_A || pin == BTN_START) {
        startNewSnakeGame();
      }
      return;
    }
    if (pin == BTN_START) {
      g_snakePaused = !g_snakePaused;
      if (g_snakePaused) drawSnakePauseBanner();
      else drawSnakeBoardFull();
      return;
    }
    if (!g_snakePaused) {
      if (pin == BTN_UP && g_dirY != 1)         { g_nextDirX = 0;  g_nextDirY = -1; }
      else if (pin == BTN_DOWN && g_dirY != -1) { g_nextDirX = 0;  g_nextDirY = 1;  }
      else if (pin == BTN_LEFT && g_dirX != 1)  { g_nextDirX = -1; g_nextDirY = 0;  }
      else if (pin == BTN_RIGHT && g_dirX != -1){ g_nextDirX = 1;  g_nextDirY = 0;  }
    }
    return;
  }

  if (pin == BTN_SELECT) {
    g_hasLiveSignal = !g_hasLiveSignal;
    if (!g_hasLiveSignal) {
      g_autoWarmup = false;
      pushChat("Sin datos del auto!", true);
      char buf[32];
      snprintf(buf, sizeof(buf), "Última lectura: %d°C", g_tempC);
      pushChat(buf, true);
    } else {
      g_lastValidMs = millis();
      g_validCount++;
      char buf[32];
      snprintf(buf, sizeof(buf), "Señal OK! Ahora %d°C", g_tempC);
      pushChat(buf, false);
    }
    renderAll();
    return;
  }

  if (g_screen == ScreenId::MAIN_MENU) {
    if (pin == BTN_UP && g_menuIdx > 0) {
      g_menuIdx--;
      drawMenuItemsOnly();
    } else if (pin == BTN_DOWN && g_menuIdx + 1 < MENU_COUNT) {
      g_menuIdx++;
      drawMenuItemsOnly();
    } else if (pin == BTN_A) {
      if (g_menuIdx == 0) {
        g_screen = ScreenId::TEMPERATURE;
        pushChat("Abriendo Temperatura...", false);
        g_fullRedraw = true;
        renderAll();
      } else if (g_menuIdx == 1) {
        g_screen = ScreenId::CONNECTION_STATUS;
        pushChat("Revisando enlace ECU.", false);
        g_fullRedraw = true;
        renderAll();
      } else {
        g_screen = ScreenId::SNAKE;
        startNewSnakeGame();
      }
    }
    return;
  }

  if (pin == BTN_B) {
    g_screen = ScreenId::MAIN_MENU;
    pushChat("Menú principal listo.", false);
    g_fullRedraw = true;
    renderAll();
    return;
  }

  if (g_screen == ScreenId::TEMPERATURE) {
    if (pin == BTN_RIGHT) {
      g_autoWarmup = false;
      applyNewTemp(static_cast<int16_t>(g_tempC + 3));
      renderAll();
    } else if (pin == BTN_LEFT) {
      g_autoWarmup = false;
      applyNewTemp(static_cast<int16_t>(g_tempC - 3));
      renderAll();
    } else if (pin == BTN_START) {
      g_autoWarmup = !g_autoWarmup;
      if (g_autoWarmup) {
        g_tempC = 28;
        g_prevTempC = 28;
        g_lastChatTemp = -999;
        pushChat("Calentando motor...", false);
        applyNewTemp(30);
      } else {
        pushChat("Auto-calent. pausado.", false);
      }
      renderAll();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("========================================="));
  Serial.println(F("  lilyxm7 v1.0 - Prueba visual pantalla"));
  Serial.println(F("========================================="));

  const uint32_t now = millis();
  for (auto& b : g_buttons) {
    pinMode(b.pin, INPUT_PULLUP);
    const bool high = (digitalRead(b.pin) == HIGH);
    b.lastRaw = high;
    b.stable = high;
    b.changedMs = now;
  }

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  g_tft.begin();
  g_tft.setRotation(g_rotation);
  g_tft.cp437(true);
  g_tft.setTextWrap(false);

  drawSplashScreen();

  g_lastValidMs = millis();
  pushChat("Hola! Soy Lily v1.0.", false);
  pushChat("Modo SIMULACIÓN activo.", false);
  pushChat("Vitara lista. ¿Tú?", false);

  g_fullRedraw = true;
  renderAll();
}

void loop() {
  handleButton(pollButtons());

  const uint32_t now = millis();

  // Paso del juego Snake (acelera suave conforme sumas puntos)
  if (g_screen == ScreenId::SNAKE) {
    uint32_t stepMs = 160;
    const uint32_t speedup = (g_snakeScore / 3) * 10;
    if (speedup < 75) stepMs -= speedup;
    else stepMs = 85;

    if (!g_snakeGameOver && !g_snakePaused && (now - g_lastSnakeStepMs) >= stepMs) {
      g_lastSnakeStepMs = now;
      stepSnake();
    }
    delay(10);
    return;
  }

  // Calentamiento gradual automático al pulsar START en pantalla Temperatura
  if (g_autoWarmup && (now - g_lastAutoMs) >= 700) {
    g_lastAutoMs = now;
    int16_t nextT = static_cast<int16_t>(g_tempC + 3);
    if (nextT > 114) nextT = 28;
    applyNewTemp(nextT);
    renderAll();
  }

  // Pulso sutil sin parpadeo: solo actualiza el puntito del chat y la linea de segundos
  if ((now - g_lastAnimMs) >= 500) {
    g_lastAnimMs = now;
    g_animTick++;
    updateChatPulseDot();
    if (g_screen == ScreenId::CONNECTION_STATUS) {
      updateConnectionAgeLineOnly();
    }
  }

  delay(15);
}
