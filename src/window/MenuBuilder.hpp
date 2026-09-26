/** ==========================================================================
 *  window/MenuBuilder.hpp — Menús nativos Win32 (módulo window/).
 *  ----------------------------------------------------------------------------
 *  Cero HTML/CSS: todos los menús son HMENU puros (TrackPopupMenu).
 *  ========================================================================== */
#pragma once

#include <windows.h>

namespace smac::window {

/** IDs de menú (los consume la App en OnMenuCommand). */
enum MenuCmd : int {
    kMenuNewTab      = 3001,
    kMenuCloseTab    = 3002,
    kMenuExit        = 3003,
    kMenuHome        = 3004,
    kMenuBack        = 3005,
    kMenuForward     = 3006,
    kMenuReload      = 3007,
    kMenuZoomIn      = 3008,
    kMenuZoomOut     = 3009,
    kMenuZoomReset   = 3010,
    kMenuFullScreen  = 3011,
    kMenuAddBookmark = 3012,   ///< Añadir la pestaña activa a favoritos
    kMenuHistory     = 3100,   ///< +1000 entradas dinámicas
    kMenuBookmark    = 3200,   ///< +1000 entradas dinámicas
};

/** Construye menús contextuales/de app a demanda (SRP: solo menús). */
class MenuBuilder {
public:
    /**
     * Menú de app (botón derecho sobre la tira). Incluye secciones de
     * historial y favoritos si se pasan. Devuelve el ID elegido (0 = cancel).
     */
    static int ShowAppMenu(HWND hwnd, POINT ptScreen,
                           const wchar_t* const* history = nullptr,
                           int historyCount = 0,
                           const wchar_t* const* bookmarks = nullptr,
                           int bookmarkCount = 0);
};

} // namespace smac::window
