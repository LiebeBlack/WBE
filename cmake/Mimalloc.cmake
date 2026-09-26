# ============================================================================
#  Mimalloc.cmake — Asignador de memoria mimalloc (Microsoft), estático.
#  ----------------------------------------------------------------------------
#  NO usamos MI_OVERRIDE: MemoryOptimizer.cpp redirige operator new/delete
#  explícitamente hacia mi_* (control total y determinista desde C++).
# ============================================================================

include(FetchContent)

set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON  CACHE BOOL "" FORCE)
set(MI_OVERRIDE     OFF CACHE BOOL "" FORCE)
set(MI_SECURE       OFF CACHE BOOL "" FORCE)
set(MI_DEBUG_FULL   OFF CACHE BOOL "" FORCE)
set(MI_SEE_ASM      OFF CACHE BOOL "" FORCE)

FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v2.1.7        # Pinnado: reproducibilidad de CI
    GIT_SHALLOW    TRUE
)

message(STATUS "S.M.A.C — Obteniendo mimalloc v2.1.7 ...")
FetchContent_MakeAvailable(mimalloc)

# El target mimalloc-static compila C puro; nuestras flags de idioma no le
# afectan negativamente (clang-cl las acepta en .c) y le heredan el
# -march=goldmont-plus, que afina sus primitivas atómicas al target real.
