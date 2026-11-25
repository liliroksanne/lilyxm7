#pragma once

#include <stddef.h>
#include <stdint.h>
#include "data_types.h"

// ============================================================================
// Lógica pura de protocolo OBD-II sobre CAN (ISO 15765-4, 11-bit, 500 kbps)
// Sin dependencias de Arduino para poder reutilizar y probar en Linux y Windows.
// ============================================================================

namespace obd {

constexpr uint32_t CAN_REQUEST_ID_FUNCTIONAL = 0x7DF;
constexpr uint32_t CAN_RESPONSE_ID_MIN       = 0x7E8;
constexpr uint32_t CAN_RESPONSE_ID_MAX       = 0x7EF;

constexpr uint8_t SERVICE_SHOW_CURRENT_DATA  = 0x01;
constexpr uint8_t SERVICE_CURRENT_DATA_RESP  = 0x41; // 0x01 + 0x40
constexpr uint8_t PID_ENGINE_COOLANT_TEMP    = 0x05;

constexpr int16_t COOLANT_TEMP_MIN_C         = -40;
constexpr int16_t COOLANT_TEMP_MAX_C         = 215;

struct CanFrame {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
};

// Construye la trama OBD-II Servicio 01, PID 05 (temperatura del refrigerante)
inline CanFrame buildCoolantTempRequest() {
  CanFrame frame{};
  frame.id = CAN_REQUEST_ID_FUNCTIONAL;
  frame.dlc = 8;
  frame.data[0] = 0x02;                      // 2 bytes útiles (Servicio + PID)
  frame.data[1] = SERVICE_SHOW_CURRENT_DATA; // Servicio 01
  frame.data[2] = PID_ENGINE_COOLANT_TEMP;   // PID 05
  frame.data[3] = 0x00;                      // Relleno estándar
  frame.data[4] = 0x00;
  frame.data[5] = 0x00;
  frame.data[6] = 0x00;
  frame.data[7] = 0x00;
  return frame;
}

// Convierte °C (-40..215) al byte A de OBD-II (A = °C + 40)
inline uint8_t encodeCoolantTempByte(int16_t tempC) {
  if (tempC < COOLANT_TEMP_MIN_C) {
    tempC = COOLANT_TEMP_MIN_C;
  } else if (tempC > COOLANT_TEMP_MAX_C) {
    tempC = COOLANT_TEMP_MAX_C;
  }
  return static_cast<uint8_t>(tempC + 40);
}

// Decodifica una respuesta OBD-II de temperatura del refrigerante (Servicio 01, PID 05)
// Fórmula estándar SAE J1979: Temperatura (°C) = A - 40
inline bool parseCoolantTempResponse(const CanFrame& frame, int16_t& outTempC) {
  if (frame.id < CAN_RESPONSE_ID_MIN || frame.id > CAN_RESPONSE_ID_MAX) {
    return false;
  }
  if (frame.dlc < 4) {
    return false;
  }

  // ISO-TP Single Frame: nibble alto = 0x0, nibble bajo = longitud útil (>= 3 bytes: 41 05 A)
  const uint8_t pciType = (frame.data[0] & 0xF0) >> 4;
  const uint8_t payloadLen = frame.data[0] & 0x0F;
  if (pciType != 0x0 || payloadLen < 3 || payloadLen > 7) {
    return false;
  }

  if (frame.data[1] != SERVICE_CURRENT_DATA_RESP) {
    return false;
  }
  if (frame.data[2] != PID_ENGINE_COOLANT_TEMP) {
    return false;
  }

  outTempC = static_cast<int16_t>(frame.data[3]) - 40;
  return true;
}

// ============================================================================
// TempTracker: gestiona tendencia (subió/bajó), pérdida de señal y última lectura
// ============================================================================
class TempTracker {
public:
  explicit TempTracker(bool simulated = false, int16_t deadbandC = 1)
      : reading_{}, deadbandC_(deadbandC) {
    reset(simulated);
  }

  void reset(bool simulated) {
    reading_.hasLiveSignal = false;
    reading_.hasLastValue = false;
    reading_.tempC = 0;
    reading_.previousTempC = 0;
    reading_.deltaC = 0;
    reading_.trend = TempTrend::TREND_NONE;
    reading_.lastValidMs = 0;
    reading_.validCount = 0;
    reading_.isSimulated = simulated;
  }

  void recordValidReading(int16_t newTempC, uint32_t nowMs) {
    if (!reading_.hasLastValue) {
      reading_.previousTempC = newTempC;
      reading_.tempC = newTempC;
      reading_.deltaC = 0;
      reading_.trend = TempTrend::TREND_NONE;
      reading_.hasLastValue = true;
    } else {
      const int16_t diff = static_cast<int16_t>(newTempC - reading_.tempC);
      reading_.previousTempC = reading_.tempC;
      reading_.tempC = newTempC;
      reading_.deltaC = diff;

      if (diff >= deadbandC_) {
        reading_.trend = TempTrend::TREND_UP;
      } else if (diff <= -deadbandC_) {
        reading_.trend = TempTrend::TREND_DOWN;
      } else {
        reading_.trend = TempTrend::TREND_STABLE;
      }
    }

    reading_.hasLiveSignal = true;
    reading_.lastValidMs = nowMs;
    reading_.validCount++;
  }

  void updateTimeout(uint32_t nowMs, uint32_t timeoutMs) {
    if (!reading_.hasLiveSignal) {
      return;
    }
    if ((nowMs - reading_.lastValidMs) >= timeoutMs) {
      reading_.hasLiveSignal = false;
    }
  }

  void markSignalLost() {
    reading_.hasLiveSignal = false;
  }

  const TempReading& reading() const {
    return reading_;
  }

private:
  TempReading reading_;
  int16_t deadbandC_;
};

} // namespace obd
