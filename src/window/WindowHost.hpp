/** ==========================================================================
 *  window/WindowHost.hpp — Ventana principal Win32 (módulo window/).
 *  ----------------------------------------------------------------------------
 *  Responsabilidad ÚNICA: ciclo de vida de la ventana nativa.
 *   - Frameless real: WM_NCCALCSIZE consume la barra de título del SO.
 *   - Arrastre/redimensionado/botones por WM_NCHITTEST (nativos, sin HTML).
 *   - Composición DWM: fondo negro + borde de 1 px delegados a la GPU.
 *   - Reparte el resto de mensajes a ChromeBar/TabStrip/Omnibox y a la App
 *     mediante ClientBridge (window/ nunca conoce al motor WebView2).
 *  ========================================================================== */
#pragma once

#include "smac/Config.hpp"

#include <windows.h>

namespace smac::window {

class ChromeBar;
class TabStrip;
class Omnibox;

/**
 * Puente de eventos ventana -> aplicación. La App lo implementa (orchestra
 * browser/ y system/). Métodos = mensajes de negocio, no de Win32.
 */
class ClientBridge {
public:
    virtual ~ClientBridge() = default;

    // --- Ciclo de vida -------------------------------------------------
    virtual void OnWindowResize(int clientW, int clientH) = 0;
    virtual void OnCloseRequest() = 0;

    // --- Pestañas ------------------------------------------------------
    virtual void OnNewTab() = 0;
    virtual void OnCloseTab(int index) = 0;
    virtual void OnSelectTab(int index) = 0;
    virtual void OnActivateNextTab(int direction) = 0;   ///< Ctrl+PgUp/PgDn
    virtual void OnMaxAliveTabsExceeded() = 0;           ///< Aviso al usuario

    // --- Navegación ----------------------------------------------------
    virtual void OnNavigate(const wchar_t* text) = 0;    ///< Omnibox Enter
    virtual void OnBack() = 0;
    virtual void OnForward() = 0;
    virtual void OnReload() = 0;
    virtual void OnStop() = 0;
    virtual void OnHome() = 0;

    // --- Estado (la App re-renderiza la barra con esto) ----------------
    virtual void OnUpdateUIState() = 0;

    // --- Menús (contextual de la barra y comandos de menú) --------------
    virtual void OnShowMenu(POINT ptScreen) = 0;
    virtual void OnMenuCommand(int id) = 0;
};

/** Ventana principal de S.M.A.C (instancia única por proceso). */
class WindowHost {
public:
    WindowHost() = default;
    ~WindowHost();

    WindowHost(const WindowHost&) = delete;
    WindowHost& operator=(const WindowHost&) = delete;

    /**
     * Crea y muestra la ventana. `chrome`, `tabs` y `omnibox` son módulos
     * window/ que esta clase orquesta a nivel de layout e input.
     */
    bool Create(HINSTANCE hInstance,
                const smac::Config& cfg,
                ClientBridge* bridge,
                ChromeBar* chrome,
                TabStrip* tabs,
                Omnibox* omnibox);

    /** Bucle de mensajes (bombeo puro, IsDialogMessage para el EDIT). */
    int RunMessageLoop();

    HWND Hwnd() const { return hwnd_; }

    /** Geometría actual del área del motor (recalculada en resize). */
    RECT ContentRect() const { return contentRect_; }

    /** Geometría de la franja del TabStrip (para hit-testing del shell). */
    RECT TabStripRect() const { return tabStripRect_; }

    /** Aplica el color de acento DWM (cyan/purple/green/off). */
    void ApplyAccent(smac::AccentColor accent);

    /** Modo contenido-fullscreen: oculta barra+pestañas (vídeo a pantalla completa). */
    void SetChromeHidden(bool hidden);

    /** Invalida la barra decorativa (uso tras cambios de estado). */
    void RepaintChrome();

    /** Recalcula el layout completo (lo llama la App tras crear hijos). */
    void ForceLayout();

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

    /** Handlers privados por mensaje (SRP dentro de la propia clase). */
    LRESULT OnNCCalcSize(WPARAM wParam);
    LRESULT OnNCHitTest(LPARAM lParam);
    void    OnSize(int w, int h);
    void    Layout();
    void    ApplyBlackBackground();

    HWND        hwnd_ = nullptr;
    HINSTANCE   hInstance_ = nullptr;
    ClientBridge* bridge_ = nullptr;

    ChromeBar*  chrome_ = nullptr;
    TabStrip*   tabs_ = nullptr;
    Omnibox*    omnibox_ = nullptr;

    RECT contentRect_  {};   ///< Zona del WebView (bajo la barra de chrome).
    RECT tabStripRect_ {};   ///< Zona del TabStrip (para WM_NCHITTEST).

    int clientW_ = 0;
    int clientH_ = 0;
    bool chromeHidden_ = false;   ///< fullscreen de contenido
};

/** Alturas de layout en px (a DPI 96; se escalan en tiempo real). */
constexpr int kTopBarHeight   = 36;   ///< ChromeBar (nav + omnibox + caption)
constexpr int kTabStripHeight = 28;   ///< Tira de pestañas

/** Escala un valor de 96 DPI al DPI actual del monitor primario. */
int ScaleAtDpi(int value);

} // namespace smac::window
