/** ==========================================================================
 *  browser/TabManager.hpp — Orquestador de pestañas (módulo browser/).
 *  ----------------------------------------------------------------------------
 *  SRP: ciclo de vida del CONJUNTO de pestañas. Políticas:
 *   - Cap de pestañas vivas (maxAliveTabs): la más vieja no-audio se
 *     suspende (TrySuspend); si el cap es 1, la LRU se cierra y sustituye.
 *   - Suspensión = ICoreWebView2_3::TrySuspend (renderer parado).
 *   - Sesión persistente a session.txt (URLs en orden de pestañas).
 *  Implementa TabHost y multiplexa los eventos hacia la App.
 *  ========================================================================== */
#pragma once

#include "TabHost.hpp"
#include "smac/ComPtr.hpp"
#include "smac/Config.hpp"

#include <windows.h>
#include <WebView2.h>
#include "smac/Commands.hpp"
#include <vector>
#include <memory>
#include <string>

namespace smac::web {

class WebViewTab;

/** Estrategia cuando la App pide una pestaña más allá del cap. */
enum class CapOverflow {
    SuspendOldest,   ///< Suspende la LRU no-audio (política por defecto)
    CloseOldest,     ///< Cierra la LRU (modo ultra-lowram)
    Allow            ///< Ignora el cap (usuario manda)
};

/** Gestor de pestañas (una instancia por ventana). */
class TabManager final : public TabHost {
public:
    /** Puente hacia la App (UI y políticas globales). */
    class Delegate {
    public:
        virtual ~Delegate() = default;
        virtual void OnTabsChanged() = 0;                 ///< repaint tira
        virtual void OnActiveTabState() = 0;              ///< repaint barra
        virtual void OnMaxAliveTabsExceeded() = 0;        ///< aviso cap
        virtual void OnDownloadStarted(const wchar_t* fileName) = 0;
        virtual void OnFullScreen(bool enabled) = 0;
        /** Comando de teclado capturado en el motor (UI global). */
        virtual void OnEngineCommand(smac::EngineCmd cmd) = 0;
        /** URL navegable completada: candidato a historial. */
        virtual void OnHistoryCandidate(const wchar_t* url) = 0;
        virtual HWND ContentHwnd() = 0;                   ///< host del motor
    };

    TabManager() = default;
    ~TabManager() override;

    /** Inyecta el entorno compartido y el delegate. */
    void Init(const smac::ComPtr<ICoreWebView2Environment>& env,
              smac::ComPtr<ICoreWebView2Profile> profile,
              Delegate* delegate,
              const smac::Config& cfg);

    /** Crea una pestaña (navega a url) y la devuelve (índice activo). */
    WebViewTab* CreateTab(const wchar_t* url, bool makeActive);

    /** Cierra la pestaña i (activa la vecina si era la activa). */
    void CloseTab(int index);

    /** Selección explícita. */
    void SelectTab(int index);

    /** Siguiente/anterior (dirección = +1/-1). */
    void CycleTab(int direction);

    /** Activa la pestaña index de las vivas (Ctrl+1..9). */
    void SelectIndex(int index);

    /** Accesos directos para la App. */
    WebViewTab* Active() const;
    int  ActiveIndex() const;
    int  Count() const { return static_cast<int>(tabs_.size()); }
    /** Pestaña por índice (nullptr si está fuera de rango). */
    WebViewTab* At(int index) const;
    /** Índice de la pestaña por id (para CloseTab por EngineCmd). */
    int  IndexOfId(int id) const;

    /** Layout: reposiciona la pestaña activa (llamado en resize). */
    void SetContentBounds(RECT rc);

    /** Aplica el cap: suspende LRU no-audio hasta quedar <= maxAlive. */
    void EnforceAliveCap();

    /** Suspende todo lo suspendible (llamado al perder foco la ventana). */
    void SuspendBackground();

    /** Sesión: guarda URLs y restaura (Devuelve true si restauró algo). */
    bool SaveSession();
    bool RestoreSession(const wchar_t* homepageFallback);

    /** Instala uBlock en el perfil y notifica (lo llama App una vez). */
    void InstallUblockOrigin();

    // --- TabHost (eventos de pestañas -> App) -------------------------------
    void OnTabNavigated(int tabId, const wchar_t* url,
                        bool isLoading) override;
    void OnTabTitle(int tabId, const wchar_t* title) override;
    void OnTabAudio(int tabId, bool audible) override;
    void OnTabZoom(int tabId, double zoom) override;
    void OnTabFullScreen(int tabId, bool fullScreen) override;
    void OnTabNewWindow(int tabId,
                        ICoreWebView2NewWindowRequestedEventArgs* args) override;
    void OnTabDownload(int tabId,
                       ICoreWebView2DownloadStartingEventArgs* args) override;
    void OnTabStateDirty(int tabId) override;
    void OnEngineCommand(smac::EngineCmd cmd, int tabId) override;

    /** Reenvía new-window del ACTIVE como nueva pestaña (política). */
    void SetCapOverflow(CapOverflow policy) { capPolicy_ = policy; }

private:
    /** Registro interno de pestaña. */
    struct Tab {
        std::unique_ptr<WebViewTab> view;
        int  id = 0;
        bool suspended = false;
    };

    /** Crea el objeto pestaña + dispara la creación del controller. */
    Tab* SpawnTab(const wchar_t* url, bool makeActive);
    /** Busca por id. */
    Tab* FindById(int id);
    /** Índice por id (-1 si no existe). */
    int  IndexById(int id) const;
    /** Id de la pestaña activa (-1 si no hay). */
    int  ActiveId() const;

    /** Envía el estado de la pestaña activa a la barra (vía delegate). */
    void PushActiveState();

    smac::ComPtr<ICoreWebView2Environment> env_;
    smac::ComPtr<ICoreWebView2Profile>     profile_;
    Delegate* delegate_ = nullptr;
    smac::Config cfg_;

    std::vector<Tab> tabs_;
    int activeIndex_ = -1;
    int nextId_ = 1;
    CapOverflow capPolicy_ = CapOverflow::SuspendOldest;

    RECT contentBounds_{};
    std::wstring sessionPath_;
};

} // namespace smac::web
