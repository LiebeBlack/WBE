/** ==========================================================================
 *  browser/WebViewEngine.cpp — Flags de Chromium y opciones del entorno.
 *  ----------------------------------------------------------------------------
 *  Flags exigidos por el diseño (todos auditables aquí):
 *   --renderer-process-limit=2        --max_old_space_size=350
 *   --disable-features=IsolateOrigins,site-per-process
 *   --enable-zero-copy --enable-gpu-rasterization --ignore-gpu-blocklist
 *   --enable-features=Vulkan (configurable: el driver UHD 600 a veces falla)
 *   telemetría/sync/crash-report: OFF (EnvironmentOptions2/3)
 *  ========================================================================== */
#include "WebViewEngine.hpp"

#include "smac/Log.hpp"
#include "smac/Version.hpp"
#include "system/Paths.hpp"

#include <string>
#include <objbase.h>

// Versión pinnada del SDK (debe casar con cmake/WebView2.cmake; el build
// la inyecta normalmente como definición del compilador).
#ifndef SMAC_WV2_TARGET_VERSION
#define SMAC_WV2_TARGET_VERSION "1.0.2957.106"
#endif

namespace smac::web {
namespace {

wchar_t g_lastError[128] = L"ok";

/** Construye la lista AdditionalBrowserArguments (una sola vez). */
std::wstring BuildChromiumFlags(const smac::Config& cfg) {
    std::wstring s;
    s.reserve(512);

    // --- Presupuesto V8 y de procesos (exigencias del diseño) -------------
    s += L" --renderer-process-limit=";
    s += std::to_wstring(cfg.rendererLimit);
    s += L" --max_old_space_size=";
    s += std::to_wstring(cfg.v8HeapMB);

    // Un solo pool de renderers para todos los sitios (menos RAM).
    s += L" --disable-features=IsolateOrigins,site-per-process";

    // --- Zero-copy / GPU (Intel UHD 600) -----------------------------------
    s += L" --enable-zero-copy --enable-gpu-rasterization";
    s += L" --ignore-gpu-blocklist";
    if (cfg.enableVulkan) {
        s += L" --enable-features=Vulkan";
    }

    // --- Anti-telemetría / anti-redes en segundo plano ----------------------
    s += L" --disable-background-networking --disable-component-update"
         L" --disable-sync --disable-domain-reliability"
         L" --no-first-run --no-default-browser-check";
    return s;
}

/** Copia acotada hacia CoTaskMemAlloc (contrato get_* de WebView2). */
HRESULT DupString(const wchar_t* src, LPWSTR* out) {
    if (!out) return E_POINTER;
    const int chars = lstrlenW(src) + 1;
    wchar_t* copy = static_cast<wchar_t*>(CoTaskMemAlloc(chars * sizeof(wchar_t)));
    if (!copy) return E_OUTOFMEMORY;
    lstrcpynW(copy, src, chars);
    *out = copy;
    return S_OK;
}

/**
 * Objeto COM único que implementa Options (base) + 2 + 3 + 6.
 * Sin RTTI: QueryInterface manual contra los IIDs del SDK.
 */
class OptionsImpl final
    : public ICoreWebView2EnvironmentOptions,
      public ICoreWebView2EnvironmentOptions2,
      public ICoreWebView2EnvironmentOptions3,
      public ICoreWebView2EnvironmentOptions6 {
public:
    explicit OptionsImpl(const smac::Config& cfg)
        : flags_(BuildChromiumFlags(cfg)),
          language_(L"es-ES"),
          targetVersion_(L"" SMAC_WV2_TARGET_VERSION),
          crashReporting_(FALSE),
          exclusiveUserData_(TRUE),
          extensionsEnabled_(TRUE) {}

    // --- IUnknown ------------------------------------------------------------
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                             void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown ||
            riid == __uuidof(ICoreWebView2EnvironmentOptions))
            *out = static_cast<ICoreWebView2EnvironmentOptions*>(this);
        else if (riid == __uuidof(ICoreWebView2EnvironmentOptions2))
            *out = static_cast<ICoreWebView2EnvironmentOptions2*>(this);
        else if (riid == __uuidof(ICoreWebView2EnvironmentOptions3))
            *out = static_cast<ICoreWebView2EnvironmentOptions3*>(this);
        else if (riid == __uuidof(ICoreWebView2EnvironmentOptions6))
            *out = static_cast<ICoreWebView2EnvironmentOptions6*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    // --- ICoreWebView2EnvironmentOptions --------------------------------------
    HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments(
        LPWSTR* value) override {
        return DupString(flags_.c_str(), value);
    }
    HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments(
        LPCWSTR value) override {
        if (value) flags_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Language(LPWSTR* value) override {
        return DupString(language_.c_str(), value);
    }
    HRESULT STDMETHODCALLTYPE put_Language(LPCWSTR value) override {
        if (value) language_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion(
        LPWSTR* value) override {
        return DupString(targetVersion_.c_str(), value);
    }
    HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion(
        LPCWSTR value) override {
        if (value) targetVersion_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount(
        BOOL* value) override {
        *value = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount(
        BOOL value) override {
        (void)value;
        return S_OK;
    }

    // --- ICoreWebView2EnvironmentOptions2 --------------------------------------
    HRESULT STDMETHODCALLTYPE get_ExclusiveUserDataFolderAccess(
        BOOL* value) override {
        *value = exclusiveUserData_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_ExclusiveUserDataFolderAccess(
        BOOL value) override {
        exclusiveUserData_ = value;
        return S_OK;
    }

    // --- ICoreWebView2EnvironmentOptions3 --------------------------------------
    HRESULT STDMETHODCALLTYPE get_IsCustomCrashReportingEnabled(
        BOOL* value) override {
        *value = crashReporting_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsCustomCrashReportingEnabled(
        BOOL value) override {
        crashReporting_ = value;
        return S_OK;
    }

    // --- ICoreWebView2EnvironmentOptions6 --------------------------------------
    HRESULT STDMETHODCALLTYPE get_AreBrowserExtensionsEnabled(
        BOOL* value) override {
        *value = extensionsEnabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AreBrowserExtensionsEnabled(
        BOOL value) override {
        extensionsEnabled_ = value;
        return S_OK;
    }

private:
    std::wstring flags_;
    std::wstring language_;
    std::wstring targetVersion_;
    BOOL crashReporting_;
    BOOL exclusiveUserData_;
    BOOL extensionsEnabled_;
    ULONG refs_ = 1;
};

/** Handler de creación de entorno (SRP: espera y retiene el resultado). */
class EnvCreatedHandler
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    EnvCreatedHandler(HRESULT* hrOut, HANDLE done)
        : hrOut_(hrOut), done_(done) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                             void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown ||
            riid ==
                __uuidof(
                    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *out = static_cast<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(
                this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Invoke(
        HRESULT errorCode, ICoreWebView2Environment* env) override {
        *hrOut_ = errorCode;
        env_.Attach(env);
        SetEvent(done_);
        return S_OK;
    }

    smac::ComPtr<ICoreWebView2Environment> env_;

private:
    HRESULT* hrOut_;
    HANDLE done_;
    ULONG refs_ = 1;
};

} // namespace

const wchar_t* LastEngineError() { return g_lastError; }

/** Fábrica del objeto de opciones (único punto que conoce OptionsImpl). */
static smac::ComPtr<ICoreWebView2EnvironmentOptions> CreateOptionsObject(
    const smac::Config& cfg) {
    smac::ComPtr<ICoreWebView2EnvironmentOptions> opts;
    *opts.operator&() = new OptionsImpl(cfg);
    return opts;
}

smac::ComPtr<ICoreWebView2Environment> CreateEnvironment(
    const smac::Config& cfg) {
    lstrcpynW(g_lastError, L"ok", 128);

    smac::ComPtr<ICoreWebView2EnvironmentOptions> opts =
        CreateOptionsObject(cfg);
    if (!opts) {
        lstrcpynW(g_lastError, L"sin memoria para options", 128);
        return nullptr;
    }

    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done) {
        lstrcpynW(g_lastError, L"sin memoria para evento", 128);
        return nullptr;
    }

    HRESULT hr = E_FAIL;
    auto* handler = new EnvCreatedHandler(&hr, done);

    // userDataFolder dedicado: aislado del Edge del sistema (Options2 lo
    // declara exclusivo para este host).
    std::wstring ud = std::wstring(paths::UserDir()) + L"\\" +
                      paths::kProfileDirName;

    const HRESULT createHr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,          // browserExecutableFolder: auto (Evergreen)
        ud.c_str(),
        opts.Get(),
        handler);
    if (FAILED(createHr)) {
        _snwprintf_s(g_lastError, 128, _TRUNCATE,
                     L"CreateEnvironment: 0x%08X", createHr);
        CloseHandle(done);
        handler->Release();
        return nullptr;
    }

    // La creación necesita que el hilo bombee mensajes: bomba restringida
    // hasta el evento (sin ventana, sin IsDialogMessage).
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (WaitForSingleObject(done, 25) == WAIT_OBJECT_0) break;
    }
    CloseHandle(done);

    if (FAILED(hr)) {
        _snwprintf_s(g_lastError, 128, _TRUNCATE,
                     L"entorno del motor: 0x%08X", hr);
        handler->Release();
        return nullptr;
    }

    smac::ComPtr<ICoreWebView2Environment> env = handler->env_;
    handler->Release();
    return env;
}

} // namespace smac::web
