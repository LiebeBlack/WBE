/** ==========================================================================
 *  system/MemoryOptimizer.hpp — Memoria y scheduler (módulo system/).
 *  ----------------------------------------------------------------------------
 *  Tres responsabilidades separadas (cada una en su archivo de cabecera):
 *   1. MemoryOptimizer: allocator global mimalloc + trimming de Working Set.
 *   2. ProcessPriority: clases de prioridad host/children.
 *  ========================================================================== */
#pragma once

#include <windows.h>

namespace smac::mem {

/** Configuración del subsistema de memoria (copiada desde smac::Config). */
struct Params {
    int  trimIdleSeconds   = 30;    ///< Inactividad para paginar (s)
    int  trimTimerMS       = 5000;  ///< Periodo del SetTimer (ms)
    bool trimChildren      = true;  ///< Recortar también los hijos del motor
    bool childrenBelowNormal = true;///< Hijos no-GPU a BELOW_NORMAL
};

/** Reemplazo global de operator new/delete por mimalloc (ver .cpp). */
void InstallAllocator();

/** Debe llamarse una vez al arrancar: ajusta opciones de mimalloc. */
void ConfigureAllocator();

/**
 * Instala el temporizador Win32 de trimming. hwnd debe ser la ventana
 * principal (hilo UI). El trim solo dispara tras kIdleSeconds de inactividad
 * real medida con GetLastInputInfo.
 */
void StartTrimTimer(HWND hwnd, const Params& params);

/** Detiene el temporizador (al cerrar la ventana). */
void StopTrimTimer(HWND hwnd);

/** Consulta de diagnóstico: ¿se recortó el working set en el último tick? */
bool LastTickTrimmed();

/** TabManager la llama al entrar/salir audio de una pestaña (pausa el trim). */
void SetAudioActive(bool active);

/**
 * Abort controlado ante OOM (visible para el allocator global del .cpp y
 * reutilizable por otros módulos bajo -fno-exceptions).
 */
[[noreturn]] void smac_abort_oom();

} // namespace smac::mem
