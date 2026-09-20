; =============================================================================
; asm_basics.asm : premières fonctions en assembleur x86-64
; Assembleur : MASM (ml64.exe, fourni avec Visual Studio)
; Convention d'appel : Windows x64
;   - arguments entiers/pointeurs : RCX, RDX, R8, R9 (puis la pile)
;   - arguments flottants         : XMM0, XMM1, XMM2, XMM3
;   - retour                      : RAX (entier) ou XMM0 (flottant)
;   - volatiles (le callee peut les écraser) : RAX RCX RDX R8-R11 XMM0-XMM5
;   - non-volatiles (à restaurer si on les modifie) : RBX RBP RDI RSI RSP R12-R15 XMM6-XMM15
;   - avant un CALL : RSP aligné sur 16 et 32 octets de "shadow space" réservés
; =============================================================================

option casemap:none

.code

; -----------------------------------------------------------------------------
; int64_t asm_add(int64_t a, int64_t b)
; Fonction "leaf" (n'appelle rien, ne touche pas la pile) : pas besoin de prologue.
; -----------------------------------------------------------------------------
asm_add PROC
    lea     rax, [rcx + rdx]        ; LEA calcule une adresse... ou n'importe quelle somme
    ret
asm_add ENDP

; -----------------------------------------------------------------------------
; int64_t asm_sum_array(const int32_t* data, uint64_t count)
; Boucle classique : compteur décrémenté, pointeur incrémenté.
; -----------------------------------------------------------------------------
asm_sum_array PROC
    xor     eax, eax                ; rax = 0 (écrire eax met à zéro les 32 bits hauts)
    test    rdx, rdx                ; count == 0 ?
    jz      sum_done
sum_loop:
    movsxd  r8, DWORD PTR [rcx]     ; lit un int32 et l'étend en int64 (sign-extend)
    add     rax, r8
    add     rcx, 4                  ; data++ (sizeof(int32_t) = 4)
    dec     rdx
    jnz     sum_loop
sum_done:
    ret
asm_sum_array ENDP

; -----------------------------------------------------------------------------
; int64_t asm_max(int64_t a, int64_t b)
; Sans branchement : CMOVcc (conditional move).
; -----------------------------------------------------------------------------
asm_max PROC
    mov     rax, rcx                ; résultat = a
    cmp     rcx, rdx                ; positionne les flags selon a - b
    cmovl   rax, rdx                ; si a < b (signé) : résultat = b
    ret
asm_max ENDP

; -----------------------------------------------------------------------------
; uint64_t asm_strlen(const char* s)
; -----------------------------------------------------------------------------
asm_strlen PROC
    mov     rax, rcx                ; rax = curseur
strlen_loop:
    cmp     BYTE PTR [rax], 0       ; *curseur == '\0' ?
    je      strlen_end
    inc     rax
    jmp     strlen_loop
strlen_end:
    sub     rax, rcx                ; longueur = curseur - début
    ret
asm_strlen ENDP

; -----------------------------------------------------------------------------
; uint64_t asm_fib(uint32_t n)   (itératif, fib(0) = 0)
; -----------------------------------------------------------------------------
asm_fib PROC
    xor     eax, eax                ; a = 0
    mov     edx, 1                  ; b = 1
    test    ecx, ecx
    jz      fib_end
fib_loop:
    lea     r8, [rax + rdx]         ; t = a + b
    mov     rax, rdx                ; a = b
    mov     rdx, r8                 ; b = t
    dec     ecx
    jnz     fib_loop
fib_end:
    ret
asm_fib ENDP

; -----------------------------------------------------------------------------
; int64_t asm_apply_twice(int64_t (*fn)(int64_t), int64_t x)   -> fn(fn(x))
; Fonction NON-leaf : elle appelle du C++.
;  - elle doit sauvegarder les registres non-volatiles qu'elle utilise (RBX)
;  - elle doit réserver 32 octets de shadow space pour le callee
;  - RSP doit être multiple de 16 au moment du CALL
;  - PROC FRAME + directives .pushreg/.allocstack/.endprolog génèrent les
;    "unwind infos" (table .pdata/.xdata) : obligatoires sous Windows x64 pour
;    les exceptions C++, le debugger et les crash dumps.
; -----------------------------------------------------------------------------
asm_apply_twice PROC FRAME
    push    rbx                     ; entrée : RSP = 8 mod 16 (adresse de retour) -> push : 0 mod 16
    .pushreg rbx
    sub     rsp, 32                 ; shadow space ; RSP reste 0 mod 16
    .allocstack 32
    .endprolog

    mov     rbx, rcx                ; RBX survit aux appels (non-volatile)
    mov     rcx, rdx                ; argument 1 = x
    call    rbx                     ; rax = fn(x)
    mov     rcx, rax
    call    rbx                     ; rax = fn(fn(x))

    add     rsp, 32                 ; épilogue : exactement l'inverse du prologue
    pop     rbx
    ret
asm_apply_twice ENDP

; -----------------------------------------------------------------------------
; void asm_cpu_vendor(char out[13])
; CPUID leaf 0 : EBX, EDX, ECX contiennent "GenuineIntel" ou "AuthenticAMD".
; CPUID écrase RBX (non-volatile) -> sauvegarde obligatoire.
; -----------------------------------------------------------------------------
asm_cpu_vendor PROC FRAME
    push    rbx
    .pushreg rbx
    .endprolog

    mov     r8, rcx                 ; CPUID écrase ECX : on garde le pointeur ailleurs
    xor     eax, eax                ; leaf 0
    cpuid
    mov     DWORD PTR [r8], ebx
    mov     DWORD PTR [r8 + 4], edx
    mov     DWORD PTR [r8 + 8], ecx
    mov     BYTE PTR [r8 + 12], 0

    pop     rbx
    ret
asm_cpu_vendor ENDP

; -----------------------------------------------------------------------------
; float asm_dot4(const float* a, const float* b)
; Premier contact avec les registres SIMD : XMM = 128 bits = 4 floats.
; -----------------------------------------------------------------------------
asm_dot4 PROC
    movups  xmm0, XMMWORD PTR [rcx] ; charge a[0..3] (u = unaligned)
    movups  xmm1, XMMWORD PTR [rdx] ; charge b[0..3]
    mulps   xmm0, xmm1              ; (a0*b0, a1*b1, a2*b2, a3*b3) en UNE instruction
    movhlps xmm1, xmm0              ; xmm1[0..1] = xmm0[2..3]
    addps   xmm0, xmm1              ; (p0+p2, p1+p3, ...)
    movaps  xmm1, xmm0
    shufps  xmm1, xmm1, 1           ; xmm1[0] = xmm0[1]
    addss   xmm0, xmm1              ; xmm0[0] = p0+p2+p1+p3  (retour dans XMM0)
    ret
asm_dot4 ENDP

; -----------------------------------------------------------------------------
; int64_t asm_vm_run(const uint32_t* code, int64_t* regs)
; Une mini machine virtuelle A REGISTRES écrite en assembleur.
; Instruction sur 32 bits (little-endian) : [op:8][a:8][b:8][c:8]
;   0 HALT                 -> retourne regs[0]
;   1 LOADI a, imm16       -> regs[a] = (int16)(b | c << 8)
;   2 ADD   a, b, c        -> regs[a] = regs[b] + regs[c]
;   3 SUB   a, b, c        -> regs[a] = regs[b] - regs[c]
;   4 MUL   a, b, c        -> regs[a] = regs[b] * regs[c]
;   5 JNZ   a, off16       -> if (regs[a] != 0) pc += (int16)off
; Le dispatch est une "jump table" : exactement ce que MSVC génère pour un switch dense.
; -----------------------------------------------------------------------------
asm_vm_run PROC
    lea     r11, vm_table           ; adresse de la table (RIP-relative)
vm_dispatch::
    mov     eax, DWORD PTR [rcx]    ; FETCH
    add     rcx, 4                  ; pc++
    movzx   r8d, al                 ; DECODE : op
    mov     r9d, eax
    shr     r9d, 8
    movzx   r9d, r9b                ; a
    mov     r10d, eax
    shr     r10d, 16                ; r10w = imm16, r10b = b
    jmp     QWORD PTR [r11 + r8*8]  ; EXECUTE : saut indirect

op_halt::
    mov     rax, QWORD PTR [rdx]
    ret

op_loadi::
    movsx   r10, r10w
    mov     QWORD PTR [rdx + r9*8], r10
    jmp     vm_dispatch

op_add::
    movzx   r8d, r10b               ; b
    shr     eax, 24                 ; c
    mov     r10, QWORD PTR [rdx + r8*8]
    add     r10, QWORD PTR [rdx + rax*8]
    mov     QWORD PTR [rdx + r9*8], r10
    jmp     vm_dispatch

op_sub::
    movzx   r8d, r10b
    shr     eax, 24
    mov     r10, QWORD PTR [rdx + r8*8]
    sub     r10, QWORD PTR [rdx + rax*8]
    mov     QWORD PTR [rdx + r9*8], r10
    jmp     vm_dispatch

op_mul::
    movzx   r8d, r10b
    shr     eax, 24
    mov     r10, QWORD PTR [rdx + r8*8]
    imul    r10, QWORD PTR [rdx + rax*8]
    mov     QWORD PTR [rdx + r9*8], r10
    jmp     vm_dispatch

op_jnz::
    cmp     QWORD PTR [rdx + r9*8], 0
    je      vm_dispatch
    movsx   r10, r10w
    lea     rcx, [rcx + r10*4]      ; pc += off (en instructions)
    jmp     vm_dispatch
asm_vm_run ENDP

.const
vm_table QWORD op_halt, op_loadi, op_add, op_sub, op_mul, op_jnz

END
