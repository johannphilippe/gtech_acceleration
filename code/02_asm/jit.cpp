// =============================================================================
// jit.cpp : de l'interpréteur au JIT, sur le MÊME bytecode.
//
// 1) un interpréteur C++ (switch) pour une mini VM à registres
// 2) un "template JIT" : chaque instruction VM est traduite en octets de machine
//    code x86-64 écrits À LA MAIN (encodage REX / opcode / ModRM / disp32),
//    copiés dans une page exécutable, puis appelés comme une fonction C++.
//
// Même format d'instruction que asm_vm_run (asm_basics.asm) :
//   [op:8][a:8][b:8][c:8]  -- HALT, LOADI a imm16, ADD/SUB/MUL a b c, JNZ a off16
// =============================================================================
#include "../common/bench.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#   define NOMINMAX
#   include <windows.h>
#else
#   include <sys/mman.h>
#endif

#if defined(__GNUC__) && !defined(_WIN32)
#   define WINABI __attribute__((ms_abi))   // on génère du code "convention Windows" partout
#else
#   define WINABI
#endif

enum Op : uint8_t { HALT, LOADI, ADD, SUB, MUL, JNZ };

static constexpr uint32_t ins(Op op, uint8_t a, uint8_t b, uint8_t c) { return op | (a << 8) | (b << 16) | (uint32_t(c) << 24); }
static constexpr uint32_t ins16(Op op, uint8_t a, int16_t imm)        { return op | (a << 8) | (uint32_t(uint16_t(imm)) << 16); }

// -----------------------------------------------------------------------------
// 1) Interpréteur
// -----------------------------------------------------------------------------
static int64_t interpret(const uint32_t* code, int64_t* r)
{
    const uint32_t* pc = code;
    for (;;)
    {
        uint32_t i = *pc++;
        uint8_t a = (i >> 8) & 0xFF, b = (i >> 16) & 0xFF, c = i >> 24;
        switch (Op(i & 0xFF))
        {
        case HALT:  return r[0];
        case LOADI: r[a] = int16_t(i >> 16); break;
        case ADD:   r[a] = r[b] + r[c]; break;
        case SUB:   r[a] = r[b] - r[c]; break;
        case MUL:   r[a] = r[b] * r[c]; break;
        case JNZ:   if (r[a] != 0) pc += int16_t(i >> 16); break;
        }
    }
}

// -----------------------------------------------------------------------------
// 2) JIT
// -----------------------------------------------------------------------------
class Emitter
{
public:
    std::vector<uint8_t> bytes;

    void u8(uint8_t v)   { bytes.push_back(v); }
    void i32(int32_t v)  { for (int k = 0; k < 4; ++k) bytes.push_back(uint8_t(v >> (8 * k))); } // little-endian
    size_t pos() const   { return bytes.size(); }
    void patch_i32(size_t at, int32_t v) { for (int k = 0; k < 4; ++k) bytes[at + k] = uint8_t(v >> (8 * k)); }

    // ModRM = [mod:2][reg:3][rm:3]. mod=10 -> [rm + disp32]. rm=010 -> RDX (pointeur regs).
    void modrm_rdx_disp32(uint8_t reg, int32_t disp) { u8(0x80 | (reg << 3) | 0x02); i32(disp); }

    // REX.W (0x48) = opérande 64 bits.
    void mov_rax_mem(int reg)  { u8(0x48); u8(0x8B); modrm_rdx_disp32(0, reg * 8); }        // mov rax, [rdx + reg*8]
    void mov_mem_rax(int reg)  { u8(0x48); u8(0x89); modrm_rdx_disp32(0, reg * 8); }        // mov [rdx + reg*8], rax
    void add_rax_mem(int reg)  { u8(0x48); u8(0x03); modrm_rdx_disp32(0, reg * 8); }        // add rax, [..]
    void sub_rax_mem(int reg)  { u8(0x48); u8(0x2B); modrm_rdx_disp32(0, reg * 8); }        // sub rax, [..]
    void imul_rax_mem(int reg) { u8(0x48); u8(0x0F); u8(0xAF); modrm_rdx_disp32(0, reg * 8); } // imul rax, [..]
    void mov_mem_imm32(int reg, int32_t imm) { u8(0x48); u8(0xC7); modrm_rdx_disp32(0, reg * 8); i32(imm); } // mov qword [..], imm32
    void cmp_mem_0(int reg)    { u8(0x48); u8(0x83); modrm_rdx_disp32(7, reg * 8); u8(0); } // cmp qword [..], 0   (83 /7 ib)
    size_t jne_rel32()         { u8(0x0F); u8(0x85); size_t at = pos(); i32(0); return at; } // jne rel32 (à patcher)
    void ret()                 { u8(0xC3); }
};

using JitFn = int64_t (WINABI*)(int64_t* regs_unused_rcx, int64_t* regs);   // Windows ABI : regs arrive dans RDX

static std::vector<uint8_t> jit_compile(const uint32_t* code, size_t count)
{
    Emitter e;
    std::vector<size_t> native_offset(count + 1);
    struct Fixup { size_t at; size_t target; };
    std::vector<Fixup> fixups;

    for (size_t n = 0; n < count; ++n)
    {
        native_offset[n] = e.pos();
        uint32_t i = code[n];
        uint8_t a = (i >> 8) & 0xFF, b = (i >> 16) & 0xFF, c = i >> 24;
        switch (Op(i & 0xFF))
        {
        case HALT:  e.mov_rax_mem(0); e.ret(); break;
        case LOADI: e.mov_mem_imm32(a, int16_t(i >> 16)); break;
        case ADD:   e.mov_rax_mem(b); e.add_rax_mem(c);  e.mov_mem_rax(a); break;
        case SUB:   e.mov_rax_mem(b); e.sub_rax_mem(c);  e.mov_mem_rax(a); break;
        case MUL:   e.mov_rax_mem(b); e.imul_rax_mem(c); e.mov_mem_rax(a); break;
        case JNZ:
            e.cmp_mem_0(a);
            fixups.push_back({ e.jne_rel32(), n + 1 + int16_t(i >> 16) });
            break;
        }
    }
    native_offset[count] = e.pos();

    // Backpatching : on connaît maintenant l'adresse native de chaque instruction VM.
    // rel32 est relatif à la fin de l'instruction de saut (= at + 4).
    for (auto& f : fixups)
        e.patch_i32(f.at, int32_t(native_offset[f.target] - (f.at + 4)));

    return e.bytes;
}

static void* make_executable(const std::vector<uint8_t>& bytes)
{
#ifdef _WIN32
    // W^X : on écrit dans une page RW, puis on la passe en RX (jamais RWX).
    void* mem = VirtualAlloc(nullptr, bytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    std::memcpy(mem, bytes.data(), bytes.size());
    DWORD old;
    VirtualProtect(mem, bytes.size(), PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), mem, bytes.size());
#else
    void* mem = mmap(nullptr, bytes.size(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    std::memcpy(mem, bytes.data(), bytes.size());
    mprotect(mem, bytes.size(), PROT_READ | PROT_EXEC);
#endif
    return mem;
}

int main()
{
    // somme des entiers de 1 à 10000*10000
    const uint32_t program[] = {
        ins16(LOADI, 0, 0),         // r0 = 0
        ins16(LOADI, 1, 10000),     // r1 = 10000
        ins16(LOADI, 3, 10000),     // r3 = 10000
        ins(MUL, 1, 1, 3),          // r1 = r1 * r3   (100 000 000)
        ins16(LOADI, 2, -1),        // r2 = -1
        ins(ADD, 0, 0, 1),          // loop: r0 += r1
        ins(ADD, 1, 1, 2),          //       r1 += r2
        ins16(JNZ, 1, -3),          //       if r1 != 0 goto loop
        ins(HALT, 0, 0, 0),
    };
    const size_t count = sizeof(program) / sizeof(program[0]);

    auto bytes = jit_compile(program, count);
    std::printf("JIT : %zu instructions VM -> %zu octets de machine code\n", count, bytes.size());
    for (size_t k = 0; k < bytes.size(); ++k) std::printf("%02X%s", bytes[k], (k % 16 == 15) ? "\n" : " ");
    std::printf("\n");

    auto fn = reinterpret_cast<JitFn>(make_executable(bytes));   // data pointer -> function pointer : autorisé sous MSVC/GCC/Clang

    int64_t r1[256] = {}, r2[256] = {};
    int64_t res_i = 0, res_j = 0;
    bench("interpréteur (switch)", [&] { res_i = interpret(program, r1); }, 3);
    bench("JIT (template)",        [&] { res_j = fn(nullptr, r2); }, 3);
    std::printf("résultats : %lld / %lld (attendu %lld)\n",
                (long long)res_i, (long long)res_j, (long long)(100000000LL * 100000001LL / 2));
    return res_i == res_j ? 0 : 1;
}
