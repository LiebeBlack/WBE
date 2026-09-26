# ============================================================================
#  Toolchain.cmake — Detección de Clang/LLVM y flags de compilación
#  ----------------------------------------------------------------------------
#  S.M.A.C Web Core usa Clang-cl (sintaxis GNU para -march, backend MSVC para
#  enlazar contra el SDK de Windows). ThinLTO vía lld-link.
# ============================================================================

# ----------------------------- Detección del compilador ---------------------
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Este proyecto exige Clang (clang-cl). Configura con:\n"
        "  cmake -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ...\n"
        "Compilador detectado: ${CMAKE_CXX_COMPILER_ID}")
endif()

message(STATUS "S.M.A.C — Compilador: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")# ----------------------------- Flags comunes --------------------------------
# Flags de idioma C++ (no aplican a C: mimalloc es C puro y debe compilar limpio).
# /clang:XXXX pasa flags de sintaxis GNU a través del driver clang-cl.
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:/clang:-fno-exceptions>   # Reducción de binario (exigido)
    $<$<COMPILE_LANGUAGE:CXX>:/clang:-fno-rtti>         # Sin typeinfo en el binario
    $<$<COMPILE_LANGUAGE:CXX>:/clang:-fno-plt>
    $<$<COMPILE_LANGUAGE:CXX>:/clang:-fvisibility=hidden>
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>          # __cplusplus correcto con clang-cl
    $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->                  # Sin manejo de excepciones C++
)

# Sin RTTI (sintaxis MSVC, solo C++).
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/GR->)

# Arquitectura destino: Goldmont Plus (Celeron N4120). SSE4.2 como TECHO.
# PROHIBIDO AVX/AVX2/AVX-512: esa CPU carece de las unidades y el binario
# fallaría con SIGILL en producción.
# NOTA: NO desactivamos unwind tables (.pdata): en x86-64 Windows el SEH
# (__try/__except de main.cpp) las exige.
if(SMAC_TARGET_ARCH_GOLDMONT_PLUS)
    add_compile_options(/clang:-march=goldmont-plus)
else()
    add_compile_options(/clang:-msse4.2)   # Límite duro para x86-64 genérico
endif()
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:/clang:-mno-avx>
    $<$<COMPILE_LANGUAGE:CXX>:/clang:-mno-avx2>
)

# ----------------------------- Optimización Release/MinSizeRel --------------
add_compile_options(
    $<$<CONFIG:MinSizeRel>:/clang:-Os>
    $<$<CONFIG:MinSizeRel>:/clang:-fdata-sections>
    $<$<CONFIG:MinSizeRel>:/clang:-ffunction-sections>
    $<$<CONFIG:MinSizeRel>:/clang:-fno-stack-protector>
    $<$<CONFIG:MinSizeRel>:/clang:-fwrapv>          # UB firmado determinista
)

# ----------------------------- ThinLTO --------------------------------------
if(SMAC_LTO)
    add_compile_options($<$<CONFIG:MinSizeRel>:/clang:-flto=thin>)
    add_link_options(
        $<$<CONFIG:MinSizeRel>:/LTCG>
        $<$<CONFIG:MinSizeRel>:/OPT:REF,ICF>
        $<$<CONFIG:MinSizeRel>:/INCREMENTAL:NO>
    )
endif()

# ----------------------------- Enlazado -------------------------------------
add_link_options(
    /SUBSYSTEM:WINDOWS
    /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
    /STACK:1048576,262144            # 1 MB reserva, 256 KB commit
    /DEBUG:NONE
)

# Warnings: calidad de código exigente (no bloqueante para avisos de SDK).
add_compile_options(/W4 /permissive- /wd4100 /wd4189 /wd4324)
