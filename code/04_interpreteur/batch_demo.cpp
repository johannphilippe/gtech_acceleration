// =============================================================================
// batch_demo.cpp : interpréter PAR ENTITÉ ou PAR LOT (batch) ?
//
// Un script de comportement (ici : une particule qui tombe et rebondit) doit être
// exécuté sur N entités à chaque frame.
//
//  (a) par entité : on lance l'interpréteur N fois. Coût du dispatch = N x nb_instructions.
//  (b) par lot    : chaque registre de la VM devient un TABLEAU de N valeurs (SoA) ;
//                   chaque instruction est UNE boucle sur N éléments.
//                   Coût du dispatch = nb_instructions, et les boucles se vectorisent.
//  (c) par lot + AVX2 écrit à la main pour les opcodes.
//
// C'est le modèle d'exécution des shaders, d'ISPC, des graphes audio (traitement par
// blocs) et des "kernels" d'un ECS. Contrainte : pas de branchement dépendant des
// données -> les "if" deviennent des masques (opcodes LT + SELECT).
// =============================================================================
#include "../common/simd_config.hpp"
#include "../common/bench.hpp"
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

enum Op : uint8_t
{
    LOAD_ATTR,   // R[a] = attribut b de l'entité
    STORE_ATTR,  // attribut b = R[a]
    LOADK,       // R[a] = K[b]
    ADD, MUL,    // R[a] = R[b] op R[c]
    NEG,         // R[a] = -R[b]
    LT,          // R[a] = R[b] < R[c]  (masque : 1.0 ou 0.0 en scalaire, tous bits en SIMD)
    SELECT,      // R[a] = R[mask] ? R[b] : R[c]   (mask est l'octet "d")
    HALT,
};

struct Ins { uint8_t op, a = 0, b = 0, c = 0, d = 0; };

enum Attr { Y, VY, ATTR_COUNT };

// Programme :  vy += g*dt ; y += vy*dt ; if (y < 0) { y = -y ; vy = -vy * 0.8 }
static const float K[] = { -9.81f * 0.016f, 0.016f, 0.0f, -0.8f };
static const Ins program[] = {
    { LOAD_ATTR, 0, Y },  { LOAD_ATTR, 1, VY },
    { LOADK, 2, 0 },      { ADD, 1, 1, 2 },          // vy += g*dt
    { LOADK, 3, 1 },      { MUL, 4, 1, 3 },          // r4 = vy * dt
    { ADD, 0, 0, 4 },                                // y += r4
    { LOADK, 5, 2 },      { LT, 6, 0, 5 },           // mask = y < 0
    { NEG, 7, 0 },                                   // r7 = -y
    { SELECT, 0, 7, 0, 6 },                          // y = mask ? -y : y
    { LOADK, 8, 3 },      { MUL, 9, 1, 8 },          // r9 = vy * -0.8
    { SELECT, 1, 9, 1, 6 },                          // vy = mask ? r9 : vy
    { STORE_ATTR, 0, Y }, { STORE_ATTR, 1, VY },
    { HALT },
};

// --------------------------------------------------------------------------- (a)
static void run_per_entity(float* attrs[ATTR_COUNT], size_t n)
{
    float R[16];
    for (size_t e = 0; e < n; ++e)
    {
        for (const Ins* pc = program;; ++pc)
        {
            switch (pc->op)
            {
            case LOAD_ATTR:  R[pc->a] = attrs[pc->b][e]; continue;
            case STORE_ATTR: attrs[pc->b][e] = R[pc->a]; continue;
            case LOADK:      R[pc->a] = K[pc->b]; continue;
            case ADD:        R[pc->a] = R[pc->b] + R[pc->c]; continue;
            case MUL:        R[pc->a] = R[pc->b] * R[pc->c]; continue;
            case NEG:        R[pc->a] = -R[pc->b]; continue;
            case LT:         R[pc->a] = R[pc->b] < R[pc->c] ? 1.0f : 0.0f; continue;
            case SELECT:     R[pc->a] = R[pc->d] != 0.0f ? R[pc->b] : R[pc->c]; continue;
            case HALT:       break;
            }
            break;
        }
    }
}

// --------------------------------------------------------------------------- (b)
// Registres = tableaux de N floats. Chaque opcode = une boucle simple, vectorisable.
struct BatchRegisters
{
    size_t n;
    float* data;   // 16 registres x n (arrondi à 8), aligné 32
    explicit BatchRegisters(size_t count) : n((count + 7) & ~size_t(7)),
        data(static_cast<float*>(::operator new(16 * n * sizeof(float), std::align_val_t{ 32 }))) {}
    ~BatchRegisters() { ::operator delete(data, std::align_val_t{ 32 }); }
    float* operator[](size_t r) { return data + r * n; }
};

static void run_batch(float* attrs[ATTR_COUNT], size_t n, BatchRegisters& R)
{
    for (const Ins* pc = program;; ++pc)
    {
        float* __restrict a = R[pc->a];
        const float* __restrict b = R[pc->b];
        const float* __restrict c = R[pc->c];
        switch (pc->op)
        {
        case LOAD_ATTR:  std::memcpy(a, attrs[pc->b], n * sizeof(float)); break;
        case STORE_ATTR: std::memcpy(attrs[pc->b], a, n * sizeof(float)); break;
        case LOADK:      { float k = K[pc->b]; for (size_t i = 0; i < n; ++i) a[i] = k; } break;
        case ADD:        for (size_t i = 0; i < n; ++i) a[i] = b[i] + c[i]; break;
        case MUL:        for (size_t i = 0; i < n; ++i) a[i] = b[i] * c[i]; break;
        case NEG:        for (size_t i = 0; i < n; ++i) a[i] = -b[i]; break;
        case LT:         for (size_t i = 0; i < n; ++i) a[i] = b[i] < c[i] ? 1.0f : 0.0f; break;
        case SELECT:
        {
            const float* __restrict m = R[pc->d];
            for (size_t i = 0; i < n; ++i) a[i] = m[i] != 0.0f ? b[i] : c[i];
            break;
        }
        case HALT: return;
        }
    }
}

// --------------------------------------------------------------------------- (c)
TARGET_AVX2 static void run_batch_avx2(float* attrs[ATTR_COUNT], size_t n, BatchRegisters& R)
{
    const size_t w = R.n;   // multiple de 8 : pas de tail à gérer (padding, cf. partie 3)
    for (const Ins* pc = program;; ++pc)
    {
        float* a = R[pc->a];
        const float* b = R[pc->b];
        const float* c = R[pc->c];
        switch (pc->op)
        {
        case LOAD_ATTR:  std::memcpy(a, attrs[pc->b], n * sizeof(float)); break;
        case STORE_ATTR: std::memcpy(attrs[pc->b], a, n * sizeof(float)); break;
        case LOADK:      { __m256 k = _mm256_set1_ps(K[pc->b]); for (size_t i = 0; i < w; i += 8) _mm256_store_ps(a + i, k); } break;
        case ADD:        for (size_t i = 0; i < w; i += 8) _mm256_store_ps(a + i, _mm256_add_ps(_mm256_load_ps(b + i), _mm256_load_ps(c + i))); break;
        case MUL:        for (size_t i = 0; i < w; i += 8) _mm256_store_ps(a + i, _mm256_mul_ps(_mm256_load_ps(b + i), _mm256_load_ps(c + i))); break;
        case NEG:        for (size_t i = 0; i < w; i += 8) _mm256_store_ps(a + i, _mm256_sub_ps(_mm256_setzero_ps(), _mm256_load_ps(b + i))); break;
        case LT:         for (size_t i = 0; i < w; i += 8) _mm256_store_ps(a + i, _mm256_cmp_ps(_mm256_load_ps(b + i), _mm256_load_ps(c + i), _CMP_LT_OQ)); break;
        case SELECT:
        {
            const float* m = R[pc->d];
            for (size_t i = 0; i < w; i += 8)
                _mm256_store_ps(a + i, _mm256_blendv_ps(_mm256_load_ps(c + i), _mm256_load_ps(b + i), _mm256_load_ps(m + i)));
            break;
        }
        case HALT: _mm256_zeroupper(); return;
        }
    }
}

int main()
{
    const size_t N = 100'000;
    const int frames = 100;
    std::vector<float> y0(N), vy0(N);
    for (size_t i = 0; i < N; ++i) { y0[i] = float(i % 1000) * 0.01f; vy0[i] = float(i % 7) - 3.0f; }

    auto run = [&](const char* name, auto&& fn) {
        std::vector<float> y = y0, vy = vy0;
        // Attributs dans des vecteurs alignés 32 et arrondis à 8 (padding pour AVX2)
        size_t padded = (N + 7) & ~size_t(7);
        y.resize(padded, 1.0f); vy.resize(padded, 0.0f);
        float* attrs[ATTR_COUNT] = { y.data(), vy.data() };
        bench(name, [&] {
            std::copy(y0.begin(), y0.end(), y.begin()); std::copy(vy0.begin(), vy0.end(), vy.begin());
            for (int f = 0; f < frames; ++f) fn(attrs);
        }, 3);
        double sum = 0; for (size_t i = 0; i < N; ++i) sum += y[i];
        std::printf("    somme des y = %.3f\n", sum);
    };

    std::printf("%zu entités, %d frames, %zu instructions par exécution du script\n", N, frames, sizeof(program) / sizeof(program[0]));
    run("(a) interprétation par entité", [&](float** attrs) { opaque(run_per_entity)(attrs, N); });
    BatchRegisters R(N);
    run("(b) interprétation par lot (auto-vec)", [&](float** attrs) { run_batch(attrs, N, R); });
    if (detect_cpu().avx2)
        run("(c) interprétation par lot (AVX2)", [&](float** attrs) { run_batch_avx2(attrs, N, R); });
}
