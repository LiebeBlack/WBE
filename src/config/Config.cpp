/** ==========================================================================
 *  config/Config.cpp — smac.ini: parseo robusto sin STL ni excepciones.
 *  ========================================================================== */
#include "Config.hpp"

#include "smac/Log.hpp"
#include "smac/Version.hpp"
#include "system/Paths.hpp"

namespace smac::config {
namespace {

wchar_t g_iniPath[MAX_PATH] = L"";

/** Helper: lee una clave y la copia acotada en out. */
void GetStr(const wchar_t* section, const wchar_t* key,
            const wchar_t* def, wchar_t* out, DWORD outChars) {
    GetPrivateProfileStringW(section, key, def, out, outChars, g_iniPath);
}

int GetInt(const wchar_t* section, const wchar_t* key, int def) {
    return GetPrivateProfileIntW(section, key, def, g_iniPath);
}

/** Helper: escribe una clave (solo si cambió: evita toques de disco). */
void PutStr(const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    wchar_t cur[256];
    GetPrivateProfileStringW(section, key, L"", cur, 256, g_iniPath);
    if (lstrcmpW(cur, value) != 0) {
        WritePrivateProfileStringW(section, key, value, g_iniPath);
    }
}

void PutInt(const wchar_t* section, const wchar_t* key, int value) {
    wchar_t buf[32];
    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", value);
    PutStr(section, key, buf);
}

} // namespace

const wchar_t* IniPath() { return g_iniPath; }

void Load(smac::Config* out) {
    _snwprintf_s(g_iniPath, MAX_PATH, _TRUNCATE, L"%s\\%s",
                 paths::UserDir(), paths::kConfigFile);

    // Si no existe, sembramos un ini comentado para que el usuario lo edite.
    if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringW(L"window", L"x",      L"-1",       g_iniPath);
        WritePrivateProfileStringW(L"window", L"y",      L"-1",       g_iniPath);
        WritePrivateProfileStringW(L"window", L"width",  L"1200",     g_iniPath);
        WritePrivateProfileStringW(L"window", L"height", L"800",      g_iniPath);
        WritePrivateProfileStringW(L"window", L"accent", L"cyan",     g_iniPath);
        WritePrivateProfileStringW(L"engine", L"v8HeapMB",   L"350",  g_iniPath);
        WritePrivateProfileStringW(L"engine", L"rendererLimit", L"2", g_iniPath);
        WritePrivateProfileStringW(L"engine", L"vulkan",  L"1",        g_iniPath);
        WritePrivateProfileStringW(L"engine", L"homepage",
                                   L"smac://newtab", g_iniPath);
        WritePrivateProfileStringW(L"engine", L"searchEngine",
                                   L"https://duckduckgo.com/?q=%s", g_iniPath);
        WritePrivateProfileStringW(L"system", L"trimIdleSeconds", L"30", g_iniPath);
        WritePrivateProfileStringW(L"system", L"maxAliveTabs",    L"8",  g_iniPath);
        WritePrivateProfileStringW(L"system", L"childrenBelowNormal", L"1", g_iniPath);
    }

    wchar_t buf[256];

    // --- window ------------------------------------------------------------
    int x = GetInt(L"window", L"x", -1);
    int y = GetInt(L"window", L"y", -1);
    out->windowW = GetInt(L"window", L"width",  1200);
    out->windowH = GetInt(L"window", L"height", 800);
    if (x > -20000 && y > -20000 && x < 20000 && y < 20000) {
        out->windowX = x;
        out->windowY = y;
    }
    out->maximizeStart =
        GetPrivateProfileIntW(L"window", L"maximized", 0, g_iniPath) != 0;

    GetStr(L"window", L"accent", L"cyan", buf, 256);
    out->accent = ParseAccent(buf);

    // --- engine ------------------------------------------------------------
    out->v8HeapMB      = GetInt(L"engine", L"v8HeapMB",      350);
    out->rendererLimit = GetInt(L"engine", L"rendererLimit", 2);
    out->enableVulkan =
        GetPrivateProfileIntW(L"engine", L"vulkan", 1, g_iniPath) != 0;
    out->telemetryOff =
        GetPrivateProfileIntW(L"engine", L"telemetryOff", 1, g_iniPath) != 0;
    out->syncOff =
        GetPrivateProfileIntW(L"engine", L"syncOff", 1, g_iniPath) != 0;

    GetStr(L"engine", L"homepage", L"smac://newtab",
           out->homepage, 256);
    GetStr(L"engine", L"searchEngine", L"https://duckduckgo.com/?q=%s",
           out->searchEngine, 64);

    // --- system ------------------------------------------------------------
    out->trimIdleSeconds = GetInt(L"system", L"trimIdleSeconds", 30);
    out->maxAliveTabs    = GetInt(L"system", L"maxAliveTabs", 8);
    if (out->maxAliveTabs < 1)   out->maxAliveTabs = 1;
    if (out->maxAliveTabs > 32)  out->maxAliveTabs = 32;
    out->childrenBelowNormal =
        GetPrivateProfileIntW(L"system", L"childrenBelowNormal", 1, g_iniPath) != 0;

    log::Info("config: ini=%ls v8=%dMB limit=%d vulkan=%d trim=%ds",
              g_iniPath, out->v8HeapMB, out->rendererLimit,
              out->enableVulkan ? 1 : 0, out->trimIdleSeconds);
}

void Save(const smac::Config& cfg) {
    if (!g_iniPath[0]) return;

    wchar_t buf[32];
    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", cfg.windowX);
    PutStr(L"window", L"x", (cfg.windowX == CW_USEDEFAULT) ? L"-1" : buf);
    _snwprintf_s(buf, 32, _TRUNCATE, L"%d", cfg.windowY);
    PutStr(L"window", L"y", (cfg.windowY == CW_USEDEFAULT) ? L"-1" : buf);
    PutInt(L"window", L"width",  cfg.windowW);
    PutInt(L"window", L"height", cfg.windowH);

    PutStr(L"window", L"accent",
           cfg.accent == AccentColor::Cyan   ? L"cyan"   :
           cfg.accent == AccentColor::Purple ? L"purple" :
           cfg.accent == AccentColor::Green  ? L"green"  : L"off");

    PutInt(L"engine", L"v8HeapMB",      cfg.v8HeapMB);
    PutInt(L"engine", L"rendererLimit", cfg.rendererLimit);
    PutInt(L"engine", L"vulkan",        cfg.enableVulkan ? 1 : 0);
    PutStr(L"engine", L"homepage",      cfg.homepage);
    PutStr(L"engine", L"searchEngine",  cfg.searchEngine);

    PutInt(L"system", L"trimIdleSeconds",     cfg.trimIdleSeconds);
    PutInt(L"system", L"maxAliveTabs",        cfg.maxAliveTabs);
    PutInt(L"system", L"childrenBelowNormal", cfg.childrenBelowNormal ? 1 : 0);
}

AccentColor ParseAccent(const wchar_t* text) {
    if (!text) return AccentColor::Cyan;
    if (lstrcmpiW(text, L"purple") == 0) return AccentColor::Purple;
    if (lstrcmpiW(text, L"green")  == 0) return AccentColor::Green;
    if (lstrcmpiW(text, L"off")    == 0) return AccentColor::Off;
    return AccentColor::Cyan;
}

COLORREF AccentColorRef(AccentColor c) {
    switch (c) {
        case AccentColor::Purple: return RGB(0xB0, 0x26, 0xFF);
        case AccentColor::Green:  return RGB(0x39, 0xFF, 0x14);
        case AccentColor::Off:    return 0xFFFFFFFF; // DWMWA_NONE de Win11
        default:                  return RGB(0x00, 0xE5, 0xFF);
    }
}

} // namespace smac::config
