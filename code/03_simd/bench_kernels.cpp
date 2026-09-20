// =============================================================================
// bench_kernels.cpp : scalaire vs auto-vectorisation vs SSE vs AVX2 vs AVX-512
// sur quatre noyaux typiques d'un moteur.
//
// MSVC : cl /O2 /EHsc /std:c++20 bench_kernels.cpp       (puis essayer /fp:fast, /arch:AVX2)
// GCC  : g++ -O2 -std=c++20 bench_kernels.cpp
// =============================================================================
#include "../common/simd_config.hpp"
#include "../common/bench.hpp"
#include <vector>
#include <random>
#include <cmath>
#include <bit>

// -----------------------------------------------------------------------------
// 1. Somme (réduction flottante)
// -----------------------------------------------------------------------------
NO_VECTORIZE_FUNC static float sum_scalar(const float* x, size_t n)
{
    float s = 0.0f;
    NO_VECTORIZE_LOOP
    for (size_t i = 0; i < n; ++i) s += x[i];
    return s;
}

static float sum_auto(const float* x, size_t n)   // MSVC ne vectorise pas sans /fp:fast (reason 1105)
{
    float s = 0.0f;
    for (size_t i = 0; i < n; ++i) s += x[i];
    return s;
}

static float sum_sse(const float* x, size_t n)
{
    __m128 acc = _mm_setzero_ps();
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
        acc = _mm_add_ps(acc, _mm_loadu_ps(x + i));
    // Réduction horizontale des 4 accumulateurs
    __m128 hi = _mm_movehl_ps(acc, acc);            // (a2, a3, a2, a3)
    __m128 s2 = _mm_add_ps(acc, hi);                // (a0+a2, a1+a3, ...)
    __m128 s1 = _mm_add_ss(s2, _mm_shuffle_ps(s2, s2, _MM_SHUFFLE(1, 1, 1, 1)));
    float s = _mm_cvtss_f32(s1);
    for (; i < n; ++i) s += x[i];                   // TAIL : les 0 à 3 derniers éléments
    return s;
}

TARGET_AVX2 static float sum_avx2(const float* x, size_t n)
{
    // 4 accumulateurs de 8 : casse la dépendance entre itérations (le CPU peut
    // exécuter les 4 additions en parallèle au lieu d'attendre la précédente).
    __m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0;
    size_t i = 0;
    for (; i + 32 <= n; i += 32)
    {
        a0 = _mm256_add_ps(a0, _mm256_loadu_ps(x + i));
        a1 = _mm256_add_ps(a1, _mm256_loadu_ps(x + i + 8));
        a2 = _mm256_add_ps(a2, _mm256_loadu_ps(x + i + 16));
        a3 = _mm256_add_ps(a3, _mm256_loadu_ps(x + i + 24));
    }
    __m256 acc = _mm256_add_ps(_mm256_add_ps(a0, a1), _mm256_add_ps(a2, a3));
    for (; i + 8 <= n; i += 8) acc = _mm256_add_ps(acc, _mm256_loadu_ps(x + i));
    __m128 lo = _mm256_castps256_ps128(acc), hi = _mm256_extractf128_ps(acc, 1);
    __m128 v = _mm_add_ps(lo, hi);
    v = _mm_add_ps(v, _mm_movehl_ps(v, v));
    v = _mm_add_ss(v, _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1)));
    float s = _mm_cvtss_f32(v);
    for (; i < n; ++i) s += x[i];
    _mm256_zeroupper();
    return s;
}

TARGET_AVX512 static float sum_avx512(const float* x, size_t n)
{
    __m512 acc = _mm512_setzero_ps();
    size_t i = 0;
    for (; i + 16 <= n; i += 16)
        acc = _mm512_add_ps(acc, _mm512_loadu_ps(x + i));
    // TAIL avec masque : plus de boucle scalaire, on charge seulement les éléments restants
    __mmask16 tail = __mmask16((1u << (n - i)) - 1);
    acc = _mm512_add_ps(acc, _mm512_maskz_loadu_ps(tail, x + i));
    float s = _mm512_reduce_add_ps(acc);
    _mm256_zeroupper();
    return s;
}

// -----------------------------------------------------------------------------
// 2. saxpy : y = a*x + y  (le "hello world" du calcul vectoriel)
// -----------------------------------------------------------------------------
NO_VECTORIZE_FUNC static void saxpy_scalar(float* y, const float* x, float a, size_t n)
{
    NO_VECTORIZE_LOOP
    for (size_t i = 0; i < n; ++i) y[i] += a * x[i];
}

static void saxpy_auto(float* y, const float* x, float a, size_t n)
{
    for (size_t i = 0; i < n; ++i) y[i] += a * x[i];
}

static void saxpy_sse(float* y, const float* x, float a, size_t n)
{
    __m128 va = _mm_set1_ps(a);
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
        _mm_storeu_ps(y + i, _mm_add_ps(_mm_loadu_ps(y + i), _mm_mul_ps(va, _mm_loadu_ps(x + i))));
    for (; i < n; ++i) y[i] += a * x[i];
}

TARGET_AVX2 static void saxpy_avx2(float* y, const float* x, float a, size_t n)
{
    __m256 va = _mm256_set1_ps(a);
    size_t i = 0;
    for (; i + 8 <= n; i += 8)   // FMA : a*x + y en UNE instruction (vfmadd)
        _mm256_storeu_ps(y + i, _mm256_fmadd_ps(va, _mm256_loadu_ps(x + i), _mm256_loadu_ps(y + i)));
    for (; i < n; ++i) y[i] += a * x[i];
    _mm256_zeroupper();
}

// -----------------------------------------------------------------------------
// 3. Clamp avec branches vs min/max
// -----------------------------------------------------------------------------
NO_VECTORIZE_FUNC static void clamp_branchy(float* out, const float* x, size_t n, float lo, float hi)
{
    NO_VECTORIZE_LOOP
    for (size_t i = 0; i < n; ++i)
    {
        if (x[i] < lo)      out[i] = lo;
        else if (x[i] > hi) out[i] = hi;
        else                out[i] = x[i];
    }
}

static void clamp_sse(float* out, const float* x, size_t n, float lo, float hi)
{
    __m128 vlo = _mm_set1_ps(lo), vhi = _mm_set1_ps(hi);
    size_t i = 0;
    for (; i + 4 <= n; i += 4)
        _mm_storeu_ps(out + i, _mm_min_ps(_mm_max_ps(_mm_loadu_ps(x + i), vlo), vhi));
    for (; i < n; ++i) out[i] = std::fmin(std::fmax(x[i], lo), hi);
}

// -----------------------------------------------------------------------------
// 4. Compter les éléments au-dessus d'un seuil (masque + popcount)
// -----------------------------------------------------------------------------
NO_VECTORIZE_FUNC static size_t count_scalar(const float* x, size_t n, float t)
{
    size_t c = 0;
    NO_VECTORIZE_LOOP
    for (size_t i = 0; i < n; ++i) if (x[i] > t) ++c;
    return c;
}

TARGET_AVX2 static size_t count_avx2(const float* x, size_t n, float t)
{
    __m256 vt = _mm256_set1_ps(t);
    size_t c = 0, i = 0;
    for (; i + 8 <= n; i += 8)
    {
        __m256 m = _mm256_cmp_ps(_mm256_loadu_ps(x + i), vt, _CMP_GT_OQ);
        c += std::popcount(unsigned(_mm256_movemask_ps(m)));   // 8 bits -> nombre de "vrai"
    }
    for (; i < n; ++i) c += x[i] > t;
    _mm256_zeroupper();
    return c;
}

TARGET_AVX512 static size_t count_avx512(const float* x, size_t n, float t)
{
    __m512 vt = _mm512_set1_ps(t);
    size_t c = 0, i = 0;
    for (; i + 16 <= n; i += 16)   // AVX-512 : la comparaison produit DIRECTEMENT un masque de bits (registre k)
        c += std::popcount(unsigned(_mm512_cmp_ps_mask(_mm512_loadu_ps(x + i), vt, _CMP_GT_OQ)));
    __mmask16 tail = __mmask16((1u << (n - i)) - 1);
    c += std::popcount(unsigned(_mm512_mask_cmp_ps_mask(tail, _mm512_maskz_loadu_ps(tail, x + i), vt, _CMP_GT_OQ)));
    _mm256_zeroupper();
    return c;
}

// Chaque mesure = 20 exécutions du noyau (sinon les temps sont trop courts pour être fiables).
#define REPEAT for (int rep = 0; rep < 20; ++rep)

int main()
{
    CpuFeatures cpu = detect_cpu();
    const size_t N = (1 << 20) + 3;   // +3 : pour tester la gestion du tail
    std::vector<float> x(N), y(N), tmp(N);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& v : x) v = dist(rng);

    std::printf("--- somme de %zu floats ---\n", N);
    float r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0;
    bench("scalaire (no_vector)", [&] { REPEAT r0 = opaque(sum_scalar)(x.data(), N); });
    bench("auto-vectorisation",   [&] { REPEAT r1 = opaque(sum_auto)(x.data(), N); });
    bench("SSE",                  [&] { REPEAT r2 = opaque(sum_sse)(x.data(), N); });
    if (cpu.avx2)    bench("AVX2 (4 accumulateurs)", [&] { REPEAT r3 = opaque(sum_avx2)(x.data(), N); });
    if (cpu.avx512f) bench("AVX-512 (tail masqué)",  [&] { REPEAT r4 = opaque(sum_avx512)(x.data(), N); });
    std::printf("résultats : %.4f %.4f %.4f %.4f %.4f  (différents : ordre des additions !)\n", r0, r1, r2, r3, r4);

    std::printf("--- saxpy ---\n");
    bench("scalaire (no_vector)", [&] { REPEAT opaque(saxpy_scalar)(y.data(), x.data(), 0.5f, N); });
    bench("auto-vectorisation",   [&] { REPEAT opaque(saxpy_auto)(y.data(), x.data(), 0.5f, N); });
    bench("SSE",                  [&] { REPEAT opaque(saxpy_sse)(y.data(), x.data(), 0.5f, N); });
    if (cpu.avx2) bench("AVX2 + FMA", [&] { REPEAT opaque(saxpy_avx2)(y.data(), x.data(), 0.5f, N); });

    std::printf("--- clamp [-0.5, 0.5] (données aléatoires = branches imprévisibles) ---\n");
    bench("scalaire avec if",     [&] { REPEAT opaque(clamp_branchy)(tmp.data(), x.data(), N, -0.5f, 0.5f); });
    bench("SSE min/max",          [&] { REPEAT opaque(clamp_sse)(tmp.data(), x.data(), N, -0.5f, 0.5f); });

    std::printf("--- compter x > 0.25 ---\n");
    size_t c0 = 0, c1 = 0, c2 = 0;
    bench("scalaire",             [&] { REPEAT c0 = opaque(count_scalar)(x.data(), N, 0.25f); });
    if (cpu.avx2)    bench("AVX2 movemask + popcount", [&] { REPEAT c1 = opaque(count_avx2)(x.data(), N, 0.25f); });
    if (cpu.avx512f) bench("AVX-512 mask + popcount",  [&] { REPEAT c2 = opaque(count_avx512)(x.data(), N, 0.25f); });
    std::printf("résultats : %zu %zu %zu\n", c0, c1, c2);
}
