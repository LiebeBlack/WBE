/** ==========================================================================
 *  browser/ResourceBlocker.cpp — uBlock Origin (API oficial) + DOM surgery.
 *  ----------------------------------------------------------------------------
 *  Ruta de uBO: <exe>\extensions\uBlock (carpeta descomprimida con
 *  manifest.json en la raíz). El release de CI la trae; un usuario puede
 *  sustituirla por su propia copia sin recompilar.
 *  ========================================================================== */
#include "ResourceBlocker.hpp"

#include "DomSurgeryScript.hpp"
#include "smac/Log.hpp"
#include "system/Paths.hpp"

#include <string>
#include <fileapi.h>
#include <WebView2.h>

namespace smac::web {
namespace {

/** Carpeta de uBO dentro del dir de extensiones. */
std::wstring UblockPath() {
    return std::wstring(paths::ExtensionsDir()) + L"\\uBlock";
}

/** Handler asíncrono de AddBrowserExtension (log + release). */
class UblockInstallHandler final
    : public ICoreWebView2ProfileAddBrowserExtensionCompletedHandler {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown ||
            riid == __uuidof(
                ICoreWebView2ProfileAddBrowserExtensionCompletedHandler)) {
            *out = static_cast<
                ICoreWebView2ProfileAddBrowserExtensionCompletedHandler*>(
                this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Invoke(
        HRESULT errorCode, ICoreWebView2BrowserExtension* result) override {
        if (SUCCEEDED(errorCode) && result) {
            log::Info("ublock: instalada correctamente");
        } else {
            log::Warn("ublock: fallo de instalacion (0x%08X)", errorCode);
        }
        delete this;
        return S_OK;
    }

private:
    ULONG refs_ = 1;
};

/** Handler de AddScriptToExecuteOnDocumentCreated (libera sin log). */
class ScriptInjectHandler final
    : public ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown ||
            riid == __uuidof(
                ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler)) {
            *out = static_cast<
                ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler*>(
                this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,
                                     LPCWSTR) override {
        if (FAILED(errorCode)) {
            log::Warn("dom: fallo de inyeccion (0x%08X)", errorCode);
        }
        delete this;
        return S_OK;
    }

private:
    ULONG refs_ = 1;
};

} // namespace

bool UblockFolderExists() {
    const std::wstring p = UblockPath() + L"\\manifest.json";
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool InstallUblockOrigin(const smac::ComPtr<ICoreWebView2Environment>& env,
                         const smac::ComPtr<ICoreWebView2Profile>& profile) {
    (void)env;   // reservado: el entorno podría validar versiones futuras

    if (!profile) {
        log::Warn("ublock: sin perfil, no se instala");
        return false;
    }
    if (!UblockFolderExists()) {
        log::Warn(
            "ublock: carpeta no encontrada (<exe>\\extensions\\uBlock); "
            "sin uBO esta sesion");
        return false;
    }

    // La API vive en ICoreWebView2Profile7 (SDK >= 1.0.1661.34).
    smac::ComPtr<ICoreWebView2Profile7> p7;
    if (FAILED(profile.As(&p7)) || !p7) {
        log::Warn("ublock: runtime sin ICoreWebView2Profile7 (motor viejo)");
        return false;
    }

    const std::wstring folder = UblockPath();
    const HRESULT hr = p7->AddBrowserExtension(
        folder.c_str(), new UblockInstallHandler());
    if (FAILED(hr)) {
        log::Warn("ublock: AddBrowserExtension fallo (0x%08X)", hr);
        return false;
    }
    log::Info("ublock: instalacion despachada desde %ls", folder.c_str());
    return true;
}

void InjectDomSurgery(const smac::ComPtr<ICoreWebView2>& webview) {
    if (!webview) return;
    webview->AddScriptToExecuteOnDocumentCreated(
        kDomSurgeryScript, new ScriptInjectHandler());
}

} // namespace smac::web
