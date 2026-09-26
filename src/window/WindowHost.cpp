/** ==========================================================================
 *  window/WindowHost.cpp — Win32 puro: frameless, DWM, hit-testing nativo.
 *  ----------------------------------------------------------------------------
 *  Decisiones de rendimiento:
 *   - Cero WM_PAINT: el fondo lo compone DWM (DwmExtendFrameIntoClientArea
 *     con margins -1 = "sheet of glass" sobre fondo de clase negro puro).
 *   - El borde neón de 1 px lo pinta el DWM (DWMWA_BORDER_COLOR, Win11) o,
 *     en Win10, un único FrameRect en el mensaje WM_PAINT (que solo llega
 *     tras invalidaciones explícitas y raras).
 *   - El arrastre/redimensionado es del sistema: HTCAPTION/HTBORDER de
 *     WM_NCHITTEST. Los botones de la barra usan HT*BUTTON (snap nativo).
 *  ========================================================================== */
#include "WindowHost.hpp"
#include "ChromeBar.hpp"
#include "TabStrip.hpp"
#include "Omnibox.hpp"

#include "smac/Log.hpp"
#include "config/Config.hpp"
#include "system/MemoryOptimizer.hpp"

#include <dwmapi.h>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")

namespace smac::window {
namespace {

constexpr wchar_t kClassName[] = L"SMAC_WebCore_Host";

/** HIWORD propio del WM_COMMAND de Enter (evita colisiones con EN_*). */
constexpr int kOmniboxEnter = 0x1234;

/** WM_APP::wParam que la tira usa para pedir el menú de app. */
constexpr WPARAM kAppMenuMsg = 0x414D4D31;  // 'AMM'

/** Extrae el WindowHost* del GWLP_USERDATA. */
WindowHost* HostOf(HWND hwnd) {
    return reinterpret_cast<WindowHost*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

/** Convierte coordenadas de pantalla del lParam a cliente. */
POINT ClientPoint(LPARAM lParam, HWND hwnd) {
    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ScreenToClient(hwnd, &pt);
    return pt;
}

/**
 * Escala un valor de 96 DPI al DPI del monitor primario. Los hijos lo
 * llaman en cada layout; el valor se cachea por proceso (los cambios de
 * DPI en caliente no son un caso de uso del N4120).
 */
int ScaleAtDpi(int value) {
    static UINT dpi = 0;   // caché: un solo GetDpiForSystem
    if (dpi == 0) {
        dpi = GetDpiForSystem();
        if (dpi == 0) dpi = 96;
    }
    return MulDiv(value, static_cast<int>(dpi), 96);
}

} // namespace

// ============================ Ciclo de vida =================================

WindowHost::~WindowHost() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool WindowHost::Create(HINSTANCE hInstance,
                        const smac::Config& cfg,
                        ClientBridge* bridge,
                        ChromeBar* chrome,
                        TabStrip* tabs,
                        Omnibox* omnibox) {
    hInstance_ = hInstance;
    bridge_    = bridge;
    chrome_    = chrome;
    tabs_      = tabs;
    omnibox_   = omnibox;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &WindowHost::WndProcThunk;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));   // Pitch-black puro
    wc.lpszClassName = kClassName;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm       = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        log::Error("win: RegisterClassExW fallo (%lu)", GetLastError());
        return false;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;   // El frame se elimina en NCCALCSIZE
    RECT r{ cfg.windowX, cfg.windowY,
            cfg.windowX + cfg.windowW, cfg.windowY + cfg.windowH };
    if (cfg.windowX == CW_USEDEFAULT) {
        r.right  = cfg.windowW;
        r.bottom = cfg.windowH;
    }
    AdjustWindowRect(&r, style, FALSE);

    hwnd_ = CreateWindowExW(
        0, kClassName, SMAC_PRODUCT_NAME_W L" " SMAC_VERSION_W, style,
        cfg.windowX == CW_USEDEFAULT ? CW_USEDEFAULT : cfg.windowX,
        cfg.windowY == CW_USEDEFAULT ? CW_USEDEFAULT : cfg.windowY,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance, this);
    if (!hwnd_) {
        log::Error("win: CreateWindowExW fallo (%lu)", GetLastError());
        return false;
    }

    ApplyBlackBackground();
    ApplyAccent(cfg.accent);

    ShowWindow(hwnd_, cfg.maximizeStart ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd_);
    log::Info("win: ventana creada (%dx%d)", cfg.windowW, cfg.windowH);
    return true;
}

int WindowHost::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // El omnibox es un EDIT: IsDialogMessage le da Tab/cursor correctos.
        if (Omnibox::WantsDialogMessages(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ============================ DWM / estilo ==================================

void WindowHost::ApplyBlackBackground() {
    // Sheet of glass: el DWM compone nuestro fondo de clase (#000000).
    MARGINS margins{-1, -1, -1, -1};
    if (FAILED(DwmExtendFrameIntoClientArea(hwnd_, &margins))) {
        log::Warn("win: DwmExtendFrameIntoClientArea fallo");
    }
}

void WindowHost::ApplyAccent(smac::AccentColor accent) {
    const COLORREF c = smac::config::AccentColorRef(accent);
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_NONE_COLOR
#define DWMWA_NONE_COLOR 0xFFFFFFFE
#endif
    // Win11: borde neón de 1 px nativo del DWM. En Win10 la llamada falla
    // y quedará el marco GDI de respaldo (pintado solo en WM_PAINT).
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &c, sizeof(c));

    // Barra de título del DWM en negro (afecta a snap preview).
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
    const COLORREF black = RGB(0, 0, 0);
    DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &black, sizeof(black));

    // Modo inmersivo oscuro (afecta a los botones min/max/close del SO).
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &dark, sizeof(dark));
}

void WindowHost::RepaintChrome() {
    if (chrome_) chrome_->Repaint();
    if (tabs_)   tabs_->Repaint();
}

void WindowHost::ForceLayout() {
    RECT rc;
    if (GetClientRect(hwnd_, &rc)) {
        clientW_ = rc.right - rc.left;
        clientH_ = rc.bottom - rc.top;
    }
    Layout();
}

// ============================ Layout ========================================

void WindowHost::Layout() {
    if (!chrome_ || !tabs_ || !omnibox_) return;

    // Fullscreen de contenido: el motor ocupa TODO el cliente.
    if (chromeHidden_) {
        RECT rcContent{ 0, 0, clientW_, clientH_ };
        chrome_->SetBounds(RECT{0, 0, 0, 0});
        tabs_->SetBounds(RECT{0, 0, 0, 0});
        omnibox_->SetBounds(RECT{0, 0, 0, 0});
        tabStripRect_ = RECT{};
        contentRect_  = rcContent;
        if (bridge_) bridge_->OnWindowResize(clientW_, clientH_);
        return;
    }

    const int top = ScaleAtDpi(kTopBarHeight);
    const int strip = ScaleAtDpi(kTabStripHeight);

    // Fila 1: ChromeBar (botones + omnibox + caption). Fila 2: pestañas.
    RECT rcBar{ 0, 0, clientW_, top };
    RECT rcStrip{ 0, top, clientW_, top + strip };
    RECT rcContent{ 0, top + strip, clientW_, clientH_ };

    chrome_->SetBounds(rcBar);
    tabs_->SetBounds(rcStrip);
    omnibox_->SetBounds(chrome_->OmniboxRect());

    tabStripRect_ = rcStrip;
    contentRect_  = rcContent;

    if (bridge_) bridge_->OnWindowResize(rcContent.right - rcContent.left,
                                         rcContent.bottom - rcContent.top);
}

void WindowHost::SetChromeHidden(bool hidden) {
    if (chromeHidden_ == hidden) return;
    chromeHidden_ = hidden;
    if (chrome_) chrome_->SetVisibleSelf(hidden ? false : true);
    if (tabs_)   tabs_->SetVisibleSelf(hidden ? false : true);
    if (omnibox_) omnibox_->SetVisibleSelf(hidden ? false : true);
    Layout();
}

void WindowHost::OnSize(int w, int h) {
    clientW_ = w;
    clientH_ = h;
    Layout();
}

// ============================ WndProc =======================================

LRESULT CALLBACK WindowHost::WndProcThunk(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (auto* self = HostOf(hwnd)) {
        LRESULT r = self->HandleMessage(hwnd, msg, wParam, lParam);
        if (r != -1) return r;   // -1 = "no gestionado"
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT WindowHost::HandleMessage(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCCALCSIZE:
            return OnNCCalcSize(wParam);

        case WM_NCHITTEST:
            return OnNCHitTest(lParam);

        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_ERASEBKGND:
            // DWM ya compuso el fondo: no hay parpadeo ni gasto de GDI.
            return 1;

        case WM_PAINT: {
            // Solo llega si algo invalida; usamos BeginPaint para vaciar.
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_TIMER:
            // El temporizador de trim lleva su propio TIMERPROC (ver
            // MemoryOptimizer::StartTrimTimer): no pasa por aquí.
            break;

        case WM_COMMAND: {
            // Router único de WM_COMMAND: chrome, pestañas y omnibox.
            const int id = LOWORD(wParam);
            const int notif = HIWORD(wParam);

            if (bridge_) {
                // --- ChromeBar (botones de navegación) -------------------
                switch (id) {
                    case ChromeBar::kBack:    bridge_->OnBack();    goto done_cmd;
                    case ChromeBar::kForward: bridge_->OnForward(); goto done_cmd;
                    case ChromeBar::kReload:  bridge_->OnReload();  goto done_cmd;
                    case ChromeBar::kStop:    bridge_->OnStop();    goto done_cmd;
                    case ChromeBar::kHome:    bridge_->OnHome();    goto done_cmd;
                    default: break;
                }

                // --- TabStrip (+, seleccionar, cerrar) -------------------
                if (id == TabStrip::kNewTab) {
                    bridge_->OnNewTab();
                    goto done_cmd;
                }
                if (id >= TabStrip::kSelectBase &&
                    id < TabStrip::kSelectBase + 64) {
                    bridge_->OnSelectTab(id - TabStrip::kSelectBase);
                    goto done_cmd;
                }
                if (id >= TabStrip::kCloseBase &&
                    id < TabStrip::kCloseBase + 64) {
                    bridge_->OnCloseTab(id - TabStrip::kCloseBase);
                    goto done_cmd;
                }

                // --- Omnibox: Enter (señal propia, no EN_CHANGE) ----------
                if (id == Omnibox::kNavigateCmd && notif == kOmniboxEnter &&
                    omnibox_) {
                    wchar_t buf[2048];
                    omnibox_->GetText(buf, 2048);
                    bridge_->OnNavigate(buf);
                    return 0;
                }
            }

            if (chrome_) chrome_->OnCommand(id);
        done_cmd:
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            // Colores del omnibox: fondo #121214, texto claro. El pincel
            // vive durante todo el proceso (un solo objeto, cero leaks).
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(18, 18, 20));
            SetTextColor(hdc, RGB(220, 220, 220));
            static HBRUSH brush = CreateSolidBrush(RGB(18, 18, 20));
            return reinterpret_cast<LRESULT>(brush);
        }

        case WM_APP:
            if (wParam == kAppMenuMsg) {
                // Petición de la tira: abrir el menú de app en el cursor.
                POINT pt;
                GetCursorPos(&pt);
                if (bridge_) bridge_->OnShowMenu(pt);
                return 0;
            }
            break;

        case WM_APP + 1:
            // Notificación de la pestaña activa (título/progreso).
            if (bridge_) bridge_->OnUpdateUIState();
            return 0;

        case WM_CLOSE:
            // El cierre pasa por el bridge: la App guarda sesión/placement.
            if (bridge_) bridge_->OnCloseRequest();
            return 0;

        case WM_DESTROY:
            mem::StopTrimTimer(hwnd_);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return -1;   // sin gestión: DefWindowProc
}

LRESULT WindowHost::OnNCCalcSize(WPARAM wParam) {
    // Frameless real: el cliente ocupa TODA la ventana (0 píxeles de frame).
    if (wParam) return 0;
    return -1;
}

LRESULT WindowHost::OnNCHitTest(LPARAM lParam) {
    const POINT pt = ClientPoint(lParam, hwnd_);

    RECT rc;
    GetClientRect(hwnd_, &rc);

    // --- Bordes de redimensionado (8 px exteriores) ------------------------
    constexpr int kResizeBorder = 8;
    const bool left   = pt.x < kResizeBorder;
    const bool right  = pt.x >= rc.right - kResizeBorder;
    const bool top    = pt.y < kResizeBorder;
    const bool bottom = pt.y >= rc.bottom - kResizeBorder;

    if (top && left)     return HTTOPLEFT;
    if (top && right)    return HTTOPRIGHT;
    if (bottom && left)  return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left)            return HTLEFT;
    if (right)           return HTRIGHT;
    if (top)             return HTTOP;
    if (bottom)          return HTBOTTOM;

    // --- Botones de la barra (caption buttons nativos: snap incluido) -----
    if (chrome_) {
        switch (chrome_->CaptionHit(pt)) {
            case ChromeBar::CaptionHit::Close: return HTCLOSE;
            case ChromeBar::CaptionHit::Max:   return HTMAXBUTTON;
            case ChromeBar::CaptionHit::Min:   return HTMINBUTTON;
            default: break;
        }
    }

    // --- Franja arrastrable: zona vacía de la barra superior ---------------
    if (pt.y < ScaleAtDpi(kTopBarHeight) && chrome_ &&
        chrome_->IsDragZone(pt)) {
        return HTCAPTION;
    }

    return HTCLIENT;
}

} // namespace smac::window
