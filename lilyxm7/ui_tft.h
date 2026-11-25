#pragma once

#include <stdint.h>
#include "data_types.h"

// ============================================================================
// Interfaz gráfica TFT 2.4" ILI9341 (320x240 landscape) con estética Matrix
// ============================================================================

void uiInit();
void uiForceRedraw();
void uiRender(ScreenId screen,
              MenuOption selectedOption,
              const TempReading& tempReading,
              const ConnectionInfo& connInfo);
