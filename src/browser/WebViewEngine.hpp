/** ==========================================================================
 *  browser/WebViewEngine.hpp — Entorno Chromium (módulo browser/).
 *  ----------------------------------------------------------------------------
 *  Responsabilidad única: crear ICoreWebView2Environment con las opciones
 *  hostiles a RAM/telemetría. No toca ventanas ni pestañas.
 *  ========================================================================== */
#pragma once

#include "smac/ComPtr.hpp"
#include "smac/Config.hpp"

#include <windows.h>
#include <WebView2.h>

namespace smac::web {

/**
 * Crea el entorno del motor. Bloquea con una bomba de mensajes interna
 * (CreateCoreWebView2EnvironmentWithOptions es asíncrono).
 * Devuelve nullptr si falla (log con el HRESULT).
 */
smac::ComPtr<ICoreWebView2Environment> CreateEnvironment(
    const smac::Config& cfg);

/** Texto de error legible (para el log y la página de error interna). */
const wchar_t* LastEngineError();

} // namespace smac::web
