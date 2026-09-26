/** ==========================================================================
 *  browser/TabManager.cpp — Pestañas: cap LRU, suspensión, sesión.
 *  ========================================================================== */
#include "TabManager.hpp"
#include "WebViewTab.hpp"
#include "ResourceBlocker.hpp"

#include "smac/Log.hpp"
#include "system/Paths.hpp"
#include "system/MemoryOptimizer.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <memory>
#include <string>
#include <vector>

namespace smac::web {

// ============================ Handlers de creación ==========================
namespace {

/** Handler de CreateCoreWebView2Controller -> entrega la pestaña. */
class ControllerCreatedHandler final
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    ControllerCreatedHandler() = default;

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown ||
            riid == __uuidof(
                ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *out = static_cast<
                ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(
                this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Invoke(
        HRESULT errorCode,
        ICoreWebView2Controller* controller) override {
        hr_ = errorCode;
        controller_ = controller;   // retiene (AddRef) para SpawnTab
        if (done_) SetEvent(done_);
        return S_OK;
    }

    HANDLE done_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT hr_ = E_FAIL;
    smac::ComPtr<ICoreWebView2Controller> controller_;

private:
    ULONG refs_ = 1;
};

} // namespace

// ============================ Ciclo de vida =================================

TabManager::~TabManager() {
    SaveSession();
    tabs_.clear();
}

void TabManager::Init(const smac::ComPtr<ICoreWebView2Environment>& env,
                      smac::ComPtr<ICoreWebView2Profile> profile,
                      Delegate* delegate,
                      const smac::Config& cfg) {
    env_ = env;
    profile_ = profile;
    delegate_ = delegate;
    cfg_ = cfg;
    sessionPath_ = std::wstring(paths::UserDir()) + L"\\" +
                   paths::kSessionFile;
    log::Info("tabs: manager listo (cap %d)", cfg_.maxAliveTabs);
}

// ============================ Creación/cierre ===============================

WebViewTab* TabManager::CreateTab(const wchar_t* url, bool makeActive) {
    Tab* t = SpawnTab(url, makeActive);
    return t ? t->view.get() : nullptr;
}

TabManager::Tab* TabManager::SpawnTab(const wchar_t* url, bool makeActive) {
    if (!env_ || !delegate_) return nullptr;

    Tab t;
    t.id = nextId_++;
    t.view = std::make_unique<WebViewTab>();
    t.view->SetId(t.id);
    t.view->SetHost(this);

    auto* handler = new ControllerCreatedHandler();
    const HRESULT hr = env_->CreateCoreWebView2Controller(
        delegate_->ContentHwnd(), handler);
    if (FAILED(hr)) {
        log::Error("tabs: CreateCoreWebView2Controller fallo (0x%08X)", hr);
        CloseHandle(handler->done_);
        handler->Release();
        return nullptr;
    }

    // Bomba restringida hasta el controller (creación asíncrona). Se hace
    // aquí porque es política del manager: bloquear solo la pestaña en
    // construcción, no toda la app.
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (WaitForSingleObject(handler->done_, 25) == WAIT_OBJECT_0) break;
    }
    CloseHandle(handler->done_);

    if (FAILED(handler->hr_) || !handler->controller_) {
        log::Error("tabs: controller de pestaña %d fallo (0x%08X)",
                   t.id, handler->hr_);
        handler->Release();
        return nullptr;
    }
    if (!t.view->AttachController(handler->controller_)) {
        log::Error("tabs: pestaña %d sin controller", t.id);
        handler->Release();
        return nullptr;
    }
    handler->Release();

    // La pestaña nueva entra al vector ANTES de tocarla (evita use-after-move).
    if (makeActive && activeIndex_ >= 0 && activeIndex_ < Count()) {
        tabs_[activeIndex_].view->SetVisible(false);
    }
    tabs_.push_back(std::move(t));
    if (makeActive) activeIndex_ = Count() - 1;

    tabs_.back().view->SetBounds(contentBounds_);
    tabs_.back().view->SetVisible(makeActive);

    EnforceAliveCap();

    if (url && *url) tabs_.back().view->Navigate(url);
    if (delegate_) delegate_->OnTabsChanged();
    return &tabs_.back();
}

void TabManager::CloseTab(int index) {
    if (index < 0 || index >= Count()) return;

    const bool wasActive = (index == activeIndex_);
    const bool hadAudio  = tabs_[index].view->GetState().hasAudio;
    if (hadAudio) {
        mem::SetAudioActive(false);   // por si era la del audio
    }

    tabs_.erase(tabs_.begin() + index);

    if (Count() == 0) {
        activeIndex_ = -1;
    } else if (wasActive) {
        activeIndex_ = index > 0 ? index - 1 : 0;
        tabs_[activeIndex_].view->SetVisible(true);
    } else if (index < activeIndex_) {
        --activeIndex_;
    }

    if (delegate_) delegate_->OnTabsChanged();
}

void TabManager::SelectTab(int index) {
    if (index < 0 || index >= Count() || index == activeIndex_) return;
    if (activeIndex_ >= 0) tabs_[activeIndex_].view->SetVisible(false);
    activeIndex_ = index;
    tabs_[activeIndex_].view->SetVisible(true);
    if (delegate_) {
        delegate_->OnTabsChanged();
        delegate_->OnActiveTabState();
    }
}

void TabManager::CycleTab(int direction) {
    if (Count() <= 1) return;
    int next = activeIndex_ + direction;
    if (next < 0) next = Count() - 1;
    if (next >= Count()) next = 0;
    SelectTab(next);
}

void TabManager::SelectIndex(int index) {
    if (index >= 0 && index < Count()) SelectTab(index);
}

// ============================ Cap LRU =======================================

void TabManager::EnforceAliveCap() {
    const int cap = cfg_.maxAliveTabs;
    if (cap <= 0 || Count() <= cap) return;

    switch (capPolicy_) {
        case CapOverflow::Allow:
            return;

        case CapOverflow::CloseOldest: {
            // Cierra la más vieja no-activa hasta el cap.
            int victim = 0;
            while (Count() > cap && Count() > 1) {
                if (victim == activeIndex_) ++victim;
                if (victim >= Count()) break;
                CloseTab(victim);
            }
            if (Count() > cap && delegate_) {
                delegate_->OnMaxAliveTabsExceeded();
            }
            return;
        }

        case CapOverflow::SuspendOldest:
        default: {
            // Suspende la más vieja no-audio y no-activa hasta el cap.
            int scanned = 0;
            while (Count() > cap && scanned < Count()) {
                const int idx = scanned;
                if (idx == activeIndex_) { ++scanned; continue; }
                Tab& t = tabs_[idx];
                if (!t.suspended && !t.view->GetState().hasAudio) {
                    t.view->TrySuspend();
                    t.suspended = true;
                }
                ++scanned;
            }
            if (Count() > cap + 4 && delegate_) {
                delegate_->OnMaxAliveTabsExceeded();
            }
            return;
        }
    }
}

void TabManager::SuspendBackground() {
    for (int i = 0; i < Count(); ++i) {
        if (i == activeIndex_) continue;
        Tab& t = tabs_[i];
        if (!t.suspended && !t.view->GetState().hasAudio) {
            if (t.view->TrySuspend()) t.suspended = true;
        }
    }
}

// ============================ Bounds ========================================

void TabManager::SetContentBounds(RECT rc) {
    contentBounds_ = rc;
    if (activeIndex_ >= 0 && tabs_[activeIndex_].view) {
        tabs_[activeIndex_].view->SetBounds(rc);
    }
}

// ============================ TabHost =======================================

TabManager::Tab* TabManager::FindById(int id) {
    for (auto& t : tabs_) if (t.id == id) return &t;
    return nullptr;
}

int TabManager::ActiveId() const {
    return (activeIndex_ >= 0 && activeIndex_ < Count())
        ? tabs_[activeIndex_].id : -1;
}

int TabManager::IndexById(int id) const {
    for (int i = 0; i < Count(); ++i) if (tabs_[i].id == id) return i;
    return -1;
}

int TabManager::IndexOfId(int id) const { return IndexById(id); }

WebViewTab* TabManager::At(int index) const {
    return (index >= 0 && index < Count()) ? tabs_[index].view.get() : nullptr;
}

WebViewTab* TabManager::Active() const { return At(activeIndex_); }

int TabManager::ActiveIndex() const { return activeIndex_; }

void TabManager::PushActiveState() {
    if (delegate_) delegate_->OnActiveTabState();
}

void TabManager::OnTabNavigated(int tabId, const wchar_t* url,
                                bool isLoading) {
    if (!isLoading && url && delegate_) {
        delegate_->OnHistoryCandidate(url);   // fin de navegación
    }
    if (Tab* t = FindById(tabId)) {
        // Despierta: navegación explícita invalida suspensión.
        t.suspended = false;
    }
    if (tabId == ActiveId()) PushActiveState();
}

void TabManager::OnTabTitle(int tabId, const wchar_t* title) {
    (void)title;
    if (delegate_) delegate_->OnTabsChanged();
}

void TabManager::OnTabAudio(int tabId, bool audible) {
    if (Tab* t = FindById(tabId)) {
        t.suspended = false;
    }
    // El trim global se pausa si CUALQUIER pestaña suena.
    bool anyAudio = false;
    for (const auto& t : tabs_) {
        if (t.view->GetState().hasAudio) { anyAudio = true; break; }
    }
    mem::SetAudioActive(anyAudio);
    if (delegate_) delegate_->OnTabsChanged();
}

void TabManager::OnTabZoom(int, double) { PushActiveState(); }

void TabManager::OnTabFullScreen(int, bool fullScreen) {
    if (delegate_) delegate_->OnFullScreen(fullScreen);
}

void TabManager::OnTabNewWindow(int tabId,
    ICoreWebView2NewWindowRequestedEventArgs* args) {
    // Política: toda ventana nueva es una pestaña de S.M.A.C.
    LPWSTR uri = nullptr;
    if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
        CreateTab(uri, true);
        CoTaskMemFree(uri);
    }
    args->put_Handled(TRUE);
    (void)tabId;
}

void TabManager::OnTabDownload(int tabId,
    ICoreWebView2DownloadStartingEventArgs* args) {
    (void)tabId;
    // Redirige a la carpeta Descargas real del perfil (KNOWNFOLDERID).
    wchar_t* downloads = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr,
                                       &downloads))) {
        args->put_ResultFilePath(downloads);
        CoTaskMemFree(downloads);
    }
    args->put_Handled(FALSE);   // el motor gestiona la transferencia.

    // Nombre amigable para el aviso de la barra (cola de la URI).
    smac::ComPtr<ICoreWebView2DownloadOperation> op;
    if (SUCCEEDED(args->get_DownloadOperation(op.GetAddressOf())) && op) {
        LPWSTR uri = nullptr;
        if (SUCCEEDED(op->get_Uri(&uri)) && uri) {
            const wchar_t* slash = wcsrchr(uri, L'/');
            if (delegate_) {
                delegate_->OnDownloadStarted(
                    slash ? slash + 1 : L"archivo");
            }
            CoTaskMemFree(uri);
        }
    }
}

void TabManager::OnTabStateDirty(int) { PushActiveState(); }

/**
 * Comando de teclado capturado dentro del motor: lo ejecuta el manager
 * directamente (navegación/pestañas) o lo escala a la App (UI, zoom).
 */
void TabManager::OnEngineCommand(smac::EngineCmd cmd, int tabId) {
    using EC = smac::EngineCmd;
    switch (cmd) {
        // --- Pestañas: política del manager -------------------------------
        case EC::NextTab:  CycleTab(+1); return;
        case EC::PrevTab:  CycleTab(-1); return;
        case EC::NewTab:   CreateTab(cfg_.homepage, true); return;
        case EC::CloseTab: {
            const int i = IndexById(tabId);
            if (i >= 0) CloseTab(i);
            return;
        }
        default: {
            // Ctrl+1..9: SelectTab0+n (n==8 -> última pestaña).
            const int c  = static_cast<int>(cmd);
            const int s0 = static_cast<int>(EC::SelectTab0);
            if (c >= s0 && c <= s0 + 8) {
                const int n = c - s0;
                SelectIndex(n >= Count() - 1 ? Count() - 1 : n);
            }
            return;
        }

        // --- El resto los ejecuta la App (UI global del shell) ------------
        default:
            if (delegate_) delegate_->OnEngineCommand(cmd);
            return;
    }
}

// ============================ Sesión ========================================

bool TabManager::SaveSession() {
    if (sessionPath_.empty()) return false;
    HANDLE f = CreateFileW(sessionPath_.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    char line[2100];
    DWORD written = 0;
    for (const auto& t : tabs_) {
        if (!t.view) continue;
        const auto st = t.view->GetState();
        if (st.url.empty()) continue;
        const int n = WideCharToMultiByte(
            CP_UTF8, 0, st.url.c_str(), -1, line, 2048, nullptr, nullptr);
        if (n > 1) {
            line[n - 1] = '\n';   // sustituye el NUL por salto de línea
            WriteFile(f, line, static_cast<DWORD>(n), &written, nullptr);
        }
    }
    CloseHandle(f);
    return true;
}

bool TabManager::RestoreSession(const wchar_t* homepageFallback) {
    if (sessionPath_.empty()) return false;
    HANDLE f = CreateFileW(sessionPath_.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        CreateTab(homepageFallback, true);
        return false;
    }

    // Sesión simple: una URL UTF-8 por línea (archivo pequeño por diseño).
    char raw[8192];
    DWORD read = 0;
    bool restored = false;
    if (ReadFile(f, raw, sizeof(raw) - 1, &read, nullptr) && read > 0) {
        raw[read] = '\0';
        char* ctx = nullptr;
        char* tok = strtok_s(raw, "\r\n", &ctx);
        int budget = cfg_.maxAliveTabs > 0 ? cfg_.maxAliveTabs : 8;
        while (tok && budget-- > 0) {
            wchar_t url[2048];
            if (MultiByteToWideChar(CP_UTF8, 0, tok, -1, url, 2048) > 0) {
                CreateTab(url, !restored);   // la primera es la activa
                restored = true;
            }
            tok = strtok_s(nullptr, "\r\n", &ctx);
        }
    }
    CloseHandle(f);
    if (!restored) CreateTab(homepageFallback, true);
    return restored;
}

// ============================ uBlock ========================================

void TabManager::InstallUblockOrigin() {
    if (InstallUblockOrigin(env_, profile_)) {
        log::Info("tabs: uBlock despachada al perfil");
    }
}

} // namespace smac::web
