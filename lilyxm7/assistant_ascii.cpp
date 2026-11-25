#include "assistant_ascii.h"
#include "config.h"
#include <stdio.h>

namespace {

char g_dynamicLine2[48];

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

void formatTrendNote(const TempReading& reading, char* buf, size_t len) {
  if (!reading.hasLiveSignal) {
    if (reading.hasLastValue) {
      snprintf(buf, len, "Conservando última: %d°C", reading.tempC);
    } else {
      snprintf(buf, len, "Revisa ignición o cable OBD");
    }
    return;
  }

  switch (reading.trend) {
    case TempTrend::TREND_UP:
      snprintf(buf, len, "Subió +%d°C desde %d°C", reading.deltaC, reading.previousTempC);
      break;
    case TempTrend::TREND_DOWN:
      snprintf(buf, len, "Bajó %d°C desde %d°C", reading.deltaC, reading.previousTempC);
      break;
    case TempTrend::TREND_STABLE:
      snprintf(buf, len, "Estable respecto a %d°C", reading.previousTempC);
      break;
    case TempTrend::TREND_NONE:
    default:
      snprintf(buf, len, "Primera lectura válida");
      break;
  }
}

} // namespace

AssistantState computeAssistantState(ScreenId screen, const TempReading& reading) {
  AssistantState st{};

  if (screen == ScreenId::MAIN_MENU) {
    st.mood = reading.hasLiveSignal ? AssistantMood::MENU_IDLE : AssistantMood::NO_SIGNAL;
    st.moodTag = reading.hasLiveSignal ? "EN LINEA" : "EN ESPERA";
    st.line1 = reading.isSimulated
                   ? "Modo simulación listo."
                   : "Lista para la Vitara.";
    st.line2 = "Usa ↑/↓ y A para entrar.";
    st.isAlert = false;
    return st;
  }

  if (screen == ScreenId::CONNECTION_STATUS) {
    if (reading.hasLiveSignal) {
      st.mood = AssistantMood::OPTIMAL;
      st.moodTag = "ENLACE OK";
      st.line1 = reading.isSimulated
                     ? "Enlace virtual activo."
                     : "ECU respondiendo 01 05.";
      st.line2 = "Pulsa B para volver.";
      st.isAlert = false;
    } else {
      st.mood = AssistantMood::NO_SIGNAL;
      st.moodTag = "SIN SENAL";
      st.line1 = "Sin datos del auto.";
      st.line2 = reading.hasLastValue
                     ? "Última lectura guardada."
                     : "Esperando respuesta ECU.";
      st.isAlert = true;
    }
    return st;
  }

  // Pantalla principal de Temperatura en vivo
  st.mood = moodFromTemperature(reading);
  formatTrendNote(reading, g_dynamicLine2, sizeof(g_dynamicLine2));
  st.line2 = g_dynamicLine2;

  switch (st.mood) {
    case AssistantMood::COLD:
      st.moodTag = "MOTOR FRIO";
      st.isAlert = false;
      if (reading.trend == TempTrend::TREND_UP) {
        st.line1 = "Subiendo, buen arranque.";
      } else if (reading.trend == TempTrend::TREND_DOWN) {
        st.line1 = "Bajó, motor aún frío.";
      } else {
        st.line1 = "Motor frío, acelera suave.";
      }
      break;

    case AssistantMood::WARMING:
      st.moodTag = "CALENTANDO";
      st.isAlert = false;
      if (reading.trend == TempTrend::TREND_UP) {
        st.line1 = "Subiendo a punto ideal.";
      } else if (reading.trend == TempTrend::TREND_DOWN) {
        st.line1 = "Bajó un poco, atenta.";
      } else {
        st.line1 = "Tomando temperatura.";
      }
      break;

    case AssistantMood::OPTIMAL:
      st.moodTag = "OPTIMO";
      st.isAlert = false;
      if (reading.trend == TempTrend::TREND_UP) {
        st.line1 = "Subió, pero sigue ideal.";
      } else if (reading.trend == TempTrend::TREND_DOWN) {
        st.line1 = "Bajó, todo bajo control.";
      } else {
        st.line1 = "Vitara en temp óptima.";
      }
      break;

    case AssistantMood::WARM_WARN:
      st.moodTag = "TEMP ALTA";
      st.isAlert = true;
      if (reading.trend == TempTrend::TREND_UP) {
        st.line1 = "¡Subió! Vigila el motor.";
      } else if (reading.trend == TempTrend::TREND_DOWN) {
        st.line1 = "Está bajando, bien.";
      } else {
        st.line1 = "Temperatura alta, alerta.";
      }
      break;

    case AssistantMood::OVERHEAT:
      st.moodTag = "PELIGRO!";
      st.isAlert = true;
      if (reading.trend == TempTrend::TREND_UP) {
        st.line1 = "¡ALERTA! Sigue subiendo.";
      } else if (reading.trend == TempTrend::TREND_DOWN) {
        st.line1 = "Aún crítica, detente.";
      } else {
        st.line1 = "¡PELIGRO! Sobrecalent.";
      }
      break;

    case AssistantMood::NO_SIGNAL:
    default:
      st.moodTag = "SIN SENAL";
      st.isAlert = true;
      st.line1 = reading.hasLastValue
                     ? "Sin datos. Guarde última."
                     : "Sin datos del auto...";
      break;
  }

  return st;
}

void getAssistantAsciiLines(AssistantMood mood, bool blinkClosed, const char* outLines[ASCII_ART_LINES]) {
  // Base común de cabello y silueta cyberpunk Matrix (21 caracteres de ancho máx.)
  outLines[0]  = "   .---=======---.   ";
  outLines[1]  = "  / .-/|||||||\\-. \\  ";
  outLines[2]  = " | / /  _   _  \\ \\ | ";

  // Líneas 3..6 cambian según la expresión controlada por código
  switch (mood) {
    case AssistantMood::MENU_IDLE:
      outLines[3] = blinkClosed ? " || |  -     -  | || " : " || |  ^     ^  | || ";
      outLines[4] = " || |     .     | || ";
      outLines[5] = " | \\ \\   \\_/   / / | ";
      outLines[6] = " |  \\ '-------' /  | ";
      break;

    case AssistantMood::COLD:
      outLines[3] = blinkClosed ? " || |  -     -  | || " : " || |  >     <  | || ";
      outLines[4] = " || |     .     | || ";
      outLines[5] = " | \\ \\   ~~~   / / | ";
      outLines[6] = " |  \\ '-------' /  | ";
      break;

    case AssistantMood::WARMING:
      outLines[3] = blinkClosed ? " || |  -     -  | || " : " || |  o     o  | || ";
      outLines[4] = " || |     .     | || ";
      outLines[5] = " | \\ \\    v    / / | ";
      outLines[6] = " |  \\ '-------' /  | ";
      break;

    case AssistantMood::OPTIMAL:
      outLines[3] = blinkClosed ? " || |  -     -  | || " : " || |  ^     ^  | || ";
      outLines[4] = " || |   * . *   | || ";
      outLines[5] = " | \\ \\  \\___/  / / | ";
      outLines[6] = " |  \\ '-------' /  | ";
      break;

    case AssistantMood::WARM_WARN:
      outLines[3] = blinkClosed ? " || |  -    -'  | || " : " || |  O    O'  | || ";
      outLines[4] = " || |     .     | || ";
      outLines[5] = " | \\ \\    o    / / | ";
      outLines[6] = " |  \\ '-------' /  | ";
      break;

    case AssistantMood::OVERHEAT:
      outLines[0] = " ! .---=======---. ! ";
      outLines[3] = blinkClosed ? " || |  >     <  | || " : " || |  X     X  | || ";
      outLines[4] = " || |     !     | || ";
      outLines[5] = " | \\ \\   ===   / / | ";
      outLines[6] = " !  \\ '-------' /  ! ";
      break;

    case AssistantMood::NO_SIGNAL:
    default:
      outLines[3] = blinkClosed ? " || |  -     -  | || " : " || |  ?     ?  | || ";
      outLines[4] = " || |     .     | || ";
      outLines[5] = " | \\ \\   ___   / / | ";
      outLines[6] = " |  \\ '-------' /  | ";
      break;
  }

  // Hombros / collar Matrix
  outLines[7]  = " |   '.__   __.'   | ";
  outLines[8]  = " |  .--/ | | \\--.  | ";
  outLines[9]  = " \\_/  | [010] |  \\_/ ";
  outLines[10] = "      |  ___  |      ";
  outLines[11] = "     /  /   \\  \\     ";
}
