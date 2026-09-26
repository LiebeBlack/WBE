/** ==========================================================================
 *  system/MemoryOptimizer.cpp — Trimming de Working Set + allocator mimalloc.
 *  ----------------------------------------------------------------------------
 *  Ciclo del temporizador (hilo UI):
 *    1. GetLastInputInfo -> segundos de inactividad real del usuario.
 *    2. Si supera trimIdleSeconds: SetProcessWorkingSetSizeEx(-1,-1) en:
 *        - el proceso propio (shell Win32),
 *        - cada hijo msedgewebview2.exe cuyo padre seamos nosotros.
 *    3. El trim se OMITE si hay audio activo (TabManager -> SetAudioActive).
 *
 *  El trim usa (SIZE_T)-1: recorte explícito del working set (paginación
 *  pura de páginas inactivas) sin destruir caches ni estado del motor.
 *  ========================================================================== */
#include "MemoryOptimizer.hpp"

#include "smac/Log.hpp"
#include "ProcessPriority.hpp"
#include "Paths.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>
#include <mimalloc.h>
#include <psapi.h>
#include <tlhelp32.h>

#ifdef SMAC_MIMALLOC
extern "C" {
/** Primitiva del CRT MSVC que dispara el new_handler en OOM. */
__declspec(dllimport) int __cdecl _callnewh(size_t size);
}
#endif

namespace smac::mem {

// ============================ Allocator global ==============================
//  Con -fno-exceptions el fallo de asignación no puede propagarse: si el
//  new_handler no libera nada, abortamos de forma controlada y registrada.

#ifdef SMAC_MIMALLOC

/** Aborta el proceso ante OOM (terminación limpia y registrada). */
[[noreturn]] static void AbortOom() {
    log::Error("mem: OOM fatal, abortando proceso");
    TerminateProcess(GetCurrentProcess(), 3);
    _exit(3);   // por si TerminateProcess no llegara a ejecutarse
}

void* operator new(std::size_t size) {
    if (void* p = mi_malloc(size)) return p;
    if (_callnewh(size) == 0) AbortOom();
    if (void* p = mi_malloc(size)) return p;
    AbortOom();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* p) noexcept { mi_free(p); }
void operator delete[](void* p) noexcept { mi_free(p); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return mi_malloc(size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return mi_malloc(size);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { mi_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { mi_free(p); }

// Variantes con alineación (C++17). mi_free reconoce bloques alineados.
void* operator new(std::size_t size, std::align_val_t al) {
    const size_t alignment = static_cast<size_t>(al);
    if (void* p = mi_malloc_aligned(size, alignment)) return p;
    if (_callnewh(size) == 0) AbortOom();
    if (void* p = mi_malloc_aligned(size, alignment)) return p;
    AbortOom();
}
void* operator new[](std::size_t size, std::align_val_t al) {
    return ::operator new(size, al);
}
void operator delete(void* p, std::align_val_t) noexcept { mi_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { mi_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    mi_free(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    mi_free(p);
}

#endif // SMAC_MIMALLOC

// ============================ API declarada =================================

void InstallAllocator() {
    // El reemplazo es estático (sobrecargas de arriba); aquí solo validamos
    // que mimalloc esté operativo y fijamos políticas de devolución al OS.
    // - purge_delay: losDirty pages vuelven al sistema antes.
    mi_option_set(mi_option_purge_delay, 10);
#ifdef SMAC_MIMALLOC
    log::Info("mem: mimalloc activo (new/delete sustituidos)");
#else
    log::Warn("mem: mimalloc NO compilado; usando heap de Windows");
#endif
}

void ConfigureAllocator() {
    // Reservado para ajustes futuros (mileage de páginas enormes, etc.).
}

// ============================ Trimming ======================================

namespace {

constexpr UINT_PTR kTrimTimerId = 0x524D494D;  // Identificador único en-app.

volatile LONG g_audioActive = 0;   ///< Lo escribe TabManager (audio/vídeo).

Params g_params;                   ///< Copia local de los parámetros.

bool      g_lastTrimmed   = false; ///< Diagnóstico del último tick.
ULONGLONG g_lastTrimTick  = 0;     ///< Anti-rebote del trim.

/** ¿Lleva el usuario >= seconds segundos sin tocar nada? */
bool UserIdleSeconds(int seconds) {
    LASTINPUTINFO lii{};
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&lii)) return false;
    const ULONGLONG now = GetTickCount64();
    if (lii.dwTime > now) return false;   // reloj hacia atrás: no trim
    return (now - lii.dwTime) >= static_cast<ULONGLONG>(seconds) * 1000ULL;
}

/** Recorta el working set de un handle de proceso (best-effort). */
void TrimProcessHandle(HANDLE h) {
    SetProcessWorkingSetSizeEx(h, static_cast<SIZE_T>(-1),
                               static_cast<SIZE_T>(-1), 0);
}

/**
 * Recorta cada proceso hijo msedgewebview2.exe cuyo PID padre sea el nuestro.
 * Un único snapshot de Toolhelp por tick (barato: se ejecuta 1 vez / 30 s).
 */
void TrimChildrenProcesses() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    const DWORD selfPid = GetCurrentProcessId();

    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ParentProcessID != selfPid) continue;

            const wchar_t* base = pe.szExeFile;
            const wchar_t* slash = wcsrchr(base, L'\\');
            if (slash) base = slash + 1;
            if (_wcsicmp(base, L"msedgewebview2.exe") != 0) continue;

            HANDLE h = OpenProcess(
                PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, pe.th32ProcessID);
            if (h) {
                TrimProcessHandle(h);
                CloseHandle(h);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

/** TIMERPROC: se ejecuta en el hilo UI (el que instaló el timer). */
void CALLBACK TrimTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    TrimTick();
}

/** Tick del temporizador: decide y ejecuta el trim. */
void TrimTick() {
    g_lastTrimmed = false;
    if (g_params.trimIdleSeconds <= 0) return;
    if (g_audioActive != 0) return;   // nunca durante reproducción de audio
    if (!UserIdleSeconds(g_params.trimIdleSeconds)) return;

    // Anti-rebote: máximo un trim por intervalo de inactividad.
    const ULONGLONG now = GetTickCount64();
    if (g_lastTrimTick != 0 && now - g_lastTrimTick < 30000) return;

    log::Info("mem: inactividad >= %d s, recortando working sets",
              g_params.trimIdleSeconds);

    TrimProcessHandle(GetCurrentProcess());
    if (g_params.trimChildren) TrimChildrenProcesses();

    g_lastTrimTick = now;
    g_lastTrimmed  = true;
}

} // namespace

void StartTrimTimer(HWND hwnd, const Params& params) {
    g_params = params;
    const UINT period = static_cast<UINT>(
        params.trimTimerMS > 0 ? params.trimTimerMS : 5000);
    SetTimer(hwnd, kTrimTimerId, period, &TrimTimerProc);
    log::Info("mem: trim cada %u ms, umbral de inactividad %d s",
              period, params.trimIdleSeconds);
}

void StopTrimTimer(HWND hwnd) {
    KillTimer(hwnd, kTrimTimerId);
}

bool LastTickTrimmed() { return g_lastTrimmed; }

void SetAudioActive(bool active) {
    InterlockedExchange(&g_audioActive, active ? 1 : 0);
}

} // namespace smac::mem
