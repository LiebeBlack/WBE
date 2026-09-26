/** ==========================================================================
 *  window/ChromeBar.cpp — Barra superior owner-drawn (fondo negro puro).
 *  ----------------------------------------------------------------------------
 *  Todo el dibujado es Buffered Paint (UxTheme): doble buffer vía DWM,
 *  sin parpadeo. Los glifos son Segoe MDL2/Fluent Assets (presentes en
 *  Win10/11): cero recursos gráficos extra en el binario.
 *  ========================================================================== */
#include "ChromeBar.hpp"
#include "Omnibox.hpp"
#include "WindowHost.hpp"      // ScaleAtDpi
#include "smac/Log.hpp"

#include <windowsx.h>
#include <uxtheme.h>
#include <dwmapi.h>

#pragma comment(lib, "uxtheme.lib")

namespace smac::window {
namespace {

constexpr wchar_t kClassName[] = L"SMAC_WebCore_ChromeBar";

// Glifos Segoe MDL2 Assets.
constexpr wchar_t kGlyphBack[]   = L"\xE72B";  // Back
constexpr wchar_t kGlyphFwd[]    = L"\xE72A";  // Forward
constexpr wchar_t kGlyphReload[] = L"\xE72C";  // Refresh
constexpr wchar_t kGlyphStop[]   = L"\xE711";  // Cancel
constexpr wchar_t kGlyphHome[]   = L"\xE80F";  // Home

constexpr COLORREF kBg       = RGB(0, 0, 0);
constexpr COLORREF kFg       = RGB(200, 200, 200);
constexpr COLORREF kFgDim    = RGB(90, 90, 90);
constexpr COLORREF kAccent   = RGB(0, 0xE5, 0xFF);
constexpr COLORREF kHover    = RGB(28, 28, 30);
constexpr COLORREF kOmniboxBg = RGB(18, 18, 20);

constexpr UINT_PTR kHoverTimerId = 0x484F5652;  // 'HOVR' — sondeo de hover

ChromeBar* BarOf(HWND hwnd) {
    return reinterpret_cast<ChromeBar*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

/** Un botón cuadrado del lado izquierdo. */
struct BtnSpec { int id; const wchar_t* glyph; };

constexpr BtnSpec kButtons[] = {
    { ChromeBar::kBack,   kGlyphBack },
    { ChromeBar::kForward,kGlyphFwd },
    { ChromeBar::kReload, kGlyphReload },
    { ChromeBar::kStop,   kGlyphStop },
    { ChromeBar::kHome,   kGlyphHome },
};
constexpr int kButtonCount = 5;

} // namespace

//=============================================================================
// Zona de caption (botones nativos min/max/close dibujados por nosotros y
// reportados como HT*BUTTON para que Windows aplique snap, animaciones y
// accesibilidad). Se declara antes para que WindowHost pueda consultarlo.
//=============================================================================

// ============================ Ventana hija ==================================

ChromeBar::~ChromeBar() {
    if (fontGlyph_) DeleteObject(fontGlyph_);
    if (fontText_)  DeleteObject(fontText_);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool ChromeBar::Create(HWND parent, HINSTANCE hInstance, Omnibox* omnibox) {
    omnibox_ = omnibox;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (auto* self = BarOf(h)) {
            const LRESULT r = self->Handle(h, m, w, l);
            if (r != -1) return r;
        }
        return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        log::Error("chrome: RegisterClassExW fallo (%lu)", GetLastError());
        return false;
    }

    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClassName, L"",
                            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                            parent, nullptr, hInstance, nullptr);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Sondeo de hover para repintar botones (60 ms, sin TrackMouseEvent:
    // en ventanas frameless el WM_MOUSELEAVE llega tarde y deja el hover
    // pegado). El WM_TIMER del lambda hace el trabajo real.
    SetTimer(hwnd_, kHoverTimerId, 60, nullptr);

    // Fuente de glifos y fuente de texto.
    fontGlyph_ = CreateFontW(-ScaleAtDpi(14), 0, 0, 0, FW_NORMAL, FALSE,
                             FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH,
                             L"Segoe MDL2 Assets");
    fontText_ = CreateFontW(-ScaleAtDpi(12), 0, 0, 0, FW_NORMAL, FALSE,
                            FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH,
                            L"Segoe UI");

    return true;
}

void ChromeBar::SetBounds(const RECT& rc) {
    bounds_ = rc;
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

RECT ChromeBar::OmniboxRect() const {
    const int h = bounds_.bottom - bounds_.top;
    const int btnZone = kButtonCount * ScaleAtDpi(32);
    const int caption = ScaleAtDpi(135);   // Reserva min/max/close del SO
    RECT rc;
    rc.left   = btnZone + ScaleAtDpi(6);
    rc.right  = (bounds_.right - bounds_.left) - caption;
    rc.top    = (h - ScaleAtDpi(24)) / 2;
    rc.bottom = rc.top + ScaleAtDpi(24);
    return rc;
}

bool ChromeBar::IsDragZone(POINT pt) const {
    if (ButtonAt(pt) > 0) return false;
    RECT omni = OmniboxRect();
    if (PtInRect(&omni, pt)) return false;
    return true;
}

ChromeBar::CaptionHit ChromeBar::CaptionHit(POINT pt) const {
    if (!captionButtons_) return CaptionHit::None;

    // Zona de caption: esquina superior derecha (convención del SO).
    const int h = bounds_.bottom - bounds_.top;
    if (pt.y > h) return CaptionHit::None;

    const int w = bounds_.right - bounds_.left;
    const int btnW = ScaleAtDpi(46);
    const int zone = w - 3 * btnW;
    if (pt.x < zone) return CaptionHit::None;

    if (pt.x < zone + btnW)         return CaptionHit::Min;
    if (pt.x < zone + 2 * btnW)     return CaptionHit::Max;
    return CaptionHit::Close;
}

int ChromeBar::ButtonAt(POINT pt) const {
    const int size = ScaleAtDpi(32);
    for (int i = 0; i < kButtonCount; ++i) {
        RECT rc{ i * size, 0, (i + 1) * size,
                 bounds_.bottom - bounds_.top };
        if (PtInRect(&rc, pt)) return kButtons[i].id;
    }
    return -1;
}

// ============================ Dispatcher de mensajes =========================

LRESULT ChromeBar::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            PaintButtons();
            return 0;

        case WM_ERASEBKGND:
            return 1;   // el fondo lo pinta WM_PAINT (doble buffer)

        case WM_MOUSEMOVE: {
            hoverId_ = ButtonAt(POINT{ GET_X_LPARAM(lParam),
                                       GET_Y_LPARAM(lParam) });
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE:
            hoverId_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            const int id = ButtonAt(POINT{ GET_X_LPARAM(lParam),
                                           GET_Y_LPARAM(lParam) });
            if (id > 0) {
                PostMessageW(GetParent(hwnd), WM_COMMAND,
                             MAKEWPARAM(id, BN_CLICKED),
                             reinterpret_cast<LPARAM>(hwnd));
            }
            return 0;
        }

        case WM_NCHITTEST:
            return HTTRANSPARENT;   // los clicks de fondo pasan al padre

        case WM_TIMER:
            // Sondeo barato de hover (60 ms): en ventanas frameless el
            // WM_MOUSELEAVE llega tarde y deja el hover pegado.
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                const int now = ButtonAt(pt);
                if (now != hoverId_) {
                    hoverId_ = now;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        default:
            return -1;
    }
}

void ChromeBar::SetNavState(bool canBack, bool canForward, bool loading) {
    canBack_ = canBack;
    canForward_ = canForward;
    loading_ = loading;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChromeBar::Repaint() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChromeBar::SetVisibleSelf(bool visible) {
    if (hwnd_) ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE);
}

// ============================ Pintado =======================================

void ChromeBar::PaintButtons() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    if (!hdc) return;

    // Doble buffer simple: memory DC del tamaño de la zona sucia.
    RECT rcClient;
    GetClientRect(hwnd_, &rcClient);
    const int w = rcClient.right - rcClient.left;
    const int h = rcClient.bottom - rcClient.top;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    // Fondo negro puro.
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(mem, &rcClient, bg);
    DeleteObject(bg);

    // Botones.
    const int size = ScaleAtDpi(32);
    for (int i = 0; i < kButtonCount; ++i) {
        const BtnSpec& b = kButtons[i];
        RECT rc{ i * size, 0, (i + 1) * size, h };

        const bool enabled =
            (b.id == kBack   && canBack_)   ||
            (b.id == kForward&& canForward_)||
            (b.id == kReload && !loading_)  ||
            (b.id == kStop   && loading_)   ||
            (b.id == kHome);

        if (b.id == hoverId_ && enabled) {
            HBRUSH hb = CreateSolidBrush(kHover);
            FillRect(mem, &rc, hb);
            DeleteObject(hb);
        }

        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, enabled ? kFg : kFgDim);
        SelectObject(mem, fontGlyph_);
        DrawTextW(mem, b.glyph, -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

} // namespace smac::window
