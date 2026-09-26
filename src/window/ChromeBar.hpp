/** ==========================================================================
 *  window/ChromeBar.hpp — Barra superior (módulo window/).
 *  ----------------------------------------------------------------------------
 *  Fila superior: [<-] [->] [R] [ Home ] + Omnibox + caption(min/max/close).
 *  Owner-drawn con UxTheme Buffered Paint (doble buffer, sin parpadeo).
 *  ========================================================================== */
#pragma once

#include <windows.h>

namespace smac::window {

class Omnibox;

/** Barra de herramientas superior (una por ventana). */
class ChromeBar {
public:
    /** IDs de WM_COMMAND propios. */
    enum Cmd : int {
        kBack   = 2001,
        kForward = 2002,
        kReload = 2003,
        kStop   = 2004,
        kHome   = 2005,
    };

    /** Resultado de CaptionHit (mapea a HT*BUTTON en WindowHost). */
    enum class CaptionHit { None, Min, Max, Close };

    /**
     * Activa los caption buttons reales (min/max/close) dibujados en la
     * barra y reportados por CaptionHit. Los devuelve WindowHost a través
     * de WM_NCHITTEST (HTMINBUTTON/HTMAXBUTTON/HTCLOSEBUTTON).
     */
    void EnableCaptionButtons(bool on) { captionButtons_ = on; }

    ChromeBar() = default;
    ~ChromeBar();

    bool Create(HWND parent, HINSTANCE hInstance, Omnibox* omnibox);

    /** Nueva geometría (la llama WindowHost en cada layout). */
    void SetBounds(const RECT& rc);

    /** Rect del EDIT del omnibox dentro de la barra. */
    RECT OmniboxRect() const;

    /** ¿Es zona de arrastre (no botón, no omnibox)? -> HTCAPTION. */
    bool IsDragZone(POINT clientPt) const;

    /** ¿Qué botón de caption hay bajo este punto (cliente)? */
    CaptionHit CaptionHit(POINT clientPt) const;

    /** Invalida la barra (repintado asíncrono barato). */
    void Repaint();

    /** Visibilidad de la barra (fullscreen de contenido). */
    void SetVisibleSelf(bool visible);

    /** Estado visual: habilita atrás/adelante/recargar. */
    void SetNavState(bool canBack, bool canForward, bool loading);

    HWND Hwnd() const { return hwnd_; }

    /**
     * Dispatcher de mensajes (público: lo invoca el thunk WndProc).
     * Devuelve -1 si el mensaje no se gestionó (-> DefWindowProc).
     */
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    /** Botón bajo un punto (cliente), o -1. */
    int ButtonAt(POINT pt) const;

    void PaintButtons();

    HWND      hwnd_ = nullptr;
    Omnibox*  omnibox_ = nullptr;
    RECT      bounds_{};
    HFONT     fontGlyph_ = nullptr;
    HFONT     fontText_ = nullptr;
    int       hoverId_ = -1;
    bool      captionButtons_ = false;
    bool      canBack_ = false;
    bool      canForward_ = false;
    bool      loading_ = false;
};

} // namespace smac::window
