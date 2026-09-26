/** ==========================================================================
 *  smac/Config.hpp — Estructura de configuración compartida (POD plano).
 *  ----------------------------------------------------------------------------
 *  Se rellena desde smac.ini en Config.cpp (módulo config/) y se consulta
 *  por referencia constante desde window/, browser/ y system/. SRP: solo
 *  declara datos; la carga y la persistencia viven en su módulo.
 *  ========================================================================== */
#pragma once

#include <windows.h>

namespace smac {

/** Identidades de las páginas internas (URL ficticias smac://). */
enum class InternalPage {
    Home,
    NewTab,
    Blank,
    Error,
    About,
};

/** Colores permitidos para el borde DWM de 1 px. */
enum class AccentColor {
    Cyan,     ///< #00E5FF (por defecto)
    Purple,   ///< #B026FF
    Green,    ///< #39FF14
    Off,      ///< Sin borde (color de sistema)
};

/** Configuración completa del navegador. POD: cero asignaciones. */
struct Config {
    // --- Ventana -----------------------------------------------------------
    int  windowX      = CW_USEDEFAULT;
    int  windowY      = CW_USEDEFAULT;
    int  windowW      = 1200;
    int  windowH      = 800;
    bool maximizeStart = false;
    AccentColor accent = AccentColor::Cyan;

    // --- Motor -------------------------------------------------------------
    bool telemetryOff       = true;   ///< Apaga métricas de Chromium
    bool syncOff            = true;   ///< Apaga sincronización
    bool enableVulkan       = true;   ///< --enable-features=Vulkan
    int  v8HeapMB           = 350;    ///< --max_old_space_size
    int  rendererLimit      = 2;      ///< --renderer-process-limit
    wchar_t searchEngine[64] = L"https://duckduckgo.com/?q=%s";
    wchar_t homepage[256]    = L"smac://newtab";

    // --- Optimizador (system/) --------------------------------------------
    int  trimIdleSeconds   = 30;   ///< Inactividad para paginar a swap
    int  trimTimerMS       = 5000; ///< Periodo del temporizador Win32
    int  maxAliveTabs      = 8;    ///< Cap de pestañas vivas (LRU)
    bool childrenBelowNormal = true; ///< Hijos del motor a BELOW_NORMAL
};

/** Convierte texto del ini a enum (cyan|purple|green|off). */
AccentColor ParseAccent(const wchar_t* text);

/** Devuelve el COLORREF DWM del acento configurado. */
COLORREF AccentColorRef(AccentColor c);

} // namespace smac
