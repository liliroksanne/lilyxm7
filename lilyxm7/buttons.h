#pragma once

#include <stdint.h>

// ============================================================================
// Manejo de botones físicos con antirrebote: ↑, ↓, A y B
// ============================================================================

enum class ButtonEvent : uint8_t {
  NONE = 0,
  UP = 1,
  DOWN = 2,
  SELECT_A = 3,
  BACK_B = 4
};

void buttonsInit();
ButtonEvent buttonsPoll();
