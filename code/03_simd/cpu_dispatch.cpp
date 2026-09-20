// Détection des jeux d'instructions et choix de l'implémentation à l'exécution.
#include "../common/simd_config.hpp"
#include <cstdio>

static float sum_scalar(const float* x, size_t n)
{
    float s = 0; for (size_t i = 0; i < n; ++i) s += x[i]; return s;
}

TARGET_AVX2
static float sum_avx2(const float* x, size_t n)
{
    __m256 acc = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 8 <= n; i += 8) acc = _mm256_add_ps(acc, _mm256_loadu_ps(x + i));
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, acc);
    float s = tmp[0] + tmp[1] + tmp[2] + tmp[3] + tmp[4] + tmp[5] + tmp[6] + tmp[7];
    for (; i < n; ++i) s += x[i];
    _mm256_zeroupper();          // évite la pénalité de transition AVX -> SSE
    return s;
}

using SumFn = float (*)(const float*, size_t);

int main()
{
    CpuFeatures f = detect_cpu();
    std::printf("SSE4.1 %d | SSE4.2 %d | AVX %d | AVX2 %d | FMA %d | AVX-512F %d BW %d VL %d\n",
                f.sse41, f.sse42, f.avx, f.avx2, f.fma, f.avx512f, f.avx512bw, f.avx512vl);

    // Dispatch : on choisit UNE fois, au démarrage.
    SumFn sum = f.avx2 ? sum_avx2 : sum_scalar;
    std::printf("implémentation choisie : %s\n", f.avx2 ? "AVX2" : "scalaire");

    float data[37];
    for (int i = 0; i < 37; ++i) data[i] = float(i);
    std::printf("somme = %.1f (attendu 666.0)\n", sum(data, 37));
}
