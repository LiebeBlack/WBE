/** ==========================================================================
 *  window/TabStrip.hpp — Tira de pestañas (módulo window/).
 *  ----------------------------------------------------------------------------
 *  Owner-drawn. WindowHost la alimenta con el estado de TabManager (vía
 *  App) y ella solo pinta y reporta clicks. Sin STL dinámico por frame.
 *  ========================================================================== */
#pragma once

#include <windows.h>
#include <vector>

namespace smac::window {

/** Datos que la App empuja a la tira antes de cada repaint. */
struct TabView {
    const wchar_t* title;    ///< Puntero prestado (App posee el string)
    bool active;
    bool loading;
    bool muted;              ///< Reproduciendo audio (nota en la UI)
};

/** Tira de pestañas owner-drawn (ventana hija). */
class TabStrip {
public:
    enum Cmd : int {
        kNewTab = 2101,
        kSelectBase = 2200,   ///< kSelectBase + i = seleccionar pestaña i
        kCloseBase  = 2300,   ///< kCloseBase  + i = cerrar pestaña i
    };

    TabStrip() = default;
    ~TabStrip();

    bool Create(HWND parent, HINSTANCE hInstance);

    /** Reemplaza el estado visual y repinta (llamada barata). */
    void SetTabs(const TabView* tabs, int count);

    void SetBounds(const RECT& rc);

    /** Visibilidad (fullscreen de contenido). */
    void SetVisibleSelf(bool visible);

    /** Visibilidad (fullscreen de contenido). */
    void SetVisibleSelf(bool visible);

    HWND Hwnd() const { return hwnd_; }

private:
    /** Pinta la tira completa (BeginPaint interno). */
    void PaintStrip();

    /** Ancho efectivo de pestaña (coherente entre pintado y hit-test). */
    int TabWidthAt(int index) const;

    /** Índice de pestaña bajo pt, o -1 (kNewTab si es el botón +). */
    int HitTest(POINT pt) const;

    static LRESULT CALLBACK ProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    std::vector<TabView> views_;
    HFONT font_ = nullptr;
    int hoverIndex_ = -1;
    int closeHover_ = -1;
    RECT bounds_{};
};

} // namespace smac::window
