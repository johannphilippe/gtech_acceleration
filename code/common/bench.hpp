// Petit utilitaire de mesure, volontairement minimal (pas de dépendance).
// Pour des mesures sérieuses : Google Benchmark, Tracy, VTune, Superluminal.
#pragma once
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <limits>

// Empêche le compilateur de supprimer un calcul dont le résultat n'est pas utilisé.
template <typename T>
inline void do_not_optimize(T const& value)
{
    static volatile T sink;
    sink = value;
    (void)sink;
}

// Cache une fonction derrière un pointeur volatile : le compilateur ne peut ni l'inliner,
// ni fusionner des appels identiques (sinon une fonction "pure" appelée 20 fois avec les
// mêmes arguments peut n'être calculée qu'une seule fois, et le benchmark ment).
template <typename F>
inline F* opaque(F* f)
{
    static F* volatile p = nullptr;
    p = f;
    return p;
}

// Exécute f() `runs` fois et affiche le meilleur temps (le minimum est le moins bruité).
template <typename F>
inline double bench(const char* name, F&& f, int runs = 5)
{
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < runs; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();
        f();
        auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::printf("%-40s %10.3f ms\n", name, best);
    return best;
}
