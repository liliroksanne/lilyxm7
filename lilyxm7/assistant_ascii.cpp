#include "assistant_ascii.h"
#include "config.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

ChatMessage g_chatHistory[CHAT_MAX_MESSAGES];
uint8_t g_chatCount = 0;
uint32_t g_chatVersion = 0;

bool g_hasInitialized = false;
ScreenId g_prevScreen = ScreenId::MAIN_MENU;
AssistantMood g_prevMood = AssistantMood::MENU_IDLE;
bool g_prevLiveSignal = false;
int16_t g_lastAnnouncedTemp = -999;
char g_trendBuf[48];

void pushChatMessage(const char* msg, bool isAlert) {
  if (!msg || msg[0] == '\0') {
    return;
  }

  // Evita duplicar exactamente el último mensaje consecutivo
  if (g_chatCount > 0 && strcmp(g_chatHistory[g_chatCount - 1].text, msg) == 0) {
    return;
  }

  if (g_chatCount < CHAT_MAX_MESSAGES) {
    snprintf(g_chatHistory[g_chatCount].text, CHAT_MSG_MAX_LEN, "%s", msg);
    g_chatHistory[g_chatCount].isAlert = isAlert;
    g_chatCount++;
  } else {
    for (uint8_t i = 1; i < CHAT_MAX_MESSAGES; ++i) {
      g_chatHistory[i - 1] = g_chatHistory[i];
    }
    snprintf(g_chatHistory[CHAT_MAX_MESSAGES - 1].text, CHAT_MSG_MAX_LEN, "%s", msg);
    g_chatHistory[CHAT_MAX_MESSAGES - 1].isAlert = isAlert;
  }

  g_chatVersion++;
  Serial.printf("[LOG][CHAT] LILY: %s\n", msg);
}

AssistantMood moodFromTemperature(const TempReading& reading) {
  if (!reading.hasLiveSignal) {
    return AssistantMood::NO_SIGNAL;
  }
  if (reading.tempC <= TEMP_THRESHOLD_COLD_MAX) {
    return AssistantMood::COLD;
  }
  if (reading.tempC <= TEMP_THRESHOLD_WARMING_MAX) {
    return AssistantMood::WARMING;
  }
  if (reading.tempC <= TEMP_THRESHOLD_OPTIMAL_MAX) {
    return AssistantMood::OPTIMAL;
  }
  if (reading.tempC <= TEMP_THRESHOLD_WARN_MAX) {
    return AssistantMood::WARM_WARN;
  }
  return AssistantMood::OVERHEAT;
}

const char* moodToTag(AssistantMood mood) {
  switch (mood) {
    case AssistantMood::MENU_IDLE:  return "EN LINEA";
    case AssistantMood::COLD:       return "MOTOR FRIO";
    case AssistantMood::WARMING:    return "CALENTANDO";
    case AssistantMood::OPTIMAL:    return "OPTIMO";
    case AssistantMood::WARM_WARN:  return "TEMP ALTA";
    case AssistantMood::OVERHEAT:   return "PELIGRO!";
    case AssistantMood::NO_SIGNAL:
    default:                        return "SIN SENAL";
  }
}

void formatTrendNote(const TempReading& reading, char* buf, size_t len) {
  if (!reading.hasLiveSignal) {
    if (reading.hasLastValue) {
      snprintf(buf, len, "Última lectura: %d°C", reading.tempC);
    } else {
      snprintf(buf, len, "Esperando ECU 01 05");
    }
    return;
  }

  switch (reading.trend) {
    case TempTrend::TREND_UP:
      snprintf(buf, len, "Subió +%d°C (de %d°C)", reading.deltaC, reading.previousTempC);
      break;
    case TempTrend::TREND_DOWN:
      snprintf(buf, len, "Bajó %d°C (de %d°C)", reading.deltaC, reading.previousTempC);
      break;
    case TempTrend::TREND_STABLE:
      snprintf(buf, len, "Estable en %d°C", reading.tempC);
      break;
    case TempTrend::TREND_NONE:
    default:
      snprintf(buf, len, "Primera lectura: %d°C", reading.tempC);
      break;
  }
}

} // namespace

void assistantChatInit(bool isSimulated) {
  g_chatCount = 0;
  g_chatVersion = 0;
  g_hasInitialized = true;
  g_prevScreen = ScreenId::MAIN_MENU;
  g_prevMood = AssistantMood::MENU_IDLE;
  g_prevLiveSignal = false;
  g_lastAnnouncedTemp = -999;

  pushChatMessage("Hola! Soy Lily v1.0.", false);
  if (isSimulated) {
    pushChatMessage("Modo SIMULACIÓN activo.", false);
  } else {
    pushChatMessage("Lista p/ Vitara 2.7L.", false);
  }
}

AssistantState updateAssistantChat(ScreenId screen, const TempReading& reading) {
  if (!g_hasInitialized) {
    assistantChatInit(reading.isSimulated);
  }

  AssistantState st{};
  formatTrendNote(reading, g_trendBuf, sizeof(g_trendBuf));
  st.trendLine = g_trendBuf;

  const AssistantMood tempMood = moodFromTemperature(reading);
  const bool screenChanged = (screen != g_prevScreen);

  if (screenChanged) {
    if (screen == ScreenId::TEMPERATURE) {
      pushChatMessage("Abriendo Temperatura...", false);
    } else if (screen == ScreenId::CONNECTION_STATUS) {
      pushChatMessage("Revisando enlace ECU.", false);
    } else {
      pushChatMessage("Menú principal listo.", false);
    }
    g_prevScreen = screen;
  }

  // Detectar pérdida o recuperación de señal
  if (!reading.hasLiveSignal && g_prevLiveSignal) {
    char buf[CHAT_MSG_MAX_LEN];
    pushChatMessage("Sin datos del auto!", true);
    if (reading.hasLastValue) {
      snprintf(buf, sizeof(buf), "Última lectura: %d°C", reading.tempC);
      pushChatMessage(buf, true);
    }
    g_prevLiveSignal = false;
    g_prevMood = AssistantMood::NO_SIGNAL;
  } else if (reading.hasLiveSignal) {
    const bool recovered = !g_prevLiveSignal;
    g_prevLiveSignal = true;

    const bool moodChanged = (tempMood != g_prevMood);
    const int16_t stepDelta = (g_lastAnnouncedTemp == -999)
                                  ? 99
                                  : static_cast<int16_t>(reading.tempC - g_lastAnnouncedTemp);
    const bool bigStep = (stepDelta >= 3 || stepDelta <= -3);

    if (recovered || moodChanged || bigStep) {
      char msg[CHAT_MSG_MAX_LEN];
      if (recovered && g_lastAnnouncedTemp != -999) {
        snprintf(msg, sizeof(msg), "Señal OK! Ahora %d°C", reading.tempC);
        pushChatMessage(msg, false);
      } else {
        switch (tempMood) {
          case AssistantMood::COLD:
            if (reading.trend == TempTrend::TREND_UP) {
              snprintf(msg, sizeof(msg), "Subió a %d°C, calent.", reading.tempC);
            } else {
              snprintf(msg, sizeof(msg), "Motor frío: %d°C.", reading.tempC);
            }
            pushChatMessage(msg, false);
            break;

          case AssistantMood::WARMING:
            if (reading.trend == TempTrend::TREND_UP) {
              snprintf(msg, sizeof(msg), "Calentando: %d°C (+)", reading.tempC);
            } else if (reading.trend == TempTrend::TREND_DOWN) {
              snprintf(msg, sizeof(msg), "Bajó a %d°C, atenta.", reading.tempC);
            } else {
              snprintf(msg, sizeof(msg), "Tomando temp: %d°C.", reading.tempC);
            }
            pushChatMessage(msg, false);
            break;

          case AssistantMood::OPTIMAL:
            if (moodChanged) {
              snprintf(msg, sizeof(msg), "%d°C! Temp óptima :)", reading.tempC);
            } else if (reading.trend == TempTrend::TREND_UP) {
              snprintf(msg, sizeof(msg), "Subió a %d°C (ideal).", reading.tempC);
            } else {
              snprintf(msg, sizeof(msg), "Bajó a %d°C (ideal).", reading.tempC);
            }
            pushChatMessage(msg, false);
            break;

          case AssistantMood::WARM_WARN:
            snprintf(msg, sizeof(msg), "Ojo! Temp alta: %d°C", reading.tempC);
            pushChatMessage(msg, true);
            break;

          case AssistantMood::OVERHEAT:
            snprintf(msg, sizeof(msg), "ALERTA! %d°C detente!", reading.tempC);
            pushChatMessage(msg, true);
            break;

          default:
            break;
        }
      }
      g_lastAnnouncedTemp = reading.tempC;
      g_prevMood = tempMood;
    }
  }

  st.mood = (screen == ScreenId::MAIN_MENU && reading.hasLiveSignal)
                ? AssistantMood::MENU_IDLE
                : tempMood;
  st.moodTag = moodToTag(st.mood);
  st.isAlert = (st.mood == AssistantMood::WARM_WARN ||
                st.mood == AssistantMood::OVERHEAT ||
                st.mood == AssistantMood::NO_SIGNAL);
  st.latestLine = (g_chatCount > 0) ? g_chatHistory[g_chatCount - 1].text : "Lista.";
  st.chatVersion = g_chatVersion;
  return st;
}

uint8_t getChatMessageCount() {
  return g_chatCount;
}

const ChatMessage& getChatMessage(uint8_t index) {
  if (index >= g_chatCount) {
    return g_chatHistory[0];
  }
  return g_chatHistory[index];
}
