/** ==========================================================================
 *  browser/DomSurgeryScript.hpp — Cirugía DOM temprana (módulo browser/).
 *  ----------------------------------------------------------------------------
 *  Un único archivo de datos: el JavaScript que se inyecta con
 *  AddScriptToExecuteOnDocumentCreated en TODA pestaña nueva.
 *  Objetivo: destruir nodos pesados ANTES de que el motor pinte:
 *   - YouTube: Shorts, barra lateral, chat en vivo, estanterías, comments.
 *   - Regla genérica: `display:none` para selectores de ruido conocidos.
 *  MutationObserver: los sitios modernos reconstruyen el DOM (SPA), así
 *  que re-aplicamos en cada mutación (con filtro por selector).
 *  ========================================================================== */
#pragma once

namespace smac::web {

/** Script de cirugía (raw string: sin escapes, sin recursos externos). */
inline constexpr wchar_t kDomSurgeryScript[] = LR"JS(
(function () {
  'use strict';

  // Selectores que S.M.A.C elimina del DOM (YouTube + genéricos).
  var KILL = [
    // YouTube
    'ytd-mini-guide-renderer',            // mini barra lateral
    '#guide',                              // sidebar completa
    'ytd-rich-shelf-renderer',            // estanterías del home
    'ytd-reel-shelf-renderer',            // fila de Shorts en el home
    'ytd-shorts',                          // vista de Shorts
    '#secondary.ytd-watch-flexy',          // sidebar del watch (up-next)
    'ytd-watch-next-secondary-results-renderer',
    '#chat',                               // chat en vivo
    '#panels',                             // paneles laterales del watch
    'ytd-comments',                        // comentarios (opcional pero pesado)
    'ytd-popup-container ytd-mealbar-promo-renderer',
    // Genéricos de ruido
    'ins.adsbygoogle',
    '[data-ad]',
    '[aria-label="Anuncios"]'
  ];

  var once = false;

  function cssHideAll() {
    if (!document.documentElement) return;
    if (!once) {
      var st = document.createElement('style');
      st.id = 'smac-surgery';
      st.textContent = KILL.join(',') + '{display:none !important;}';
      (document.head || document.documentElement).appendChild(st);
      once = true;
    }
  }

  // Eliminación real (no solo ocultar) para liberar los nodos antes de
  // que el compositor los rasterice.
  function destroy() {
    for (var i = 0; i < KILL.length; ++i) {
      var nodes = document.querySelectorAll(KILL[i]);
      for (var j = 0; j < nodes.length; ++j) {
        var n = nodes[j];
        if (n && n.parentNode) n.parentNode.removeChild(n);
      }
    }
  }

  function sweep() {
    cssHideAll();
    destroy();
  }

  // Pasa inicial (document_created => DOM aún vacío pero head listo).
  sweep();

  // Re-aplicación sobre SPAs (YouTube reconstruye nodos constantemente).
  var mo = new MutationObserver(function () { sweep(); });
  function arm() {
    if (document.body) {
      mo.observe(document.body, { childList: true, subtree: true });
    } else {
      document.addEventListener('DOMContentLoaded', function () {
        mo.observe(document.body, { childList: true, subtree: true });
      });
    }
  }
  arm();

  // Pase final cuando el DOM está completo.
  document.addEventListener('DOMContentLoaded', sweep);
})();
)JS";

} // namespace smac::web
