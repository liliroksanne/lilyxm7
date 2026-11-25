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
// Pines ESP32-S3 (compatibles con LAFVIN ESP32-S3 y Wokwi DevKitC-1)
// Evita pines strapping (0, 3, 45, 46), USB nativo (19, 20) y PSRAM (33-37).
// ============================================================================

// Pantalla TFT 2.4" ILI9341 (SPI hardware)
constexpr int PIN_TFT_CS   = 10;
constexpr int PIN_TFT_DC   = 9;
constexpr int PIN_TFT_RST  = 14;
constexpr int PIN_TFT_MOSI = 11;
constexpr int PIN_TFT_SCK  = 12;
constexpr int PIN_TFT_MISO = 13;
constexpr int PIN_TFT_BL   = 21; // Opcional: se pone en HIGH al iniciar

// Botones físicos del menú (conectados entre GPIO y GND, usando INPUT_PULLUP)
constexpr int PIN_BTN_UP   = 4;  // Flecha arriba (↑)
constexpr int PIN_BTN_DOWN = 5;  // Flecha abajo (↓)
constexpr int PIN_BTN_A    = 6;  // Botón A (Seleccionar)
constexpr int PIN_BTN_B    = 7;  // Botón B (Volver)

// Entradas de simulación para Wokwi (solo activas cuando LILYXM7_SIMULATION_MODE == 1)
constexpr int PIN_SIM_POT        = 1; // Potenciómetro de temperatura (ADC1_CH0)
constexpr int PIN_SIM_SIGNAL_SW  = 2; // Switch de señal: LOW = ECU responde, HIGH = sin señal

// Bus CAN / TWAI hacia transceptor SN65HVD230 (solo cuando LILYXM7_SIMULATION_MODE == 0)
constexpr int PIN_CAN_TX = 17; // Conectar a CTX (D) del SN65HVD230
constexpr int PIN_CAN_RX = 18; // Conectar a CRX (R) del SN65HVD230

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
