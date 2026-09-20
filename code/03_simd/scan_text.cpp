// =============================================================================
// scan_text.cpp : SIMD sur des OCTETS - le cas du lexer.
// Compter les lignes et trouver les caractères "intéressants" d'un gros fichier
// source : 16 (SSE2) ou 32 (AVX2) octets comparés en une instruction.
// Même principe que simdjson (Lemire & Langdale, 2019).
// =============================================================================
#include "../common/simd_config.hpp"
#include "../common/bench.hpp"
#include <string>
#include <random>
#include <bit>
#include <algorithm>

NO_VECTORIZE_FUNC static size_t count_lines_scalar(const char* s, size_t n)
{
    size_t c = 0;
    NO_VECTORIZE_LOOP
    for (size_t i = 0; i < n; ++i) c += (s[i] == '\n');
    return c;
}

// POPCNT : présent sur tous les CPU x86-64 depuis ~2008, mais GCC/Clang exigent qu'on l'autorise.
TARGET_POPCNT static size_t count_lines_sse2(const char* s, size_t n)
{
    const __m128i nl = _mm_set1_epi8('\n');
    size_t c = 0, i = 0;
    for (; i + 16 <= n; i += 16)
    {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + i));
        unsigned bits = unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, nl)));   // 1 bit par octet égal
        c += std::popcount(bits);
    }
    for (; i < n; ++i) c += (s[i] == '\n');
    return c;
}

TARGET_AVX2 static size_t count_lines_avx2(const char* s, size_t n)
{
    const __m256i nl = _mm256_set1_epi8('\n');
    size_t c = 0, i = 0;
    for (; i + 32 <= n; i += 32)
    {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
        c += std::popcount(unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, nl))));
    }
    for (; i < n; ++i) c += (s[i] == '\n');
    _mm256_zeroupper();
    return c;
}

// Trouver le prochain caractère qui n'est PAS un caractère d'identifiant [a-z0-9_]
// (ce que fait un lexer après avoir lu la première lettre d'un identifiant).
NO_VECTORIZE_FUNC static size_t skip_ident_scalar(const char* s, size_t pos, size_t n)
{
    NO_VECTORIZE_LOOP
    while (pos < n && ((s[pos] >= 'a' && s[pos] <= 'z') || (s[pos] >= '0' && s[pos] <= '9') || s[pos] == '_')) ++pos;
    return pos;
}

static size_t skip_ident_sse2(const char* s, size_t pos, size_t n)
{
    // Test d'intervalle en SIMD : (c >= 'a' && c <= 'z')  <=>  (c - 'a') <= 25 en non signé.
    // SSE2 n'a que des comparaisons SIGNÉES : on décale de 0x80 pour les rendre non signées.
    const __m128i bias = _mm_set1_epi8(-128);                                  // 0x80
    const __m128i a = _mm_set1_epi8('a'), lim_az = _mm_set1_epi8(-128 + 26);   // (c-'a') + 0x80 < 26 + 0x80
    const __m128i zero = _mm_set1_epi8('0'), lim_09 = _mm_set1_epi8(-128 + 10);
    const __m128i under = _mm_set1_epi8('_');
    while (pos + 16 <= n)
    {
        __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + pos));
        __m128i is_az = _mm_cmplt_epi8(_mm_add_epi8(_mm_sub_epi8(c, a), bias), lim_az);
        __m128i is_09 = _mm_cmplt_epi8(_mm_add_epi8(_mm_sub_epi8(c, zero), bias), lim_09);
        __m128i is_ident = _mm_or_si128(_mm_or_si128(is_az, is_09), _mm_cmpeq_epi8(c, under));
        unsigned not_ident = ~unsigned(_mm_movemask_epi8(is_ident)) & 0xFFFF;
        if (not_ident) return pos + std::countr_zero(not_ident);   // premier octet "non identifiant"
        pos += 16;
    }
    return skip_ident_scalar(s, pos, n);
}

int main()
{
    // Génère ~64 Mo de "code source" avec des identifiants de longueurs variées
    std::mt19937 rng(3);
    std::string src;
    src.reserve(64 << 20);
    const char* words[] = { "position", "velocity_x", "let", "health_regen_rate", "i", "vec4", "update_particles_system" };
    while (src.size() < (64u << 20))
    {
        src += words[rng() % 7];
        src += (rng() % 8 == 0) ? '\n' : ' ';
    }
    const char* s = src.data();
    const size_t n = src.size();

    size_t l0 = 0, l1 = 0, l2 = 0, l3 = 0;
    CpuFeatures cpu = detect_cpu();
    std::printf("--- compter les '\\n' dans %zu Mo ---\n", n >> 20);
    bench("scalaire",          [&] { l0 = opaque(count_lines_scalar)(s, n); });
    bench("SSE2 (16 octets)",  [&] { l1 = opaque(count_lines_sse2)(s, n); });
    if (cpu.avx2) bench("AVX2 (32 octets)", [&] { l2 = opaque(count_lines_avx2)(s, n); }); else l2 = l1;
    bench("std::count (STL)",  [&] { l3 = size_t(std::count(s, s + n, '\n')); });
    std::printf("lignes : %zu %zu %zu %zu\n", l0, l1, l2, l3);

    std::printf("--- lexer : sauter tous les identifiants ---\n");
    size_t t0 = 0, t1 = 0;
    auto lex = [&](size_t (*skip)(const char*, size_t, size_t)) {
        size_t tokens = 0, pos = 0;
        while (pos < n) { size_t end = skip(s, pos, n); if (end == pos) ++end; else ++tokens; pos = end; }
        return tokens;
    };
    bench("scalaire", [&] { t0 = lex(opaque(skip_ident_scalar)); });
    bench("SSE2",     [&] { t1 = lex(opaque(skip_ident_sse2)); });
    std::printf("identifiants : %zu %zu\n", t0, t1);
}
