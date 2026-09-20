// Appel des fonctions assembleur depuis le C++.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

// Sous Windows, la convention x64 est la seule : rien à préciser.
// (Pour tester ce code sous Linux, on force la convention Windows côté C++.)
#if defined(__GNUC__) && !defined(_WIN32)
#   define WINABI __attribute__((ms_abi))
#else
#   define WINABI
#endif

extern "C"
{
    WINABI int64_t  asm_add(int64_t a, int64_t b);
    WINABI int64_t  asm_sum_array(const int32_t* data, uint64_t count);
    WINABI int64_t  asm_max(int64_t a, int64_t b);
    WINABI uint64_t asm_strlen(const char* s);
    WINABI uint64_t asm_fib(uint32_t n);
    WINABI int64_t  asm_apply_twice(int64_t (WINABI *fn)(int64_t), int64_t x);
    WINABI void     asm_cpu_vendor(char out[13]);
    WINABI float    asm_dot4(const float* a, const float* b);
    WINABI int64_t  asm_vm_run(const uint32_t* code, int64_t* regs);
}

static WINABI int64_t square_plus_one(int64_t x) { return x * x + 1; }

static constexpr uint32_t ins(uint8_t op, uint8_t a, uint8_t b, uint8_t c)
{
    return op | (a << 8) | (b << 16) | (uint32_t(c) << 24);
}
static constexpr uint32_t ins16(uint8_t op, uint8_t a, int16_t imm)
{
    return op | (a << 8) | (uint32_t(uint16_t(imm)) << 16);
}

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("ECHEC: %s\n", #expr); ++failures; } } while (0)

int main()
{
    CHECK(asm_add(40, 2) == 42);
    CHECK(asm_add(-5, 3) == -2);

    int32_t data[] = { 1, -2, 3, 4, 100000, -7 };
    CHECK(asm_sum_array(data, 6) == 1 - 2 + 3 + 4 + 100000 - 7);
    CHECK(asm_sum_array(data, 0) == 0);

    CHECK(asm_max(3, 9) == 9);
    CHECK(asm_max(-3, -9) == -3);

    CHECK(asm_strlen("hello, gtech") == std::strlen("hello, gtech"));
    CHECK(asm_strlen("") == 0);

    CHECK(asm_fib(0) == 0);
    CHECK(asm_fib(1) == 1);
    CHECK(asm_fib(10) == 55);
    CHECK(asm_fib(90) == 2880067194370816120ull);

    CHECK(asm_apply_twice(square_plus_one, 2) == 26);   // (2*2+1)=5 -> 5*5+1 = 26

    char vendor[13];
    asm_cpu_vendor(vendor);
    std::printf("CPU vendor : %s\n", vendor);

    float a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 };
    CHECK(asm_dot4(a, b) == 70.0f);

    // Programme VM : somme de 1 à 10
    int64_t regs[256] = {};
    const uint32_t program[] = {
        ins16(1, 0, 0),         // r0 = 0         (accumulateur)
        ins16(1, 1, 10),        // r1 = 10        (compteur)
        ins16(1, 2, -1),        // r2 = -1
        ins(2, 0, 0, 1),        // loop: r0 = r0 + r1
        ins(2, 1, 1, 2),        //       r1 = r1 + r2
        ins16(5, 1, -3),        //       if r1 != 0 goto loop
        ins(0, 0, 0, 0),        // halt
    };
    CHECK(asm_vm_run(program, regs) == 55);

    std::printf(failures ? "%d test(s) en échec\n" : "Tous les tests passent\n", failures);
    return failures;
}
