/** ==========================================================================
 *  config/Config.hpp — Carga/persistencia de smac.ini (módulo config/).
 *  ----------------------------------------------------------------------------
 *  Usa GetPrivateProfileStringW / WritePrivateProfileStringW: sin STL, sin
 *  asignación dinámica, sin excepciones. Si el ini no existe se crea con
 *  valores por defecto documentados.
 *  ========================================================================== */
#pragma once

#include "smac/Config.hpp"

namespace smac::config {

/** Carga (o crea) %LOCALAPPDATA%\SMAC\WebCore\smac.ini y rellena out. */
void Load(smac::Config* out);

/** Guarda los valores actuales (escritura atómica del ini). */
void Save(const smac::Config& cfg);

/** Ruta completa del ini (para diagnóstico). */
const wchar_t* IniPath();

} // namespace smac::config
