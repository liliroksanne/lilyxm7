// ============================================================================
// Pruebas unitarias en C++ puro para obd_protocol.h y TempTracker
// Compilar: g++ -std=c++17 -Wall -Wextra -o test_obd_protocol tests/test_obd_protocol.cpp
// ============================================================================

#include "../lilyxm7/obd_protocol.h"
#include <cassert>
#include <cstdio>

static void testBuildRequest() {
  const obd::CanFrame req = obd::buildCoolantTempRequest();
  assert(req.id == 0x7DF);
  assert(req.dlc == 8);
  assert(req.data[0] == 0x02);
  assert(req.data[1] == 0x01);
  assert(req.data[2] == 0x05);
}

static void testParseValidResponses() {
  struct Case {
    uint32_t id;
    uint8_t byteA;
    int16_t expectedC;
  };

  const Case cases[] = {
    { 0x7E8, 0x00, -40 },
    { 0x7E8, 0x28,   0 },
    { 0x7E8, 0x5A,  50 },
    { 0x7E9, 0x82,  90 },
    { 0x7EF, 0x96, 110 },
    { 0x7E8, 0xFF, 215 }
  };

  for (const auto& tc : cases) {
    obd::CanFrame resp{};
    resp.id = tc.id;
    resp.dlc = 8;
    resp.data[0] = 0x03;
    resp.data[1] = 0x41;
    resp.data[2] = 0x05;
    resp.data[3] = tc.byteA;

    int16_t outTemp = -999;
    assert(obd::parseCoolantTempResponse(resp, outTemp));
    assert(outTemp == tc.expectedC);
    assert(obd::encodeCoolantTempByte(tc.expectedC) == tc.byteA);
  }
}

static void testRejectInvalidResponses() {
  int16_t outTemp = 0;

  // ID fuera del rango ECU (0x7E8..0x7EF)
  obd::CanFrame badId{ 0x7DF, 8, {0x03, 0x41, 0x05, 0x82, 0, 0, 0, 0} };
  assert(!obd::parseCoolantTempResponse(badId, outTemp));

  // Servicio incorrecto (0x42 en lugar de 0x41)
  obd::CanFrame badSvc{ 0x7E8, 8, {0x03, 0x42, 0x05, 0x82, 0, 0, 0, 0} };
  assert(!obd::parseCoolantTempResponse(badSvc, outTemp));

  // PID incorrecto (0x0C RPM en lugar de 0x05 refrigerante)
  obd::CanFrame badPid{ 0x7E8, 8, {0x04, 0x41, 0x0C, 0x1A, 0xF8, 0, 0, 0} };
  assert(!obd::parseCoolantTempResponse(badPid, outTemp));

  // DLC demasiado corto
  obd::CanFrame shortDlc{ 0x7E8, 3, {0x03, 0x41, 0x05, 0x82, 0, 0, 0, 0} };
  assert(!obd::parseCoolantTempResponse(shortDlc, outTemp));

  // Trama multi-frame ISO-TP (PCI != 0)
  obd::CanFrame multiFrame{ 0x7E8, 8, {0x10, 0x41, 0x05, 0x82, 0, 0, 0, 0} };
  assert(!obd::parseCoolantTempResponse(multiFrame, outTemp));
}

static void testTempTrackerTrendsAndSignalLoss() {
  obd::TempTracker tracker(false, 1);

  // Estado inicial: sin señal ni última lectura
  assert(!tracker.reading().hasLiveSignal);
  assert(!tracker.reading().hasLastValue);

  // Primera lectura válida: 85 °C
  tracker.recordValidReading(85, 1000);
  assert(tracker.reading().hasLiveSignal);
  assert(tracker.reading().hasLastValue);
  assert(tracker.reading().tempC == 85);
  assert(tracker.reading().trend == TempTrend::TREND_NONE);

  // Sube a 88 °C
  tracker.recordValidReading(88, 1500);
  assert(tracker.reading().tempC == 88);
  assert(tracker.reading().previousTempC == 85);
  assert(tracker.reading().deltaC == 3);
  assert(tracker.reading().trend == TempTrend::TREND_UP);

  // Se mantiene estable en 88 °C
  tracker.recordValidReading(88, 2000);
  assert(tracker.reading().trend == TempTrend::TREND_STABLE);
  assert(tracker.reading().deltaC == 0);

  // Baja a 86 °C
  tracker.recordValidReading(86, 2500);
  assert(tracker.reading().trend == TempTrend::TREND_DOWN);
  assert(tracker.reading().deltaC == -2);

  // Aún dentro del timeout (3000 ms)
  tracker.updateTimeout(5499, 3000);
  assert(tracker.reading().hasLiveSignal);

  // Pérdida de señal (>= 3000 ms): "Sin datos del auto" conservando "Última lectura" (86 °C)
  tracker.updateTimeout(5500, 3000);
  assert(!tracker.reading().hasLiveSignal);
  assert(tracker.reading().hasLastValue);
  assert(tracker.reading().tempC == 86);

  // Recuperación de señal
  tracker.recordValidReading(87, 7000);
  assert(tracker.reading().hasLiveSignal);
  assert(tracker.reading().tempC == 87);
  assert(tracker.reading().trend == TempTrend::TREND_UP);
}

void testStoredDtcTransport() {
  using Result = obd::StoredDtcResponse::Result;
  obd::StoredDtcResponse response;
  const auto request = obd::buildRequest(0x03);
  assert(request.id == 0x7DF && request.data[0] == 1 && request.data[1] == 3);
  const obd::CanFrame noCodes{0x7E8, 8, {0x02, 0x43, 0x00}};
  assert(response.accept(noCodes) == Result::COMPLETE && response.count() == 0);

  const obd::CanFrame single{0x7E8, 8, {0x04, 0x43, 0x01, 0x03, 0x00}};
  assert(response.accept(single) == Result::COMPLETE);
  char text[6];
  obd::formatDtc(response.code(0), text);
  assert(text[0] == 'P' && text[1] == '0' && text[2] == '3' && text[3] == '0' && text[4] == '0');

  const obd::CanFrame first{0x7E8, 8, {0x10, 0x08, 0x43, 0x03, 0x03, 0x00, 0x01, 0x18}};
  const obd::CanFrame badSequence{0x7E8, 8, {0x22, 0x04, 0x20}};
  const obd::CanFrame next{0x7E8, 8, {0x21, 0x04, 0x20}};
  assert(response.accept(first) == Result::FLOW_CONTROL);
  assert(response.accept(badSequence) == Result::INVALID);
  response.reset();
  assert(response.accept(first) == Result::FLOW_CONTROL);
  assert(response.accept(next) == Result::COMPLETE);
  assert(response.count() == 3 && response.code(2) == 0x0420);
  const obd::CanFrame truncated{0x7E8, 3, {0x04, 0x43, 0x01}};
  assert(response.accept(truncated) == Result::INVALID);
  uint8_t speed = 0;
  const obd::CanFrame velocity{0x7E8, 8, {0x03, 0x41, 0x0D, 82}};
  assert(obd::parseCurrentByte(velocity, obd::PID_VEHICLE_SPEED, speed) && speed == 82);
  assert(!obd::parseCurrentByte(velocity, obd::PID_MONITOR_STATUS, speed));
}

int main() {
  testStoredDtcTransport();
  testBuildRequest();
  testParseValidResponses();
  testRejectInvalidResponses();
  testTempTrackerTrendsAndSignalLoss();
  std::printf("[PASS] Todas las pruebas de obd_protocol.h y TempTracker pasaron.\n");
  return 0;
}
