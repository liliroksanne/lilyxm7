#include "data_source.h"
#include "obd_protocol.h"
#include "ui_tft.h"
#include "assistant_ascii.h"
#include "config.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "st7789_tft.h"
#include <stdio.h>
#include <string.h>

namespace {

// Paleta Matrix (RGB565): negro puro y distintos niveles de verde fósforo
constexpr uint16_t COLOR_BG             = 0x0000; // Negro puro
constexpr uint16_t COLOR_MATRIX_DARK    = 0x502A; // Rosa oscuro (rejilla / fondo suave)
constexpr uint16_t COLOR_MATRIX_DIM     = 0x996F; // Rosa tenue (marcos y etiquetas secundarias)
constexpr uint16_t COLOR_MATRIX_MID     = 0xCAD6; // Rosa medio (cabello / texto secundario)
constexpr uint16_t COLOR_MATRIX_BRIGHT  = 0xF3DB; // Rosa fosforescente suave (principal)
constexpr uint16_t COLOR_MATRIX_GLOW    = 0xFE1C; // Rosa claro (destello / alerta)

constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;

St7789Tft g_tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// Caché de estado para redibujar solo lo que cambia y evitar parpadeos
bool g_needFullRedraw = true;
ScreenId g_lastScreen = ScreenId::MAIN_MENU;
MenuOption g_lastMenuOption = MenuOption::TEMPERATURE;
bool g_lastHasLiveSignal = false;
bool g_lastHasLastValue = false;
int16_t g_lastTempC = -999;
TempTrend g_lastTrend = TempTrend::TREND_NONE;
uint32_t g_lastValidCount = 0;
uint32_t g_lastConnSec = 999999;
uint32_t g_lastTelemetryRevision = 0;
uint32_t g_lastChatVersion = 0;
uint8_t g_animTick = 0;
uint32_t g_lastAnimMs = 0;

// Dibuja un acento agudo limpio sobre mayúsculas (Á, Í, Ó, Ú) cuando cabe arriba
void drawUpperAcuteAccent(int16_t x, int16_t y, uint8_t size, uint16_t color) {
  if (y < 2) {
    return;
  }
  const int16_t ax = x + (2 * size);
  const int16_t ay = y - 2;
  g_tft.drawLine(ax, ay + 1, ax + size + 1, ay, color);
}

// Imprime texto UTF-8 español usando la tabla CP437 de Adafruit GFX
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

    // Secuencias UTF-8 de 2 bytes (Latín-1 / español)
    if (c == 0xC2 && p[1] != '\0') {
      const uint8_t c2 = p[1];
      if (c2 == 0xB0) g_tft.write(0xF8);      // °
      else if (c2 == 0xB2) g_tft.write(0xFD); // ²
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
        case 0x81: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('A'); break; // Á
        case 0x8D: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('I'); break; // Í
        case 0x93: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('O'); break; // Ó
        case 0x9A: drawUpperAcuteAccent(cx, cy, size, color); g_tft.write('U'); break; // Ú
        default: g_tft.write('?'); break;
      }
      p += 2;
      continue;
    }

    // Flechas UTF-8 de 3 bytes: ↑ (E2 86 91) y ↓ (E2 86 93)
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

void drawTopBar(const char* title, bool simulated, bool hasLiveSignal) {
  g_tft.fillRect(0, 0, SCREEN_W, 22, COLOR_BG);
  g_tft.drawFastHLine(0, 21, SCREEN_W, COLOR_MATRIX_DIM);

  g_tft.setCursor(6, 7);
  printEs(title, COLOR_MATRIX_BRIGHT, 1);

  if (simulated) {
    // Requerimiento explícito: mostrar "SIMULACIÓN" durante pruebas en Wokwi
    g_tft.fillRect(222, 3, 94, 15, COLOR_MATRIX_BRIGHT);
    g_tft.setCursor(239, 7);
    printEs("SIMULACIÓN", COLOR_BG, 1);
  } else if (hasLiveSignal) {
    g_tft.drawRect(228, 3, 88, 15, COLOR_MATRIX_BRIGHT);
    g_tft.setCursor(236, 7);
    printEs("OBD-II CAN", COLOR_MATRIX_BRIGHT, 1);
  } else {
    g_tft.drawRect(228, 3, 88, 15, COLOR_MATRIX_DIM);
    g_tft.setCursor(238, 7);
    printEs("SIN SEÑAL", COLOR_MATRIX_MID, 1);
  }
}

void drawFooter(const char* hint) {
  g_tft.fillRect(0, 226, SCREEN_W, 14, COLOR_BG);
  g_tft.drawFastHLine(0, 225, SCREEN_W, COLOR_MATRIX_DARK);
  g_tft.setCursor(8, 229);
  printEs(hint, COLOR_MATRIX_DIM, 1);
}

void drawDialogueBox(const AssistantState& st) {
  constexpr int16_t bx = 4;
  constexpr int16_t by = 186;
  constexpr int16_t bw = 312;
  constexpr int16_t bh = 36;

  g_tft.fillRect(bx, by, bw, bh, COLOR_BG);
  g_tft.drawRect(bx, by, bw, bh, st.isAlert ? COLOR_MATRIX_GLOW : COLOR_MATRIX_DIM);

  g_tft.setCursor(bx + 6, by + 6);
  printEs("LILY> ", COLOR_MATRIX_MID, 1);
  printEs(st.latestLine, st.isAlert ? COLOR_MATRIX_GLOW : COLOR_MATRIX_BRIGHT, 1);

  g_tft.setCursor(bx + 6, by + 20);
  printEs("INFO> ", COLOR_MATRIX_DIM, 1);
  printEs(st.trendLine, COLOR_MATRIX_MID, 1);
}

void drawChatPanel(const AssistantState& st, uint8_t animTick) {
  constexpr int16_t px = 4;
  constexpr int16_t py = 26;
  constexpr int16_t pw = 144;
  constexpr int16_t ph = 156;

  g_tft.fillRect(px, py, pw, ph, COLOR_BG);
  g_tft.drawRect(px, py, pw, ph, st.isAlert ? COLOR_MATRIX_GLOW : COLOR_MATRIX_DIM);

  // Encabezado del chatbot Matrix
  g_tft.setCursor(px + 6, py + 6);
  printEs("CHAT // LILY", COLOR_MATRIX_BRIGHT, 1);
  g_tft.setCursor(px + pw - 16, py + 6);
  printEs((animTick & 1) ? "*" : "o", COLOR_MATRIX_MID, 1);
  g_tft.drawFastHLine(px + 4, py + 17, pw - 8, COLOR_MATRIX_DARK);

  // Mensajes tipo burbuja de chatbot (hasta 5 visibles en el panel izquierdo)
  const uint8_t total = getChatMessageCount();
  const uint8_t maxVisible = 5;
  const uint8_t startIdx = (total > maxVisible) ? static_cast<uint8_t>(total - maxVisible) : 0;

  int16_t y = py + 23;
  for (uint8_t i = startIdx; i < total; ++i) {
    const ChatMessage& m = getChatMessage(i);
    const bool isNewest = (i == total - 1);
    const uint16_t boxColor = isNewest
                                  ? (m.isAlert ? COLOR_MATRIX_GLOW : COLOR_MATRIX_BRIGHT)
                                  : COLOR_MATRIX_DARK;
    const uint16_t txtColor = isNewest
                                  ? (m.isAlert ? COLOR_MATRIX_GLOW : COLOR_MATRIX_BRIGHT)
                                  : COLOR_MATRIX_MID;

    // Burbuja compacta (20 px alto, hasta 20 caracteres visibles sin cortar UTF-8)
    g_tft.drawRect(px + 5, y, pw - 10, 20, boxColor);
    char clipped[36];
    size_t srcIdx = 0;
    size_t dstIdx = 0;
    uint8_t glyphs = 0;
    while (m.text[srcIdx] != '\0' && glyphs < 20 && dstIdx < sizeof(clipped) - 4) {
      const uint8_t b = static_cast<uint8_t>(m.text[srcIdx]);
      size_t seqLen = 1;
      if ((b & 0xE0) == 0xC0) seqLen = 2;
      else if ((b & 0xF0) == 0xE0) seqLen = 3;
      for (size_t k = 0; k < seqLen && m.text[srcIdx] != '\0'; ++k) {
        clipped[dstIdx++] = m.text[srcIdx++];
      }
      glyphs++;
    }
    clipped[dstIdx] = '\0';
    g_tft.setCursor(px + 9, y + 6);
    printEs(clipped, txtColor, 1);
    y += 23;
  }

  // Etiqueta de estado al pie del panel de chat
  g_tft.drawFastHLine(px + 4, py + ph - 18, pw - 8, COLOR_MATRIX_DARK);
  g_tft.setCursor(px + 8, py + ph - 13);
  printEs("ESTADO: ", COLOR_MATRIX_DIM, 1);
  printEs(st.moodTag, st.isAlert ? COLOR_MATRIX_GLOW : COLOR_MATRIX_BRIGHT, 1);
}

void drawTempBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t tempC, bool live) {
  g_tft.drawRect(x, y, w, h, COLOR_MATRIX_DIM);
  g_tft.fillRect(x + 2, y + 2, w - 4, h - 4, COLOR_BG);

  // Rango visual útil: 20 °C .. 120 °C
  int16_t clamped = tempC;
  if (clamped < 20) clamped = 20;
  if (clamped > 120) clamped = 120;

  const int16_t fillW = static_cast<int16_t>(((clamped - 20) * (w - 4)) / 100);
  if (fillW > 0) {
    uint16_t barColor = live ? COLOR_MATRIX_BRIGHT : COLOR_MATRIX_DIM;
    if (live && tempC >= (TEMP_THRESHOLD_WARN_MAX + 1)) {
      barColor = COLOR_MATRIX_GLOW;
    }
    g_tft.fillRect(x + 2, y + 2, fillW, h - 4, barColor);
  }

  // Marcas de referencia (60 °C y 100 °C)
  const int16_t mark60 = x + 2 + ((40 * (w - 4)) / 100);
  const int16_t mark100 = x + 2 + ((80 * (w - 4)) / 100);
  g_tft.drawFastVLine(mark60, y, h, COLOR_BG);
  g_tft.drawFastVLine(mark100, y, h, COLOR_BG);
}

void drawMenuRightPanel(MenuOption selected) {
  constexpr int16_t rx = 154;
  constexpr int16_t ry = 26;
  constexpr int16_t rw = 162;
  constexpr int16_t rh = 156;

  g_tft.fillRect(rx, ry, rw, rh, COLOR_BG);
  g_tft.drawRect(rx, ry, rw, rh, COLOR_MATRIX_DIM);

  g_tft.setCursor(rx + 10, ry + 10);
  printEs("MENÚ PRINCIPAL", COLOR_MATRIX_BRIGHT, 1);
  g_tft.drawFastHLine(rx + 8, ry + 22, rw - 16, COLOR_MATRIX_DARK);

  const char* items[2] = {
    "Temperatura",
    "Estado de conexión"
  };

  for (uint8_t i = 0; i < 2; ++i) {
    const int16_t itemY = ry + 36 + (i * 38);
    const bool isSel = (static_cast<uint8_t>(selected) == i);

    if (isSel) {
      g_tft.fillRect(rx + 8, itemY, rw - 16, 28, COLOR_MATRIX_BRIGHT);
      g_tft.setCursor(rx + 14, itemY + 10);
      printEs("> ", COLOR_BG, 1);
      printEs(items[i], COLOR_BG, 1);
    } else {
      g_tft.drawRect(rx + 8, itemY, rw - 16, 28, COLOR_MATRIX_DIM);
      g_tft.setCursor(rx + 14, itemY + 10);
      printEs("  ", COLOR_MATRIX_MID, 1);
      printEs(items[i], COLOR_MATRIX_MID, 1);
    }
  }

  g_tft.drawFastHLine(rx + 8, ry + 122, rw - 16, COLOR_MATRIX_DARK);
  g_tft.setCursor(rx + 10, ry + 130);
  printEs("Vitara 2008 V6 2.7L", COLOR_MATRIX_DIM, 1);
  g_tft.setCursor(rx + 10, ry + 142);
  printEs("OBD-II 01 05 // " LILYXM7_VERSION, COLOR_MATRIX_DARK, 1);
}

void drawTemperatureRightPanel(const TempReading& r) {
  constexpr int16_t rx = 154;
  constexpr int16_t ry = 26;
  constexpr int16_t rw = 162;
  constexpr int16_t rh = 156;

  g_tft.fillRect(rx, ry, rw, rh, COLOR_BG);
  g_tft.drawRect(rx, ry, rw, rh, r.hasLiveSignal ? COLOR_MATRIX_DIM : COLOR_MATRIX_GLOW);

  g_tft.setCursor(rx + 8, ry + 8);
  printEs("REFRIGERANTE (01 05)", COLOR_MATRIX_MID, 1);
  g_tft.drawFastHLine(rx + 6, ry + 20, rw - 12, COLOR_MATRIX_DARK);

  if (r.hasLiveSignal) {
    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%3d°C", r.tempC);

    const bool critical = (r.tempC > TEMP_THRESHOLD_WARN_MAX);
    g_tft.setCursor(rx + 14, ry + 36);
    printEs(tempBuf, critical ? COLOR_MATRIX_GLOW : COLOR_MATRIX_BRIGHT, 4);

    // Indicador de tendencia respecto a la lectura anterior
    char trendBuf[28];
    if (r.trend == TempTrend::TREND_UP) {
      snprintf(trendBuf, sizeof(trendBuf), "↑ Subió (+%d°C)", r.deltaC);
    } else if (r.trend == TempTrend::TREND_DOWN) {
      snprintf(trendBuf, sizeof(trendBuf), "↓ Bajó (%d°C)", r.deltaC);
    } else if (r.trend == TempTrend::TREND_STABLE) {
      snprintf(trendBuf, sizeof(trendBuf), "= Estable");
    } else {
      snprintf(trendBuf, sizeof(trendBuf), "* Lectura inicial");
    }

    g_tft.setCursor(rx + 12, ry + 82);
    printEs(trendBuf, COLOR_MATRIX_BRIGHT, 1);

    // Barra visual de temperatura
    drawTempBar(rx + 10, ry + 102, rw - 20, 14, r.tempC, true);

    g_tft.setCursor(rx + 10, ry + 122);
    printEs("20°C", COLOR_MATRIX_DARK, 1);
    g_tft.setCursor(rx + 68, ry + 122);
    printEs("85°C", COLOR_MATRIX_DIM, 1);
    g_tft.setCursor(rx + 118, ry + 122);
    printEs("120°C", COLOR_MATRIX_DARK, 1);

    char countBuf[28];
    snprintf(countBuf, sizeof(countBuf), "Lecturas OK: %lu", static_cast<unsigned long>(r.validCount));
    g_tft.setCursor(rx + 10, ry + 140);
    printEs(countBuf, COLOR_MATRIX_DIM, 1);
  } else {
    // Requerimiento explícito: mostrar "Sin datos del auto" y, si existe, "Última lectura"
    g_tft.fillRect(rx + 8, ry + 30, rw - 16, 26, COLOR_MATRIX_DARK);
    g_tft.drawRect(rx + 8, ry + 30, rw - 16, 26, COLOR_MATRIX_GLOW);
    g_tft.setCursor(rx + 27, ry + 39);
    printEs("Sin datos del auto", COLOR_MATRIX_GLOW, 1);

    if (r.hasLastValue) {
      g_tft.setCursor(rx + 12, ry + 68);
      printEs("Última lectura:", COLOR_MATRIX_MID, 1);

      char lastBuf[16];
      snprintf(lastBuf, sizeof(lastBuf), "%d °C", r.tempC);
      g_tft.setCursor(rx + 22, ry + 84);
      printEs(lastBuf, COLOR_MATRIX_DIM, 3);

      drawTempBar(rx + 10, ry + 118, rw - 20, 12, r.tempC, false);
      g_tft.setCursor(rx + 10, ry + 138);
      printEs("Valor congelado", COLOR_MATRIX_DARK, 1);
    } else {
      g_tft.setCursor(rx + 28, ry + 76);
      printEs("-- °C", COLOR_MATRIX_DIM, 3);
      g_tft.setCursor(rx + 12, ry + 114);
      printEs("Sin lecturas previas", COLOR_MATRIX_DIM, 1);
      g_tft.setCursor(rx + 12, ry + 130);
      printEs("Esperando ECU 01 05", COLOR_MATRIX_DARK, 1);
    }
  }
}

void drawConnectionPanel(const ConnectionInfo& info, const VehicleTelemetry& telemetry) {
  constexpr int16_t rx = 154;
  constexpr int16_t ry = 26;
  constexpr int16_t rw = 162;
  constexpr int16_t rh = 156;

  g_tft.fillRect(rx, ry, rw, rh, COLOR_BG);
  g_tft.drawRect(rx, ry, rw, rh, info.ecuResponding ? COLOR_MATRIX_DIM : COLOR_MATRIX_GLOW);

  g_tft.setCursor(rx + 8, ry + 8);
  printEs("DIAGNÓSTICO ENLACE", COLOR_MATRIX_BRIGHT, 1);
  g_tft.drawFastHLine(rx + 6, ry + 20, rw - 12, COLOR_MATRIX_DARK);

  int16_t y = ry + 28;
  const int16_t step = 15;

  g_tft.setCursor(rx + 8, y);
  printEs("Modo: ", COLOR_MATRIX_DIM, 1);
  printEs(info.isSimulated ? "SIMULACIÓN" : "OBD-II REAL", COLOR_MATRIX_BRIGHT, 1);
  y += step;

  g_tft.setCursor(rx + 8, y);
  printEs("ECU:  ", COLOR_MATRIX_DIM, 1);
  printEs(info.ecuResponding ? "CONECTADA" : "Sin datos",
          info.ecuResponding ? COLOR_MATRIX_BRIGHT : COLOR_MATRIX_GLOW, 1);
  y += step;

  g_tft.setCursor(rx + 8, y);
  printEs("Bus:  ", COLOR_MATRIX_DIM, 1);
  const char* busTxt = "OK";
  switch (info.busState) {
    case BusHardwareState::SIMULATED:          busTxt = "Wokwi virtual"; break;
    case BusHardwareState::RUNNING:            busTxt = "CAN 500 kbps"; break;
    case BusHardwareState::BUS_OFF_RECOVERING: busTxt = "Recuperando..."; break;
    case BusHardwareState::INIT_ERROR:         busTxt = "Error TWAI"; break;
  }
  printEs(busTxt, COLOR_MATRIX_MID, 1);
  y += step;

  g_tft.setCursor(rx + 8, y);
  char buf[42];
  if (telemetry.speedKph >= 0) snprintf(buf, sizeof(buf), "Vel: %d km/h", telemetry.speedKph);
  else snprintf(buf, sizeof(buf), "Vel: sin datos");
  printEs(buf, COLOR_MATRIX_BRIGHT, 1);
  y += step;

  if (telemetry.accelerationValid) {
    const int value = telemetry.accelerationTenths;
    const int magnitude = value < 0 ? -value : value;
    snprintf(buf, sizeof(buf), "Acel: %s%d.%d m/s²", value < 0 ? "-" : "", magnitude / 10, magnitude % 10);
  } else {
    snprintf(buf, sizeof(buf), "Acel: sin datos");
  }
  g_tft.setCursor(rx + 8, y);
  printEs(buf, COLOR_MATRIX_MID, 1);
  y += step;

  const char* mil = telemetry.mil < 0 ? "sin datos" : telemetry.mil ? "ENCENDIDO" : "apagado";
  snprintf(buf, sizeof(buf), "MIL: %s", mil);
  g_tft.setCursor(rx + 8, y);
  printEs(buf, telemetry.mil == 1 ? COLOR_MATRIX_GLOW : COLOR_MATRIX_MID, 1);
  y += step;

  if (!telemetry.dtcValid) snprintf(buf, sizeof(buf), "DTC: sin datos");
  else if (!telemetry.storedDtcCount) snprintf(buf, sizeof(buf), "DTC: 0 guardados");
  else {
    char code[6];
    const unsigned total = telemetry.storedDtcCount;
    const unsigned shown = total < MAX_STORED_DTCS ? total : MAX_STORED_DTCS;
    const unsigned index = (info.nowMs / 3000) % shown;
    obd::formatDtc(telemetry.dtcs[index], code);
    snprintf(buf, sizeof(buf), "DTC: %s (%u/%u)", code, index + 1, total);
  }
  g_tft.setCursor(rx + 8, y);
  printEs(buf, telemetry.dtcValid && telemetry.storedDtcCount ? COLOR_MATRIX_GLOW : COLOR_MATRIX_MID, 1);
  y += step;
  snprintf(buf, sizeof(buf), "TX %lu | RX temp %lu",
           static_cast<unsigned long>(info.txRequests), static_cast<unsigned long>(info.rxValidFrames));
  g_tft.setCursor(rx + 8, y);
  printEs(buf, COLOR_MATRIX_DIM, 1);
}

} // namespace

void uiInit() {
  if (PIN_TFT_BL >= 0) {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
  }

  SPI.begin(PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);
  g_tft.begin();
  g_tft.setRotation(1); // 320x240 horizontal (landscape)
  g_tft.cp437(true);
  g_tft.setTextWrap(false);

  // Pantalla breve de arranque Matrix
  g_tft.fillScreen(COLOR_BG);
  g_tft.drawRect(8, 8, SCREEN_W - 16, SCREEN_H - 16, COLOR_MATRIX_DIM);
  g_tft.setCursor(76, 84);
  printEs("LILYXM7", COLOR_MATRIX_BRIGHT, 4);
  g_tft.setCursor(54, 130);
  printEs("OBD-II // SUZUKI GRAND VITARA", COLOR_MATRIX_MID, 1);
  g_tft.setCursor(98, 148);
  printEs("2008 V6 2.7L  " LILYXM7_VERSION, COLOR_MATRIX_DIM, 1);
  if (LILYXM7_SIMULATION_MODE) {
    g_tft.setCursor(104, 174);
    printEs("[ MODO SIMULACIÓN ]", COLOR_MATRIX_GLOW, 1);
  }
  delay(900);

  g_needFullRedraw = true;
}

void uiForceRedraw() {
  g_needFullRedraw = true;
}

void uiRender(ScreenId screen,
              MenuOption selectedOption,
              const TempReading& tempReading,
              const ConnectionInfo& connInfo) {
  const uint32_t now = millis();

  // Pulso sutil del indicador del chatbot
  bool animChanged = false;
  if ((now - g_lastAnimMs) >= UI_ANIMATION_INTERVAL_MS) {
    g_lastAnimMs = now;
    g_animTick++;
    animChanged = true;
  }

  const VehicleTelemetry& telemetry = getVehicleTelemetry();
  assistantVehicleAlerts(telemetry);
  const AssistantState assistant = updateAssistantChat(screen, tempReading);

  const uint32_t connAgeSec =
      (connInfo.hasLastValue && connInfo.nowMs >= connInfo.lastValidMs)
          ? ((connInfo.nowMs - connInfo.lastValidMs) / 1000)
          : 999998;

  const bool screenChanged = (screen != g_lastScreen);
  const bool menuChanged = (selectedOption != g_lastMenuOption);
  const bool dataChanged =
      (tempReading.hasLiveSignal != g_lastHasLiveSignal) ||
      (tempReading.hasLastValue != g_lastHasLastValue) ||
      (tempReading.tempC != g_lastTempC) ||
      (tempReading.trend != g_lastTrend) ||
      (tempReading.validCount != g_lastValidCount);
  const bool chatChanged = (assistant.chatVersion != g_lastChatVersion);
  const bool connTimeChanged = (screen == ScreenId::CONNECTION_STATUS &&
      (connAgeSec != g_lastConnSec || telemetry.revision != g_lastTelemetryRevision));

  if (!g_needFullRedraw && !screenChanged && !menuChanged && !dataChanged &&
      !connTimeChanged && !chatChanged && !animChanged) {
    return;
  }

  if (g_needFullRedraw || screenChanged) {
    g_tft.fillScreen(COLOR_BG);
  }

  // Barra superior
  if (g_needFullRedraw || screenChanged || (tempReading.hasLiveSignal != g_lastHasLiveSignal)) {
    const char* title = "LILYXM7 // " LILYXM7_VEHICLE_LABEL;
    if (screen == ScreenId::TEMPERATURE) {
      title = "LILYXM7 // TEMPERATURA";
    } else if (screen == ScreenId::CONNECTION_STATUS) {
      title = "LILYXM7 // CONEXIÓN";
    }
    drawTopBar(title, tempReading.isSimulated, tempReading.hasLiveSignal);
  }

  // Panel izquierdo: Chatbot simple Matrix
  if (g_needFullRedraw || screenChanged || dataChanged || chatChanged || animChanged) {
    drawChatPanel(assistant, g_animTick);
  }

  // Panel derecho según pantalla activa
  if (screen == ScreenId::MAIN_MENU) {
    if (g_needFullRedraw || screenChanged || menuChanged) {
      drawMenuRightPanel(selectedOption);
    }
  } else if (screen == ScreenId::TEMPERATURE) {
    if (g_needFullRedraw || screenChanged || dataChanged) {
      drawTemperatureRightPanel(tempReading);
    }
  } else if (screen == ScreenId::CONNECTION_STATUS) {
    if (g_needFullRedraw || screenChanged || dataChanged || connTimeChanged) {
      drawConnectionPanel(connInfo, telemetry);
    }
  }

  // Caja de último mensaje / tendencia
  if (g_needFullRedraw || screenChanged || dataChanged || chatChanged) {
    drawDialogueBox(assistant);
  }

  // Pie de ayuda de botones
  if (g_needFullRedraw || screenChanged) {
    if (screen == ScreenId::MAIN_MENU) {
      drawFooter("[↑/↓] Mover   [A] Seleccionar");
    } else {
      drawFooter("[B] Volver al menú principal");
    }
  }

  g_needFullRedraw = false;
  g_lastScreen = screen;
  g_lastMenuOption = selectedOption;
  g_lastHasLiveSignal = tempReading.hasLiveSignal;
  g_lastHasLastValue = tempReading.hasLastValue;
  g_lastTempC = tempReading.tempC;
  g_lastTrend = tempReading.trend;
  g_lastValidCount = tempReading.validCount;
  g_lastConnSec = connAgeSec;
  g_lastTelemetryRevision = telemetry.revision;
  g_lastChatVersion = assistant.chatVersion;
}
