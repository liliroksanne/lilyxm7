#pragma once

#include <cstdint>

// ============================================================================
// Emulador ligero de driver/twai.h (ESP32-S3 CAN 500 kbps) + ECU virtual Vitara
// Permite probar el código OBD-II real (LILYXM7_SIMULATION_MODE=0) en el web sim.
// ============================================================================

typedef int esp_err_t;
typedef int gpio_num_t;
typedef uint32_t TickType_t;

#define ESP_OK   0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 0x107
#define pdMS_TO_TICKS(ms) (ms)

enum twai_mode_t {
  TWAI_MODE_NORMAL = 0,
  TWAI_MODE_NO_ACK = 1,
  TWAI_MODE_LISTEN_ONLY = 2
};

enum twai_state_t {
  TWAI_STATE_STOPPED = 0,
  TWAI_STATE_RUNNING = 1,
  TWAI_STATE_BUS_OFF = 2,
  TWAI_STATE_RECOVERING = 3
};

struct twai_general_config_t {
  gpio_num_t tx_io;
  gpio_num_t rx_io;
  twai_mode_t mode;
  uint32_t tx_queue_len;
  uint32_t rx_queue_len;
};

struct twai_timing_config_t {
  uint32_t kbps;
};

struct twai_filter_config_t {
  bool accept_all;
};

struct twai_status_info_t {
  twai_state_t state;
};

struct twai_message_t {
  uint32_t identifier;
  uint8_t extd;
  uint8_t rtr;
  uint8_t data_length_code;
  uint8_t data[8];
};

#define TWAI_GENERAL_CONFIG_DEFAULT(tx, rx, m) { (tx), (rx), (m), 8, 16 }
#define TWAI_TIMING_CONFIG_500KBITS() { 500 }
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() { true }

namespace sim_can {
void setEcuTemp(int tempC);
void setEcuOnline(bool online);
void triggerBusOff();
int popCanLog(char* outBuf, int maxLen);
} // namespace sim_can

esp_err_t twai_driver_install(const twai_general_config_t* g, const twai_timing_config_t* t, const twai_filter_config_t* f);
esp_err_t twai_start();
esp_err_t twai_transmit(const twai_message_t* msg, TickType_t ticks);
esp_err_t twai_receive(twai_message_t* msg, TickType_t ticks);
esp_err_t twai_get_status_info(twai_status_info_t* status);
esp_err_t twai_initiate_recovery();
