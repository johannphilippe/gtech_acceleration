# =============================================================================
# Exemples du cours ASM / SIMD / Interpréteur
#
# Visual Studio : File > Open > Folder... sur ce dossier (support CMake natif),
#                 choisir la configuration x64-Release.
# Ligne de commande : cmake -S . -B build && cmake --build build --config Release
#
# Note : showdown.cpp et l'interpréteur de référence (vektor/) ne sont pas ici —
# ils sont distribués progressivement comme corrigés au fil des exercices
# (voir exercices/4_Interpreteur.md dans le dépôt privé du cours).
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(gtech_acceleration_code CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    )
else()
    add_compile_options(-Wall -Wextra)
endif()

function(example name)
    add_executable(${name} ${ARGN})
endfunction()

# --- 1. Mémoire ------------------------------------------------------------------
example(alignment      01_memoire/alignment.cpp)
example(cache          01_memoire/cache.cpp)

# --- 2. Assembleur -----------------------------------------------------------------
example(jit            02_asm/jit.cpp)
if(MSVC)
    enable_language(ASM_MASM)
    example(asm_basics     02_asm/main.cpp           02_asm/asm_basics.asm)
else()
    message(STATUS "MASM indisponible hors MSVC : asm_basics ignoré")
endif()

# --- 3. SIMD -----------------------------------------------------------------------
# autovec.cpp n'a pas de main : on le compile seulement, pour lire le rapport de vectorisation
add_library(autovec OBJECT 03_simd/autovec.cpp)
if(MSVC)
    target_compile_options(autovec PRIVATE /Qvec-report:2)
else()
    target_compile_options(autovec PRIVATE -fopt-info-vec-all)
endif()
example(intrinsics_tour  03_simd/intrinsics_tour.cpp)
example(bench_kernels    03_simd/bench_kernels.cpp)
example(vec4_test        03_simd/vec4_test.cpp)
example(culling          03_simd/culling.cpp)
example(scan_text        03_simd/scan_text.cpp)
example(denormals        03_simd/denormals.cpp)
example(cpu_dispatch     03_simd/cpu_dispatch.cpp)

# --- 4. Interpréteur -----------------------------------------------------------------
example(batch_demo       04_interpreteur/batch_demo.cpp)
