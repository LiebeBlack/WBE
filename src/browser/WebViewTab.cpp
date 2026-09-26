/** ==========================================================================
 *  browser/WebViewTab.cpp — Pestaña: controller, eventos COM, suspensión.
 *  ----------------------------------------------------------------------------
 *  Handlers COM manuales (sin RTTI/excepciones). Los eventos se registran
 *  una vez; el motor retiene la única referencia -> cero gestión manual.
 *  La pestaña NO toca UI: todo sale por TabHost (TabManager -> App).
 *  ========================================================================== */
#include "WebViewTab.hpp"
#include "TabHost.hpp"
#include "smac/Commands.hpp"

#include "smac/Log.hpp"

#include <string>
#include <objbase.h>

namespace smac::web {
namespace {

// ============================ Handlers COM ==================================
// (Los setters NotifyLoading/NotifyAudio son public en el header: los
//  handlers viven en este namespace anónimo y no pueden ser "friend".)

/** Plantilla base: AddRef/Release/QI sobre una interfaz handler. */
template <typename Iface>
class HandlerBase : public Iface {
public:
    ULONG STDMETHODCALLTYPE AddRef() final { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() final {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                             void** out) final {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(Iface)) {
            *out = static_cast<Iface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

private:
    ULONG refs_ = 1;
};

/** NavigationStarting: bloquea smac:// (reservada del host). */
class NavStartHandler final
    : public HandlerBase<ICoreWebView2NavigationStartingEventHandler> {
public:
    NavStartHandler(WebViewTab* tab, TabHost* host) : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender,
        ICoreWebView2NavigationStartingEventArgs* args) override {
        (void)sender; (void)tab_; (void)host_;
        LPWSTR uri = nullptr;
        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
            if (wcsncmp(uri, L"smac://", 7) == 0) {
                args->put_Handled(TRUE);   // el host la resuelve aparte
            }
            CoTaskMemFree(uri);
        }
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** NavigationCompleted: apaga el spinner y marca estado sucio. */
class NavCompletedHandler final
    : public HandlerBase<ICoreWebView2NavigationCompletedEventHandler> {
public:
    NavCompletedHandler(WebViewTab* tab, TabHost* host)
        : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender,
        ICoreWebView2NavigationCompletedEventArgs* args) override {
        (void)sender;
        BOOL ok = FALSE;
        args->get_IsSuccess(&ok);
        tab_->NotifyLoading(false);
        if (host_) host_->OnTabStateDirty(tab_->Id());
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** SourceChanged: la URL cambió (nav clásica o pushState). */
class SourceChangedHandler final
    : public HandlerBase<ICoreWebView2SourceChangedEventHandler> {
public:
    SourceChangedHandler(WebViewTab* tab, TabHost* host)
        : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender,
        ICoreWebView2SourceChangedEventArgs* args) override {
        (void)sender; (void)args;
        if (host_) host_->OnTabStateDirty(tab_->Id());
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** DocumentTitleChanged: título para la pestaña y la barra. */
class DocTitleHandler final
    : public HandlerBase<ICoreWebView2DocumentTitleChangedEventHandler> {
public:
    DocTitleHandler(WebViewTab* tab, TabHost* host) : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender, IUnknown* args) override {
        (void)args;
        LPWSTR title = nullptr;
        if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
            if (host_) host_->OnTabTitle(tab_->Id(), title);
            CoTaskMemFree(title);
        }
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** ContainsFullScreenElementChanged: F11 dentro de vídeos (YouTube). */
class FullScreenHandler final
    : public HandlerBase<ICoreWebView2ContainsFullScreenElementChangedEventHandler> {
public:
    FullScreenHandler(WebViewTab* tab, TabHost* host)
        : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender, IUnknown* args) override {
        (void)sender; (void)args;
        BOOL fs = FALSE;
        sender->get_ContainsFullScreenElement(&fs);
        if (host_) host_->OnTabFullScreen(tab_->Id(), fs != FALSE);
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** IsDocumentPlayingAudioChanged: pausa el trimming + indicador UI. */
class AudioHandler final
    : public HandlerBase<ICoreWebView2IsDocumentPlayingAudioChangedEventHandler> {
public:
    AudioHandler(WebViewTab* tab, TabHost* host) : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender, IUnknown* args) override {
        (void)sender; (void)args;
        smac::ComPtr<ICoreWebView2_4> wv4;
        BOOL audio = FALSE;
        if (SUCCEEDED(sender->QueryInterface(
                __uuidof(ICoreWebView2_4),
                reinterpret_cast<void**>(wv4.GetAddressOf()))) && wv4) {
            wv4->get_IsDocumentPlayingAudio(&audio);
        }
        tab_->NotifyAudio(audio != FALSE);
        if (host_) host_->OnTabAudio(tab_->Id(), audio != FALSE);
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** ZoomFactorChanged: zoom con Ctrl+rueda llega aquí (UI lo refleja). */
class ZoomHandler final
    : public HandlerBase<ICoreWebView2ZoomFactorChangedEventHandler> {
public:
    ZoomHandler(WebViewTab* tab, TabHost* host) : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2Controller* sender, IUnknown* args) override {
        (void)args;
        double z = 1.0;
        sender->get_ZoomFactor(&z);
        if (host_) host_->OnTabZoom(tab_->Id(), z);
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** NewWindowRequested: window.open/target=_blank -> nueva pestaña. */
class NewWindowHandler final
    : public HandlerBase<ICoreWebView2NewWindowRequestedEventHandler> {
public:
    NewWindowHandler(WebViewTab* tab, TabHost* host) : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender,
        ICoreWebView2NewWindowRequestedEventArgs* args) override {
        (void)sender;
        if (host_) host_->OnTabNewWindow(tab_->Id(), args);
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** DownloadStarting: redirige a Descargas y muestra estado en la barra. */
class DownloadHandler final
    : public HandlerBase<ICoreWebView2DownloadStartingEventHandler> {
public:
    DownloadHandler(WebViewTab* tab, TabHost* host) : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender,
        ICoreWebView2DownloadStartingEventArgs* args) override {
        (void)sender;
        if (host_) host_->OnTabDownload(tab_->Id(), args);
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/**
 * AcceleratorKeyPressed: aceleradores capturados DENTRO del motor y
 * traducidos a EngineCmd neutral (browser/ no conoce la UI).
 */
class AccelHandler final
    : public HandlerBase<ICoreWebView2AcceleratorKeyPressedEventHandler> {
public:
    AccelHandler(WebViewTab* tab, TabHost* host) : tab_(tab), host_(host) {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2Controller* sender,
        ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        (void)sender;
        COREWEBVIEW2_KEY_EVENT_KIND kind;
        if (FAILED(args->get_KeyEventKind(&kind))) return S_OK;
        if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
            kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            return S_OK;
        }
        UINT vk = 0;
        if (FAILED(args->get_VirtualKey(&vk))) return S_OK;

        // Los modificadores llegan por GetKeyState: el evento del motor no
        // los descompone (PhysicalKeyStatus solo da la tecla física).
        const bool ctrl   = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift  = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        const bool alt    = (GetKeyState(VK_MENU)    & 0x8000) != 0;

        smac::EngineCmd cmd = smac::EngineCmd::None;

        if (ctrl && !alt) {
            switch (vk) {
                case 'T': cmd = smac::EngineCmd::NewTab; break;
                case 'W': cmd = smac::EngineCmd::CloseTab; break;
                case 'L': cmd = smac::EngineCmd::FocusOmnibox; break;
                case 'F': cmd = smac::EngineCmd::Find; break;
                case 'R': cmd = smac::EngineCmd::Reload; break;
                case VK_TAB:      cmd = shift ? smac::EngineCmd::PrevTab
                                              : smac::EngineCmd::NextTab; break;
                case VK_PRIOR:    cmd = smac::EngineCmd::PrevTab; break;
                case VK_NEXT:     cmd = smac::EngineCmd::NextTab; break;
                case VK_ADD:      cmd = smac::EngineCmd::ZoomIn; break;
                case VK_SUBTRACT: cmd = smac::EngineCmd::ZoomOut; break;
                case '0':         cmd = smac::EngineCmd::ZoomReset; break;
                default:
                    if (vk >= '1' && vk <= '9') {
                        cmd = smac::SelectTabN(vk - '1');
                    }
                    break;
            }
        } else if (alt && !ctrl) {
            switch (vk) {
                case VK_LEFT:  cmd = smac::EngineCmd::Back; break;
                case VK_RIGHT: cmd = smac::EngineCmd::Forward; break;
                default: break;
            }
        } else if (!ctrl && !alt) {
            switch (vk) {
                case VK_F5:  cmd = smac::EngineCmd::Reload; break;
                case VK_F11: cmd = smac::EngineCmd::FullScreen; break;
                default: break;
            }
        }

        if (cmd != smac::EngineCmd::None) {
            args->put_Handled(TRUE);   // el motor no procesa el atajo
            if (host_) host_->OnEngineCommand(cmd, tab_->Id());
        }
        return S_OK;
    }

private:
    WebViewTab* tab_;
    TabHost* host_;
};

/** Handler de TrySuspend: corrige el flag optimista. */
class SuspendHandler final
    : public HandlerBase<ICoreWebView2TrySuspendCompletedHandler> {
public:
    SuspendHandler(int id, bool* flagOut) : id_(id), flagOut_(flagOut) {}

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,
                                     BOOL isSuccessful) override {
        const bool ok = SUCCEEDED(errorCode) && isSuccessful;
        if (flagOut_) *flagOut_ = ok;
        if (!ok) {
            log::Warn("tab %d: TrySuspend no disponible ahora (0x%08X)",
                      id_, errorCode);
        } else {
            log::Info("tab %d: suspendida (RAM devuelta al OS)", id_);
        }
        delete this;
        return S_OK;
    }

private:
    int id_;
    bool* flagOut_;
};

} // namespace (handlers COM)

// ============================ Ciclo de vida =================================

WebViewTab::~WebViewTab() {
    Close();
}

bool WebViewTab::AttachController(
    const smac::ComPtr<ICoreWebView2Controller>& controller) {
    controller_ = controller;
    if (!controller_) return false;

    HRESULT hr = controller_->get_CoreWebView2(
        webview_.GetAddressOf());
    if (FAILED(hr) || !webview_) {
        log::Error("tab: get_CoreWebView2 fallo (0x%08X)", hr);
        return false;
    }

    // --- Settings hostiles a RAM/telemetría --------------------------------
    if (SUCCEEDED(webview_->get_Settings(settings_.GetAddressOf())) &&
        settings_) {
        settings_->put_IsStatusBarEnabled(FALSE);       // sin barra de estado
        settings_->put_AreDevToolsEnabled(FALSE);       // sin DevTools (RAM)
        settings_->put_AreDefaultContextMenusEnabled(TRUE); // menú del motor
        settings_->put_AreDefaultScriptDialogsEnabled(TRUE);
        settings_->put_IsZoomControlEnabled(TRUE);      // Ctrl+rueda nativo
        settings_->put_IsBuiltInErrorPageEnabled(TRUE);
        settings_->put_IsPasswordAutosaveEnabled(FALSE); // sin gestor (RAM)
        settings_->put_IsGeneralAutofillEnabled(FALSE);  // sin autofill
        settings_->put_IsSwipeNavigationEnabled(FALSE);  // sin gestos táctiles
    }

    // Fondo negro desde el primer frame (sin flash blanco).
    smac::ComPtr<ICoreWebView2Controller2> c2;
    if (SUCCEEDED(controller_.As(&c2)) && c2) {
        COREWEBVIEW2_COLOR black{0, 0, 0, 0};
        c2->put_DefaultBackgroundColor(black);
    }

    RegisterEvents();

    // La pestaña nace oculta: el TabManager decide visibilidad.
    controller_->put_IsVisible(FALSE);
    loading_ = false;
    hasAudio_ = false;
    suspended_ = false;
    return true;
}

void WebViewTab::RegisterEvents() {
    if (!webview_ || !host_) return;

    webview_->add_NavigationStarting(new NavStartHandler(this, host_));
    webview_->add_NavigationCompleted(new NavCompletedHandler(this, host_));
    webview_->add_SourceChanged(new SourceChangedHandler(this, host_));
    webview_->add_DocumentTitleChanged(new DocTitleHandler(this, host_));
    webview_->add_ContainsFullScreenElementChanged(
        new FullScreenHandler(this, host_));
    webview_->add_NewWindowRequested(new NewWindowHandler(this, host_));
    webview_->add_DownloadStarting(new DownloadHandler(this, host_));

    // Audio: ICoreWebView2_4.
    smac::ComPtr<ICoreWebView2_4> wv4;
    if (SUCCEEDED(webview_.As(&wv4)) && wv4) {
        wv4->add_IsDocumentPlayingAudioChanged(new AudioHandler(this, host_));
    }

    // Zoom y aceleradores: controller.
    controller_->add_ZoomFactorChanged(new ZoomHandler(this, host_));
    controller_->add_AcceleratorKeyPressed(new AccelHandler(this, host_));
}

void WebViewTab::Close() {
    if (controller_) {
        controller_->Close();
        controller_.Release();
    }
    webview_.Release();
    settings_.Release();
}

// ============================ Geometría / visibilidad =======================

void WebViewTab::SetBounds(RECT rc) {
    if (controller_) controller_->put_Bounds(rc);
}

void WebViewTab::SetVisible(bool visible) {
    if (controller_) {
        controller_->put_IsVisible(visible ? TRUE : FALSE);
        // Triage de RAM: pestaña oculta => renderer dormido por Chromium;
        // TrySuspend lo convierte en suspensión real (Sleeping Tabs).
    }
}

// ============================ Navegación ====================================

void WebViewTab::Navigate(const wchar_t* url) {
    if (webview_ && url && *url) {
        suspended_ = false;   // navegar despierta al renderer
        webview_->Navigate(url);
    }
}

void WebViewTab::GoBack() {
    BOOL can = FALSE;
    if (webview_ && SUCCEEDED(webview_->get_CanGoBack(&can)) && can) {
        webview_->GoBack();
    }
}

void WebViewTab::GoForward() {
    BOOL can = FALSE;
    if (webview_ && SUCCEEDED(webview_->get_CanGoForward(&can)) && can) {
        webview_->GoForward();
    }
}

void WebViewTab::Reload() { if (webview_) webview_->Reload(); }
void WebViewTab::Stop()   { if (webview_) webview_->Stop(); }

void WebViewTab::SetZoom(double f) {
    if (controller_) controller_->put_ZoomFactor(f);
}

void WebViewTab::ExecuteScript(const wchar_t* js) {
    if (webview_ && js && *js) webview_->ExecuteScript(js, nullptr);
}

void WebViewTab::FindInPage(const wchar_t* text, bool forward, bool fresh) {
    if (!webview_ || !text || !*text) return;
    // JSON-escape mínimo (comillas y backslash) para no romper el script.
    wchar_t safe[256];
    size_t n = 0;
    for (const wchar_t* p = text; *p && n < 250; ++p) {
        if (*p == L'\\' || *p == L'"') safe[n++] = L'\\';
        safe[n++] = *p;
    }
    safe[n] = 0;

    wchar_t cmd[640];
    _snwprintf_s(cmd, 640, _TRUNCATE,
                 L"window.find(\"%s\", false, %s, true, false, false, false);",
                 safe, forward ? L"false" : L"true");
    (void)fresh;
    webview_->ExecuteScript(cmd, nullptr);
}

// ============================ Suspensión ====================================

bool WebViewTab::TrySuspend() {
    if (suspended_ || !webview_) return false;
    if (hasAudio_) return false;   // nunca con audio (lo valida el manager)
    smac::ComPtr<ICoreWebView2_3> wv3;
    if (FAILED(webview_.As(&wv3)) || !wv3) return false;
    suspended_ = true;   // optimista; el handler confirma/corrige
    wv3->TrySuspend(new SuspendHandler(id_, &suspended_));
    return true;
}

bool WebViewTab::TryResume() {
    if (!suspended_ || !webview_) return false;
    smac::ComPtr<ICoreWebView2_3> wv3;
    if (FAILED(webview_.As(&wv3)) || !wv3) return false;
    wv3->Resume();
    suspended_ = false;
    return true;
}

// ============================ Estado ========================================

WebViewTab::State WebViewTab::GetState() const {
    State s;
    if (webview_) {
        LPWSTR title = nullptr, uri = nullptr;
        if (SUCCEEDED(webview_->get_DocumentTitle(&title)) && title) {
            s.title = title;
            CoTaskMemFree(title);
        }
        if (SUCCEEDED(webview_->get_Source(&uri)) && uri) {
            s.url = uri;
            CoTaskMemFree(uri);
        }
        BOOL b = FALSE;
        if (SUCCEEDED(webview_->get_CanGoBack(&b)))   s.canGoBack = b != FALSE;
        if (SUCCEEDED(webview_->get_CanGoForward(&b)))
            s.canGoForward = b != FALSE;
    }
    if (controller_) {
        controller_->get_ZoomFactor(&s.zoom);
    }
    s.loading   = loading_;
    s.hasAudio  = hasAudio_;
    s.suspended = suspended_;
    return s;
}

} // namespace smac::web
