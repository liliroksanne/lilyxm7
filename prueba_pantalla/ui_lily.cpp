#include "ui_lily.h"
#include "display_driver.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

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

constexpr uint8_t MAX_CHAT = 5;

// Datos simulados de la Suzuki Grand Vitara 2008 V6 2.7 L
bool g_hasLiveSignal = true;
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
ChatItem g_chat[MAX_CHAT];
uint8_t g_chatCount = 0;
int16_t g_lastChatTemp = -999;

// Pulso sutil sin parpadeo
uint8_t g_animTick = 0;
uint32_t g_lastAnimMs = 0;

bool isAlertState() {
  return !g_hasLiveSignal || g_tempC > TEMP_OPTIMAL_MAX;
}

const char* currentMoodTag(ScreenId screen) {
  if (!g_hasLiveSignal) return "SIN SENAL";
  if (screen == ScreenId::MAIN_MENU) return "EN LINEA";
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
    snprintf(msg, sizeof(msg),
             g_trend == TrendId::TREND_UP ? "Subió a %d°C, calent." : "Motor frío: %d°C.",
             g_tempC);
    uiLilyPushChat(msg, false);
  } else if (g_tempC <= TEMP_WARMING_MAX) {
    snprintf(msg, sizeof(msg),
             g_trend == TrendId::TREND_UP ? "Calentando: %d°C (+)" : "Bajó a %d°C, atenta.",
             g_tempC);
    uiLilyPushChat(msg, false);
  } else if (g_tempC <= TEMP_OPTIMAL_MAX) {
    if (g_lastChatTemp <= TEMP_WARMING_MAX || g_lastChatTemp > TEMP_OPTIMAL_MAX) {
      snprintf(msg, sizeof(msg), "%d°C! Temp óptima :)", g_tempC);
    } else if (g_trend == TrendId::TREND_UP) {
      snprintf(msg, sizeof(msg), "Subió a %d°C (ideal).", g_tempC);
    } else {
      snprintf(msg, sizeof(msg), "Bajó a %d°C (ideal).", g_tempC);
    }
    uiLilyPushChat(msg, false);
  } else if (g_tempC <= TEMP_WARN_MAX) {
    snprintf(msg, sizeof(msg), "Ojo! Temp alta: %d°C", g_tempC);
    uiLilyPushChat(msg, true);
  } else {
    snprintf(msg, sizeof(msg), "ALERTA! %d°C detente!", g_tempC);
    uiLilyPushChat(msg, true);
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
  g_validCount++;
  g_txCount++;
  g_lastValidMs = millis();
  announceTempToChat();
}

void drawTopBar(ScreenId screen) {
  g_tft.fillRect(0, 0, SCREEN_W, 22, g_theme.bg);
  g_tft.drawFastHLine(0, 21, SCREEN_W, g_theme.dim);

  const char* title = "LILYXM7 // VITARA 2.7";
  if (screen == ScreenId::TEMPERATURE) title = "LILYXM7 // TEMPERATURA";
  else if (screen == ScreenId::CONNECTION_STATUS) title = "LILYXM7 // CONEXIÓN";

  g_tft.setCursor(6, 7);
  printEs(title, g_theme.bright, 1);

  g_tft.fillRect(222, 3, 94, 15, g_theme.selBg);
  g_tft.drawRect(222, 3, 94, 15, g_theme.bright);
  g_tft.setCursor(239, 7);
  printEs("SIMULACIÓN", g_theme.glow, 1);
}

void updateChatPulseDot() {
  constexpr int16_t px = 4;
  constexpr int16_t py = 26;
  constexpr int16_t pw = 144;
  g_tft.setTextColor(g_theme.mid, g_theme.bg);
  g_tft.setTextSize(1);
  g_tft.setCursor(px + pw - 16, py + 6);
  g_tft.write((g_animTick & 1) ? '*' : 'o');
}

void drawChatPanel(ScreenId screen) {
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

    char clipped[36];
    size_t si = 0, di = 0;
    uint8_t glyphs = 0;
    while (g_chat[i].text[si] != '\0' && glyphs < 20 && di < sizeof(clipped) - 4) {
      const uint8_t b = static_cast<uint8_t>(g_chat[i].text[si]);
      size_t len = 1;
      if ((b & 0xE0) == 0xC0) len = 2;
      else if ((b & 0xF0) == 0xE0) len = 3;
      for (size_t k = 0; k < len && g_chat[i].text[si] != '\0'; ++k) {
        clipped[di++] = g_chat[i].text[si++];
      }
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
  printEs(currentMoodTag(screen), alert ? g_theme.glow : g_theme.bright, 1);
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

void drawMenuItemsOnly(uint8_t menuIdx) {
  constexpr int16_t rx = 154, ry = 26, rw = 162;
  const char* items[MENU_COUNT] = {
    "Temperatura",
    "Estado de conexión",
    "Snake",
    "Lily DOOM"
  };

  for (uint8_t i = 0; i < MENU_COUNT; ++i) {
    const int16_t iy = ry + 22 + (i * 23);
    if (menuIdx == i) {
      g_tft.fillRect(rx + 8, iy, rw - 16, 20, g_theme.selBg);
      g_tft.drawRect(rx + 8, iy, rw - 16, 20, g_theme.bright);
      g_tft.setCursor(rx + 14, iy + 6);
      printEs("> ", g_theme.glow, 1);
      printEs(items[i], g_theme.glow, 1);
    } else {
      g_tft.fillRect(rx + 8, iy, rw - 16, 20, g_theme.bg);
      g_tft.drawRect(rx + 8, iy, rw - 16, 20, g_theme.dark);
      g_tft.setCursor(rx + 14, iy + 6);
      printEs("  ", g_theme.dim, 1);
      printEs(items[i], g_theme.mid, 1);
    }
  }
}

void drawMenuRightPanel(uint8_t menuIdx) {
  constexpr int16_t rx = 154, ry = 26, rw = 162, rh = 156;
  g_tft.fillRect(rx, ry, rw, rh, g_theme.bg);
  g_tft.drawRect(rx, ry, rw, rh, g_theme.dim);

  g_tft.setCursor(rx + 10, ry + 6);
  printEs("MENÚ PRINCIPAL", g_theme.bright, 1);
  g_tft.drawFastHLine(rx + 8, ry + 17, rw - 16, g_theme.dark);

  drawMenuItemsOnly(menuIdx);

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

    g_tft.setCursor(rx + 10, ry + 122);  printEs("20°C", g_theme.dark, 1);
    g_tft.setCursor(rx + 68, ry + 122);  printEs("85°C", g_theme.dim, 1);
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
  snprintf(buf, sizeof(buf), "TX: %lu  RX: %lu",
           static_cast<unsigned long>(g_txCount),
           static_cast<unsigned long>(g_validCount));
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

void drawFooter(ScreenId screen) {
  g_tft.fillRect(0, 226, SCREEN_W, 14, g_theme.bg);
  g_tft.drawFastHLine(0, 225, SCREEN_W, g_theme.dark);
  g_tft.setCursor(6, 229);
  if (screen == ScreenId::MAIN_MENU) {
    printEs("[↑/↓]Mover [A]Abrir [MENU]Color [OPT]Girar", g_theme.dim, 1);
  } else if (screen == ScreenId::TEMPERATURE) {
    printEs("[B]Menú [←/→]Temp [ST]Auto [SEL]Sin señal", g_theme.dim, 1);
  } else {
    printEs("[B]Volver [SEL]Cortar señal [MENU]Color", g_theme.dim, 1);
  }
}

} // namespace

void uiLilyInit() {
  g_lastValidMs = millis();
  g_lastAnimMs = g_lastValidMs;
  uiLilyPushChat("Hola! Soy Lily v1.0.", false);
  uiLilyPushChat("Modo SIMULACIÓN activo.", false);
  uiLilyPushChat("Vitara lista. ¿Tú?", false);
}

void uiLilyShowSplash() {
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

void uiLilyPushChat(const char* msg, bool isAlert) {
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
  Serial.printf("[CHAT] LILY: %s\n", msg);
}

void uiLilyRenderFull(ScreenId screen, uint8_t menuIdx, bool clearScreen) {
  if (clearScreen) {
    g_tft.fillScreen(g_theme.bg);
  }
  drawTopBar(screen);
  drawChatPanel(screen);
  if (screen == ScreenId::MAIN_MENU) drawMenuRightPanel(menuIdx);
  else if (screen == ScreenId::TEMPERATURE) drawTemperatureRightPanel();
  else drawConnectionRightPanel();
  drawDialogueBox();
  drawFooter(screen);
}

void uiLilyHandleButton(ScreenId& screen, uint8_t& menuIdx, int pin) {
  if (pin == BTN_SELECT) {
    g_hasLiveSignal = !g_hasLiveSignal;
    if (!g_hasLiveSignal) {
      g_autoWarmup = false;
      uiLilyPushChat("Sin datos del auto!", true);
      char buf[32];
      snprintf(buf, sizeof(buf), "Última lectura: %d°C", g_tempC);
      uiLilyPushChat(buf, true);
    } else {
      g_lastValidMs = millis();
      g_validCount++;
      char buf[32];
      snprintf(buf, sizeof(buf), "Señal OK! Ahora %d°C", g_tempC);
      uiLilyPushChat(buf, false);
    }
    uiLilyRenderFull(screen, menuIdx, false);
    return;
  }

  if (screen == ScreenId::MAIN_MENU) {
    if (pin == BTN_UP && menuIdx > 0) {
      menuIdx--;
      drawMenuItemsOnly(menuIdx);
    } else if (pin == BTN_DOWN && menuIdx + 1 < MENU_COUNT) {
      menuIdx++;
      drawMenuItemsOnly(menuIdx);
    } else if (pin == BTN_A) {
      if (menuIdx == 0) {
        screen = ScreenId::TEMPERATURE;
        uiLilyPushChat("Abriendo Temperatura...", false);
        uiLilyRenderFull(screen, menuIdx, true);
      } else if (menuIdx == 1) {
        screen = ScreenId::CONNECTION_STATUS;
        uiLilyPushChat("Revisando enlace ECU.", false);
        uiLilyRenderFull(screen, menuIdx, true);
      } else if (menuIdx == 2) {
        screen = ScreenId::SNAKE;
      } else {
        screen = ScreenId::DOOM;
      }
    }
    return;
  }

  if (pin == BTN_B) {
    screen = ScreenId::MAIN_MENU;
    uiLilyPushChat("Menú principal listo.", false);
    uiLilyRenderFull(screen, menuIdx, true);
    return;
  }

  if (screen == ScreenId::TEMPERATURE) {
    if (pin == BTN_RIGHT) {
      g_autoWarmup = false;
      applyNewTemp(static_cast<int16_t>(g_tempC + 3));
      uiLilyRenderFull(screen, menuIdx, false);
    } else if (pin == BTN_LEFT) {
      g_autoWarmup = false;
      applyNewTemp(static_cast<int16_t>(g_tempC - 3));
      uiLilyRenderFull(screen, menuIdx, false);
    } else if (pin == BTN_START) {
      g_autoWarmup = !g_autoWarmup;
      if (g_autoWarmup) {
        g_tempC = 28;
        g_prevTempC = 28;
        g_lastChatTemp = -999;
        uiLilyPushChat("Calentando motor...", false);
        applyNewTemp(30);
      } else {
        uiLilyPushChat("Auto-calent. pausado.", false);
      }
      uiLilyRenderFull(screen, menuIdx, false);
    }
  }
}

void uiLilyTick(ScreenId screen, uint8_t menuIdx) {
  const uint32_t now = millis();

  if (g_autoWarmup && (now - g_lastAutoMs) >= 700) {
    g_lastAutoMs = now;
    int16_t nextT = static_cast<int16_t>(g_tempC + 3);
    if (nextT > 114) nextT = 28;
    applyNewTemp(nextT);
    uiLilyRenderFull(screen, menuIdx, false);
  }

  if ((now - g_lastAnimMs) >= 500) {
    g_lastAnimMs = now;
    g_animTick++;
    updateChatPulseDot();
    if (screen == ScreenId::CONNECTION_STATUS) {
      updateConnectionAgeLineOnly();
    }
  }
}
