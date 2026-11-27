#pragma once

// ============================================================================
// Motor 3D Raycasting "Lily DOOM" (160x90 -> 320x180, sin microSD)
// ============================================================================

void doomStart();
void doomRenderFull();
bool doomHandleButton(int pin); // Devuelve true si se presionó [SELECT] para volver al menú
void doomTick();
