// ============================================================================
// sim_bridge.cpp — Compila los archivos REALES de lilyxm7/ en una DLL nativa
// para Windows y expone una API C simple que consume sim_web/server.py.
// ============================================================================

#include "Arduino.h"
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "driver/twai.h"
#include "../../lilyxm7/obd_protocol.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

SimSerialClass Serial;
SPIClass SPI;

namespace sim_tft {
uint16_t g_fb[TFT_H][TFT_W] = {{0}};
} // namespace sim_tft

namespace sim_hw {

namespace {
uint32_t g_millis = 0;
int g_pinMode[64] = {0};
int g_digitalVal[64];
int g_analogVal[64] = {0};
bool g_initialized = false;
std::mutex g_serialMtx;
std::string g_serialBuf;

void ensureInit() {
  if (!g_initialized) {
    for (int i = 0; i < 64; ++i) g_digitalVal[i] = HIGH;
    g_initialized = true;
  }
}
} // namespace

void setVirtualMillis(uint32_t ms) { g_millis = ms; }
void advanceVirtualMillis(uint32_t dt) { g_millis += dt; }
uint32_t getVirtualMillis() { return g_millis; }

void setPinMode(int pin, int mode) {
  ensureInit();
  if (pin >= 0 && pin < 64) {
    g_pinMode[pin] = mode;
    if (mode == INPUT_PULLUP) {
      // Mantiene estado si ya fue asignado; por defecto HIGH
    }
  }
}

void setDigitalInput(int pin, int val) {
  ensureInit();
  if (pin >= 0 && pin < 64) {
    g_digitalVal[pin] = val ? HIGH : LOW;
  }
}

int getDigitalPin(int pin) {
  ensureInit();
  if (pin >= 0 && pin < 64) return g_digitalVal[pin];
  return HIGH;
}

void setAnalogInput(int pin, int adcVal) {
  ensureInit();
  if (pin >= 0 && pin < 64) {
    if (adcVal < 0) adcVal = 0;
    if (adcVal > 4095) adcVal = 4095;
    g_analogVal[pin] = adcVal;
  }
}

int getAnalogPin(int pin) {
  ensureInit();
  if (pin >= 0 && pin < 64) return g_analogVal[pin];
  return 0;
}

uint32_t getActiveGpioMask() {
  ensureInit();
  uint32_t mask = 0;
  for (int p = 0; p < 32; ++p) {
    if (g_digitalVal[p] == LOW) {
      mask |= (1u << p);
    }
  }
  return mask;
}

void appendSerial(const char* text) {
  std::lock_guard<std::mutex> lk(g_serialMtx);
  g_serialBuf.append(text);
  if (g_serialBuf.size() > 32768) {
    g_serialBuf.erase(0, g_serialBuf.size() - 32768);
  }
}

int popSerial(char* outBuf, int maxLen) {
  std::lock_guard<std::mutex> lk(g_serialMtx);
  if (!outBuf || maxLen <= 1 || g_serialBuf.empty()) return 0;
  int n = static_cast<int>(g_serialBuf.size());
  if (n > maxLen - 1) n = maxLen - 1;
  std::memcpy(outBuf, g_serialBuf.data(), n);
  outBuf[n] = '\0';
  g_serialBuf.erase(0, n);
  return n;
}

} // namespace sim_hw

namespace sim_can {
namespace {
twai_state_t g_state = TWAI_STATE_STOPPED;
int g_ecuTempC = 86;
bool g_ecuOnline = true;
int g_ecuSpeed = 0;
uint16_t g_ecuCodes[MAX_STORED_DTCS]{};
int g_ecuCodeCount = 0;
std::deque<twai_message_t> g_dtcContinuation;
uint32_t g_recoveryStartedMs = 0;
std::deque<twai_message_t> g_rxQueue;
std::mutex g_canMtx;
std::string g_canLog;

void appendCanLog(const char* line) {
  std::lock_guard<std::mutex> lk(g_canMtx);
  g_canLog.append(line);
  g_canLog.push_back('\n');
  if (g_canLog.size() > 32768) {
    g_canLog.erase(0, g_canLog.size() - 32768);
  }
}
} // namespace

void setEcuTemp(int tempC) { g_ecuTempC = tempC; }
void setEcuOnline(bool online) { g_ecuOnline = online; }
void triggerBusOff() {
  g_state = TWAI_STATE_BUS_OFF;
  appendCanLog("[BUS] Fallo inducido: TWAI_STATE_BUS_OFF");
}

int popCanLog(char* outBuf, int maxLen) {
  std::lock_guard<std::mutex> lk(g_canMtx);
  if (!outBuf || maxLen <= 1 || g_canLog.empty()) return 0;
  int n = static_cast<int>(g_canLog.size());
  if (n > maxLen - 1) n = maxLen - 1;
  std::memcpy(outBuf, g_canLog.data(), n);
  outBuf[n] = '\0';
  g_canLog.erase(0, n);
  return n;
}

} // namespace sim_can

esp_err_t twai_driver_install(const twai_general_config_t*, const twai_timing_config_t*, const twai_filter_config_t*) {
  sim_can::g_state = TWAI_STATE_STOPPED;
  sim_can::g_rxQueue.clear();
  sim_can::g_dtcContinuation.clear();
  sim_can::appendCanLog("[BUS] TWAI instalado a 500 kbps (ISO 15765-4 Vitara 2008)");
  return ESP_OK;
}

esp_err_t twai_start() {
  sim_can::g_state = TWAI_STATE_RUNNING;
  sim_can::appendCanLog("[BUS] TWAI iniciado (RUNNING)");
  return ESP_OK;
}

esp_err_t twai_transmit(const twai_message_t* msg, TickType_t) {
  if (!msg || sim_can::g_state != TWAI_STATE_RUNNING) {
    return ESP_FAIL;
  }

  char txLine[128];
  std::snprintf(txLine, sizeof(txLine), "TX 0x%03X [%u] %02X %02X %02X %02X %02X %02X %02X %02X",
                msg->identifier, msg->data_length_code,
                msg->data[0], msg->data[1], msg->data[2], msg->data[3],
                msg->data[4], msg->data[5], msg->data[6], msg->data[7]);
  sim_can::appendCanLog(txLine);

  if (!sim_can::g_ecuOnline) {
    return ESP_OK; // Sin respuesta de la ECU -> disparará timeout en data_source.cpp
  }

  if (msg->identifier == 0x7E0 && msg->data[0] == 0x30) {
    while (!sim_can::g_dtcContinuation.empty()) {
      sim_can::g_rxQueue.push_back(sim_can::g_dtcContinuation.front());
      sim_can::g_dtcContinuation.pop_front();
    }
    return ESP_OK;
  }
  if (msg->identifier != 0x7DF && msg->identifier != 0x7E0) return ESP_OK;

  twai_message_t answer{};
  answer.identifier = 0x7E8;
  answer.data_length_code = 8;
  if (msg->data[0] == 0x02 && msg->data[1] == 0x01) {
    const uint8_t pid = msg->data[2];
    if (pid == obd::PID_VEHICLE_SPEED || pid == obd::PID_MONITOR_STATUS) {
      answer.data[0] = pid == obd::PID_MONITOR_STATUS ? 6 : 3;
      answer.data[1] = 0x41;
      answer.data[2] = pid;
      answer.data[3] = pid == obd::PID_VEHICLE_SPEED ? sim_can::g_ecuSpeed :
          (sim_can::g_ecuCodeCount ? 0x80 | sim_can::g_ecuCodeCount : 0);
      sim_can::g_rxQueue.push_back(answer);
      return ESP_OK;
    }
  }
  if (msg->data[0] == 0x01 && msg->data[1] == 0x03) {
    uint8_t payload[2 + 2 * MAX_STORED_DTCS] = {0x43, static_cast<uint8_t>(sim_can::g_ecuCodeCount)};
    const int length = 2 + 2 * sim_can::g_ecuCodeCount;
    for (int i = 0; i < sim_can::g_ecuCodeCount; ++i) {
      payload[2 + i * 2] = sim_can::g_ecuCodes[i] >> 8;
      payload[3 + i * 2] = sim_can::g_ecuCodes[i] & 255;
    }
    sim_can::g_dtcContinuation.clear();
    if (length <= 7) {
      answer.data[0] = length;
      std::memcpy(answer.data + 1, payload, length);
    } else {
      answer.data[0] = 0x10 | (length >> 8);
      answer.data[1] = length & 255;
      std::memcpy(answer.data + 2, payload, 6);
      for (int pos = 6, sequence = 1; pos < length; pos += 7, ++sequence) {
        twai_message_t next{};
        next.identifier = answer.identifier;
        next.data_length_code = 8;
        next.data[0] = 0x20 | (sequence & 15);
        std::memcpy(next.data + 1, payload + pos, length - pos < 7 ? length - pos : 7);
        sim_can::g_dtcContinuation.push_back(next);
      }
    }
    sim_can::g_rxQueue.push_back(answer);
    return ESP_OK;
  }

  // Respuesta virtual de ECU Suzuki Grand Vitara para Servicio 01, PID 05
  if ((msg->identifier == 0x7DF || msg->identifier == 0x7E0) &&
      msg->data_length_code >= 3 && msg->data[1] == 0x01 && msg->data[2] == 0x05) {
    int clamped = sim_can::g_ecuTempC;
    if (clamped < -40) clamped = -40;
    if (clamped > 215) clamped = 215;
    uint8_t byteA = static_cast<uint8_t>(clamped + 40);

    twai_message_t resp{};
    resp.identifier = 0x7E8;
    resp.extd = 0;
    resp.rtr = 0;
    resp.data_length_code = 8;
    resp.data[0] = 0x03;
    resp.data[1] = 0x41;
    resp.data[2] = 0x05;
    resp.data[3] = byteA;
    sim_can::g_rxQueue.push_back(resp);

    char rxLine[128];
    std::snprintf(rxLine, sizeof(rxLine), "RX 0x7E8 [8] 03 41 05 %02X 00 00 00 00  (A=%u -> %d °C)",
                  byteA, byteA, clamped);
    sim_can::appendCanLog(rxLine);
  }
  return ESP_OK;
}

esp_err_t twai_receive(twai_message_t* msg, TickType_t) {
  if (!msg || sim_can::g_rxQueue.empty()) return ESP_ERR_TIMEOUT;
  *msg = sim_can::g_rxQueue.front();
  sim_can::g_rxQueue.pop_front();
  return ESP_OK;
}

esp_err_t twai_get_status_info(twai_status_info_t* status) {
  if (!status) return ESP_FAIL;
  // A fixed virtual idle interval models completion without depending on the polling rate.
  if (sim_can::g_state == TWAI_STATE_RECOVERING &&
      millis() - sim_can::g_recoveryStartedMs >= 600) {
    sim_can::g_state = TWAI_STATE_STOPPED;
  }
  status->state = sim_can::g_state;
  return ESP_OK;
}

esp_err_t twai_initiate_recovery() {
  sim_can::g_state = TWAI_STATE_RECOVERING;
  sim_can::g_recoveryStartedMs = millis();
  sim_can::appendCanLog("[BUS] Iniciando recuperacion de BUS_OFF...");
  return ESP_OK;
}

// ============================================================================
// Inclusión directa del firmware REAL de lilyxm7/
// ============================================================================
#include "../../lilyxm7/config.h"
#include "../../lilyxm7/data_types.h"
#include "../../lilyxm7/obd_protocol.h"
#include "../../lilyxm7/buttons.h"
#include "../../lilyxm7/data_source.h"
#include "../../lilyxm7/assistant_ascii.h"
#include "../../lilyxm7/ui_tft.h"

#include "../../lilyxm7/buttons.cpp"
#include "../../lilyxm7/data_source.cpp"
#include "../../lilyxm7/assistant_ascii.cpp"
#include "../../lilyxm7/ui_tft.cpp"
#include "../../lilyxm7/lilyxm7.ino"

// ============================================================================
// API C exportada para sim_web/server.py
// ============================================================================
#ifdef _WIN32
#define SIM_EXPORT extern "C" __declspec(dllexport)
#else
#define SIM_EXPORT extern "C"
#endif

SIM_EXPORT void sim_init() {
  sim_hw::setVirtualMillis(0);
  // Por defecto: botones sueltos (HIGH), señal activa (LOW), potenciómetro en ~86 °C
  sim_hw::setDigitalInput(PIN_BTN_UP, HIGH);
  sim_hw::setDigitalInput(PIN_BTN_DOWN, HIGH);
  sim_hw::setDigitalInput(PIN_BTN_A, HIGH);
  sim_hw::setDigitalInput(PIN_BTN_B, HIGH);
  sim_hw::setDigitalInput(PIN_SIM_SIGNAL_SW, LOW);
  sim_hw::setAnalogInput(PIN_SIM_POT, static_cast<int>(map(86, SIM_TEMP_MIN_C, SIM_TEMP_MAX_C, 0, 4095)));
  sim_can::setEcuTemp(86);
  sim_can::setEcuOnline(true);
  setup();
}

SIM_EXPORT void sim_tick(uint32_t dt_ms) {
  sim_hw::advanceVirtualMillis(dt_ms);
  loop();
}

SIM_EXPORT void sim_set_button(int pin, int pressed) {
  sim_hw::setDigitalInput(pin, pressed ? LOW : HIGH);
}

SIM_EXPORT void sim_set_temp_c(int temp_c) {
  const int ecu_temp_c = temp_c;
  if (temp_c < SIM_TEMP_MIN_C) temp_c = SIM_TEMP_MIN_C;
  if (temp_c > SIM_TEMP_MAX_C) temp_c = SIM_TEMP_MAX_C;
  // Round up to the first ADC sample the firmware decodes as this integer.
  constexpr int span = SIM_TEMP_MAX_C - SIM_TEMP_MIN_C;
  int adc = ((temp_c - SIM_TEMP_MIN_C) * 4095 + span - 1) / span;
  sim_hw::setAnalogInput(PIN_SIM_POT, adc);
  sim_can::setEcuTemp(ecu_temp_c);
}

SIM_EXPORT void sim_set_signal(int enabled) {
  sim_hw::setDigitalInput(PIN_SIM_SIGNAL_SW, enabled ? LOW : HIGH);
  sim_can::setEcuOnline(enabled != 0);
}

SIM_EXPORT void sim_trigger_bus_off() {
  sim_can::triggerBusOff();
}

SIM_EXPORT int sim_get_framebuffer_rgb(uint8_t* out_rgb, int max_bytes) {
  constexpr int needed = sim_tft::TFT_W * sim_tft::TFT_H * 3;
  if (!out_rgb || max_bytes < needed) return 0;
  int idx = 0;
  for (int y = 0; y < sim_tft::TFT_H; ++y) {
    for (int x = 0; x < sim_tft::TFT_W; ++x) {
      const uint16_t p = sim_tft::g_fb[y][x];
      uint8_t r = static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
      uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x3F) * 255 / 63);
      uint8_t b = static_cast<uint8_t>((p & 0x1F) * 255 / 31);
      out_rgb[idx++] = r;
      out_rgb[idx++] = g;
      out_rgb[idx++] = b;
    }
  }
  return needed;
}

SIM_EXPORT int sim_pop_serial(char* out_buf, int max_len) {
  return sim_hw::popSerial(out_buf, max_len);
}

SIM_EXPORT int sim_pop_can_log(char* out_buf, int max_len) {
  return sim_can::popCanLog(out_buf, max_len);
}

SIM_EXPORT uint32_t sim_get_gpio_mask() {
  return sim_hw::getActiveGpioMask();
}

SIM_EXPORT int sim_get_eye() {
  return digitalRead(PIN_LILY_EYE) == HIGH;
}

SIM_EXPORT void sim_set_ecu_telemetry(int speed, const uint16_t* codes, int count) {
  sim_can::g_ecuSpeed = speed;
  sim_can::g_ecuCodeCount = count < MAX_STORED_DTCS ? count : MAX_STORED_DTCS;
  for (int i = 0; i < sim_can::g_ecuCodeCount; ++i) sim_can::g_ecuCodes[i] = codes[i];
}

SIM_EXPORT int sim_read_vehicle_telemetry(int* out, int capacity) {
  constexpr int fields = 6;
  if (!out || capacity < fields + MAX_STORED_DTCS) return 0;
  const VehicleTelemetry& readings = getVehicleTelemetry();
  out[0] = readings.speedKph;
  out[1] = readings.accelerationValid ? readings.accelerationTenths : -32768;
  out[2] = readings.mil;
  out[3] = readings.mil < 0 ? -1 : readings.reportedDtcCount;
  out[4] = readings.dtcValid ? readings.storedDtcCount : -1;
  out[5] = static_cast<int>(getConnectionInfo().busState);
  const int returned = out[4] < 0 ? 0 : out[4] < MAX_STORED_DTCS ? out[4] : MAX_STORED_DTCS;
  for (int i = 0; i < returned; ++i) out[fields + i] = readings.dtcs[i];
  return fields + returned;
}

SIM_EXPORT void sim_get_board_snapshot(uint32_t* out) {
  out[0] = millis();
  out[1] = static_cast<uint32_t>(g_currentScreen);
  out[2] = getCoolantTemperature().hasLiveSignal;
}
