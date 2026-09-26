# ============================================================================
#  S.M.A.C Web Core — Navegador ultra-minimalista para Windows
#  ----------------------------------------------------------------------------
#  Target de hardware: Intel Celeron N4120 (Goldmont Plus, 4C/4T, UHD 600).
#  Restricción dura: NINGUNA instrucción AVX/AVX2 (SIGILL garantizado en esa
#  CPU). Techo vectorial: SSE4.2 vía -march=goldmont-plus.
# ============================================================================
cmake_minimum_required(VERSION 3.28)

# DeferCompilerGNU: permitimos que los módulos ajusten flags después de
# project(); necesario porque ClangCl hereda banderas del toolset de MSVC.
project(SmacWebCore LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "S.M.A.C Web Core solo compila para Windows (Win32 + WebView2).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Compila solo en 64 bits: el motor WebView2 x86 no soporta nuestro presupuesto de RAM.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Versión del producto (fuente única para el exe y los recursos).
set(SMAC_VERSION "0.1.0" CACHE STRING "Versión de S.M.A.C Web Core")
set(SMAC_VENDOR  "S.M.A.C Project" CACHE STRING "Vendedor")

# ----------------------------------------------------------------------------
# Opciones
# ----------------------------------------------------------------------------
option(SMAC_LTO          "ThinLTO (requiere lld-link)"      ON)
option(SMAC_MIMALLOC     "Reemplazar new/delete por mimalloc estático" ON)
option(SMAC_TESTS_FATAL  "Abortar el proceso ante OOM"      ON)

# ----------------------------------------------------------------------------
# Submódulos de compilación (Separación de responsabilidades)
# ----------------------------------------------------------------------------
include(cmake/Toolchain.cmake)   # Detección Clang/LLVM + flags de arquitectura
include(cmake/WebView2.cmake)    # SDK WebView2 pinnado (loader estático)
if(SMAC_MIMALLOC)
    include(cmake/Mimalloc.cmake)
endif()

# ----------------------------------------------------------------------------
# Ejecutable WIN32 (sin consola) con wWinMain
# ----------------------------------------------------------------------------
add_executable(smac-web WIN32
    src/main.cpp
    src/app/App.cpp
    src/app/Log.cpp
    src/config/Config.cpp
    src/system/Paths.cpp
    src/system/MemoryOptimizer.cpp
    src/system/ProcessPriority.cpp
    src/window/WindowHost.cpp
    src/window/ChromeBar.cpp
    src/window/TabStrip.cpp
    src/window/Omnibox.cpp
    src/window/MenuBuilder.cpp
    src/browser/WebViewEngine.cpp
    src/browser/WebViewTab.cpp
    src/browser/TabManager.cpp
    src/browser/ResourceBlocker.cpp
    resources/app.rc
)

target_include_directories(smac-web PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${WEBVIEW2_INCLUDE_DIR}
)

target_compile_definitions(smac-web PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NOGDICAPMASKS NOSYSMETRICS NOMENUS NOSCROLL NOSTYLE NOTEXTMETRIC
    NORASTEROPS NOSOUND NOCOMM NOKANJI NOCRYPT NOIMG NOHELP NOPROFILE
    NOWHISTLE NODBC
    SMAC_VERSION="${SMAC_VERSION}"
    SMAC_VENDOR="${SMAC_VENDOR}"
    SMAC_WV2_TARGET_VERSION="${SMAC_WV2_SDK_VERSION}"
    _CRT_SECURE_NO_WARNINGS
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    $<$<BOOL:${SMAC_MIMALLOC}>:SMAC_MIMALLOC=1>
    $<$<BOOL:${SMAC_TESTS_FATAL}>:SMAC_OOM_FATAL=1>
)

# Perfil de producción: la config MinSizeRel define SMAC_PRODUCTION_BUILD.
add_compile_definitions(
    $<$<CONFIG:MinSizeRel>:SMAC_PRODUCTION_BUILD>
)
set(SMAC_RELEASE_CONFIG MinSizeRel CACHE STRING "Configuración de producción")

target_link_libraries(smac-web PRIVATE
    dwmapi user32 gdi32 shell32 shlwapi advapi32 ole32 oleaut32
    version uxtheme comctl32 winmm normaliz
    ${WEBVIEW2_LOADER_STATIC}
)
if(SMAC_MIMALLOC)
    target_link_libraries(smac-web PRIVATE mimalloc-static)
endif()

# Manifest + icono: la invocación la hace app.rc via mt.exe (ver resources/).
set_target_properties(smac-web PROPERTIES
    OUTPUT_NAME "SMAC-WebCore"
    MSVC_RUNTIME_LIBRARY "MultiThreaded"          # CRT estático (/MT)
    LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb"
)

# ----------------------------------------------------------------------------
# Empaquetado: copiar el exe + extensión uBO junto al binario
# ----------------------------------------------------------------------------
add_custom_command(TARGET smac-web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:smac-web>/extensions"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:smac-web>" "$<TARGET_FILE_DIR:smac-web>"
    COMMENT "Preparando carpeta de distribución"
)

include(InstallRequiredSystemLibraries)
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "SMAC-WebCore")
include(CPack)
