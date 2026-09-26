/** ==========================================================================
 *  system/Paths.hpp — Resolución única de rutas del sistema.
 *  ----------------------------------------------------------------------------
 *  Toda ruta que use S.M.A.C pasa por aquí (SRP). Cálculo lazy + caché.
 *  ========================================================================== */
#pragma once

#include <windows.h>

namespace smac::paths {

/** Binario actual: <carpeta del exe> (sin barra final). */
const wchar_t* ExeDir();

/** Datos por usuario: %LOCALAPPDATA%\SMAC\WebCore (se crea si falta). */
const wchar_t* UserDir();

/** Carpeta de extensiones esperada junto al exe (no se crea). */
const wchar_t* ExtensionsDir();

/** Escritorio del usuario (para diálogos de descarga). */
const wchar_t* DesktopDir();

/** Nombre canónico de la base del perfil del motor (sin ruta). */
constexpr wchar_t kProfileDirName[] = L"EngineProfile";

/** Nombre del archivo de sesión (pestañas restaurables). */
constexpr wchar_t kSessionFile[] = L"session.txt";

/** Nombre del ini de configuración. */
constexpr wchar_t kConfigFile[] = L"smac.ini";

} // namespace smac::paths
