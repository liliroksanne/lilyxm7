// ============================================================================
// lilyxm7_obd_tester.cpp
// Cliente OBD-II para Linux SocketCAN (vcan0 / can0) que reutiliza EXACTAMENTE
// ../lilyxm7/obd_protocol.h para validar Servicio 01 PID 05 contra ecu-simulator.
// ============================================================================

#include "../lilyxm7/obd_protocol.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_running = 1;

void onSignal(int) {
  g_running = 0;
}

void logToCentralFiles(const char* level, const std::string& msg) {
  ::mkdir("../logs", 0755);
  std::time_t now = std::time(nullptr);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  const std::string line = std::string("[") + ts + "][ecu_tester_linux][" + level + "] " + msg + "\n";

  for (const char* path : { "../logs/ecu_tester_linux.log", "../logs/all.log" }) {
    std::ofstream out(path, std::ios::app);
    if (out.is_open()) {
      out << line;
    }
  }
}

uint32_t nowMs() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

const char* trendToText(TempTrend trend) {
  switch (trend) {
    case TempTrend::TREND_UP:     return "SUBIÓ";
    case TempTrend::TREND_DOWN:   return "BAJÓ";
    case TempTrend::TREND_STABLE: return "ESTABLE";
    case TempTrend::TREND_NONE:
    default:                      return "INICIAL";
  }
}

} // namespace

int main(int argc, char* argv[]) {
  const std::string iface = (argc >= 2) ? argv[1] : "vcan0";
  const uint32_t pollIntervalMs = 500;
  const uint32_t timeoutMs = 3000;

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  const int sock = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (sock < 0) {
    std::perror("socket(PF_CAN)");
    return 1;
  }

  struct ifreq ifr{};
  std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
  if (::ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
    std::perror("ioctl(SIOCGIFINDEX)");
    std::cerr << "Asegúrate de levantar primero la interfaz (ej. ./setup_vcan.sh " << iface << ")\n";
    ::close(sock);
    return 1;
  }

  struct sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::perror("bind(AF_CAN)");
    ::close(sock);
    return 1;
  }

  std::cout << "========================================================\n";
  std::cout << " lilyxm7 OBD-II Linux Tester (Servicio 01, PID 05)\n";
  std::cout << " Interfaz SocketCAN: " << iface << "\n";
  std::cout << " Log centralizado: ../logs/ecu_tester_linux.log\n";
  std::cout << " Presiona Ctrl+C para salir.\n";
  std::cout << "========================================================\n";
  logToCentralFiles("INFO", "Tester iniciado en interfaz " + iface);

  obd::TempTracker tracker(false, 1);
  uint32_t lastTxMs = 0;
  bool lastReportedNoSignal = false;

  while (g_running) {
    const uint32_t now = nowMs();

    // 1. Enviar consulta OBD-II 0x7DF -> 02 01 05 00 00 00 00 00
    if (lastTxMs == 0 || (now - lastTxMs) >= pollIntervalMs) {
      lastTxMs = now;
      const obd::CanFrame req = obd::buildCoolantTempRequest();
      struct can_frame tx{};
      tx.can_id = req.id;
      tx.can_dlc = req.dlc;
      std::memcpy(tx.data, req.data, 8);

      if (::write(sock, &tx, sizeof(tx)) != static_cast<ssize_t>(sizeof(tx))) {
        std::perror("write(can_frame)");
      }
    }

    // 2. Esperar respuestas entrantes de la ECU (ecu-simulator)
    struct pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, 100);
    if (ready > 0 && (pfd.revents & POLLIN)) {
      struct can_frame rx{};
      const ssize_t nbytes = ::read(sock, &rx, sizeof(rx));
      if (nbytes == static_cast<ssize_t>(sizeof(rx))) {
        obd::CanFrame frame{};
        frame.id = rx.can_id & CAN_SFF_MASK;
        frame.dlc = rx.can_dlc;
        std::memcpy(frame.data, rx.data, 8);

        int16_t tempC = 0;
        if (obd::parseCoolantTempResponse(frame, tempC)) {
          tracker.recordValidReading(tempC, nowMs());
          const TempReading& r = tracker.reading();
          lastReportedNoSignal = false;
          char msgBuf[160];
          std::snprintf(msgBuf, sizeof(msgBuf),
                        "ID=0x%03X -> Refrigerante: %d °C | Tendencia: %s (delta %+d °C) | Lecturas: %u",
                        frame.id, r.tempC, trendToText(r.trend), r.deltaC, r.validCount);
          std::printf("[OK] %s\n", msgBuf);
          std::fflush(stdout);
          logToCentralFiles("INFO", msgBuf);
        }
      }
    }

    // 3. Verificar pérdida de señal
    tracker.updateTimeout(nowMs(), timeoutMs);
    const TempReading& r = tracker.reading();
    if (!r.hasLiveSignal && !lastReportedNoSignal && now >= timeoutMs) {
      lastReportedNoSignal = true;
      char alertBuf[128];
      if (r.hasLastValue) {
        std::snprintf(alertBuf, sizeof(alertBuf), "Sin datos del auto | Última lectura: %d °C", r.tempC);
      } else {
        std::snprintf(alertBuf, sizeof(alertBuf), "Sin datos del auto | Sin lecturas previas");
      }
      std::printf("[ALERTA] %s\n", alertBuf);
      std::fflush(stdout);
      logToCentralFiles("WARN", alertBuf);
    }
  }

  ::close(sock);
  logToCentralFiles("INFO", "Prueba finalizada.");
  std::cout << "\n[lilyxm7] Prueba finalizada.\n";
  return 0;
}
