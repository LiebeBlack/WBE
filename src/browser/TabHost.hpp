/** ==========================================================================
 *  browser/TabHost.hpp — Callbacks de eventos de pestaña (browser/).
 *  ----------------------------------------------------------------------------
 *  Los handlers COM de WebViewTab reportan aquí; TabManager implementa la
 *  interfaz y redirige a la App. Todo llega en el hilo UI.
 *  ========================================================================== */
#pragma once

#include <windows.h>
#include <WebView2.h>

namespace smac { enum class EngineCmd : int; }

namespace smac::web {

/** Eventos de negocio que una pestaña reporta (hilo UI siempre). */
class TabHost {
public:
    virtual ~TabHost() = default;

    /** Navegación empezada/terminada (spinner + estado de botones). */
    virtual void OnTabNavigated(int tabId, const wchar_t* url,
                                bool isLoading) = 0;
    /** Título del documento cambió (tira de pestañas). */
    virtual void OnTabTitle(int tabId, const wchar_t* title) = 0;
    /** Audio empezó/terminó (pausa el trim + icono). */
    virtual void OnTabAudio(int tabId, bool audible) = 0;
    /** Zoom cambió (Ctrl+rueda del motor). */
    virtual void OnTabZoom(int tabId, double zoom) = 0;
    /** Fullscreen dentro del contenido (vídeo YouTube). */
    virtual void OnTabFullScreen(int tabId, bool fullScreen) = 0;
    /** window.open / target=_blank: la App decide (nueva pestaña). */
    virtual void OnTabNewWindow(int tabId,
                                ICoreWebView2NewWindowRequestedEventArgs* args) = 0;
    /** Descarga iniciada: redirigir a Descargas y avisar en la barra. */
    virtual void OnTabDownload(int tabId,
                               ICoreWebView2DownloadStartingEventArgs* args) = 0;
    /** Estado sucio (URL/back/forward): repintar la barra barato. */
    virtual void OnTabStateDirty(int tabId) = 0;

    /** Acelerador capturado dentro del motor -> comando neutral. */
    virtual void OnEngineCommand(smac::EngineCmd cmd, int tabId) = 0;
};

} // namespace smac::web
