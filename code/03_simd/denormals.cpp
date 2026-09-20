// =============================================================================
// denormals.cpp : le piège des nombres "subnormaux" (denormals).
// Un filtre passe-bas qui décroît vers 0 finit par produire des flottants
// minuscules (< 1.17e-38) : chaque opération devient 10 à 100x plus lente
// (microcode). Classique en audio (réverbes, filtres) et en physique (amortissement).
// Solution : MXCSR, bits FTZ (Flush To Zero) et DAZ (Denormals Are Zero).
// =============================================================================
#include "../common/simd_config.hpp"
#include "../common/bench.hpp"
#include <vector>
#include <cfloat>
#include <algorithm>

// Filtre passe-bas à un pôle (one-pole) : y[n] = a * y[n-1] + (1 - a) * x[n]
// Après un son, l'entrée devient silencieuse (x = 0) : la "queue" du filtre décroît
// indéfiniment vers 0 et traverse la zone subnormale.
static float run_filter(std::vector<float>& y, int samples)
{
    const float a = 0.9995f;
    float out = 0;
    for (int n = 0; n < samples; ++n)
    {
        const float x = 0.0f;                       // silence
        for (auto& v : y) v = a * v + (1.0f - a) * x;
        out = y[0];
    }
    return out;
}

int main()
{
    std::vector<float> state(256);             // 256 voix / filtres
    const int samples = 200'000;                // ~4 s d'audio à 48 kHz
    const float start = 1e-36f;                 // fin de queue de réverbe : déjà proche de FLT_MIN

    unsigned int csr = _mm_getcsr();
    std::printf("FLT_MIN = %g (plus petit float normal)\n", FLT_MIN);

    float r1 = 0, r2 = 0;
    bench("sans FTZ/DAZ", [&] { std::fill(state.begin(), state.end(), start); r1 = run_filter(state, samples); }, 3);

    _mm_setcsr(csr | 0x8040);   // bit 15 = FTZ, bit 6 = DAZ  (équivalent : _MM_SET_FLUSH_ZERO_MODE + _MM_SET_DENORMALS_ZERO_MODE)
    bench("avec FTZ/DAZ", [&] { std::fill(state.begin(), state.end(), start); r2 = run_filter(state, samples); }, 3);
    _mm_setcsr(csr);

    std::printf("dernière valeur : %g (sans) / %g (avec)\n", r1, r2);
}
