#include "buttons.h"
#include "config.h"
#include <Arduino.h>

namespace {

struct DebouncedButton {
  int pin;
  ButtonEvent event;
  bool lastRawState;
  bool stableState;
  uint32_t lastChangeMs;
};

DebouncedButton g_buttons[] = {
  { PIN_BTN_UP,   ButtonEvent::UP,       true, true, 0 },
  { PIN_BTN_DOWN, ButtonEvent::DOWN,     true, true, 0 },
  { PIN_BTN_A,    ButtonEvent::SELECT_A, true, true, 0 },
  { PIN_BTN_B,    ButtonEvent::BACK_B,   true, true, 0 }
};

constexpr size_t kButtonCount = sizeof(g_buttons) / sizeof(g_buttons[0]);

} // namespace

void buttonsInit() {
  const uint32_t now = millis();
  for (size_t i = 0; i < kButtonCount; ++i) {
    pinMode(g_buttons[i].pin, INPUT_PULLUP);
    const bool initial = (digitalRead(g_buttons[i].pin) == HIGH);
    g_buttons[i].lastRawState = initial;
    g_buttons[i].stableState = initial;
    g_buttons[i].lastChangeMs = now;
  }
}

ButtonEvent buttonsPoll() {
  const uint32_t now = millis();
  ButtonEvent triggered = ButtonEvent::NONE;

  for (size_t i = 0; i < kButtonCount; ++i) {
    DebouncedButton& btn = g_buttons[i];
    const bool rawHigh = (digitalRead(btn.pin) == HIGH);

    if (rawHigh != btn.lastRawState) {
      btn.lastRawState = rawHigh;
      btn.lastChangeMs = now;
    }

    if ((now - btn.lastChangeMs) >= BUTTON_DEBOUNCE_MS) {
      if (rawHigh != btn.stableState) {
        btn.stableState = rawHigh;
        // Activo en bajo (INPUT_PULLUP -> presionado conecta a GND)
        if (!btn.stableState && triggered == ButtonEvent::NONE) {
          triggered = btn.event;
        }
      }
    }
  }

  return triggered;
}
