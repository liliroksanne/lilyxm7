#pragma once

// ============================================================================
// Lectura con antirrebote de los 10 botones de la placa LAFVIN Retro Game PCB
// ============================================================================

void inputButtonsInit();
int inputButtonsPoll(); // Devuelve el GPIO recién pulsado o -1 si no hubo evento
