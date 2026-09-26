# ============================================================================
#  WebView2.cmake — Descarga pinnada del SDK de WebView2 (NuGet) y preparación
#  del loader ESTÁTICO (sin DLL en el zip final: binario autocontenido).
# ============================================================================

set(SMAC_WV2_SDK_VERSION "1.0.2957.106"
    CACHE STRING "Versión pinnada del SDK de WebView2")

# Directorio donde aterriza el SDK (cache-friendly para CI y builds locales).
set(SMAC_WV2_SDK_DIR "${CMAKE_BINARY_DIR}/_deps/webview2-sdk"
    CACHE PATH "Directorio del SDK WebView2 extraído")

set(SMAC_WV2_SDK_URL
    "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${SMAC_WV2_SDK_VERSION}")

if(NOT EXISTS "${SMAC_WV2_SDK_DIR}/build/native/include/WebView2.h")
    message(STATUS "S.M.A.C — Descargando WebView2 SDK ${SMAC_WV2_SDK_VERSION} ...")
    set(SMAC_WV2_ZIP "${CMAKE_BINARY_DIR}/_deps/webview2-${SMAC_WV2_SDK_VERSION}.zip")
    file(DOWNLOAD "${SMAC_WV2_SDK_URL}" "${SMAC_WV2_ZIP}"
         STATUS SMAC_WV2_DL_STATUS
         TLS_VERIFY ON)
    list(GET SMAC_WV2_DL_STATUS 0 SMAC_WV2_DL_CODE)
    if(NOT SMAC_WV2_DL_CODE EQUAL 0)
        message(FATAL_ERROR "No se pudo descargar el SDK WebView2: ${SMAC_WV2_DL_STATUS}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${SMAC_WV2_ZIP}" DESTINATION "${SMAC_WV2_SDK_DIR}")
    file(REMOVE "${SMAC_WV2_ZIP}")
endif()

set(WEBVIEW2_INCLUDE_DIR "${SMAC_WV2_SDK_DIR}/build/native/include")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
    set(SMAC_WV2_ARCH arm64)
else()
    set(SMAC_WV2_ARCH x64)
endif()

set(WEBVIEW2_LOADER_STATIC
    "${SMAC_WV2_SDK_DIR}/build/native/${SMAC_WV2_ARCH}/WebView2LoaderStatic.lib"
    CACHE FILEPATH "Loader estático de WebView2")

if(NOT EXISTS "${WEBVIEW2_LOADER_STATIC}")
    message(FATAL_ERROR "WebView2LoaderStatic.lib no encontrado en ${WEBVIEW2_LOADER_STATIC}")
endif()

message(STATUS "S.M.A.C — WebView2 SDK: ${SMAC_WV2_SDK_VERSION} (${SMAC_WV2_ARCH}, loader estático)")
