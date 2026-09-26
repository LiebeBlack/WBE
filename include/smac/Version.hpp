/** ==========================================================================
 *  smac/Version.hpp — Identidad del producto (fuente única de verdad).
 *  ========================================================================== */
#pragma once

#define SMAC_PRODUCT_NAME_A "S.M.A.C Web Core"
#define SMAC_PRODUCT_NAME_W L"S.M.A.C Web Core"
#define SMAC_VERSION_A      "0.1.0"
#define SMAC_VERSION_W      L"0.1.0"
#define SMAC_VENDOR_W       L"S.M.A.C Project"

namespace smac {

/** Versión numérica para comprobaciones en tiempo de compilación. */
inline constexpr unsigned kVersionMajor = 0;
inline constexpr unsigned kVersionMinor = 1;
inline constexpr unsigned kVersionPatch = 0;

} // namespace smac
