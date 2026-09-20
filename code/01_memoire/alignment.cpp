// Alignement, padding, et layout mémoire des structures.
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <new>          // std::hardware_destructive_interference_size
#include <memory>

struct Bad            // ordre "naturel" mais mal pensé
{
    bool     active;  // 1 octet
    double   x;       // 8 octets, alignement 8
    bool     visible; // 1 octet
    int32_t  id;      // 4 octets, alignement 4
    bool     dirty;   // 1 octet
};

struct Good           // même contenu, trié par alignement décroissant
{
    double   x;
    int32_t  id;
    bool     active;
    bool     visible;
    bool     dirty;
};

#pragma pack(push, 1)
struct Packed         // aucun padding : utile pour la sérialisation, dangereux pour la perf
{
    bool     active;
    double   x;
    bool     visible;
    int32_t  id;
    bool     dirty;
};
#pragma pack(pop)

struct alignas(16) Vec4   // alignement imposé : requis par _mm_load_ps
{
    float x, y, z, w;
};

#define SHOW(T) std::printf("%-8s sizeof=%2zu alignof=%2zu\n", #T, sizeof(T), alignof(T))
#define OFF(T, m) std::printf("    offsetof(%s, %s) = %zu\n", #T, #m, offsetof(T, m))

int main()
{
    SHOW(Bad);    OFF(Bad, active); OFF(Bad, x); OFF(Bad, visible); OFF(Bad, id); OFF(Bad, dirty);
    SHOW(Good);   OFF(Good, x); OFF(Good, id); OFF(Good, active); OFF(Good, visible); OFF(Good, dirty);
    SHOW(Packed);
    SHOW(Vec4);

    // Allocation alignée : trois façons en C++20 sous MSVC
    // 1) operator new aligné (C++17) : automatique pour les types sur-alignés
    auto* v = new Vec4[8];
    std::printf("new Vec4[8]  -> %p, aligné 16 ? %s\n", (void*)v, ((uintptr_t)v % 16 == 0) ? "oui" : "non");
    delete[] v;

    // 2) _aligned_malloc / _aligned_free (MSVC). std::aligned_alloc n'existe PAS sous MSVC.
#ifdef _MSC_VER
    void* p = _aligned_malloc(1024, 32);
    std::printf("_aligned_malloc(32) -> aligné 32 ? %s\n", ((uintptr_t)p % 32 == 0) ? "oui" : "non");
    _aligned_free(p);
#else
    void* p = std::aligned_alloc(32, 1024);
    std::printf("aligned_alloc(32) -> aligné 32 ? %s\n", ((uintptr_t)p % 32 == 0) ? "oui" : "non");
    std::free(p);
#endif

    // 3) operator new avec std::align_val_t explicite
    void* q = ::operator new(1024, std::align_val_t{64});
    std::printf("operator new(align 64) -> aligné 64 ? %s\n", ((uintptr_t)q % 64 == 0) ? "oui" : "non");
    ::operator delete(q, std::align_val_t{64});

    std::printf("hardware_destructive_interference_size = %zu\n", std::hardware_destructive_interference_size);
}
