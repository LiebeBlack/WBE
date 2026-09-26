/** ==========================================================================
 *  system/Paths.cpp — Resolución de rutas sin excepciones ni STL pesado.
 *  ========================================================================== */
#include "Paths.hpp"

#include "smac/Log.hpp"

#include <shlobj.h>
#include <objbase.h>

namespace smac::paths {
namespace {

/** Buffer estático de la ruta del exe (nunca cambia en la vida del proceso). */
wchar_t g_exeDir[MAX_PATH] = L"";
wchar_t g_userDir[MAX_PATH] = L"";
wchar_t g_extDir[MAX_PATH] = L"";
wchar_t g_desktop[MAX_PATH] = L"";
bool    g_resolved = false;

/** Resuelve todo una vez. Idempotente y thread-safe por idempotencia. */
void ResolveOnce() {
    if (g_resolved) return;
    g_resolved = true;

    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        log::Error("paths: GetModuleFileNameW fallo (%lu)", GetLastError());
        GetTempPathW(MAX_PATH, g_exeDir);
        return;
    }
    // Recorta el nombre de archivo para quedarnos con el directorio.
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *slash = L'\0';
    lstrcpynW(g_exeDir, exe, MAX_PATH);

    // %LOCALAPPDATA%\SMAC\WebCore
    wchar_t la[MAX_PATH];
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 32) {
        log::Warn("paths: LOCALAPPDATA no disponible");
        lstrcpynW(g_userDir, g_exeDir, MAX_PATH);
    } else {
        // Crea la jerarquía %LOCALAPPDATA%\SMAC\WebCore (dos niveles).
        wchar_t smacDir[MAX_PATH];
        _snwprintf_s(smacDir, MAX_PATH, _TRUNCATE, L"%s\\SMAC", la);
        CreateDirectoryW(smacDir, nullptr);
        _snwprintf_s(g_userDir, MAX_PATH, _TRUNCATE, L"%s\\WebCore", smacDir);
        CreateDirectoryW(g_userDir, nullptr);
    }

    _snwprintf_s(g_extDir, MAX_PATH, _TRUNCATE, L"%s\\extensions", g_exeDir);

    wchar_t* desk = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk))) {
        lstrcpynW(g_desktop, desk, MAX_PATH);
        CoTaskMemFree(desk);
    }
}

} // namespace

const wchar_t* ExeDir()        { ResolveOnce(); return g_exeDir; }
const wchar_t* UserDir()       { ResolveOnce(); return g_userDir; }
const wchar_t* ExtensionsDir() { ResolveOnce(); return g_extDir; }
const wchar_t* DesktopDir()    { ResolveOnce(); return g_desktop; }

} // namespace smac::paths
