#pragma once

// ============================================================================
// Minijuego Snake (misma estética rosa suave / verde Matrix, 0 parpadeo)
// ============================================================================

void snakeStart();
void snakeRenderFull();
bool snakeHandleButton(int pin); // Devuelve true si se presionó [B] para volver al menú
void snakeTick();
