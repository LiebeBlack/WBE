/** ==========================================================================
 *  browser/WebViewTab.hpp — Pestaña individual (módulo browser/).
 *  ----------------------------------------------------------------------------
 *  SRP: encapsula UNA pestaña (controller + webview + eventos). No conoce
 *  otras pestañas ni pinta UI: reporta a TabHost y el TabManager orquesta.
 *  Los handlers COM se registran una vez y viven mientras el webview viva
 *  (el motor posee la única referencia tras el registro: cero leaks).
 *  ========================================================================== */
#pragma once

#include "smac/ComPtr.hpp"

#include <windows.h>
#include <WebView2.h>
#include <string>

namespace smac::web {

class TabHost;

/** Una pestaña = un ICoreWebView2Controller completo. */
class WebViewTab {
public:
    /** Estado vivo (para la tira de pestañas, nav y trimming). */
    struct State {
        std::wstring title;
        std::wstring url;
        bool    loading      = false;
        bool    canGoBack    = false;
        bool    canGoForward = false;
        bool    hasAudio     = false;
        bool    suspended    = false;
        bool    fullScreen   = false;
        double  zoom         = 1.0;
    };

    WebViewTab() = default;
    ~WebViewTab();

    WebViewTab(const WebViewTab&) = delete;
    WebViewTab& operator=(const WebViewTab&) = delete;

    /** Identificador estable (índice lógico para UI y logs). */
    int  Id() const { return id_; }
    void SetId(int id) { id_ = id; }

    /** Puente de eventos (lo fija el TabManager). */
    void SetHost(TabHost* host) { host_ = host; }

    /**
     * Toma posesión del controller creado por el entorno (asíncrono),
     * obtiene el webview, aplica Settings y registra los eventos.
     */
    bool AttachController(
        const smac::ComPtr<ICoreWebView2Controller>& controller);

    /** Geometría (el host la llama en cada layout). */
    void SetBounds(RECT rc);

    /** Mostrar/ocultar (pestaña activa/inactiva). */
    void SetVisible(bool visible);

    /** Navegación y utilidades. */
    void Navigate(const wchar_t* url);
    void GoBack();
    void GoForward();
    void Reload();
    void Stop();
    void SetZoom(double factor);

    /** Suspensión (Sleeping Tabs): devuelve RAM al OS. Resume() la despierta. */
    bool TrySuspend();
    bool TryResume();
    bool IsSuspended() const { return suspended_; }

    /** Búsqueda en página (window.find). */
    void FindInPage(const wchar_t* text, bool forward, bool fresh);

    /** Ejecuta JS arbitrario (fire-and-forget). */
    void ExecuteScript(const wchar_t* js);

    /** Cierra el controller (libera renderer; el resto lo hace Release). */
    void Close();

    /** Captura del estado actual (hilo UI). */
    State GetState() const;

    bool IsValid() const { return controller_ && webview_; }

    /** Setters internos (los usan los handlers COM del .cpp). */
    void NotifyLoading(bool loading) { loading_ = loading; }
    void NotifyAudio(bool audio)     { hasAudio_ = audio; }

private:
    /** Registra todos los eventos del webview y del controller. */
    void RegisterEvents();

    int  id_ = -1;
    TabHost* host_ = nullptr;

    smac::ComPtr<ICoreWebView2Controller> controller_;
    smac::ComPtr<ICoreWebView2>           webview_;
    smac::ComPtr<ICoreWebView2Settings>   settings_;

    bool loading_    = false;
    bool hasAudio_   = false;
    bool suspended_  = false;
};

} // namespace smac::web
