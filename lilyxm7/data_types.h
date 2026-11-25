#pragma once

#include <stdint.h>

// ============================================================================
// Tipos de datos compartidos entre adquisición, lógica, asistente y pantalla
// ============================================================================

enum class ScreenId : uint8_t {
  MAIN_MENU = 0,
  TEMPERATURE = 1,
  CONNECTION_STATUS = 2
};

enum class MenuOption : uint8_t {
  TEMPERATURE = 0,
  CONNECTION_STATUS = 1,
  COUNT = 2
};

enum class TempTrend : uint8_t {
  TREND_NONE = 0,    // Aún no hay lectura previa para comparar
  TREND_STABLE = 1,  // Igual que la lectura anterior
  TREND_UP = 2,      // Subió respecto a la lectura anterior
  TREND_DOWN = 3     // Bajó respecto a la lectura anterior
};

enum class AssistantMood : uint8_t {
  MENU_IDLE = 0,
  COLD = 1,
  WARMING = 2,
  OPTIMAL = 3,
  WARM_WARN = 4,
  OVERHEAT = 5,
  NO_SIGNAL = 6
};

struct TempReading {
  bool hasLiveSignal;       // true si llegan respuestas vigentes
  bool hasLastValue;        // true si existe al menos una lectura válida guardada
  int16_t tempC;            // Lectura vigente o última lectura conservada (°C)
  int16_t previousTempC;    // Lectura anterior válida (°C)
  int16_t deltaC;           // Diferencia respecto a la lectura anterior (°C)
  TempTrend trend;          // Tendencia respecto a la lectura anterior
  uint32_t lastValidMs;     // millis() de la última respuesta válida
  uint32_t validCount;      // Cantidad acumulada de respuestas válidas
  bool isSimulated;         // true si los datos provienen de la simulación
};

enum class BusHardwareState : uint8_t {
  SIMULATED = 0,
  RUNNING = 1,
  INIT_ERROR = 2,
  BUS_OFF_RECOVERING = 3
};

struct ConnectionInfo {
  bool isSimulated;
  bool ecuResponding;
  bool hasLastValue;
  int16_t lastTempC;
  BusHardwareState busState;
  uint32_t txRequests;
  uint32_t rxValidFrames;
  uint32_t txErrors;
  uint32_t lastValidMs;
  uint32_t nowMs;
};
