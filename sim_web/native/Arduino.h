#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ============================================================================
// Emulador nativo de Arduino.h para ejecutar lilyxm7 C++ en Windows (sim_web)
// ============================================================================

#define HIGH 0x1
#define LOW  0x0

#define INPUT        0x01
#define OUTPUT       0x03
#define INPUT_PULLUP 0x05

// Macros reales de Arduino.h (mantenidas para validar compatibilidad real)
#define RISING  0x01
#define FALLING 0x02

#define F(str) (str)

namespace sim_hw {

void setVirtualMillis(uint32_t ms);
void advanceVirtualMillis(uint32_t dt);
uint32_t getVirtualMillis();

void setPinMode(int pin, int mode);
void setDigitalInput(int pin, int val);
int getDigitalPin(int pin);
void setAnalogInput(int pin, int adcVal);
int getAnalogPin(int pin);
uint32_t getActiveGpioMask();

void appendSerial(const char* text);
int popSerial(char* outBuf, int maxLen);

} // namespace sim_hw

inline uint32_t millis() { return sim_hw::getVirtualMillis(); }
inline void delay(uint32_t ms) {
  // En loop() se usa delay(15); el paso de tiempo lo controla sim_tick(dt_ms)
  (void)ms;
}
inline void pinMode(int pin, int mode) { sim_hw::setPinMode(pin, mode); }
inline int digitalRead(int pin) { return sim_hw::getDigitalPin(pin); }
inline void digitalWrite(int pin, int val) { sim_hw::setDigitalInput(pin, val); }
inline int analogRead(int pin) { return sim_hw::getAnalogPin(pin); }
inline void analogReadResolution(int) {}

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max == in_min) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

class SimSerialClass {
public:
  void begin(unsigned long) {}

  void print(const char* s) {
    if (s) sim_hw::appendSerial(s);
  }
  void println(const char* s = "") {
    if (s && s[0] != '\0') sim_hw::appendSerial(s);
    sim_hw::appendSerial("\n");
  }
  int printf(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sim_hw::appendSerial(buf);
    return n;
  }
};

extern SimSerialClass Serial;
