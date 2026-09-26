/** ==========================================================================
 *  browser/ResourceBlocker.hpp — uBlock Origin + cirugía DOM (browser/).
 *  ----------------------------------------------------------------------------
 *  SRP: todo lo que "bloquea recursos" vive aquí:
 *   1. Carga de uBlock Origin desde la carpeta descomprimida
 *      (AddBrowserExtensionAsync, vía ICoreWebView2Profile7).
 *   2. Inyección temprana del script de cirugía DOM (AddScriptToExecute
 *      OnDocumentCreated) en cada pestaña que se cree.
 *  ========================================================================== */
#pragma once

#include "smac/ComPtr.hpp"

#include <windows.h>
#include <WebView2.h>

namespace smac::web {

/**
 * Instala uBlock Origin si existe la carpeta <exe>\extensions\uBlock.
 * No bloquea: el resultado llega por log (éxito/código).
 * Devuelve true si la instalación fue despachada.
 */
bool InstallUblockOrigin(const smac::ComPtr<ICoreWebView2Environment>& env,
                         const smac::ComPtr<ICoreWebView2Profile>& profile);

/** Inyecta el script de cirugía DOM en una pestaña (asíncrono). */
void InjectDomSurgery(const smac::ComPtr<ICoreWebView2>& webview);

/** ¿Existe la carpeta de uBlock junto al exe? (diagnóstico). */
bool UblockFolderExists();

} // namespace smac::web
