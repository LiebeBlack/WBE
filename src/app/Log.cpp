/** ==========================================================================
 *  app/Log.cpp — Implementación del logging de S.M.A.C.
 *  ----------------------------------------------------------------------------
 *  Estrategia anti-overhead en producción:
 *   - Producción: solo OutputDebugStringA (baratísimo, sin IO de disco).
 *   - Desarrollo: además un archivo plano en %LOCALAPPDATA%\SMAC\WebCore\log.
 *  Todo con buffers en pila: cero asignaciones de memoria dinámica.
 *  ========================================================================== */
#include "smac/Log.hpp"

#include <cstdio>
#include <cstdarg>

namespace smac::log {
namespace {

constexpr size_t kLineMax = 512;

SRWLOCK g_fileLock = SRWLOCK_INIT;   // Inicialización estática: sin ctor
HANDLE  g_logFile = nullptr;

/** Niveles como caracteres fijos para no construir std::wstring. */
constexpr char kLevelInfo[]  = "I";
constexpr char kLevelWarn[]  = "W";
constexpr char kLevelError[] = "E";

/** Escribe una línea ya formateada. La llamada NO reserva memoria. */
void EmitLine(char levelTag, const char* line, size_t len) noexcept {
    char full[kLineMax + 16];
    full[0] = '[';
    full[1] = 'S';
    full[2] = 'M';
    full[3] = 'A';
    full[4] = 'C';
    full[5] = ':';
    full[6] = levelTag;
    full[7] = ']';
    full[8] = ' ';
    size_t off = 9;
    if (len > kLineMax) len = kLineMax;
    for (size_t i = 0; i < len; ++i) full[off + i] = line[i];
    off += len;
    full[off++] = '\r';
    full[off++] = '\n';
    full[off] = '\0';

    OutputDebugStringA(full);

#ifndef SMAC_PRODUCTION_BUILD
    if (g_logFile) {
        AcquireSRWLockExclusive(&g_fileLock);
        DWORD written = 0;
        WriteFile(g_logFile, full, static_cast<DWORD>(off), &written, nullptr);
        ReleaseSRWLockExclusive(&g_fileLock);
    }
#else
    (void)levelTag;
#endif
}

/** Punto único de formateo estilo printf. */
void EmitV(char levelTag, const char* fmt, va_list args) noexcept {
    char buf[kLineMax];
#pragma warning(push)
#pragma warning(disable : 4710)  // vsnprintf puede quedar inline: esperado
    int n = vsnprintf(buf, kLineMax, fmt, args);
#pragma warning(pop)
    if (n <= 0) return;
    size_t len = (n < static_cast<int>(kLineMax)) ? static_cast<size_t>(n)
                                                  : kLineMax;
    EmitLine(levelTag, buf, len);
}

} // namespace

void InitFileSink() {
#ifdef SMAC_PRODUCTION_BUILD
    // Producción: sin IO de disco en la máquina objetivo.
    return;
#else
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 48) return;
    lstrcatA(path, "\\SMAC");
    CreateDirectoryA(path, nullptr);
    lstrcatA(path, "\\WebCore");
    CreateDirectoryA(path, nullptr);
    lstrcatA(path, "\\smac.log");
    g_logFile = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#endif
}

void ShutdownFileSink() {
    if (g_logFile) {
        AcquireSRWLockExclusive(&g_fileLock);
        CloseHandle(g_logFile);
        g_logFile = nullptr;
        ReleaseSRWLockExclusive(&g_fileLock);
    }
}

void Info(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    EmitV('I', fmt, args);
    va_end(args);
}

void Warn(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    EmitV('W', fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    EmitV('E', fmt, args);
    va_end(args);
}

} // namespace smac::log
