// =============================================================================
// culling.cpp : frustum culling de sphères (bounding spheres) - scalaire vs SSE vs AVX2
//
// Une sphère (c, r) est visible si pour chacun des 6 plans (n, d) du frustum :
//     dot(n, c) + d > -r
// Données en SoA alignées : cx[], cy[], cz[], radius[].
// Sortie : un octet par sphère (1 = visible), comme le ferait un moteur avant le draw.
// =============================================================================
#include "../common/simd_config.hpp"
#include "../common/bench.hpp"
#include <vector>
#include <random>
#include <cstring>
#include <new>
#include <bit>

struct Plane { float nx, ny, nz, d; };

template <typename T, size_t Align>
struct AlignedAllocator   // std::vector aligné (pour _mm_load_ps / _mm256_load_ps)
{
    using value_type = T;
    template <typename U> struct rebind { using other = AlignedAllocator<U, Align>; };
    AlignedAllocator() = default;
    template <typename U> AlignedAllocator(const AlignedAllocator<U, Align>&) {}
    T* allocate(size_t n) { return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{ Align })); }
    void deallocate(T* p, size_t) { ::operator delete(p, std::align_val_t{ Align }); }
    bool operator==(const AlignedAllocator&) const { return true; }
};
using fvec = std::vector<float, AlignedAllocator<float, 32>>;

struct Spheres { fvec cx, cy, cz, r; size_t count; };

NO_VECTORIZE_FUNC static size_t cull_scalar(const Spheres& s, const Plane* planes, uint8_t* visible)
{
    size_t n = 0;
    NO_VECTORIZE_LOOP
    for (size_t i = 0; i < s.count; ++i)
    {
        bool in = true;
        for (int p = 0; p < 6 && in; ++p)   // early-out : une branche par plan
        {
            const Plane& P = planes[p];
            in = P.nx * s.cx[i] + P.ny * s.cy[i] + P.nz * s.cz[i] + P.d > -s.r[i];
        }
        visible[i] = in;
        n += in;
    }
    return n;
}

static size_t cull_sse(const Spheres& s, const Plane* planes, uint8_t* visible)
{
    size_t n = 0, i = 0;
    for (; i + 4 <= s.count; i += 4)
    {
        __m128 cx = _mm_load_ps(&s.cx[i]), cy = _mm_load_ps(&s.cy[i]), cz = _mm_load_ps(&s.cz[i]);
        __m128 neg_r = _mm_sub_ps(_mm_setzero_ps(), _mm_load_ps(&s.r[i]));
        __m128 inside = _mm_castsi128_ps(_mm_set1_epi32(-1));    // tous les bits à 1
        for (int p = 0; p < 6; ++p)                               // PAS d'early-out : 6 plans pour tout le monde
        {
            __m128 dist = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(planes[p].nx), cx),
                                                _mm_mul_ps(_mm_set1_ps(planes[p].ny), cy)),
                                     _mm_add_ps(_mm_mul_ps(_mm_set1_ps(planes[p].nz), cz),
                                                _mm_set1_ps(planes[p].d)));
            inside = _mm_and_ps(inside, _mm_cmpgt_ps(dist, neg_r));
        }
        int bits = _mm_movemask_ps(inside);
        visible[i] = bits & 1; visible[i + 1] = (bits >> 1) & 1; visible[i + 2] = (bits >> 2) & 1; visible[i + 3] = (bits >> 3) & 1;
        n += std::popcount(unsigned(bits));
    }
    for (; i < s.count; ++i)   // tail scalaire
    {
        bool in = true;
        for (int p = 0; p < 6; ++p)
            in &= planes[p].nx * s.cx[i] + planes[p].ny * s.cy[i] + planes[p].nz * s.cz[i] + planes[p].d > -s.r[i];
        visible[i] = in; n += in;
    }
    return n;
}

TARGET_AVX2 static size_t cull_avx2(const Spheres& s, const Plane* planes, uint8_t* visible)
{
    size_t n = 0, i = 0;
    for (; i + 8 <= s.count; i += 8)
    {
        __m256 cx = _mm256_load_ps(&s.cx[i]), cy = _mm256_load_ps(&s.cy[i]), cz = _mm256_load_ps(&s.cz[i]);
        __m256 neg_r = _mm256_sub_ps(_mm256_setzero_ps(), _mm256_load_ps(&s.r[i]));
        __m256 inside = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
        for (int p = 0; p < 6; ++p)
        {
            // FMA : nx*cx + ny*cy  ->  fmadd(nx, cx, ny*cy)
            __m256 dist = _mm256_fmadd_ps(_mm256_set1_ps(planes[p].nx), cx,
                          _mm256_fmadd_ps(_mm256_set1_ps(planes[p].ny), cy,
                          _mm256_fmadd_ps(_mm256_set1_ps(planes[p].nz), cz, _mm256_set1_ps(planes[p].d))));
            inside = _mm256_and_ps(inside, _mm256_cmp_ps(dist, neg_r, _CMP_GT_OQ));
        }
        unsigned bits = unsigned(_mm256_movemask_ps(inside));
        for (int k = 0; k < 8; ++k) visible[i + k] = (bits >> k) & 1;
        n += std::popcount(bits);
    }
    for (; i < s.count; ++i)
    {
        bool in = true;
        for (int p = 0; p < 6; ++p)
            in &= planes[p].nx * s.cx[i] + planes[p].ny * s.cy[i] + planes[p].nz * s.cz[i] + planes[p].d > -s.r[i];
        visible[i] = in; n += in;
    }
    _mm256_zeroupper();
    return n;
}

int main()
{
    const size_t N = 1'000'003;
    Spheres s;
    s.count = N;
    for (auto* v : { &s.cx, &s.cy, &s.cz, &s.r }) v->resize(N);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> pos(-200, 200), rad(0.5f, 3.0f);
    for (size_t i = 0; i < N; ++i) { s.cx[i] = pos(rng); s.cy[i] = pos(rng); s.cz[i] = pos(rng); s.r[i] = rad(rng); }

    // Frustum simplifié : une "boîte" [-100, 100]^3 (6 plans normalisés tournés vers l'intérieur)
    const Plane planes[6] = { { 1, 0, 0, 100 }, { -1, 0, 0, 100 }, { 0, 1, 0, 100 },
                              { 0, -1, 0, 100 }, { 0, 0, 1, 100 }, { 0, 0, -1, 100 } };

    std::vector<uint8_t> v0(N), v1(N), v2(N);
    size_t n0 = 0, n1 = 0, n2 = 0;
    CpuFeatures cpu = detect_cpu();
    bench("scalaire (early-out)", [&] { n0 = opaque(cull_scalar)(s, planes, v0.data()); });
    bench("SSE (4 sphères / itération)", [&] { n1 = opaque(cull_sse)(s, planes, v1.data()); });
    if (cpu.avx2) bench("AVX2 + FMA (8 / itération)", [&] { n2 = opaque(cull_avx2)(s, planes, v2.data()); });
    else { v2 = v1; n2 = n1; }
    std::printf("visibles : %zu / %zu / %zu sur %zu - résultats identiques : %s\n", n0, n1, n2, N,
                (v0 == v1 && v1 == v2) ? "oui" : "NON");
}
