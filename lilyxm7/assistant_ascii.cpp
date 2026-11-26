#include "assistant_ascii.h"
#include "config.h"
#include "obd_protocol.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

ChatMessage g_chatHistory[CHAT_MAX_MESSAGES];
uint8_t g_chatCount = 0;
uint32_t g_chatVersion = 0;
uint32_t g_eyeStartedMs = 0;
uint32_t g_eyeDurationMs = 0;
int8_t g_lastMil = -1;
int16_t g_lastStoredCount = -1;
uint16_t g_lastCodes[MAX_STORED_DTCS]{};

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
  g_eyeStartedMs = millis();
  g_eyeDurationMs = isAlert ? 1100 : 450;
  if (PIN_LILY_EYE >= 0) {
    digitalWrite(PIN_LILY_EYE, HIGH);
  }
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
  g_eyeDurationMs = 0;
  g_lastMil = -1;
  g_lastStoredCount = -1;
  if (PIN_LILY_EYE >= 0) {
    pinMode(PIN_LILY_EYE, OUTPUT);
    digitalWrite(PIN_LILY_EYE, LOW);
  }
  g_hasInitialized = true;
  g_prevScreen = ScreenId::MAIN_MENU;
  g_prevMood = AssistantMood::MENU_IDLE;
  g_prevLiveSignal = false;
  g_lastAnnouncedTemp = -999;

  pushChatMessage("Cobro en amperios.", false);
  if (isSimulated) {
    pushChatMessage("Modo SIMULACIÓN activo.", false);
  } else {
    pushChatMessage("Vitara lista. ¿Tú?", false);
  }
}

AssistantState updateAssistantChat(ScreenId screen, const TempReading& reading) {
  if (!g_hasInitialized) {
    assistantChatInit(reading.isSimulated);
  }

  if (g_eyeDurationMs && millis() - g_eyeStartedMs >= g_eyeDurationMs) {
    g_eyeDurationMs = 0;
    if (PIN_LILY_EYE >= 0) {
      digitalWrite(PIN_LILY_EYE, LOW);
    }
  }

  AssistantState st{};
  formatTrendNote(reading, g_trendBuf, sizeof(g_trendBuf));
  st.trendLine = g_trendBuf;

  const AssistantMood tempMood = moodFromTemperature(reading);
  const bool screenChanged = (screen != g_prevScreen);

  if (screenChanged) {
    if (screen == ScreenId::TEMPERATURE) {
      pushChatMessage("¿Quién tiene calor?", false);
    } else if (screen == ScreenId::CONNECTION_STATUS) {
      pushChatMessage("ECU, ¿me copias?", false);
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
        snprintf(msg, sizeof(msg), "Volvió la ECU: %d°C.", reading.tempC);
        pushChatMessage(msg, false);
      } else {
        switch (tempMood) {
          case AssistantMood::COLD:
            if (reading.trend == TempTrend::TREND_UP) {
              snprintf(msg, sizeof(msg), "%d°C. Sin suéter.", reading.tempC);
            } else {
              snprintf(msg, sizeof(msg), "%d°C. Pide cobija.", reading.tempC);
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
              snprintf(msg, sizeof(msg), "%d°C. Ni sopa :)", reading.tempC);
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

void assistantVehicleAlerts(const VehicleTelemetry& telemetry) {
  if (telemetry.mil >= 0 && telemetry.mil != g_lastMil) {
    if (telemetry.mil == 1) pushChatMessage("ALERTA: Check Engine", true);
    else if (g_lastMil == 1) pushChatMessage("Testigo motor apagado.", false);
    g_lastMil = telemetry.mil;
  }
  if (!telemetry.dtcValid) return;
  const uint8_t count = telemetry.storedDtcCount;
  const size_t bytes = (count < MAX_STORED_DTCS ? count : MAX_STORED_DTCS) * sizeof(uint16_t);
  if (count == g_lastStoredCount && memcmp(g_lastCodes, telemetry.dtcs, bytes) == 0) return;

  if (count) {
    char code[6], message[CHAT_MSG_MAX_LEN];
    obd::formatDtc(telemetry.dtcs[0], code);
    snprintf(message, sizeof(message), "ALERTA: %s (%u DTC)", code, static_cast<unsigned>(count));
    pushChatMessage(message, true);
  } else if (g_lastStoredCount > 0) {
    pushChatMessage("ECU sin DTC guardados.", false);
  }
  g_lastStoredCount = count;
  memcpy(g_lastCodes, telemetry.dtcs, bytes);
}
