/** ==========================================================================
 *  app/App.hpp — Orquestador central (módulo app/).
 *  ----------------------------------------------------------------------------
 *  ÚNICO módulo que conoce a todos los demás. Implementa:
 *   - window::ClientBridge  (eventos de la ventana Win32)
 *   - web::TabManager::Delegate (eventos del conjunto de pestañas)
 *  La lógica de negocio vive aquí; los módulos system/window/browser son
 *  mecanismo puro.
 *  ========================================================================== */
#pragma once

#include "window/WindowHost.hpp"
#include "browser/TabManager.hpp"
#include "smac/Config.hpp"
#include "smac/ComPtr.hpp"

#include <windows.h>
#include <WebView2.h>
#include <string>
#include <vector>
#include <fstream>

#include <windows.h>
#include <string>

namespace smac::app {

/** Fachada de la aplicación (una por proceso). */
class App final : public window::ClientBridge,
                  public web::TabManager::Delegate {
public:
    App() = default;
    ~App() override;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /** Arranque completo. Devuelve 0 si todo fue bien. */
    int Run(HINSTANCE hInstance, int nCmdShow);

private:
    // --- Fases de arranque (SRP por fase) ----------------------------------
    bool LoadConfig();
    bool CreateChrome();       ///< Ventana + barra + tira + omnibox
    bool CreateEngine();       ///< Entorno WebView2 + uBO + cirugía DOM
    void CreateMainWindowContent();  ///< pestaña inicial (sesión/homepage)
    void ApplyHostOptimizations();   ///< prioridad + timer de trim

    // --- window::ClientBridge (ventana -> app) ------------------------------
    void OnWindowResize(int clientW, int clientH) override;
    void OnCloseRequest() override;
    void OnNewTab() override;
    void OnCloseTab(int index) override;
    void OnSelectTab(int index) override;
    void OnActivateNextTab(int direction) override;
    void OnMaxAliveTabsExceeded() override;
    void OnNavigate(const wchar_t* text) override;
    void OnBack() override;
    void OnForward() override;
    void OnReload() override;
    void OnStop() override;
    void OnHome() override;
    void OnUpdateUIState() override;

    // --- Menús (contextual de la barra y comandos de menú) --------------
    void OnShowMenu(POINT ptScreen) override;
    void OnMenuCommand(int id) override;

    // --- web::TabManager::Delegate (motor -> app) ---------------------------
    void OnTabsChanged() override;
    void OnActiveTabState() override;
    void OnDownloadStarted(const wchar_t* fileName) override;
    void OnFullScreen(bool enabled) override;
    void OnEngineCommand(smac::EngineCmd cmd) override;
    void OnHistoryCandidate(const wchar_t* url) override;
    HWND ContentHwnd() override;

    // --- Acciones internas ---------------------------------------------------
    void ExecuteCommand(smac::EngineCmd cmd);
    void RefreshChromeState();
    void PushTabsToStrip();
    void SaveWindowPlacement();

    // --- Historial y favoritos (store mínimo, todo en UserDir) ------------
    void LoadUserStores();
    void AppendHistory(const wchar_t* url);
    void AddBookmarkOfActiveTab();

    HINSTANCE hInstance_ = nullptr;
    smac::Config cfg_;

    window::WindowHost* host_ = nullptr;
    window::ChromeBar*  chrome_ = nullptr;
    window::TabStrip*   strip_ = nullptr;
    window::Omnibox*    omnibox_ = nullptr;

    web::TabManager* tabs_ = nullptr;
    smac::ComPtr<ICoreWebView2Profile> profile_;

    std::vector<std::wstring> tabTitles_;  ///< Títulos estables de la tira
    std::wstring statusText_;     ///< Aviso efímero (descargas, avisos)
    ULONGLONG    statusUntil_ = 0;///< Marca de caducidad del aviso

    std::vector<std::wstring> history_;    ///< Más reciente primero (máx 100)
    std::vector<std::wstring> bookmarks_;  ///< URLs guardadas (máx 100)
};

} // namespace smac::app
