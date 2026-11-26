#pragma once

#include <stdint.h>

// ============================================================================
// Configuración principal de lilyxm7 (v1.0)
// Escáner OBD-II para Suzuki Grand Vitara 2008 V6 2.7 L
// ============================================================================

// Cambia a 1 para simulación en Wokwi (potenciómetro + switch de señal)
// Cambia a 0 para hardware real en la Grand Vitara (TWAI + SN65HVD230)
#ifndef LILYXM7_SIMULATION_MODE
#define LILYXM7_SIMULATION_MODE 1
#endif

// Versión del firmware
#define LILYXM7_VERSION "v1.0"
#define LILYXM7_VEHICLE_LABEL "VITARA 2.7"

// ============================================================================
// Pines ESP32-S3 para placa LAFVIN "Retro Game Emulator" (esp32-s3-devkit)
// Fuente oficial: lafvintech/LAFVIN-Retro-Game-Kit (branch dev, config.h)
// ============================================================================

// Pantalla TFT ST7789 240x320 soldada en la placa Retro Game Emulator (SPI2)
constexpr int PIN_TFT_BL   = 6;   // Backlight
constexpr int PIN_TFT_CS   = 7;   // Chip Select
constexpr int PIN_TFT_DC   = 15;  // Data / Command (RS)
constexpr int PIN_TFT_RST  = 16;  // Reset
constexpr int PIN_TFT_MOSI = 17;  // SDA / MOSI
constexpr int PIN_TFT_SCK  = 18;  // SCL / SCK
constexpr int PIN_TFT_MISO = -1;  // No conectado en la placa

// Botones físicos soldados en la placa Retro Game Emulator (INPUT_PULLUP, activo LOW)
// Otros disponibles en la placa: LEFT=13, RIGHT=14, START=39, SELECT=40, MENU=5, OPTION=4
constexpr int PIN_BTN_UP   = 11;  // Botón UP (↑)
constexpr int PIN_BTN_DOWN = 12;  // Botón DOWN (↓)
constexpr int PIN_BTN_A    = 21;  // Botón SW-A (Seleccionar)
constexpr int PIN_BTN_B    = 47;  // Botón SW-B (Volver)

// LED indicador externo opcional (poner en -1 si no se conecta; GPIO 16 lo usa TFT_RST)
constexpr int PIN_LILY_EYE = -1;

// Entradas de simulación para banco/simulador (solo cuando LILYXM7_SIMULATION_MODE == 1)
// Nota: GPIO 1 y 2 son CS/CLK del TFCard si montas el módulo microSD
constexpr int PIN_SIM_POT        = 1;
constexpr int PIN_SIM_SIGNAL_SW  = 2;

// Bus CAN / TWAI hacia transceptor SN65HVD230 (solo cuando LILYXM7_SIMULATION_MODE == 0)
// GPIOs libres en la placa Retro Game Emulator (sin choque con TFT, botones ni SD):
constexpr int PIN_CAN_TX = 38; // Conectar a CTX (D) del SN65HVD230
constexpr int PIN_CAN_RX = 8;  // Conectar a CRX (R) del SN65HVD230

// ============================================================================
// Temporizaciones y parámetros de operación
// ============================================================================
constexpr uint32_t OBD_POLL_INTERVAL_MS      = 500;  // Consulta PID 05 cada 500 ms
constexpr uint32_t SIGNAL_TIMEOUT_MS         = 3000; // Sin respuesta tras 3 s -> "Sin datos del auto"
constexpr uint32_t SIM_SAMPLE_INTERVAL_MS    = 350;  // Frecuencia de actualización en modo simulación
constexpr uint32_t BUTTON_DEBOUNCE_MS        = 45;   // Antirrebote de botones
constexpr uint32_t UI_ANIMATION_INTERVAL_MS  = 450;  // Parpadeo / animación Matrix sutil

// Rango del potenciómetro en simulación (°C)
constexpr int16_t SIM_TEMP_MIN_C = 20;
constexpr int16_t SIM_TEMP_MAX_C = 120;

// Umbrales de temperatura del refrigerante para Suzuki Grand Vitara V6 2.7 L (°C)
constexpr int16_t TEMP_THRESHOLD_COLD_MAX     = 59;  // < 60 °C: motor frío
constexpr int16_t TEMP_THRESHOLD_WARMING_MAX  = 79;  // 60 - 79 °C: calentando
constexpr int16_t TEMP_THRESHOLD_OPTIMAL_MAX  = 97;  // 80 - 97 °C: temperatura óptima de operación
constexpr int16_t TEMP_THRESHOLD_WARN_MAX     = 107; // 98 - 107 °C: alta, precaución
                                                     // >= 108 °C: peligro / sobrecalentamiento

// Cambio mínimo (°C) para considerar que subió o bajó respecto a la lectura anterior
constexpr int16_t TEMP_TREND_DEADBAND_C = 1;
