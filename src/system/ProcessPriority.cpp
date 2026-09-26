/** ==========================================================================
 *  system/ProcessPriority.cpp — Scheduler: host elevado, renderers bajos.
 *  ----------------------------------------------------------------------------
 *  En un Celeron N4120 (4C/4T) el shell Win32 debe ganar siempre el planificador
 *  frente a las webs: ABOVE_NORMAL para nosotros, BELOW_NORMAL para renderers.
 *  El proceso GPU lo gestiona Chromium (ya nace BELOW_NORMAL): no lo tocamos.
 *  ========================================================================== */
#include "ProcessPriority.hpp"

#include "smac/Log.hpp"

namespace smac::prio {

bool ElevateHostProcess() {
    if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
        log::Warn("prio: no se pudo elevar el host (%lu)", GetLastError());
        return false;
    }
    log::Info("prio: host a ABOVE_NORMAL_PRIORITY_CLASS");
    return true;
}

ChildKind ClassifyChild(const wchar_t* cmd) {
    if (!cmd) return ChildKind::Unknown;

    // Busca " --type=renderer" con espacio previo para evitar falsos positivos.
    struct Pair { const wchar_t* token; ChildKind kind; };
    constexpr Pair kPairs[] = {
        { L"--type=renderer", ChildKind::Renderer },
        { L"--type=gpu-process", ChildKind::Gpu },
        { L"--type=utility", ChildKind::Utility },
    };
    for (const auto& p : kPairs) {
        const wchar_t* hit = nullptr;
        for (const wchar_t* s = cmd; (s = wcsstr(s, p.token)) != nullptr; s += 1) {
            if (s > cmd && s[-1] != L' ') continue;   // debe ir precedido de espacio
            hit = s;
            break;
        }
        if (hit) return p.kind;
    }
    return ChildKind::Unknown;
}

DWORD ApplyChildPolicy(HANDLE childProcess, const wchar_t* commandLine) {
    if (!childProcess) return 0;

    switch (ClassifyChild(commandLine)) {
        case ChildKind::Renderer:
            SetPriorityClass(childProcess, BELOW_NORMAL_PRIORITY_CLASS);
            log::Info("prio: renderer -> BELOW_NORMAL");
            return BELOW_NORMAL_PRIORITY_CLASS;
        case ChildKind::Utility:
            // Chromium ya fija NORMAL para utility: lo respetamos.
            return 0;
        case ChildKind::Gpu:
            return 0;   // el GPU process lo gestiona Chromium.
        default:
            return 0;
    }
}

} // namespace smac::prio
