/** ==========================================================================
 *  system/ProcessPriority.hpp — Clases de prioridad Win32 (módulo system/).
 *  ========================================================================== */
#pragma once

#include <windows.h>

namespace smac::prio {

/**
 * Eleva el proceso host (shell Win32) a ABOVE_NORMAL_PRIORITY_CLASS para
 * que la UI nunca compita con el motor por los 4 hilos del Celeron.
 * Devuelve true si la clase se aplicó.
 */
bool ElevateHostProcess();

/**
 * Aplica la política de prioridades a un proceso hijo del motor:
 *  - gpu-process  -> se respeta (BELOW_NORMAL ya lo fija Chromium).
 *  - utility      -> NORMAL (responde a IPC del host).
 *  - renderer     -> BELOW_NORMAL (la web nunca debe matar la UI).
 * Devuelve la clase aplicada (o 0 si no se tocó).
 */
DWORD ApplyChildPolicy(HANDLE childProcess, const wchar_t* commandLine);

/** Analiza --type=... de una línea de comandos de Chromium. */
enum class ChildKind { Unknown, Renderer, Gpu, Utility };
ChildKind ClassifyChild(const wchar_t* commandLine);

} // namespace smac::prio
