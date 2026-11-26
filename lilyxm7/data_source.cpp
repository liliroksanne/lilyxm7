#include "data_source.h"
#include "config.h"
#include "obd_protocol.h"
#include <Arduino.h>
#include <string.h>

#if !LILYXM7_SIMULATION_MODE
#include "driver/twai.h"
#endif

namespace {

obd::TempTracker g_tracker(LILYXM7_SIMULATION_MODE != 0, TEMP_TREND_DEADBAND_C);
BusHardwareState g_busState =
    (LILYXM7_SIMULATION_MODE != 0) ? BusHardwareState::SIMULATED : BusHardwareState::INIT_ERROR;

VehicleTelemetry g_telemetry;
uint32_t g_lastPollMs = 0;
uint32_t g_txRequests = 0;
uint32_t g_txErrors = 0;
bool g_lastLoggedLive = false;
int16_t g_lastLoggedTemp = -999;

void logReadingChangesIfNeeded() {
  const TempReading& r = g_tracker.reading();
  if (r.hasLiveSignal && (!g_lastLoggedLive || r.tempC != g_lastLoggedTemp)) {
    Serial.printf("[LOG][DATA] temp=%dC delta=%+dC trend=%u sim=%d ok=%lu\n",
                  r.tempC, r.deltaC, static_cast<unsigned>(r.trend),
                  r.isSimulated ? 1 : 0, static_cast<unsigned long>(r.validCount));
    g_lastLoggedLive = true;
    g_lastLoggedTemp = r.tempC;
  } else if (!r.hasLiveSignal && g_lastLoggedLive) {
    if (r.hasLastValue) {
      Serial.printf("[LOG][DATA] SIN DATOS DEL AUTO | ultima_lectura=%dC\n", r.tempC);
    } else {
      Serial.println(F("[LOG][DATA] SIN DATOS DEL AUTO | sin lecturas previas"));
    }
    g_lastLoggedLive = false;
  }
}

#if LILYXM7_SIMULATION_MODE

int16_t readSimulatedPotTempC() {
  // Promedio de varias lecturas ADC para evitar ruido en el potenciómetro
  constexpr int kSamples = 8;
  uint32_t sum = 0;
  for (int i = 0; i < kSamples; ++i) {
    sum += static_cast<uint32_t>(analogRead(PIN_SIM_POT));
  }
  const int32_t avgAdc = static_cast<int32_t>(sum / kSamples);
  const int32_t mapped = map(avgAdc, 0, 4095, SIM_TEMP_MIN_C, SIM_TEMP_MAX_C);
  if (mapped < SIM_TEMP_MIN_C) return SIM_TEMP_MIN_C;
  if (mapped > SIM_TEMP_MAX_C) return SIM_TEMP_MAX_C;
  return static_cast<int16_t>(mapped);
}

void initSimulationSource() {
  analogReadResolution(12);
  pinMode(PIN_SIM_POT, INPUT);
  pinMode(PIN_SIM_SIGNAL_SW, INPUT_PULLUP);
  g_busState = BusHardwareState::SIMULATED;
  g_tracker.reset(true);
  g_lastPollMs = 0;
  Serial.println(F("[lilyxm7] Modo SIMULACION activo (potenciometro + switch de senal)"));
}

void updateSimulationSource(uint32_t nowMs) {
  // Switch en LOW = ECU simulada responde; HIGH = simula pérdida de señal
  const bool signalActive = (digitalRead(PIN_SIM_SIGNAL_SW) == LOW);

  if (nowMs - g_lastPollMs >= SIM_SAMPLE_INTERVAL_MS || g_lastPollMs == 0) {
    g_lastPollMs = nowMs;
    g_txRequests++;

    if (signalActive) {
      const int16_t simTempC = readSimulatedPotTempC();
      g_tracker.recordValidReading(simTempC, nowMs);
    }
  }

  g_tracker.updateTimeout(nowMs, SIGNAL_TIMEOUT_MS);
}

#else // MODO OBD-II REAL (TWAI + SN65HVD230)

constexpr uint32_t DIAGNOSTIC_POLL_MS = 5000;
constexpr uint32_t DTC_RESPONSE_TIMEOUT_MS = 1000;
constexpr uint32_t DIAGNOSTIC_STALE_MS = 12000;
uint32_t g_speedMs = 0, g_milMs = 0, g_dtcMs = 0, g_diagPollMs = 0;
bool g_dtcPending = false;
uint32_t g_telemetryLogMs = 0;
obd::StoredDtcResponse g_dtcResponse;

bool startTwaiDriver() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      static_cast<gpio_num_t>(PIN_CAN_TX),
      static_cast<gpio_num_t>(PIN_CAN_RX),
      TWAI_MODE_NORMAL);
  g_config.rx_queue_len = 64;
  g_config.tx_queue_len = 8;

  // Suzuki Grand Vitara 2008 V6 2.7 L: ISO 15765-4 CAN 11-bit a 500 kbps
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println(F("[lilyxm7] Error al instalar driver TWAI"));
    return false;
  }

  if (twai_start() != ESP_OK) {
    Serial.println(F("[lilyxm7] Error al iniciar driver TWAI"));
    return false;
  }

  Serial.println(F("[lilyxm7] TWAI iniciado a 500 kbps (OBD-II CAN real)"));
  return true;
}

void initObdRealSource() {
  g_tracker.reset(false);
  g_speedMs = g_milMs = g_dtcMs = g_diagPollMs = g_telemetryLogMs = 0;
  g_dtcPending = false;
  g_dtcResponse.reset();
  g_lastPollMs = 0;
  g_busState = startTwaiDriver() ? BusHardwareState::RUNNING : BusHardwareState::INIT_ERROR;
}

bool transmitObd(const obd::CanFrame& req) {
  twai_message_t txMsg{};
  txMsg.identifier = req.id;
  txMsg.extd = 0;
  txMsg.rtr = 0;
  txMsg.data_length_code = req.dlc;
  memcpy(txMsg.data, req.data, 8);

  g_txRequests++;
  const esp_err_t err = twai_transmit(&txMsg, pdMS_TO_TICKS(25));
  if (err != ESP_OK) {
    g_txErrors++;
    if (g_txErrors == 1 || (g_txErrors % 10) == 0) {
      Serial.printf("[LOG][OBD] Error TX TWAI (err=%d, total_err=%lu)\n",
                    static_cast<int>(err), static_cast<unsigned long>(g_txErrors));
    }
  }
  return err == ESP_OK;
}

void handleTwaiRecovery() {
  twai_status_info_t status{};
  if (twai_get_status_info(&status) != ESP_OK) {
    return;
  }

  if (status.state == TWAI_STATE_BUS_OFF) {
    if (g_busState != BusHardwareState::BUS_OFF_RECOVERING) {
      Serial.println(F("[LOG][OBD] Bus CAN en BUS_OFF, iniciando recuperacion..."));
    }
    g_busState = BusHardwareState::BUS_OFF_RECOVERING;
    twai_initiate_recovery();
  } else if (status.state == TWAI_STATE_STOPPED) {
    if (twai_start() == ESP_OK) {
      g_busState = BusHardwareState::RUNNING;
      Serial.println(F("[LOG][OBD] Bus CAN recuperado y reiniciado"));
    }
  } else if (status.state == TWAI_STATE_RUNNING) {
    g_busState = BusHardwareState::RUNNING;
  }
}

void readTelemetryFrame(const obd::CanFrame& frame, uint32_t nowMs) {
  // La PCM del motor responde en 7E8. Conservamos 7E8–7EF para el PID térmico heredado.
  if (frame.id != obd::CAN_RESPONSE_ID_MIN) return;
  uint8_t value;
  if (obd::parseCurrentByte(frame, obd::PID_VEHICLE_SPEED, value)) {
    const uint32_t elapsed = nowMs - g_speedMs;
    g_telemetry.accelerationValid = g_telemetry.speedKph >= 0 && elapsed >= 250 && elapsed < SIGNAL_TIMEOUT_MS;
    if (g_telemetry.accelerationValid) {
      const int32_t delta = static_cast<int32_t>(value) - g_telemetry.speedKph;
      g_telemetry.accelerationTenths = static_cast<int16_t>(delta * 25000 / (9 * static_cast<int32_t>(elapsed)));
    }
    g_speedMs = nowMs;
    g_telemetry.speedKph = value;
    ++g_telemetry.revision;
  } else if (obd::parseCurrentByte(frame, obd::PID_MONITOR_STATUS, value)) {
    g_milMs = nowMs;
    g_telemetry.mil = (value >> 7) & 1;
    g_telemetry.reportedDtcCount = value & 0x7F;
    ++g_telemetry.revision;
  } else if (g_dtcPending) {
    using Result = obd::StoredDtcResponse::Result;
    const Result result = g_dtcResponse.accept(frame);
    if (result == Result::FLOW_CONTROL) {
      // CAN físico de la PCM, ISO-TP Continue To Send, sin límite de bloque.
      obd::CanFrame flow{};
      flow.id = obd::CAN_RESPONSE_ID_MIN - 8;
      flow.dlc = 8;
      flow.data[0] = 0x30;
      flow.data[2] = 5;  // separación mínima de 5 ms entre tramas consecutivas
      g_dtcPending = transmitObd(flow);
    } else if (result == Result::COMPLETE) {
      g_dtcMs = nowMs;
      g_telemetry.dtcValid = true;
      g_telemetry.storedDtcCount = g_dtcResponse.count();
      Serial.printf("[LOG][DATA] modo03 DTC_confirmados=%u\n", static_cast<unsigned>(g_telemetry.storedDtcCount));
      for (uint8_t i = 0; i < g_telemetry.storedDtcCount && i < MAX_STORED_DTCS; ++i) {
        g_telemetry.dtcs[i] = g_dtcResponse.code(i);
      }
      ++g_telemetry.revision;
      g_dtcPending = false;
    } else if (result == Result::INVALID) {
      g_dtcPending = false;
    }
  }
}

void drainTwaiRx(uint32_t nowMs) {
  twai_message_t rxMsg{};
  while (twai_receive(&rxMsg, 0) == ESP_OK) {
    if (rxMsg.extd || rxMsg.rtr) {
      continue;
    }

    obd::CanFrame frame{};
    frame.id = rxMsg.identifier;
    frame.dlc = rxMsg.data_length_code;
    memcpy(frame.data, rxMsg.data, 8);

    if (frame.id >= obd::CAN_RESPONSE_ID_MIN && frame.id <= obd::CAN_RESPONSE_ID_MAX) {
      Serial.printf("[LOG][OBD] RX ID=0x%03lX DLC=%u DATA=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    static_cast<unsigned long>(frame.id), frame.dlc,
                    frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                    frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
    }

    int16_t tempC = 0;
    if (obd::parseCoolantTempResponse(frame, tempC)) {
      g_tracker.recordValidReading(tempC, nowMs);
    } else {
      readTelemetryFrame(frame, nowMs);
    }
  }
}

void updateObdRealSource(uint32_t nowMs) {
  if (g_busState == BusHardwareState::INIT_ERROR) {
    return;
  }

  if (g_dtcPending && nowMs - g_diagPollMs >= DTC_RESPONSE_TIMEOUT_MS) g_dtcPending = false;
  if (g_telemetry.speedKph >= 0 && nowMs - g_speedMs >= SIGNAL_TIMEOUT_MS) {
    g_telemetry.speedKph = -1;
    g_telemetry.accelerationValid = false;
    ++g_telemetry.revision;
  }
  if (g_telemetry.mil >= 0 && nowMs - g_milMs >= DIAGNOSTIC_STALE_MS) {
    g_telemetry.mil = -1;
    ++g_telemetry.revision;
  }
  if (g_telemetry.dtcValid && nowMs - g_dtcMs >= DIAGNOSTIC_STALE_MS) {
    g_telemetry.dtcValid = false;
    ++g_telemetry.revision;
  }

  handleTwaiRecovery();

  if (g_busState == BusHardwareState::RUNNING) {
    if ((nowMs - g_lastPollMs) >= OBD_POLL_INTERVAL_MS || g_lastPollMs == 0) {
      g_lastPollMs = nowMs;
      transmitObd(obd::buildCoolantTempRequest());
      transmitObd(obd::buildRequest(0x01, obd::PID_VEHICLE_SPEED));
    }
    if (!g_diagPollMs || nowMs - g_diagPollMs >= DIAGNOSTIC_POLL_MS) {
      g_diagPollMs = nowMs;
      g_dtcResponse.reset();
      transmitObd(obd::buildRequest(0x01, obd::PID_MONITOR_STATUS));
      g_dtcPending = transmitObd(obd::buildRequest(0x03));
    }
    drainTwaiRx(nowMs);
    if (g_telemetry.speedKph >= 0 && (!g_telemetryLogMs || nowMs - g_telemetryLogMs >= 1000)) {
      g_telemetryLogMs = nowMs;
      if (g_telemetry.accelerationValid) {
        Serial.printf("[LOG][DATA] velocidad_kmh=%d acel_decimas_ms2=%d\n",
                      g_telemetry.speedKph, g_telemetry.accelerationTenths);
      } else {
        Serial.printf("[LOG][DATA] velocidad_kmh=%d acel_sin_muestra\n", g_telemetry.speedKph);
      }
    }
  }

  g_tracker.updateTimeout(nowMs, SIGNAL_TIMEOUT_MS);
}

#endif // LILYXM7_SIMULATION_MODE

} // namespace

void dataSourceInit() {
  g_telemetry = {};
  g_lastLoggedLive = false;
  g_lastLoggedTemp = -999;
  g_txRequests = 0;
  g_txErrors = 0;
#if LILYXM7_SIMULATION_MODE
  initSimulationSource();
#else
  initObdRealSource();
#endif
}

void dataSourceUpdate() {
  const uint32_t nowMs = millis();
#if LILYXM7_SIMULATION_MODE
  updateSimulationSource(nowMs);
#else
  updateObdRealSource(nowMs);
#endif
  logReadingChangesIfNeeded();
}

TempReading getCoolantTemperature() {
  return g_tracker.reading();
}

ConnectionInfo getConnectionInfo() {
  const TempReading& r = g_tracker.reading();
  ConnectionInfo info{};
  info.isSimulated = r.isSimulated;
  info.ecuResponding = r.hasLiveSignal;
  info.hasLastValue = r.hasLastValue;
  info.lastTempC = r.tempC;
  info.busState = g_busState;
  info.txRequests = g_txRequests;
  info.rxValidFrames = r.validCount;
  info.txErrors = g_txErrors;
  info.lastValidMs = r.lastValidMs;
  info.nowMs = millis();
  return info;
}

bool isSimulationMode() {
  return (LILYXM7_SIMULATION_MODE != 0);
}

const VehicleTelemetry& getVehicleTelemetry() { return g_telemetry; }
