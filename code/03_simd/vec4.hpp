// =============================================================================
// vec4.hpp : mini bibliothèque de maths 3D en SSE (style DirectXMath, simplifiée).
// Brique réutilisable pour la VM du projet (registres vec4).
// =============================================================================
#pragma once
#include "../common/simd_config.hpp"
#include <cmath>

// MSVC : __vectorcall passe les __m128 dans XMM0-XMM5 au lieu de la pile.
#if defined(_MSC_VER)
#   define VECCALL __vectorcall
#else
#   define VECCALL
#endif

struct alignas(16) vec4
{
    __m128 m;

    vec4() : m(_mm_setzero_ps()) {}
    explicit vec4(__m128 v) : m(v) {}
    vec4(float x, float y, float z, float w) : m(_mm_setr_ps(x, y, z, w)) {}
    explicit vec4(float s) : m(_mm_set1_ps(s)) {}

    float x() const { return _mm_cvtss_f32(m); }
    float y() const { return _mm_cvtss_f32(_mm_shuffle_ps(m, m, _MM_SHUFFLE(1, 1, 1, 1))); }
    float z() const { return _mm_cvtss_f32(_mm_movehl_ps(m, m)); }
    float w() const { return _mm_cvtss_f32(_mm_shuffle_ps(m, m, _MM_SHUFFLE(3, 3, 3, 3))); }
    void store(float* out4) const { _mm_storeu_ps(out4, m); }
};

inline vec4 VECCALL operator+(vec4 a, vec4 b) { return vec4(_mm_add_ps(a.m, b.m)); }
inline vec4 VECCALL operator-(vec4 a, vec4 b) { return vec4(_mm_sub_ps(a.m, b.m)); }
inline vec4 VECCALL operator*(vec4 a, vec4 b) { return vec4(_mm_mul_ps(a.m, b.m)); }   // composante par composante
inline vec4 VECCALL operator/(vec4 a, vec4 b) { return vec4(_mm_div_ps(a.m, b.m)); }
inline vec4 VECCALL operator*(vec4 a, float s) { return vec4(_mm_mul_ps(a.m, _mm_set1_ps(s))); }
inline vec4 VECCALL operator-(vec4 a) { return vec4(_mm_sub_ps(_mm_setzero_ps(), a.m)); }

inline vec4 VECCALL vmin(vec4 a, vec4 b) { return vec4(_mm_min_ps(a.m, b.m)); }
inline vec4 VECCALL vmax(vec4 a, vec4 b) { return vec4(_mm_max_ps(a.m, b.m)); }

inline vec4 VECCALL lerp(vec4 a, vec4 b, float t)
{
    return vec4(_mm_add_ps(a.m, _mm_mul_ps(_mm_sub_ps(b.m, a.m), _mm_set1_ps(t))));
}

// Somme horizontale des 4 composantes
inline float VECCALL hsum(__m128 v)
{
    __m128 s = _mm_add_ps(v, _mm_movehl_ps(v, v));                       // (x+z, y+w)
    return _mm_cvtss_f32(_mm_add_ss(s, _mm_shuffle_ps(s, s, _MM_SHUFFLE(1, 1, 1, 1))));
}

inline float VECCALL dot4(vec4 a, vec4 b) { return hsum(_mm_mul_ps(a.m, b.m)); }

inline float VECCALL dot3(vec4 a, vec4 b)
{
    __m128 p = _mm_mul_ps(a.m, b.m);
    __m128 xy = _mm_add_ss(p, _mm_shuffle_ps(p, p, _MM_SHUFFLE(1, 1, 1, 1)));
    return _mm_cvtss_f32(_mm_add_ss(xy, _mm_movehl_ps(p, p)));
}

// cross(a, b) = a.yzx * b.zxy - a.zxy * b.yzx
// Astuce classique en 3 shuffles au lieu de 4 : cross = (a * b.yzx - a.yzx * b).yzx
inline vec4 VECCALL cross3(vec4 a, vec4 b)
{
    const int YZX = _MM_SHUFFLE(3, 0, 2, 1);
    __m128 a_yzx = _mm_shuffle_ps(a.m, a.m, YZX);
    __m128 b_yzx = _mm_shuffle_ps(b.m, b.m, YZX);
    __m128 c = _mm_sub_ps(_mm_mul_ps(a.m, b_yzx), _mm_mul_ps(a_yzx, b.m));
    return vec4(_mm_shuffle_ps(c, c, YZX));   // w = 0
}

inline float VECCALL length3(vec4 a) { return std::sqrt(dot3(a, a)); }

inline vec4 VECCALL normalize3(vec4 a)
{
    __m128 len = _mm_sqrt_ss(_mm_set_ss(dot3(a, a)));
    return vec4(_mm_div_ps(a.m, _mm_shuffle_ps(len, len, 0)));
}

// -----------------------------------------------------------------------------
// mat4 : 4 colonnes (convention "column vectors" : v' = M * v)
// -----------------------------------------------------------------------------
struct alignas(16) mat4
{
    __m128 c[4];

    static mat4 identity()
    {
        mat4 r;
        r.c[0] = _mm_setr_ps(1, 0, 0, 0); r.c[1] = _mm_setr_ps(0, 1, 0, 0);
        r.c[2] = _mm_setr_ps(0, 0, 1, 0); r.c[3] = _mm_setr_ps(0, 0, 0, 1);
        return r;
    }
    static mat4 translation(float x, float y, float z)
    {
        mat4 r = identity();
        r.c[3] = _mm_setr_ps(x, y, z, 1);
        return r;
    }
    static mat4 scale(float x, float y, float z)
    {
        mat4 r = identity();
        r.c[0] = _mm_setr_ps(x, 0, 0, 0); r.c[1] = _mm_setr_ps(0, y, 0, 0); r.c[2] = _mm_setr_ps(0, 0, z, 0);
        return r;
    }
    static mat4 rotation_z(float angle)
    {
        float cs = std::cos(angle), sn = std::sin(angle);
        mat4 r = identity();
        r.c[0] = _mm_setr_ps(cs, sn, 0, 0); r.c[1] = _mm_setr_ps(-sn, cs, 0, 0);
        return r;
    }
};

// M * v = c0*v.x + c1*v.y + c2*v.z + c3*v.w
inline vec4 VECCALL operator*(const mat4& M, vec4 v)
{
    __m128 x = _mm_shuffle_ps(v.m, v.m, _MM_SHUFFLE(0, 0, 0, 0));
    __m128 y = _mm_shuffle_ps(v.m, v.m, _MM_SHUFFLE(1, 1, 1, 1));
    __m128 z = _mm_shuffle_ps(v.m, v.m, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 w = _mm_shuffle_ps(v.m, v.m, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 r = _mm_add_ps(_mm_mul_ps(M.c[0], x), _mm_mul_ps(M.c[1], y));
    r = _mm_add_ps(r, _mm_add_ps(_mm_mul_ps(M.c[2], z), _mm_mul_ps(M.c[3], w)));
    return vec4(r);
}

inline mat4 operator*(const mat4& A, const mat4& B)
{
    mat4 r;
    for (int i = 0; i < 4; ++i)
        r.c[i] = (A * vec4(B.c[i])).m;   // chaque colonne de B transformée par A
    return r;
}
