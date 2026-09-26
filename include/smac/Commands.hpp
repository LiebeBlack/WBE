/** ==========================================================================
 *  smac/Commands.hpp — Comandos neutrales motor -> App.
 *  ----------------------------------------------------------------------------
 *  Cuando el foco está DENTRO del WebView, el teclado llega por
 *  ICoreWebView2Controller::add_AcceleratorKeyPressed (los aceleradores
 *  Win32 del shell NO se disparan). El handler traduce las teclas a este
 *  enum neutral y la App ejecuta. Así browser/ nunca depende de window/.
 *  ========================================================================== */
#pragma once

namespace smac {

enum class EngineCmd : int {
    None = 0,
    NewTab,        ///< Ctrl+T
    CloseTab,      ///< Ctrl+W
    FocusOmnibox,  ///< Ctrl+L
    Find,          ///< Ctrl+F (v1: reutiliza el omnibox)
    Reload,        ///< F5 / Ctrl+R
    Back,          ///< Alt+Left
    Forward,       ///< Alt+Right
    ZoomIn,        ///< Ctrl+= / Ctrl+plus
    ZoomOut,       ///< Ctrl+-
    ZoomReset,     ///< Ctrl+0
    FullScreen,    ///< F11
    NextTab,       ///< Ctrl+Tab / Ctrl+PgDn
    PrevTab,       ///< Ctrl+Shift+Tab / Ctrl+PgUp
    SelectTab0,    ///< Ctrl+1 .. Ctrl+9: SelectTab0 + n (n==8 -> última)
};

/** Base aritmética para Ctrl+1..9 (n en [0..8]; 9 = última pestaña). */
inline EngineCmd SelectTabN(int n) {
    return static_cast<EngineCmd>(static_cast<int>(EngineCmd::SelectTab0) + n);
}

} // namespace smac
