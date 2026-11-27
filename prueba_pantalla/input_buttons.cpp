#include "input_buttons.h"
#include "board_config.h"
#include <Arduino.h>

namespace {

struct DebouncedBtn {
  int pin;
  bool lastRaw;
  bool stable;
  uint32_t changedMs;
};

DebouncedBtn g_buttons[] = {
  { BTN_UP,     true, true, 0 },
  { BTN_DOWN,   true, true, 0 },
  { BTN_LEFT,   true, true, 0 },
  { BTN_RIGHT,  true, true, 0 },
  { BTN_A,      true, true, 0 },
  { BTN_B,      true, true, 0 },
  { BTN_START,  true, true, 0 },
  { BTN_SELECT, true, true, 0 },
  { BTN_MENU,   true, true, 0 },
  { BTN_OPTION, true, true, 0 },
};

} // namespace

void inputButtonsInit() {
  const uint32_t now = millis();
  for (auto& b : g_buttons) {
    pinMode(b.pin, INPUT_PULLUP);
    const bool high = (digitalRead(b.pin) == HIGH);
    b.lastRaw = high;
    b.stable = high;
    b.changedMs = now;
  }
}

int inputButtonsPoll() {
  const uint32_t now = millis();
  for (auto& b : g_buttons) {
    const bool rawHigh = (digitalRead(b.pin) == HIGH);
    if (rawHigh != b.lastRaw) {
      b.lastRaw = rawHigh;
      b.changedMs = now;
    }
    if ((now - b.changedMs) >= 40) {
      if (rawHigh != b.stable) {
        b.stable = rawHigh;
        if (!b.stable) return b.pin;
      }
    }
  }
  return -1;
}
