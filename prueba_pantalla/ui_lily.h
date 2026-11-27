#pragma once

#include "board_config.h"

// ============================================================================
// Interfaz principal lilyxm7 (menú, temperatura, conexión y chatbot Lily)
// ============================================================================

void uiLilyInit();
void uiLilyShowSplash();
void uiLilyPushChat(const char* msg, bool isAlert = false);
void uiLilyRenderFull(ScreenId screen, uint8_t menuIdx, bool clearScreen = true);
void uiLilyHandleButton(ScreenId& screen, uint8_t& menuIdx, int pin);
void uiLilyTick(ScreenId screen, uint8_t menuIdx);
