/** ==========================================================================
 *  window/Omnibox.cpp — EDIT subclasificado: Enter navega, Esc deselecciona.
 *  ========================================================================== */
#include "Omnibox.hpp"
#include "WindowHost.hpp"    // ScaleAtDpi
#include "smac/Log.hpp"

#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace smac::window {
namespace {

constexpr COLORREF kBg       = RGB(18, 18, 20);
constexpr COLORREF kBgFocus  = RGB(24, 24, 28);
constexpr COLORREF kFg       = RGB(220, 220, 220);
constexpr COLORREF kBorderIdle = RGB(40, 40, 44);
constexpr COLORREF kBorderFocus = RGB(0, 0xE5, 0xFF);

/** ¿Carácter de esquema válido al inicio (a-zA-Z)? */
inline bool IsAlpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

/** ¿Parece un host con TLD puntúa ("ejemplo.com")? */
bool HasDotHost(const wchar_t* s) {
    const wchar_t* dot = wcschr(s, L'.');
    if (!dot || dot == s) return false;
    // El TLD necesita al menos 2 letras tras el último punto.
    const wchar_t* last = s;
    for (const wchar_t* p = s; *p; ++p) if (*p == L'.') last = p;
    int letters = 0;
    for (const wchar_t* p = last + 1; *p; ++p) {
        if (!IsAlpha(*p)) return false;
        ++letters;
    }
    return letters >= 2;
}

} // namespace

bool LooksLikeUrl(const wchar_t* text) {
    if (!text || !*text) return false;
    if (wcsncmp(text, L"http://", 7) == 0) return true;
    if (wcsncmp(text, L"https://", 8) == 0) return true;
    if (wcsncmp(text, L"file://", 7) == 0) return true;
    if (wcsncmp(text, L"about:", 6) == 0) return true;
    if (wcsncmp(text, L"smac://", 7) == 0) return true;
    if (wcsncmp(text, L"localhost", 9) == 0) return true;
    if (text[0] == L'.' || text[0] == L'/') return false;
    if (!IsAlpha(text[0])) return false;
    return HasDotHost(text);
}

Omnibox::~Omnibox() {
    if (hwnd_) {
        RemoveWindowSubclass(hwnd_, &Omnibox::EditProc, 1);
    }
}

bool Omnibox::Create(HWND parent, HINSTANCE hInstance) {
    hwnd_ = CreateWindowExW(
        0, WC_EDITW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN |
        ES_AUTOHSCROLL | ES_WANTRETURN,
        0, 0, 100, 24, parent,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kNavigateCmd)),   // ID de comando
        hInstance, nullptr);
    if (!hwnd_) {
        log::Error("omnibox: CreateWindowExW fallo (%lu)", GetLastError());
        return false;
    }
    SetWindowSubclass(hwnd_, &Omnibox::EditProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

    // Fuente UI y colores oscuros (EDIT estándar no soporta CSS: usamos
    // el truco WM_CTLCOLOREDIT que la ventana padre responde).
    SendMessageW(hwnd_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(
                     GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(hwnd_, EM_SETLIMITTEXT, 2048, 0);
    return true;
}

void Omnibox::SetBounds(const RECT& rc) {
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void Omnibox::SetVisibleSelf(bool visible) {
    if (hwnd_) ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE);
}

int Omnibox::GetText(wchar_t* out, int maxChars) const {
    if (!hwnd_) return 0;
    return GetWindowTextW(hwnd_, out, maxChars);
}

void Omnibox::SetText(const wchar_t* text) {
    if (!hwnd_) return;
    SetWindowTextW(hwnd_, text ? text : L"");
    SendMessageW(hwnd_, EM_SETSEL, 0, -1);
}

void Omnibox::FocusAll() {
    if (!hwnd_) return;
    SetFocus(hwnd_);
    SendMessageW(hwnd_, EM_SETSEL, 0, -1);
}

// Subclass: gestiona Enter/Esc y repinta los bordes con foco.
LRESULT Omnibox::EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                          UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                // El parent (WindowHost) consulta GetText y navega. Usamos
                // un HIWORD propio (kOmniboxEnter=0x1234 en WindowHost.cpp)
                // para no colisionar con las notificaciones EN_* reales.
                PostMessageW(GetParent(hwnd), WM_COMMAND,
                             MAKEWPARAM(kNavigateCmd, 0x1234),
                             reinterpret_cast<LPARAM>(hwnd));
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                SendMessageW(hwnd, WM_CLEAR, 0, 0);   // v1: limpia el campo
                return 0;
            }
            break;

        case WM_CHAR:
            if (wParam == VK_RETURN) return 0;   // evita beep
            break;

        case WM_CONTEXTMENU:
            return 0;   // sin menú de edición: minimalismo estricto

        case WM_NCPAINT: {
            // Borde cian con foco, gris sin foco (1 px GDI barato).
            HDC hdc = GetWindowDC(hwnd);
            RECT rc;
            GetWindowRect(hwnd, &rc);
            OffsetRect(&rc, -rc.left, -rc.top);
            const bool focused = (GetFocus() == hwnd);
            HBRUSH br = CreateSolidBrush(focused ? kBorderFocus
                                                 : kBorderIdle);
            FrameRect(hdc, &rc, br);
            DeleteObject(br);
            ReleaseDC(hwnd, hdc);
            return 0;
        }

        case WM_NCCALCSIZE:
            // Mantenemos el borde estándar del EDIT (sin frame nativo).
            break;

        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

bool Omnibox::WantsDialogMessages(const MSG& msg) {
    // Solo interceptamos teclado mientras el omnibox tiene el foco.
    return false;   // v1: el subclass ya maneja Enter/Esc directamente.
}

} // namespace smac::window
