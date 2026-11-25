#pragma once

#include "data_types.h"

// ============================================================================
// Fuente común de datos de temperatura y conexión para lilyxm7
// La interfaz y la asistente SIEMPRE leen mediante getCoolantTemperature(),
// sin importar si el origen es Wokwi (simulación) o OBD-II real (Vitara).
// ============================================================================

void dataSourceInit();
void dataSourceUpdate();

// Función común de temperatura requerida por la arquitectura de lilyxm7
TempReading getCoolantTemperature();

// Información para la pantalla "Estado de conexión"
ConnectionInfo getConnectionInfo();

// Indica si el firmware fue compilado en modo simulación (Wokwi)
bool isSimulationMode();
