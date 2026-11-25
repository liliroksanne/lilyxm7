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
  assert(tracker.reading().trend == TempTrend::NONE);

  // Sube a 88 °C
  tracker.recordValidReading(88, 1500);
  assert(tracker.reading().tempC == 88);
  assert(tracker.reading().previousTempC == 85);
  assert(tracker.reading().deltaC == 3);
  assert(tracker.reading().trend == TempTrend::RISING);

  // Se mantiene estable en 88 °C
  tracker.recordValidReading(88, 2000);
  assert(tracker.reading().trend == TempTrend::STABLE);
  assert(tracker.reading().deltaC == 0);

  // Baja a 86 °C
  tracker.recordValidReading(86, 2500);
  assert(tracker.reading().trend == TempTrend::FALLING);
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
  assert(tracker.reading().trend == TempTrend::RISING);
}

int main() {
  testBuildRequest();
  testParseValidResponses();
  testRejectInvalidResponses();
  testTempTrackerTrendsAndSignalLoss();
  std::printf("[PASS] Todas las pruebas de obd_protocol.h y TempTracker pasaron.\n");
  return 0;
}
