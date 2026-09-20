// =============================================================================
// autovec.cpp : laboratoire d'auto-vectorisation.
// Compiler avec MSVC :  cl /O2 /Qvec-report:2 autovec.cpp
//          ou dans VS : C/C++ > Command Line > Additional Options : /Qvec-report:2
// Chaque fonction illustre un cas qui VECTORISE ou qui BLOQUE la vectorisation.
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <cmath>

// --- 1. Le cas idéal : saxpy (tableaux contigus, pas de dépendance) ---------
void v01_saxpy(float* __restrict y, const float* __restrict x, float a, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        y[i] += a * x[i];
}

// --- 2. Aliasing : y et x peuvent se chevaucher -----------------------------
void v02_saxpy_alias(float* y, const float* x, float a, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        y[i] += a * x[i];
}

// --- 3. Réduction flottante (somme) : l'ordre des additions change le résultat
float v03_sum_float(const float* x, size_t n)
{
    float s = 0.0f;
    for (size_t i = 0; i < n; ++i)
        s += x[i];
    return s;
}

// --- 4. Réduction entière : associatif, pas de problème ---------------------
int32_t v04_sum_int(const int32_t* x, size_t n)
{
    int32_t s = 0;
    for (size_t i = 0; i < n; ++i)
        s += x[i];
    return s;
}

// --- 5. Dépendance entre itérations (loop-carried dependency) ---------------
void v05_prefix_sum(float* a, size_t n)
{
    for (size_t i = 1; i < n; ++i)
        a[i] = a[i - 1] + a[i];
}

// --- 6. Sortie anticipée (break) --------------------------------------------
int v06_find(const int32_t* a, int n, int32_t value)
{
    for (int i = 0; i < n; ++i)
        if (a[i] == value) return i;
    return -1;
}

// --- 7. Appel de fonction opaque (non inlinable) ----------------------------
float opaque(float x);
void v07_call(float* a, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        a[i] = opaque(a[i]);
}

// --- 8. Condition dans la boucle (if) ---------------------------------------
void v08_relu(float* __restrict out, const float* __restrict in, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        out[i] = in[i] > 0.0f ? in[i] : 0.0f;
}

// --- 9. Accès indirect (gather) ----------------------------------------------
void v09_gather(float* __restrict out, const float* __restrict table, const int32_t* idx, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        out[i] = table[idx[i]] * 2.0f;
}

// --- 10. Fonction mathématique : sqrt vectorise, pow/sin dépendent -----------
void v10_sqrt(float* __restrict out, const float* __restrict in, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        out[i] = std::sqrt(in[i]);
}

// --- 11. AoS : même calcul que 12 mais sur une struct ------------------------
struct P { float x, y, z; float vx, vy, vz; };
void v11_aos(P* __restrict p, float dt, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        p[i].x += p[i].vx * dt;
        p[i].y += p[i].vy * dt;
        p[i].z += p[i].vz * dt;
    }
}

// --- 12. SoA ------------------------------------------------------------------
void v12_soa(float* __restrict x, const float* __restrict vx, float dt, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        x[i] += vx[i] * dt;
}

// --- 13. Compteur de boucle signé 32 bits et pas non unitaire ----------------
void v13_stride(float* a, int n)
{
    for (int i = 0; i < n; i += 3)
        a[i] = 0.0f;
}

// --- 14. Petit nombre d'itérations connu --------------------------------------
void v14_small(float* __restrict a, const float* __restrict b)
{
    for (int i = 0; i < 3; ++i)
        a[i] += b[i];
}

// --- 15. Multiplication int64 (pas d'instruction SSE/AVX2 dédiée) -----------
void v15_mul64(int64_t* __restrict a, const int64_t* __restrict b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        a[i] *= b[i];
}
