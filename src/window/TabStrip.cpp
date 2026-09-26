/** ==========================================================================
 *  window/TabStrip.cpp — Pestañas owner-drawn (doble buffer, sin STL en frame).
 *  ========================================================================== */
#include "TabStrip.hpp"
#include "WindowHost.hpp"     // ScaleAtDpi
#include "smac/Log.hpp"

#include <windowsx.h>

namespace smac::window {
namespace {

constexpr wchar_t kClassName[] = L"SMAC_WebCore_TabStrip";

constexpr int kTabWidth   = 180;   ///< Ancho máx. por pestaña (96 dpi)
constexpr int kTabMinW    = 56;    ///< Ancho mínimo cuando hay muchas
constexpr int kTabHeight  = 26;
constexpr int kCloseBox   = 18;    ///< Zona del aspa por pestaña
constexpr int kPlusWidth  = 28;

constexpr COLORREF kBg      = RGB(0, 0, 0);
constexpr COLORREF kActive  = RGB(32, 32, 36);
constexpr COLORREF kHover   = RGB(20, 20, 22);
constexpr COLORREF kText    = RGB(210, 210, 210);
constexpr COLORREF kTextDim = RGB(120, 120, 120);
constexpr COLORREF kAccent  = RGB(0, 0xE5, 0xFF);

constexpr int kNewTabCmd     = static_cast<int>(TabStrip::kNewTab);
constexpr int kSelectTabBase = static_cast<int>(TabStrip::kSelectBase);
constexpr int kCloseTabBase  = static_cast<int>(TabStrip::kCloseBase);

/** WM_APP::wParam que pide al host abrir el menú de app en el cursor. */
constexpr WPARAM kAppMenuMsg = 0x414D4D31;  // 'AMM'

} // namespace

TabStrip::~TabStrip() {
    if (font_) DeleteObject(font_);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool TabStrip::Create(HWND parent, HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TabStrip::ProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        log::Error("tabs: RegisterClassExW fallo (%lu)", GetLastError());
        return false;
    }
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClassName, L"",
                            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                            parent, nullptr, hInstance, nullptr);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    font_ = CreateFontW(-ScaleAtDpi(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH, L"Segoe UI");
    return true;
}

void TabStrip::SetTabs(const TabView* tabs, int count) {
    views_.assign(tabs, tabs + count);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void TabStrip::SetBounds(const RECT& rc) {
    bounds_ = rc;
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void TabStrip::SetVisibleSelf(bool visible) {
    if (hwnd_) ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE);
}

LRESULT TabStrip::ProcThunk(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        // this aún no existe: se inyecta tras CreateWindowExW.
        return DefWindowProcW(hwnd, msg, w, l);
    }
    auto* self = reinterpret_cast<TabStrip*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        LRESULT r = self->Handle(hwnd, msg, w, l);
        if (r != -1) return r;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

LRESULT TabStrip::Handle(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_PAINT: PaintStrip(); return 0;
        case WM_ERASEBKGND: return 1;

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            hoverIndex_ = HitTest(POINT{ GET_X_LPARAM(l), GET_Y_LPARAM(l) });
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE:
            hoverIndex_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            const POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            const int hit = HitTest(pt);
            if (hit == kNewTabCmd) {
                PostMessageW(GetParent(hwnd), WM_COMMAND,
                             MAKEWPARAM(kNewTabCmd, 0),
                             reinterpret_cast<LPARAM>(hwnd));
            } else if (hit >= 0) {
                // ¿El click cayó sobre el aspa de esa pestaña?
                const int tabW = TabWidthAt(hit);
                const int x0 = ScaleAtDpi(kPlusWidth) + hit * tabW;
                const int xClose = x0 + tabW - ScaleAtDpi(kCloseBox);
                if (pt.x >= xClose) {
                    PostMessageW(GetParent(hwnd), WM_COMMAND,
                                 MAKEWPARAM(kCloseTabBase + hit, 0),
                                 reinterpret_cast<LPARAM>(hwnd));
                } else {
                    PostMessageW(GetParent(hwnd), WM_COMMAND,
                                 MAKEWPARAM(kSelectTabBase + hit, 0),
                                 reinterpret_cast<LPARAM>(hwnd));
                }
            }
            return 0;
        }

        case WM_CONTEXTMENU: {
            // Menú de app con botón derecho sobre la tira (posiciones de
            // pantalla; el punto lo da Windows).
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);  // por teclado
            ClientToScreen(hwnd, &pt);
            PostMessageW(GetParent(hwnd), WM_APP, kAppMenuMsg, 0);
            (void)pt;
            return 0;
        }

        default:
            break;
    }
    return -1;
}

/** Ancho efectivo de pestaña para el estado actual (coherente con paint). */
int TabStrip::TabWidthAt(int /*index*/) const {
    const int w = bounds_.right - bounds_.left;
    const int avail = w - ScaleAtDpi(kPlusWidth);
    const int n = static_cast<int>(views_.size());
    int tabW = ScaleAtDpi(kTabWidth);
    if (n > 0 && tabW * n > avail) {
        tabW = avail / n;
        if (tabW < ScaleAtDpi(kTabMinW)) tabW = ScaleAtDpi(kTabMinW);
    }
    return tabW;
}

int TabStrip::HitTest(POINT pt) const {
    if (pt.y < 0 || pt.y > bounds_.bottom - bounds_.top) return -2;

    const int step = ScaleAtDpi(kTabWidth);
    const int maxTotal = (bounds_.right - bounds_.left) - ScaleAtDpi(kPlusWidth);
    const int w = static_cast<int>(views_.size()) > 0
        ? (step > maxTotal / static_cast<int>(views_.size())
            ? maxTotal / static_cast<int>(views_.size())
            : step)
        : step;

    const int x = pt.x;
    if (x >= 0 && x < ScaleAtDpi(kPlusWidth)) return kNewTabCmd;

    int idx = (x - ScaleAtDpi(kPlusWidth)) / (w > 0 ? w : 1);
    if (idx >= 0 && idx < static_cast<int>(views_.size())) return idx;
    return -1;
}

void TabStrip::PaintStrip() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    if (!hdc) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    const int avail = w - ScaleAtDpi(kPlusWidth);
    const int n = static_cast<int>(views_.size());
    int tabW = ScaleAtDpi(kTabWidth);
    if (n > 0 && tabW * n > avail) {
        tabW = avail / n;
        if (tabW < ScaleAtDpi(kTabMinW)) tabW = ScaleAtDpi(kTabMinW);
    }

    int x = ScaleAtDpi(kPlusWidth);
    SelectObject(mem, font_);
    for (int i = 0; i < n; ++i) {
        const TabView& tv = views_[i];
        RECT rcTab{ x, 2, x + tabW, h - 2 };

        const bool hovered = (i == hoverIndex_);
        const COLORREF fill = tv.active ? kActive : (hovered ? kHover : kBg);
        HBRUSH br = CreateSolidBrush(fill);
        FillRect(mem, &rcTab, br);
        DeleteObject(br);

        // Aspa de cierre a la derecha de la pestaña.
        RECT rcX{ rcTab.right - ScaleAtDpi(kCloseBox), rcTab.top,
                  rcTab.right, rcTab.bottom };
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, kTextDim);
        DrawTextW(mem, L"\u2715", -1, &rcX,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Título acotado.
        RECT rcTitle{ rcTab.left + ScaleAtDpi(8), rcTab.top,
                      rcX.left - ScaleAtDpi(2), rcTab.bottom };
        SetTextColor(mem, tv.active ? kText : kTextDim);
        DrawTextW(mem, tv.title ? tv.title : L"(sin título)", -1, &rcTitle,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Subrayado cian de la pestaña activa (2 px).
        if (tv.active) {
            RECT rcLine{ rcTab.left, rcTab.bottom - 2, rcTab.right,
                         rcTab.bottom };
            HBRUSH ab = CreateSolidBrush(kAccent);
            FillRect(mem, &rcLine, ab);
            DeleteObject(ab);
        }

        // Indicador de audio (parte superior).
        if (tv.muted) {
            RECT rcDot{ rcTab.right - ScaleAtDpi(kCloseBox) - 8,
                        rcTab.top + 3,
                        rcTab.right - ScaleAtDpi(kCloseBox) - 2,
                        rcTab.top + 9 };
            HBRUSH ab = CreateSolidBrush(kAccent);
            FillRect(mem, &rcDot, ab);
            DeleteObject(ab);
        }

        x += tabW;
    }

    // Botón +.
    RECT rcPlus{ 0, 2, ScaleAtDpi(kPlusWidth), h - 2 };
    SetTextColor(mem, kText);
    SelectObject(mem, font_);
    DrawTextW(mem, L"+", -1, &rcPlus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

} // namespace smac::window
