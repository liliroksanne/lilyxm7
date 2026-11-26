#pragma once

#include <stdint.h>
#include "data_types.h"

// ============================================================================
// Chatbot asistente ("Lily") en estética Matrix (verde sobre negro)
// En lugar de dibujo ASCII, mantiene un historial de mensajes breves tipo chat
// que se agregan automáticamente cuando cambian los datos del auto o la pantalla.
// ============================================================================

constexpr uint8_t CHAT_MAX_MESSAGES = 6;
constexpr uint8_t CHAT_MSG_MAX_LEN  = 34;

struct ChatMessage {
  char text[CHAT_MSG_MAX_LEN];
  bool isAlert;
};

struct AssistantState {
  AssistantMood mood;
  const char* moodTag;       // Etiqueta corta (ej. "OPTIMO", "FRIO", "ALERTA")
  const char* latestLine;    // Último mensaje breve
  const char* trendLine;     // Línea de detalle/tendencia
  bool isAlert;
  uint32_t chatVersion;      // Se incrementa cada vez que entra un mensaje nuevo al chat
};

// Reinicia el historial del chatbot con mensaje de bienvenida
void assistantChatInit(bool isSimulated);

// Actualiza el chatbot con la pantalla y lectura actuales (agrega mensaje solo si hay novedad)
AssistantState updateAssistantChat(ScreenId screen, const TempReading& reading);

// Consulta el historial actual (0 = más antiguo visible, count-1 = más reciente)
uint8_t getChatMessageCount();
const ChatMessage& getChatMessage(uint8_t index);

void assistantVehicleAlerts(const VehicleTelemetry& telemetry);
