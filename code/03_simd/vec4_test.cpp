// Tests de vec4.hpp contre une implémentation scalaire de référence, + mini benchmark.
#include "vec4.hpp"
#include "../common/bench.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <numbers>

static int failures = 0;
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps * (1.0f + std::fabs(a)); }
#define CHECK(expr) do { if (!(expr)) { std::printf("ECHEC ligne %d : %s\n", __LINE__, #expr); ++failures; } } while (0)

struct V3 { float x, y, z; };   // référence scalaire
static V3 cross_ref(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

int main()
{
    vec4 a(1, 2, 3, 4), b(5, 6, 7, 8);
    CHECK(near(dot4(a, b), 70));
    CHECK(near(dot3(a, b), 38));
    vec4 s = a + b;
    CHECK(near(s.x(), 6) && near(s.y(), 8) && near(s.z(), 10) && near(s.w(), 12));

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> d(-10, 10);
    for (int i = 0; i < 1000; ++i)
    {
        V3 p{ d(rng), d(rng), d(rng) }, q{ d(rng), d(rng), d(rng) };
        V3 r = cross_ref(p, q);
        vec4 c = cross3(vec4(p.x, p.y, p.z, 0), vec4(q.x, q.y, q.z, 0));
        CHECK(near(c.x(), r.x, 1e-3f) && near(c.y(), r.y, 1e-3f) && near(c.z(), r.z, 1e-3f) && c.w() == 0.0f);
        vec4 n = normalize3(vec4(p.x, p.y, p.z, 0));
        CHECK(near(length3(n), 1.0f, 1e-3f));
    }

    // Transformation : scale puis rotation 90° puis translation
    mat4 M = mat4::translation(10, 0, 0) * mat4::rotation_z(std::numbers::pi_v<float> / 2) * mat4::scale(2, 2, 2);
    vec4 p = M * vec4(1, 0, 0, 1);      // (1,0,0) -> (2,0,0) -> (0,2,0) -> (10,2,0)
    CHECK(near(p.x(), 10, 1e-3f) && near(p.y(), 2, 1e-3f) && near(p.z(), 0, 1e-3f) && near(p.w(), 1));

    // Mini benchmark : transformer 1M de points
    const size_t N = 1'000'000;
    std::vector<vec4> pts(N, vec4(1, 2, 3, 1)), out(N);
    std::vector<float> sx(N * 4, 1.0f), so(N * 4);
    float m[16];
    for (int c = 0; c < 4; ++c) _mm_storeu_ps(m + 4 * c, M.c[c]);

    bench("scalaire : M * v (1M points)", [&] {
        for (size_t i = 0; i < N; ++i)
        {
            const float* v = &sx[4 * i]; float* o = &so[4 * i];
            for (int r = 0; r < 4; ++r)
                o[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r] * v[3];
        }
    });
    bench("SSE vec4 : M * v (1M points)", [&] {
        for (size_t i = 0; i < N; ++i) out[i] = M * pts[i];
    });

    std::printf(failures ? "%d test(s) en échec\n" : "Tous les tests passent\n", failures);
    return failures;
}
