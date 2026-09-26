/** ==========================================================================
 *  window/Omnibox.hpp — Barra de direcciones (módulo window/).
 *  ----------------------------------------------------------------------------
 *  Un EDIT subclasificado: fondo negro, texto gris claro, borde de foco
 *  cian. La heurística URL-vs-búsqueda vive aquí (SRP); la App recibe el
 *  texto final y decide a dónde navegar.
 *  ========================================================================== */
#pragma once

#include <windows.h>

namespace smac::window {

/** EDIT de direcciones con subclass nativa (SetWindowSubclass). */
class Omnibox {
public:
    /** ID de WM_COMMAND para Enter (la App lee el texto con GetText). */
    static constexpr int kNavigateCmd = 2401;

    Omnibox() = default;
    ~Omnibox();

    bool Create(HWND parent, HINSTANCE hInstance);

    void SetBounds(const RECT& rc);

    /** Visibilidad (fullscreen de contenido). */
    void SetVisibleSelf(bool visible);

    /** Texto actual (UTF-16, acotado). Devuelve chars copiados. */
    int GetText(wchar_t* out, int maxChars) const;

    /** Reemplaza el texto y selecciona todo (navegación externa). */
    void SetText(const wchar_t* text);

    /** Toma el foco con todo seleccionado (Ctrl+L). */
    void FocusAll();

    HWND Hwnd() const { return hwnd_; }

    /** IsDialogMessage debe procesar Tab/Enter sobre este EDIT. */
    static bool WantsDialogMessages(const MSG& msg);

private:
    /** Subclass: Enter navega, Esc limpia, foco pinta el borde. */
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM,
                                     UINT_PTR, DWORD_PTR);

    HWND hwnd_ = nullptr;
};

/** Heurística compartida: ¿este texto es URL o consulta de buscador? */
bool LooksLikeUrl(const wchar_t* text);

} // namespace smac::window
