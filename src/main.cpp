/** ==========================================================================
 *  main.cpp — Punto de entrada (Win32, sin consola).
 *  ----------------------------------------------------------------------------
 *  wWinMain mínimo: delega TODO en app::App (orquestador). El trabajo de
 *  fondo (GPU, red, render) corre en procesos del motor WebView2, no aquí.
 *  ========================================================================== */
#include "app/App.hpp"
#include "system/MemoryOptimizer.hpp"
#include "smac/Log.hpp"
#include "smac/Version.hpp"

#include <windows.h>
#include <shellapi.h>

/** Evita que Windows muestre el diálogo de "dejó de funcionar": morimos en
 *  silencio (minimalismo también en el fallo). */
static LONG WINAPI SmacSEHFilter(EXCEPTION_POINTERS*) {
    return EXCEPTION_EXECUTE_HANDLER;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Una sola instancia: el segundo lanzamiento no abre otro motor.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"SMAC_WebCore_SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"SMAC_WebCore_Host", nullptr);
        if (prev) {
            if (IsIconic(prev)) ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        CloseHandle(mutex);
        return 0;
    }

    // El allocator global (mimalloc) se instala ANTES de cualquier new.
    smac::mem::InstallAllocator();
    smac::mem::ConfigureAllocator();

    // App vive FUERA de __try: el SEH no admite objetos que requieran
    // desenrollado en el bloque protegido.
    smac::app::App app;
    __try {
        smac::log::Info("boot %s", SMAC_VERSION_A);
        return app.Run(hInstance, nCmdShow);
    } __except (SmacSEHFilter(GetExceptionInformation())) {
        smac::log::Error("crash SEH no gestionado");
        return 42;
    }
}
