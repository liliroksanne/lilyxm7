#pragma once

#include <stdint.h>
#include "data_types.h"

// ============================================================================
// Asistente virtual ASCII ("Lily") con estética Matrix (verde sobre negro)
// Sus expresiones y mensajes se controlan por código según los datos del auto.
// ============================================================================

constexpr uint8_t ASCII_ART_LINES = 12;

struct AssistantState {
  AssistantMood mood;
  const char* moodTag;       // Etiqueta corta (ej. "NORMAL", "FRIO", "ALERTA")
  const char* line1;         // Mensaje principal de la asistente
  const char* line2;         // Mensaje secundario / tendencia
  bool isAlert;              // true en sobrecalentamiento o pérdida de señal
};

// Determina expresión y mensaje a partir de la pantalla actual y la lectura
AssistantState computeAssistantState(ScreenId screen, const TempReading& reading);

// Devuelve las líneas ASCII de la chica para el estado y fase de parpadeo dados.
// Cada línea mide como máximo 21 caracteres para encajar en el panel izquierdo.
void getAssistantAsciiLines(AssistantMood mood, bool blinkClosed, const char* outLines[ASCII_ART_LINES]);
