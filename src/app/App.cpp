/** ==========================================================================
 *  app/App.cpp — Orquestación: fases de arranque y lógica de negocio.
 *  ----------------------------------------------------------------------------
 *  Reglas de la casa:
 *   - Ningún módulo inferior conoce a la App (solo puentes/interfaces).
 *   - Las decisiones viven aquí: navegar URL vs búsqueda, cap de pestañas,
 *     avisos de la barra, persistencia de sesión y geometría de ventana.
 *  ========================================================================== */
#include "App.hpp"

#include "window/ChromeBar.hpp"
#include "window/TabStrip.hpp"
#include "window/Omnibox.hpp"
#include "window/MenuBuilder.hpp"
#include "browser/WebViewEngine.hpp"
#include "browser/WebViewTab.hpp"
#include "browser/ResourceBlocker.hpp"
#include "config/Config.hpp"
#include "system/Paths.hpp"
#include "system/MemoryOptimizer.hpp"
#include "system/ProcessPriority.hpp"
#include "smac/Commands.hpp"
#include "smac/Log.hpp"
#include "smac/Version.hpp"

#include <shellapi.h>
#include <fstream>
#include <stdio.h>

namespace smac::app {
namespace {

constexpr int kStatusSeconds = 4;   ///< Duración del aviso de la barra

/** Página de nueva pestaña (interna, sin red: fondo negro con título). */
constexpr wchar_t kNewTabPage[] = L"smac://newtab";

} // namespace

// ============================ Ciclo de vida =================================

App::~App() {
    delete tabs_;
    delete omnibox_;
    delete strip_;
    delete chrome_;
    delete host_;
}

int App::Run(HINSTANCE hInstance, int /*nCmdShow*/) {
    hInstance_ = hInstance;

    // --- Fase 1: configuración, logging y stores de usuario ----------------
    if (!LoadConfig()) return 1;
    LoadUserStores();

    // --- Fase 2: shell Win32 -------------------------------------------------
    if (!CreateChrome()) return 2;

    // --- Fase 3: optimizaciones del host (system/) --------------------------
    ApplyHostOptimizations();

    // --- Fase 4: motor --------------------------------------------------------
    if (!CreateEngine()) {
        MessageBoxW(host_->Hwnd(),
                    L"No se pudo iniciar el motor WebView2.\n"
                    L"Instala \x00ABWebView2 Runtime (Evergreen)\x00BB y reintenta.",
                    SMAC_PRODUCT_NAME_W, MB_ICONERROR);
        return 3;
    }

    // --- Fase 5: contenido inicial (sesión o homepage) ----------------------
    CreateMainWindowContent();

    // --- Fase 6: bucle de mensajes (vive en WindowHost) ---------------------
    const int exitCode = host_->RunMessageLoop();

    // --- Fase 7: salida limpia (la persistencia ya ocurrió en OnCloseRequest)
    log::ShutdownFileSink();
    return exitCode;
}

// ============================ Fases =========================================

bool App::LoadConfig() {
    log::InitFileSink();
    config::Load(&cfg_);
    return true;
}

bool App::CreateChrome() {
    chrome_  = new window::ChromeBar();
    strip_   = new window::TabStrip();
    omnibox_ = new window::Omnibox();
    host_    = new window::WindowHost();

    if (!host_->Create(hInstance_, cfg_, this, chrome_, strip_, omnibox_)) {
        return false;
    }
    if (!chrome_->Create(host_->Hwnd(), hInstance_, omnibox_) ||
        !strip_->Create(host_->Hwnd(), hInstance_)) {
        return false;
    }
    if (!omnibox_->Create(host_->Hwnd(), hInstance_)) return false;

    // Caption buttons nativos (min/max/close) gestionados por la barra.
    chrome_->EnableCaptionButtons(true);

    // Todos los hijos existen: layout completo (posiciones finales).
    host_->ForceLayout();
    return true;
}

void App::ApplyHostOptimizations() {
    prio::ElevateHostProcess();

    mem::Params p;
    p.trimIdleSeconds = cfg_.trimIdleSeconds;
    p.trimChildren    = true;
    mem::StartTrimTimer(host_->Hwnd(), p);
}

bool App::CreateEngine() {
    auto env = web::CreateEnvironment(cfg_);
    if (!env) {
        log::Error("app: entorno del motor fallo: %ls", web::LastEngineError());
        return false;
    }

    // Perfil (para uBO): ICoreWebView2_13::get_Profile.
    smac::ComPtr<ICoreWebView2_13> wv13;
    smac::ComPtr<ICoreWebView2>    probe;
    if (SUCCEEDED(env.As(&probe)) && probe &&
        SUCCEEDED(probe->get_Profile(profile_.GetAddressOf()))) {
        // perfil capturado
    } else {
        log::Warn("app: entorno sin perfil (motor viejo): sin uBO");
    }

    tabs_ = new web::TabManager();
    tabs_->Init(env, profile_, this, cfg_);

    // uBlock Origin + cirugía DOM (ResourceBlocker).
    tabs_->InstallUblockOrigin();

    log::Info("app: motor listo");
    return true;
}

void App::CreateMainWindowContent() {
    // Restaura sesión si existe; si no, homepage.
    tabs_->RestoreSession(cfg_.homepage);
}

// ============================ ClientBridge ==================================

void App::OnWindowResize(int, int) {
    if (tabs_) tabs_->SetContentBounds(host_->ContentRect());
}

void App::OnCloseRequest() {
    // Salida limpia: persistir antes de destruir la ventana (WM_DESTROY
    // dispara PostQuitMessage desde WindowHost).
    SaveWindowPlacement();
    config::Save(cfg_);
    tabs_->SaveSession();
    DestroyWindow(host_->Hwnd());
}

void App::OnNewTab() {
    tabs_->CreateTab(kNewTabPage, true);
}

void App::OnCloseTab(int index) {
    tabs_->CloseTab(index);
}

void App::OnSelectTab(int index) {
    tabs_->SelectTab(index);
}

void App::OnActivateNextTab(int direction) {
    tabs_->CycleTab(direction);
}

void App::OnMaxAliveTabsExceeded() {
    statusText_ = L"L\u00EDmite de pesta\u00F1as vivas alcanzado (las de fondo se suspenden)";
    statusUntil_ = GetTickCount64() + kStatusSeconds * 1000ULL;
    RefreshChromeState();
}

void App::OnNavigate(const wchar_t* text) {
    if (!text || !*text) return;

    wchar_t finalUrl[2048];

    // smac:// -> páginas internas.
    if (wcsncmp(text, L"smac://", 7) == 0) {
        lstrcpynW(finalUrl, text, 2048);
    } else if (window::LooksLikeUrl(text)) {
        // URL sin esquema: anteponer https:// (navegación moderna).
        if (wcsncmp(text, L"http://", 7) == 0 ||
            wcsncmp(text, L"https://", 8) == 0 ||
            wcsncmp(text, L"file://", 7) == 0 ||
            wcsncmp(text, L"about:", 6) == 0) {
            lstrcpynW(finalUrl, text, 2048);
        } else {
            _snwprintf_s(finalUrl, 2048, _TRUNCATE, L"https://%s", text);
        }
    } else {
        // Consulta al buscador configurado (DuckDuckGo por defecto).
        const wchar_t* tpl = cfg_.searchEngine;
        const wchar_t* ph = wcsstr(tpl, L"%s");

        // Codificación mínima: espacios -> + (suficiente para buscadores).
        wchar_t enc[1024];
        int ei = 0;
        for (const wchar_t* p = text; *p && ei < 1000; ++p) {
            if (*p == L' ') { enc[ei++] = L'+'; }
            else if (*p == L'&' || *p == L'%' || *p == L'+' || *p == L'?') {
                ei += _snwprintf_s(enc + ei, 1024 - ei, _TRUNCATE,
                                   L"%%%02X", *p);
            } else {
                enc[ei++] = *p;
            }
        }
        enc[ei] = 0;

        if (ph) {
            _snwprintf_s(finalUrl, 2048, _TRUNCATE, L"%.*s%s%s",
                         static_cast<int>(ph - tpl), tpl, enc, ph + 2);
        } else {
            lstrcpynW(finalUrl, tpl, 2048);
        }
    }

    if (web::WebViewTab* tab = tabs_->Active()) {
        tab->Navigate(finalUrl);
    } else {
        tabs_->CreateTab(finalUrl, true);
    }
}

void App::OnBack()    { if (tabs_) if (auto* t = tabs_->Active()) t->GoBack(); }
void App::OnForward() { if (tabs_) if (auto* t = tabs_->Active()) t->GoForward(); }
void App::OnReload()  { if (tabs_) if (auto* t = tabs_->Active()) t->Reload(); }
void App::OnStop()    { if (tabs_) if (auto* t = tabs_->Active()) t->Stop(); }

void App::OnHome() {
    if (tabs_) if (auto* t = tabs_->Active()) t->Navigate(cfg_.homepage);
}

void App::OnUpdateUIState() {
    RefreshChromeState();
}

// ============================ Menús =========================================

void App::OnShowMenu(POINT ptScreen) {
    // Los menús borran la selección del omnibox: guarda y restaura el foco.
    const wchar_t* hist[16];
    const wchar_t* marks[16];
    int nh = 0, nm = 0;
    for (const auto& h : history_) {
        if (nh >= 16) break;
        hist[nh++] = h.c_str();
    }
    for (const auto& b : bookmarks_) {
        if (nm >= 16) break;
        marks[nm++] = b.c_str();
    }

    const int cmd = window::MenuBuilder::ShowAppMenu(
        host_->Hwnd(), ptScreen, hist, nh, marks, nm);
    if (cmd) OnMenuCommand(cmd);
}

void App::OnMenuCommand(int id) {
    using WC = window::MenuCmd;
    switch (id) {
        case WC::kMenuNewTab:     OnNewTab(); break;
        case WC::kMenuCloseTab:   tabs_->CloseTab(tabs_->ActiveIndex()); break;
        case WC::kMenuExit:       OnCloseRequest(); break;
        case WC::kMenuHome:       OnHome(); break;
        case WC::kMenuBack:       OnBack(); break;
        case WC::kMenuForward:    OnForward(); break;
        case WC::kMenuReload:     OnReload(); break;
        case WC::kMenuFind:       omnibox_->FocusAll(); break;
        case WC::kMenuZoomIn:     ExecuteCommand(smac::EngineCmd::ZoomIn); break;
        case WC::kMenuZoomOut:    ExecuteCommand(smac::EngineCmd::ZoomOut); break;
        case WC::kMenuZoomReset:  ExecuteCommand(smac::EngineCmd::ZoomReset); break;
        case WC::kMenuFullScreen: ExecuteCommand(smac::EngineCmd::FullScreen); break;
        case WC::kMenuAddBookmark: AddBookmarkOfActiveTab(); break;
        default:
            // Rangos dinámicos: historial y favoritos.
            if (id >= WC::kMenuHistory &&
                id < WC::kMenuHistory + 1000) {
                const int i = id - WC::kMenuHistory;
                if (i < static_cast<int>(history_.size())) {
                    OnNavigate(history_[i].c_str());
                }
            } else if (id >= WC::kMenuBookmark &&
                       id < WC::kMenuBookmark + 1000) {
                const int i = id - WC::kMenuBookmark;
                if (i < static_cast<int>(bookmarks_.size())) {
                    OnNavigate(bookmarks_[i].c_str());
                }
            }
            break;
    }
}

// ============================ Historial / favoritos =========================

void App::LoadUserStores() {
    const std::wstring base = paths::UserDir();

    std::ifstream hfile(base + L"\\history.txt");
    std::wstring line;
    while (std::getline(hfile, line) &&
           history_.size() < 100) {
        if (!line.empty()) history_.push_back(line);
    }

    std::ifstream bfile(base + L"\\bookmarks.txt");
    while (std::getline(bfile, line) &&
           bookmarks_.size() < 100) {
        if (!line.empty()) bookmarks_.push_back(line);
    }
}

void App::AppendHistory(const wchar_t* url) {
    if (!url || !*url) return;
    if (wcsncmp(url, L"http", 4) != 0) return;   // solo URLs reales

    // Deduplica: si ya está, muévela arriba.
    for (size_t i = 0; i < history_.size(); ++i) {
        if (history_[i] == url) {
            history_.erase(history_.begin() + i);
            break;
        }
    }
    history_.insert(history_.begin(), url);
    if (history_.size() > 100) history_.resize(100);

    // Persistencia barata: una línea por URL.
    std::ofstream f(std::wstring(paths::UserDir()) + L"\\history.txt",
                    std::ios::trunc);
    for (const auto& h : history_) f << h << L"\n";
}

void App::AddBookmarkOfActiveTab() {
    if (!tabs_->Active()) return;
    const auto url = tabs_->Active()->GetState().url;
    if (url.empty()) return;

    for (const auto& b : bookmarks_) {
        if (b == url) {
            statusText_ = L"Ya est\u00e1 en favoritos";
            statusUntil_ = GetTickCount64() + kStatusSeconds * 1000ULL;
            return;
        }
    }
    bookmarks_.push_back(url);
    if (bookmarks_.size() > 100) bookmarks_.resize(100);

    std::ofstream f(std::wstring(paths::UserDir()) + L"\\bookmarks.txt",
                    std::ios::app);
    f << url << L"\n";

    statusText_ = L"A\u00f1adido a favoritos";
    statusUntil_ = GetTickCount64() + kStatusSeconds * 1000ULL;
}

// ============================ TabManager::Delegate ==========================

void App::OnTabsChanged() {
    PushTabsToStrip();
    RefreshChromeState();
}

void App::OnActiveTabState() {
    RefreshChromeState();
}

void App::OnDownloadStarted(const wchar_t* fileName) {
    statusText_ = std::wstring(L"Descarga: ") +
                  (fileName && *fileName ? fileName : L"archivo");
    statusUntil_ = GetTickCount64() + kStatusSeconds * 1000ULL;
    RefreshChromeState();
}

void App::OnFullScreen(bool enabled) {
    // Fullscreen del CONTENIDO (vídeo): oculta barra+pestañas y maximiza.
    if (!host_) return;
    host_->SetChromeHidden(enabled);
    LONG style = GetWindowLongW(host_->Hwnd(), GWL_STYLE);
    if (enabled) {
        style &= ~(WS_CAPTION | WS_THICKFRAME);
        SetWindowLongW(host_->Hwnd(), GWL_STYLE, style);
        SetWindowPos(host_->Hwnd(), nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_FRAMECHANGED);
        ShowWindow(host_->Hwnd(), SW_SHOWMAXIMIZED);
    } else {
        style |= WS_OVERLAPPEDWINDOW;
        SetWindowLongW(host_->Hwnd(), GWL_STYLE, style);
        SetWindowPos(host_->Hwnd(), nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_FRAMECHANGED);
        ShowWindow(host_->Hwnd(), SW_SHOWNOACTIVATE);
    }
}

void App::OnEngineCommand(smac::EngineCmd cmd) {
    ExecuteCommand(cmd);
}

void App::OnHistoryCandidate(const wchar_t* url) {
    AppendHistory(url);
}

HWND App::ContentHwnd() {
    return host_ ? host_->Hwnd() : nullptr;
}

// ============================ Comandos ======================================

void App::ExecuteCommand(smac::EngineCmd cmd) {
    using EC = smac::EngineCmd;
    if (!tabs_) return;

    web::WebViewTab* tab = tabs_->Active();
    switch (cmd) {
        case EC::NewTab:       OnNewTab(); break;
        case EC::CloseTab:
            tabs_->CloseTab(tabs_->ActiveIndex());
            break;
        case EC::FocusOmnibox: omnibox_->FocusAll(); break;
        case EC::Find:
            omnibox_->FocusAll();   // v1: buscar reutiliza el omnibox
            break;
        case EC::Reload:       if (tab) tab->Reload(); break;
        case EC::Back:         if (tab) tab->GoBack(); break;
        case EC::Forward:      if (tab) tab->GoForward(); break;
        case EC::Stop:         if (tab) tab->Stop(); break;
        case EC::ZoomIn:
        case EC::ZoomOut:
        case EC::ZoomReset: {
            double z = 1.0;
            if (tab) z = tab->GetState().zoom;
            static constexpr double kSteps[] = {
                0.25, 0.3333, 0.5, 0.67, 0.75, 0.8, 0.9, 1.0,
                1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0 };
            constexpr int kN = 15;
            int i = 7;   // índice del 1.0
            for (int k = 0; k < kN; ++k) {
                if (z <= kSteps[k] + 0.001) { i = k; break; }
            }
            double target = 1.0;
            if (cmd == EC::ZoomIn)  target = kSteps[(i + 1 < kN) ? i + 1 : kN - 1];
            if (cmd == EC::ZoomOut) target = kSteps[(i - 1 >= 0) ? i - 1 : 0];
            if (cmd == EC::ZoomReset) target = 1.0;
            if (tab) tab->SetZoom(target);
            break;
        }
        case EC::FullScreen: {
            const bool nowFs = GetWindowLongW(host_->Hwnd(), GWL_STYLE) &
                               WS_CAPTION;
            OnFullScreen(!nowFs);
            break;
        }
        case EC::NextTab: tabs_->CycleTab(+1); break;
        case EC::PrevTab: tabs_->CycleTab(-1); break;
        default: {
            // Ctrl+1..9: SelectTab0+n (n==8 -> última pestaña).
            const int c  = static_cast<int>(cmd);
            const int s0 = static_cast<int>(EC::SelectTab0);
            if (c >= s0 && c <= s0 + 8) {
                const int n = c - s0;
                const int last = tabs_->Count() - 1;
                tabs_->SelectIndex(n >= last ? last : n);
            }
            break;
        }
    }
}

// ============================ Estado de la UI ===============================

void App::RefreshChromeState() {
    if (!tabs_ || !chrome_) return;

    web::WebViewTab::State st;
    if (tabs_->Active()) st = tabs_->Active()->GetState();

    chrome_->SetNavState(st.canGoBack, st.canGoForward, st.loading);

    // Omnibox refleja la URL activa (si el foco no está en el EDIT).
    if (omnibox_ && GetFocus() != omnibox_->Hwnd()) {
        omnibox_->SetText(st.url.c_str());
    }
}

void App::PushTabsToStrip() {
    if (!tabs_ || !strip_) return;

    // Los títulos viven en la App (estables hasta el siguiente push):
    // la tira guarda punteros prestados y pinta ASÍNCRONAMENTE.
    const int n = tabs_->Count();
    tabTitles_.resize(n);
    window::TabView views[32];
    const int count = n > 32 ? 32 : n;
    for (int i = 0; i < count; ++i) {
        const auto st = tabs_->At(i)->GetState();
        tabTitles_[i] = st.title;
        views[i].title  = tabTitles_[i].c_str();
        views[i].active = (i == tabs_->ActiveIndex());
        views[i].loading = st.loading;
        views[i].muted  = st.hasAudio;
    }
    strip_->SetTabs(views, count);
}

void App::SaveWindowPlacement() {
    WINDOWPLACEMENT wp{ sizeof(wp) };
    if (!host_ || !GetWindowPlacement(host_->Hwnd(), &wp)) return;

    if (wp.showCmd == SW_SHOWMAXIMIZED) {
        cfg_.maximizeStart = true;
        return;
    }
    RECT r = wp.rcNormalPosition;
    cfg_.windowX = r.left;
    cfg_.windowY = r.top;
    cfg_.windowW = r.right - r.left;
    cfg_.windowH = r.bottom - r.top;
}

} // namespace smac::app
