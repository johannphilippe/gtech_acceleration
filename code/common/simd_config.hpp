// =============================================================================
// simd_config.hpp : portabilité MSVC / GCC / Clang pour le SIMD.
//
// MSVC : tous les intrinsics sont utilisables sans option. /arch:AVX2 change
//        seulement le code généré AUTOMATIQUEMENT (auto-vectorisation, VEX).
// GCC/Clang : il faut autoriser le jeu d'instructions par fonction
//        (__attribute__((target("avx2")))) ou pour tout le fichier (-mavx2).
// =============================================================================
#pragma once
#include <immintrin.h>
#include <cstdint>
#include <cstddef>

#if defined(_MSC_VER) && !defined(__clang__)
#   include <intrin.h>
#   define TARGET_SSE41
#   define TARGET_POPCNT
#   define TARGET_AVX2
#   define TARGET_AVX512
#   define FORCE_INLINE __forceinline
#   define NO_VECTORIZE_LOOP __pragma(loop(no_vector))   // à placer juste avant la boucle
#   define NO_VECTORIZE_FUNC
#else
#   include <cpuid.h>
#   define TARGET_SSE41  __attribute__((target("sse4.1")))
#   define TARGET_POPCNT __attribute__((target("popcnt")))   // sinon std::popcount = appel de fonction logiciel lent
#   define TARGET_AVX2   __attribute__((target("avx2,fma,bmi,bmi2,popcnt")))
#   define TARGET_AVX512 __attribute__((target("avx512f,avx512dq,avx512bw,avx512vl")))
#   define FORCE_INLINE inline __attribute__((always_inline))
#   if defined(__clang__)
#       define NO_VECTORIZE_LOOP _Pragma("clang loop vectorize(disable) interleave(disable)")
#       define NO_VECTORIZE_FUNC
#   else
#       define NO_VECTORIZE_LOOP
#       define NO_VECTORIZE_FUNC __attribute__((optimize("no-tree-vectorize,no-tree-slp-vectorize")))
#   endif
#endif

// -----------------------------------------------------------------------------
// Détection des jeux d'instructions à l'exécution (CPU dispatch)
// -----------------------------------------------------------------------------
struct CpuFeatures
{
    bool sse41 = false, sse42 = false, avx = false, avx2 = false, fma = false;
    bool avx512f = false, avx512bw = false, avx512vl = false;
};

inline void cpuid(int leaf, int subleaf, int regs[4])   // regs = EAX, EBX, ECX, EDX
{
#if defined(_MSC_VER) && !defined(__clang__)
    __cpuidex(regs, leaf, subleaf);
#else
    unsigned a, b, c, d;
    __cpuid_count(leaf, subleaf, a, b, c, d);
    regs[0] = int(a); regs[1] = int(b); regs[2] = int(c); regs[3] = int(d);
#endif
}

#if defined(_MSC_VER) && !defined(__clang__)
inline uint64_t read_xcr0() { return _xgetbv(0); }
#else
__attribute__((target("xsave")))
inline uint64_t read_xcr0() { return _xgetbv(0); }
#endif

inline CpuFeatures detect_cpu()
{
    CpuFeatures f;
    int r[4];
    cpuid(0, 0, r);
    const int max_leaf = r[0];

    cpuid(1, 0, r);
    f.sse41 = (r[2] >> 19) & 1;
    f.sse42 = (r[2] >> 20) & 1;
    f.fma   = (r[2] >> 12) & 1;
    const bool osxsave = (r[2] >> 27) & 1;   // l'OS utilise XSAVE
    const bool cpu_avx = (r[2] >> 28) & 1;

    // Le CPU peut supporter AVX sans que l'OS sauvegarde les registres YMM lors des
    // changements de contexte : il faut vérifier XCR0 (bits 1 = SSE, 2 = AVX).
    uint64_t xcr0 = osxsave ? read_xcr0() : 0;
    const bool os_avx    = (xcr0 & 0x06) == 0x06;
    const bool os_avx512 = (xcr0 & 0xE6) == 0xE6;  // + opmask, ZMM_Hi256, Hi16_ZMM

    f.avx = cpu_avx && os_avx;
    f.fma = f.fma && f.avx;
    if (max_leaf >= 7)
    {
        cpuid(7, 0, r);
        f.avx2     = f.avx && ((r[1] >> 5) & 1);
        f.avx512f  = os_avx512 && ((r[1] >> 16) & 1);
        f.avx512bw = os_avx512 && ((r[1] >> 30) & 1);
        f.avx512vl = os_avx512 && ((r[1] >> 31) & 1);
    }
    return f;
}
