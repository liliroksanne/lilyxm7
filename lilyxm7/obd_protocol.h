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
constexpr uint8_t PID_VEHICLE_SPEED          = 0x0D;
constexpr uint8_t PID_MONITOR_STATUS         = 0x01;

constexpr int16_t COOLANT_TEMP_MIN_C         = -40;
constexpr int16_t COOLANT_TEMP_MAX_C         = 215;

struct CanFrame {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
};

inline CanFrame buildRequest(uint8_t service, uint8_t pid = 0) {
  CanFrame frame{};
  frame.id = CAN_REQUEST_ID_FUNCTIONAL;
  frame.dlc = 8;
  frame.data[0] = service == SERVICE_SHOW_CURRENT_DATA ? 2 : 1;
  frame.data[1] = service;
  frame.data[2] = pid;
  return frame;
}

inline CanFrame buildCoolantTempRequest() {
  return buildRequest(SERVICE_SHOW_CURRENT_DATA, PID_ENGINE_COOLANT_TEMP);
}

inline uint8_t singleFrameLength(const CanFrame& frame) {
  const uint8_t length = frame.data[0];
  return frame.dlc <= 8 && length >= 1 && length <= 7 && length < frame.dlc ? length : 0;
}

inline bool parseCurrentByte(const CanFrame& frame, uint8_t pid, uint8_t& value) {
  if (frame.id < CAN_RESPONSE_ID_MIN || frame.id > CAN_RESPONSE_ID_MAX ||
      singleFrameLength(frame) < (pid == PID_MONITOR_STATUS ? 6 : 3) ||
      frame.data[1] != SERVICE_CURRENT_DATA_RESP || frame.data[2] != pid) {
    return false;
  }
  value = frame.data[3];
  return true;
}

inline void formatDtc(uint16_t value, char (&text)[6]) {
  constexpr char hex[] = "0123456789ABCDEF";
  text[0] = "PCBU"[value >> 14];
  text[1] = hex[(value >> 12) & 3];
  text[2] = hex[(value >> 8) & 15];
  text[3] = hex[(value >> 4) & 15];
  text[4] = hex[value & 15];
  text[5] = '\0';
}

class StoredDtcResponse {
public:
  enum class Result { IGNORE, FLOW_CONTROL, WAIT, COMPLETE, INVALID };

  void reset() { expected_ = 0; used_ = 0; seq_ = 1; }

  Result accept(const CanFrame& frame) {
    if (frame.id != CAN_RESPONSE_ID_MIN || frame.dlc < 2 || frame.dlc > 8) return Result::IGNORE;
    const uint8_t type = frame.data[0] >> 4;
    if (type == 0 && frame.data[1] == 0x43) {
      expected_ = singleFrameLength(frame);
      if (expected_ < 2) return Result::INVALID;
      used_ = 0;
      append(frame.data + 1, expected_);
    } else if (type == 1 && frame.dlc == 8 && frame.data[2] == 0x43) {
      expected_ = ((frame.data[0] & 15) << 8) | frame.data[1];
      if (expected_ <= 7 || expected_ > sizeof(payload_)) return Result::INVALID;
      used_ = 0;
      seq_ = 1;
      append(frame.data + 2, 6);
      return Result::FLOW_CONTROL;
    } else if (type == 2 && used_ && used_ < expected_) {
      if ((frame.data[0] & 15) != seq_) return Result::INVALID;
      seq_ = (seq_ + 1) & 15;
      append(frame.data + 1, frame.dlc - 1);
    } else {
      return Result::IGNORE;
    }
    if (used_ < expected_) return Result::WAIT;
    return expected_ == 2 + 2 * static_cast<size_t>(payload_[1]) ? Result::COMPLETE : Result::INVALID;
  }

  uint8_t count() const { return payload_[1]; }
  uint16_t code(size_t index) const {
    return (static_cast<uint16_t>(payload_[2 + index * 2]) << 8) | payload_[3 + index * 2];
  }

private:
  void append(const uint8_t* bytes, size_t length) {
    for (size_t i = 0; i < length && used_ < expected_; ++i) payload_[used_++] = bytes[i];
  }
  uint8_t payload_[512]{};
  size_t expected_ = 0;
  size_t used_ = 0;
  uint8_t seq_ = 1;
};

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
  uint8_t value;
  if (!parseCurrentByte(frame, PID_ENGINE_COOLANT_TEMP, value)) return false;
  outTempC = static_cast<int16_t>(value) - 40;
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
