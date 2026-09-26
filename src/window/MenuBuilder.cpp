/** ==========================================================================
 *  window/MenuBuilder.cpp — TrackPopupMenu puro (sin HTML, sin frameworks).
 *  ========================================================================== */
#include "MenuBuilder.hpp"
#include "smac/Log.hpp"

namespace smac::window {
namespace {

/** Ejecuta un menú y devuelve el ID elegido (0 = cancelado). */
int TrackOnce(HWND hwnd, HMENU menu, POINT pt) {
    const int cmd = TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(menu);
    return cmd;
}

} // namespace

int MenuBuilder::ShowAppMenu(HWND hwnd, POINT pt,
                             const wchar_t* const* history,
                             int historyCount,
                             const wchar_t* const* bookmarks,
                             int bookmarkCount) {
    HMENU m = CreatePopupMenu();
    if (!m) return 0;

    AppendMenuW(m, MF_STRING, kMenuNewTab,     L"Nueva pestaña\tCtrl+T");
    AppendMenuW(m, MF_STRING, kMenuCloseTab,   L"Cerrar pestaña\tCtrl+W");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuBack,       L"Atrás\tAlt+←");
    AppendMenuW(m, MF_STRING, kMenuForward,    L"Adelante\tAlt+→");
    AppendMenuW(m, MF_STRING, kMenuReload,     L"Recargar\tF5");
    AppendMenuW(m, MF_STRING, kMenuAddBookmark,L"Añadir a favoritos");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuZoomIn,     L"Ampliar\tCtrl++");
    AppendMenuW(m, MF_STRING, kMenuZoomOut,    L"Reducir\tCtrl+-");
    AppendMenuW(m, MF_STRING, kMenuZoomReset,  L"Zoom 100%\tCtrl+0");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuFullScreen, L"Pantalla completa\tF11");

    // --- Historial reciente (máx. 10) -------------------------------------
    if (historyCount > 0) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Historial");
        const int max = historyCount > 10 ? 10 : historyCount;
        for (int i = 0; i < max; ++i) {
            AppendMenuW(m, MF_STRING, kMenuHistory + i,
                        history[i] ? history[i] : L"(sin título)");
        }
    }

    // --- Favoritos (máx. 10) -----------------------------------------------
    if (bookmarkCount > 0) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Favoritos");
        const int max = bookmarkCount > 10 ? 10 : bookmarkCount;
        for (int i = 0; i < max; ++i) {
            AppendMenuW(m, MF_STRING, kMenuBookmark + i,
                        bookmarks[i] ? bookmarks[i] : L"(sin título)");
        }
    }

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuExit, L"Salir");

    return TrackOnce(hwnd, m, pt);
}

} // namespace smac::window
