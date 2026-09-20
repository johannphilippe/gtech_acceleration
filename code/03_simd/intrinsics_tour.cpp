// =============================================================================
// intrinsics_tour.cpp : visite guidée des intrinsics SSE (128 bits).
// Chaque étape affiche le contenu des registres pour "voir" le SIMD.
// =============================================================================
#include "../common/simd_config.hpp"
#include <cstdio>
#include <bit>

static void print_ps(const char* name, __m128 v)
{
    alignas(16) float f[4];
    _mm_store_ps(f, v);
    // ATTENTION à l'ordre : l'élément 0 est l'élément de poids faible.
    std::printf("%-28s [%7.2f %7.2f %7.2f %7.2f]\n", name, f[0], f[1], f[2], f[3]);
}

static void print_mask(const char* name, __m128 m)
{
    alignas(16) uint32_t u[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(u), _mm_castps_si128(m));   // cast = réinterprète les bits, sans conversion
    std::printf("%-28s [%08X %08X %08X %08X]  movemask = 0b%d%d%d%d\n", name, u[0], u[1], u[2], u[3],
                (_mm_movemask_ps(m) >> 3) & 1, (_mm_movemask_ps(m) >> 2) & 1,
                (_mm_movemask_ps(m) >> 1) & 1, _mm_movemask_ps(m) & 1);
}

// blendv est du SSE4.1 : avec GCC/Clang, il faut l'autoriser explicitement (x86-64 garantit seulement SSE2).
TARGET_SSE41 static __m128 select_blendv(__m128 if_false, __m128 if_true, __m128 mask)
{
    return _mm_blendv_ps(if_false, if_true, mask);
}

int main()
{
    // --- 1. Charger des données ------------------------------------------------
    __m128 a = _mm_set_ps(4.0f, 3.0f, 2.0f, 1.0f);   // set : ordre INVERSE (e3, e2, e1, e0) !
    __m128 b = _mm_setr_ps(10.0f, -20.0f, 30.0f, -40.0f); // setr : ordre "reversed" = naturel
    __m128 k = _mm_set1_ps(0.5f);                    // broadcast
    print_ps("a = set(4,3,2,1)", a);
    print_ps("b = setr(10,-20,30,-40)", b);

    alignas(16) float aligned[4] = { 1, 2, 3, 4 };
    float unaligned[5] = { 0, 5, 6, 7, 8 };
    print_ps("load (aligné 16)", _mm_load_ps(aligned));
    print_ps("loadu (non aligné)", _mm_loadu_ps(unaligned + 1));

    // --- 2. Arithmétique : 4 opérations en une instruction ------------------------
    print_ps("a + b   (addps)", _mm_add_ps(a, b));
    print_ps("a * k   (mulps)", _mm_mul_ps(a, k));
    print_ps("sqrt(a) (sqrtps)", _mm_sqrt_ps(a));
    print_ps("min(a, b) (minps)", _mm_min_ps(a, b));
    print_ps("a + b   (addss, scalaire)", _mm_add_ss(a, b));   // seul l'élément 0 change

    // --- 3. Comparaisons -> MASQUES (tous les bits à 1 ou à 0) --------------------
    __m128 zero = _mm_setzero_ps();
    __m128 mask = _mm_cmpgt_ps(b, zero);             // b > 0 ?
    print_mask("mask = b > 0", mask);

    // --- 4. Branchless : "if (b > 0) r = b; else r = a;" pour 4 éléments ----------
    // SSE2 : (mask & b) | (~mask & a)
    __m128 r1 = _mm_or_ps(_mm_and_ps(mask, b), _mm_andnot_ps(mask, a));
    print_ps("select and/andnot/or", r1);
    // SSE4.1 : blendv (choisit b là où le bit de signe du masque est à 1)
    print_ps("select blendv (SSE4.1)", select_blendv(a, b, mask));

    // --- 5. Shuffle : réordonner les composantes ----------------------------------
    // _MM_SHUFFLE(z, y, x, w) : élément 3 <- z, 2 <- y, 1 <- x, 0 <- w
    print_ps("shuffle(a,a, 0,1,2,3)", _mm_shuffle_ps(a, a, _MM_SHUFFLE(0, 1, 2, 3)));  // inversion
    print_ps("shuffle(a,a, 3,0,2,1) (yzx)", _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1)));

    // --- 6. Réduire vers un scalaire ----------------------------------------------
    std::printf("%-28s %.2f\n", "_mm_cvtss_f32(a)", _mm_cvtss_f32(a));   // élément 0

    // --- 7. Entiers (SSE2) : __m128i ----------------------------------------------
    __m128i ia = _mm_setr_epi32(1, 2, 3, 4);
    __m128i ib = _mm_set1_epi32(100);
    alignas(16) int32_t out[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(out), _mm_add_epi32(ia, ib));
    std::printf("%-28s [%d %d %d %d]\n", "epi32 add", out[0], out[1], out[2], out[3]);

    // --- 8. Les octets : chercher un caractère dans 16 octets --------------------
    const char text[17] = "let x = vec4(1);";
    __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(text));
    __m128i eq = _mm_cmpeq_epi8(chunk, _mm_set1_epi8('('));
    int bits = _mm_movemask_epi8(eq);                // 1 bit par octet
    std::printf("%-28s movemask = 0x%04X -> index %d\n", "find '(' dans 16 octets", bits, bits ? std::countr_zero(unsigned(bits)) : -1);
}
