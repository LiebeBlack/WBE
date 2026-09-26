/** ==========================================================================
 *  smac/Log.hpp — Logging sin excepciones, sin iostream, sin allocación.
 *  ----------------------------------------------------------------------------
 *  Escribimos con \\n explícitos (mejor throughput que std::endl) hacia
 *  OutputDebugStringW (visible en DbgView/VS) y hacia un archivo de log
 *  en el directorio de datos (solo en builds no-production, para no usar
 *  IO de disco en la máquina objetivo).
 *  ========================================================================== */
#pragma once
#include <windows.h>

namespace smac::log {

/** Inicializa el sink de archivo (llamar solo si !SMAC_PRODUCTION_BUILD). */
void InitFileSink();

/** Cierra el archivo de log si está abierto. */
void ShutdownFileSink();

/** API estilo printf: nunca lanza, nunca bloquea, nunca reserva memoria. */
void Info(const char* fmt, ...) noexcept;
void Warn(const char* fmt, ...) noexcept;
void Error(const char* fmt, ...) noexcept;

} // namespace smac::log
