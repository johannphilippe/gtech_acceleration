// Localité mémoire : trois expériences qui montrent le coût des cache misses.
#include "../common/bench.hpp"
#include <vector>
#include <list>
#include <random>
#include <numeric>
#include <cstdint>
#include <algorithm>

// --- 1. Parcours ligne/colonne d'une grille (row-major vs column-major) ---
static void grid_traversal()
{
    const size_t N = 4096;
    std::vector<int32_t> grid(N * N, 1);

    bench("grid row-major    (y puis x)", [&] {
        int64_t sum = 0;
        for (size_t y = 0; y < N; ++y)
            for (size_t x = 0; x < N; ++x)
                sum += grid[y * N + x];       // accès séquentiel : le prefetcher adore
        do_not_optimize(sum);
    });
    bench("grid column-major (x puis y)", [&] {
        int64_t sum = 0;
        for (size_t x = 0; x < N; ++x)
            for (size_t y = 0; y < N; ++y)
                sum += grid[y * N + x];       // saut de N*4 octets à chaque accès
        do_not_optimize(sum);
    });
}

// --- 2. std::vector vs std::list (mêmes données, même algorithme) ---
static void vector_vs_list()
{
    const size_t N = 5'000'000;
    std::vector<int64_t> vec(N);
    std::iota(vec.begin(), vec.end(), 0);

    // Une liste construite d'un coup a ses noeuds alloués les uns après les autres : trop "gentil".
    // On mélange l'ordre de chaînage (splice O(1)) pour simuler un heap fragmenté après des heures de jeu.
    std::list<int64_t> tmp(vec.begin(), vec.end());
    std::vector<std::list<int64_t>::iterator> its;
    its.reserve(N);
    for (auto it = tmp.begin(); it != tmp.end(); ++it) its.push_back(it);
    std::shuffle(its.begin(), its.end(), std::mt19937(42));
    std::list<int64_t> lst;
    for (auto it : its) lst.splice(lst.end(), tmp, it);

    bench("std::vector sum", [&] { do_not_optimize(std::accumulate(vec.begin(), vec.end(), int64_t{0})); });
    bench("std::list   sum", [&] { do_not_optimize(std::accumulate(lst.begin(), lst.end(), int64_t{0})); });
}

// --- 3. AoS vs SoA : on ne met à jour que la position ---
struct ParticleAoS
{
    float px, py, pz;
    float vx, vy, vz;
    float color[4];
    float life, size;
    char  name[32];   // données "froides" rarement lues
};

struct ParticlesSoA
{
    std::vector<float> px, py, pz, vx, vy, vz;
};

static void aos_vs_soa()
{
    const size_t N = 4'000'000;
    std::vector<ParticleAoS> aos(N);
    ParticlesSoA soa;
    for (auto* v : { &soa.px, &soa.py, &soa.pz, &soa.vx, &soa.vy, &soa.vz }) v->assign(N, 1.0f);
    for (auto& p : aos) { p.px = p.py = p.pz = p.vx = p.vy = p.vz = 1.0f; }

    const float dt = 0.016f;
    std::printf("sizeof(ParticleAoS) = %zu octets\n", sizeof(ParticleAoS));
    bench("AoS update position", [&] {
        for (auto& p : aos) { p.px += p.vx * dt; p.py += p.vy * dt; p.pz += p.vz * dt; }
    });
    bench("SoA update position", [&] {
        for (size_t i = 0; i < N; ++i) soa.px[i] += soa.vx[i] * dt;
        for (size_t i = 0; i < N; ++i) soa.py[i] += soa.vy[i] * dt;
        for (size_t i = 0; i < N; ++i) soa.pz[i] += soa.vz[i] * dt;
    });
}

int main()
{
    grid_traversal();
    vector_vs_list();
    aos_vs_soa();
}
