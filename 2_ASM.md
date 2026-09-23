---
title: 2 - Assembleur x86-64
author: Johann Philippe
---

# 2. Assembleur x86-64

> Vous savez déjà écrire du C++, mais probablement pas une ligne d'assembleur. C'est le sujet de ce chapitre : comprendre ce qu'un CPU exécute *réellement*, écrire quelques fonctions en assembleur x86-64, et apprendre à lire ce que produit MSVC. C'est aussi le chapitre-pont : les registres et la pile vus ici sont directement ceux que vous retrouverez dans le SIMD (partie 3) et dans la VM à registres que vous allez construire (partie 4).
> Code de démonstration : [code/02_asm](https://github.com/johannphilippe/hardware_acceleration/tree/main/code/02_asm). Des exercices accompagnent ce chapitre (distribués séparément en cours). Tous les fichiers `.asm` ont été assemblés et testés : les fonctions sont appelées depuis du C++ avec des tests unitaires. Les extraits « sortie MSVC » viennent du vrai compilateur MSVC (v19.latest x64, via Compiler Explorer).

## Ce que vous devez savoir faire à la fin

1. Expliquer la boucle *fetch, decode, execute* et la relier à ce qu'est un interpréteur.
2. Connaître les registres généraux x86-64, les flags, et les modes d'adressage courants.
3. **Lire** le disassembly produit par MSVC (fenêtre *Disassembly* de VS, Compiler Explorer).
4. **Écrire** de petites fonctions en MASM et les appeler depuis du C++, en respectant la convention d'appel Windows x64.
5. Comprendre comment une instruction est **encodée** en octets — le lien direct avec l'assembleur maison et le JIT du projet.
6. Situer x86-64 par rapport à ARM64 et RISC-V.

---

# 2.1 Préambule historique

## Des cartes perforées aux mnémoniques

Les tout premiers programmes s'écrivent directement en **code machine** : des nombres, rien d'autre. En 1947, **Kathleen Booth** (Birkbeck College, Londres) décrit dans *Coding for A.R.C.* une notation symbolique pour les instructions — on la considère aujourd'hui comme l'inventrice du **langage d'assemblage**. En 1949, l'EDSAC de Maurice Wilkes charge ses programmes avec les *initial orders* de David Wheeler, un chargeur qui traduit des mnémoniques d'une lettre : c'est l'un des tout premiers **assembleurs**.

- **Langage d'assemblage** (*assembly language*) : le texte lisible (`mov rax, 1`).
- **Assembleur** (*assembler*) : le programme qui le traduit en octets (`48 C7 C0 01 00 00 00`).
- **Désassembleur** : l'inverse.

En français, on dit souvent « l'assembleur » pour les deux. Gardez la distinction en tête : dans le projet, vous allez écrire un assembleur — un programme, pas seulement du texte.

## La lignée x86

| Année | Processeur | Ce qui change |
|-------|------------|----------------|
| 1971 | Intel 4004 | premier microprocesseur (4 bits), pour une calculatrice |
| 1974 | Intel 8080 | 8 bits, ancêtre direct (Altair 8800) |
| 1978 | **Intel 8086** | 16 bits : naissance de « x86 ». Registres `AX BX CX DX SI DI BP SP` |
| 1985 | Intel 80386 | 32 bits (IA-32) : `EAX`..., mémoire protégée, pagination |
| 1989 | Intel 80486 | FPU x87 et cache L1 intégrés |
| 1995 | Pentium Pro (P6) | exécution out-of-order ; les instructions x86 sont décodées en **micro-ops** (µops) « RISC-like » |
| 1997 | Pentium MMX | premières instructions SIMD (partie 3) |
| 2003 | **AMD Opteron / Athlon 64** | **AMD64** = x86-64 : 64 bits, `RAX`..., `R8`-`R15`. Intel suit (Itanium/IA-64 est un échec) |
| 2023+ | Intel APX | annoncé : 32 registres généraux, instructions à 3 opérandes |

**Anecdote qui surprend toujours** : x86-64 a été conçu par **AMD**, pas par Intel. Intel misait sur une architecture totalement différente (l'Itanium, IA-64), incompatible avec le x86 existant. Le marché a choisi la compatibilité plutôt que la nouveauté, et Intel a fini par adopter l'extension de son concurrent. Chaque fois que vous compilez en `x64`, vous ciblez une architecture inventée par AMD.

## CISC vs RISC

- **CISC** (*Complex Instruction Set Computer*) : x86. Instructions de longueur variable (1 à 15 octets), beaucoup de modes d'adressage, instructions qui lisent et écrivent directement en mémoire.
- **RISC** (*Reduced Instruction Set Computer*) : Berkeley RISC (David Patterson, 1980), Stanford MIPS (John Hennessy), ARM (Acorn, 1985), RISC-V (2010). Instructions de taille fixe (4 octets), modèle *load/store* : on ne calcule que sur les registres.
- Aujourd'hui, la frontière est floue : un CPU x86 décode ses instructions en µops internes de type RISC. Hennessy et Patterson ont reçu le prix Turing 2017 pour RISC.

## L'assembleur dans le jeu vidéo

Sur les machines 8/16 bits, quasiment tout était écrit en assembleur : 6502 pour la NES et le Commodore 64, Z80 pour la Game Boy et la Master System, 68000 pour la Mega Drive et l'Amiga. **Doom** (1993) et surtout **Quake** (1996) ont leurs boucles de rendu les plus critiques écrites en ASM x86 par Michael Abrash — son *Graphics Programming Black Book* est [disponible gratuitement](https://www.jagregory.com/abrash-black-book/) et vaut le détour. **Chris Sawyer** a écrit *RollerCoaster Tycoon* (1999) presque entièrement en assembleur x86, seul. Aujourd'hui, on n'écrit presque plus d'ASM à la main dans un moteur — mais on en **lit** en permanence : profiling, crash dumps, vérification de la vectorisation (partie 3), reverse engineering, émulateurs, JIT.

Pour prendre goût au genre sans attendre le projet, plusieurs jeux mettent l'assembleur (ou quelque chose de très proche) au cœur du gameplay : *TIS-100* (2015), *Shenzhen I/O* (2016) et *EXAPUNKS* (2018) de Zachtronics, ou *Human Resource Machine* (Tomorrow Corporation, 2015).

Sur ce dernier point, une citation à méditer avant de se lancer tête baissée dans l'optimisation :

> « There is no doubt that the grail of efficiency leads to abuse. Programmers waste enormous amounts of time thinking about, or worrying about, the speed of noncritical parts of their programs [...]. We should forget about small efficiencies, say about 97% of the time: premature optimization is the root of all evil. Yet we should not pass up our opportunities in that critical 3%. »
> — Donald Knuth, *Structured Programming with go to Statements*, 1974

Le mot « prématurée » est essentiel — Knuth ne dit pas « n'optimisez jamais », il dit **mesurez avant** (partie 1) pour trouver les 3 % qui comptent, et laissez le reste tranquille. C'est précisément l'esprit de ce chapitre : vous n'allez pas réécrire tout votre moteur en assembleur, vous allez apprendre à identifier et traiter les quelques points chauds qui le justifient. Fait amusant : la citation est presque toujours tronquée à sa seule phrase centrale, ce qui lui fait dire l'inverse de son intention complète — un bon rappel que même les citations les plus célèbres méritent d'être vérifiées à la source.

---

# 2.2 Le modèle d'exécution

## Fetch, decode, execute

Le CPU répète une boucle très simple — **exactement comme l'interpréteur que vous allez construire en partie 4** :

```
for (;;) {
    instruction = memory[rip];      // FETCH
    decode(instruction);             // DECODE : quel opcode ? quels opérandes ?
    rip += instruction.length;       // passe à la suivante
    execute(instruction);            // EXECUTE : peut modifier rip (jmp, call, ret)
}
```

Un CPU est un interpréteur *matériel* de code machine. Une VM est un interpréteur *logiciel* de bytecode. Un JIT supprime l'étage logiciel. Retenez cette phrase, elle va structurer toute la fin du cours.

## Registres généraux x86-64

Chaque registre 64 bits a des « sous-registres » hérités de l'histoire :

```
 63                              31              15       7      0
 +-------------------------------+---------------+--------+------+
 |                              RAX                              |   64 bits
 +-------------------------------+---------------+--------+------+
                                 |      EAX                      |   32 bits
                                 +---------------+--------+------+
                                                 |       AX      |   16 bits
                                                 +--------+------+
                                                 |   AH   |  AL  |   8 bits
                                                 +--------+------+
```

| 64 bits | 32 | 16 | 8 bas | Rôle historique | Windows x64 |
|---------|----|----|-------|-----------------|-------------|
| `rax` | `eax` | `ax` | `al` | Accumulator | valeur de retour, volatile |
| `rbx` | `ebx` | `bx` | `bl` | Base | **non-volatile** |
| `rcx` | `ecx` | `cx` | `cl` | Counter | 1er argument, volatile |
| `rdx` | `edx` | `dx` | `dl` | Data | 2e argument, volatile |
| `rsi` | `esi` | `si` | `sil` | Source index | **non-volatile** |
| `rdi` | `edi` | `di` | `dil` | Destination index | **non-volatile** |
| `rbp` | `ebp` | `bp` | `bpl` | Base pointer (frame) | **non-volatile** |
| `rsp` | `esp` | `sp` | `spl` | Stack pointer | **non-volatile** (évidemment) |
| `r8` | `r8d` | `r8w` | `r8b` | - | 3e argument, volatile |
| `r9` | `r9d` | `r9w` | `r9b` | - | 4e argument, volatile |
| `r10`, `r11` | `r10d`... | | | - | volatiles |
| `r12`-`r15` | `r12d`... | | | - | **non-volatiles** |

« Volatile » et « non-volatile » comptent énormément — on y revient en détail en 2.4 (convention d'appel).

**Piège à connaître** : écrire dans un registre 32 bits (`mov eax, 1`) **met à zéro les 32 bits hauts** de `rax`. Écrire dans 8 ou 16 bits (`mov al, 1`) **ne touche pas** au reste. D'où l'idiome `xor eax, eax` pour mettre `rax` à zéro : plus court à encoder, et le CPU le reconnaît comme un cas spécial.

## Registres spéciaux

- **`rip`** (*instruction pointer*) : l'adresse de l'instruction suivante. On ne l'écrit jamais directement — `jmp`, `call` et `ret` le modifient (`call` empile l'ancien `rip`, `ret` le restaure — voir 2.4). Il sert aussi d'adresse de base pour accéder aux globales, le *RIP-relative addressing*.

  **Pourquoi et comment** : en mode long, x86-64 ne permet pas d'encoder une adresse mémoire absolue 64 bits comme opérande général (seule une forme très particulière et quasiment abandonnée, réservée à `mov` avec l'accumulateur, le permet). La façon normale d'atteindre une donnée statique est donc un **déplacement 32 bits signé, ajouté à `rip`** au moment de l'exécution :

  ```asm
  .data
  frame_count QWORD 0

  .code
  tick PROC
      inc     QWORD PTR [frame_count]   ; MASM résout automatiquement en RIP-relatif
      ret
  tick ENDP
  ```

  Vous n'écrivez jamais `[rip + frame_count]` vous-même en MASM x64 : écrire juste `frame_count` suffit, l'assembleur choisit le mode RIP-relatif automatiquement pour une adresse statique. C'est dans la sortie **désassemblée** de MSVC que ça devient explicite — vous y verrez littéralement `[rip+...]` en face de tout accès à une globale (comparez avec `[rbx]`, une adresse *dynamique* calculée à l'exécution, qui elle ne passe jamais par `rip`). Portée : ±2 Go autour du point d'exécution, largement suffisant pour atteindre n'importe quelle donnée statique du même binaire. Bénéfice collatéral : le code reste indépendant de sa position en mémoire (ASLR, DLL rechargée à une adresse différente à chaque lancement).
- **`rflags`** : des bits d'état positionnés par les opérations arithmétiques et logiques, lus ensuite par les sauts conditionnels.

| Flag | Nom | Mis à 1 quand... |
|------|-----|-------------------|
| `ZF` | Zero | le résultat vaut 0 |
| `SF` | Sign | le résultat est négatif (bit de poids fort) |
| `CF` | Carry | retenue *non signée* (dépassement unsigned) |
| `OF` | Overflow | dépassement *signé* |
| `PF` | Parity | nombre pair de bits à 1 dans l'octet bas (vestige) |

## Registres flottants et SIMD (aperçu)

- **x87** (`st(0)`-`st(7)`) : ancienne FPU à pile, **obsolète** en x64 (encore utilisée pour `long double` sous Linux).
- **`xmm0`-`xmm15`** (128 bits) : SSE. En x64, **tout le calcul flottant scalaire passe par là** (`movss`, `addss`, `mulsd`...), même pour un simple `float`.
- **`ymm0`-`ymm15`** (256 bits) : AVX. `xmm0` est la moitié basse de `ymm0`.
- **`zmm0`-`zmm31`** (512 bits) : AVX-512, avec les registres de masque `k0`-`k7`.
- **`MXCSR`** : registre de contrôle SSE (arrondi, exceptions, *denormals*, voir partie 3).

```
 511               255             127             0
 +-----------------+---------------+---------------+
 |                     ZMM0                        |  AVX-512
 +-----------------+---------------+---------------+
                   |          YMM0                 |  AVX
                   +---------------+---------------+
                                   |     XMM0      |  SSE
                                   +---------------+
```

C'est le sujet complet de la partie 3 — pour l'instant, retenez juste que ces registres existent et qu'ils ne sont pas optionnels.

## SWAR : simuler un peu de SIMD avec des registres généraux

Avant l'apparition d'instructions SIMD dédiées (partie 3), on bricolait un parallélisme limité directement dans les GPR : empaqueter plusieurs petites valeurs dans un seul registre 64 bits et les manipuler d'un coup. Ça marche sans rien de spécial pour les opérations **logiques** (`and`, `or`, `xor` : aucune retenue à gérer), mais casse pour l'arithmétique (`add`/`sub`) à cause de la retenue qui se propage à travers les frontières entre vos « lanes » — précisément ce que le vrai matériel SIMD résout, avec des lanes isolées au niveau du silicium. Cette technique a un nom, **SWAR** (*SIMD Within A Register*) ; détail et exemple en 3.1.

---

# 2.3 Syntaxe, instructions et adressage

## Intel vs AT&T

Deux syntaxes existent pour les mêmes instructions :

| Intel (MASM, NASM, VS, Compiler Explorer par défaut sous MSVC) | AT&T (GAS, GCC, `objdump` par défaut) |
|---|---|
| `mov rax, 1` : destination à **gauche** | `movq $1, %rax` : destination à **droite** |
| `mov eax, DWORD PTR [rcx+rdx*4+8]` | `movl 8(%rcx,%rdx,4), %eax` |

**Dans ce cours : syntaxe Intel partout.** Sous Linux, `objdump -M intel` et `gcc -masm=intel` la produisent, au cas où vous en croisiez.

## Modes d'adressage

La forme générale d'une adresse mémoire x86 est :

```
[ base + index * scale + displacement ]      scale ∈ {1, 2, 4, 8}
```

```asm
mov rax, rbx                        ; registre <- registre
mov rax, 42                         ; registre <- immédiat
mov rax, QWORD PTR [rbx]            ; registre <- mémoire (8 octets à l'adresse rbx)
mov eax, DWORD PTR [rcx + rdx*4]    ; data[i] pour un tableau d'int32
mov eax, DWORD PTR [rcx + 12]       ; e->champ à l'offset 12
mov QWORD PTR [rsp + 32], rax       ; mémoire <- registre
mov rax, QWORD PTR [rip + global]   ; RIP-relative (MASM l'écrit juste "global")
lea rax, [rcx + rdx*4]              ; calcule l'ADRESSE, sans lire la mémoire
```

`BYTE PTR` (1 octet), `WORD PTR` (2), `DWORD PTR` (4), `QWORD PTR` (8), `XMMWORD PTR` (16), `YMMWORD PTR` (32) précisent la taille lue ou écrite.

**Il n'existe pas d'instruction mémoire → mémoire** (`mov [a], [b]` est impossible). On passe toujours par un registre.

## Les instructions à connaître (80 % du code réel)

| Catégorie | Instructions |
|-----------|--------------|
| Copie | `mov`, `movzx` (zero-extend), `movsx`/`movsxd` (sign-extend), `lea`, `xchg` |
| Arithmétique | `add`, `sub`, `inc`, `dec`, `neg`, `imul` (signé), `mul`, `idiv`/`div` (utilisent `rdx:rax`), `cdq`/`cqo` |
| Logique / bits | `and`, `or`, `xor`, `not`, `shl`, `shr` (logique), `sar` (arithmétique), `rol`/`ror`, `popcnt`, `lzcnt`, `tzcnt`, `bsf`/`bsr` |
| Comparaison | `cmp` (soustraction qui ne garde que les flags), `test` (AND qui ne garde que les flags) |
| Sauts | `jmp`, `je`/`jz`, `jne`/`jnz`, `jl`/`jg`/`jle`/`jge` (**signés**), `jb`/`ja`/`jbe`/`jae` (**non signés**), `js` |
| Conditionnel sans saut | `cmovcc`, `setcc` |
| Pile / appels | `push`, `pop`, `call`, `ret` |
| Flottants scalaires | `movss`/`movsd`, `addss`, `mulss`, `divss`, `sqrtss`, `minss`/`maxss`, `cvtsi2ss`, `cvttss2si`, `comiss`/`ucomiss` |
| SIMD | `movaps`/`movups`, `addps`, `mulps`, `paddd`... (partie 3) |
| Divers | `cpuid`, `rdtsc`, `nop`, `int3` (breakpoint) |

## Glossaire des instructions moins évidentes

La table ci-dessus suffit à lire 80 % du code généré, mais certains mnémoniques ne sont pas transparents. Référence exhaustive, instruction par instruction, avec encodage et latence : [felixcloutier.com/x86](https://www.felixcloutier.com/x86/) (le manuel Intel remis en HTML — gardez ce lien sous la main).

| Instruction | Rôle |
|-------------|------|
| `movzx` | copie en **étendant avec des zéros** (`uint8_t` → `uint32_t`) |
| `movsx` / `movsxd` | copie en **étendant le signe** (`int8_t`/`int32_t` → plus grand, `d` = source 32 bits) |
| `lea` | calcule une **adresse** (`base + index*scale + disp`) sans jamais lire la mémoire ; sert aussi de multiplication/addition rapide (voir « idiomes » en 2.6) |
| `imul` / `mul` | multiplication **signée** / **non signée** ; la forme à 1 opérande écrit le résultat sur `rdx:rax` (128 bits) |
| `idiv` / `div` | division **signée** / **non signée** de `rdx:rax` par l'opérande ; quotient dans `rax`, reste dans `rdx` |
| `cdq` / `cqo` | étend le **signe** de `eax`/`rax` dans `edx`/`rdx` (*sign-extend into rDX*), obligatoire **avant** `idiv` pour préparer le dividende 64/128 bits |
| `shl` / `shr` | décalage **logique** (bits entrants à 0) à gauche / droite : équivalent de `* 2^n` / `/ 2^n` non signé |
| `sar` | décalage **arithmétique** à droite : préserve le bit de signe, `/ 2^n` signé (arrondi vers -∞, pas vers 0) |
| `rol` / `ror` | **rotation** de bits (ceux qui sortent d'un côté rentrent de l'autre), utile pour les hash et le chiffrement |
| `popcnt` | compte les bits à 1 (*population count*) |
| `lzcnt` / `tzcnt` | compte les **zéros de tête** (*leading*) / **de queue** (*trailing*) avant le premier bit à 1 — utile pour trouver un index dans un `movemask` (partie 3) |
| `bsf` / `bsr` | *bit scan forward/reverse* : index du premier / dernier bit à 1 (comportement différent de `tzcnt`/`lzcnt` si l'opérande est 0 — **indéfini** vs défini) |
| `cmovcc` | déplacement **conditionnel** : `mov` qui n'a lieu que si le flag testé (`cc`) est vrai, sans branche (voir `asm_max`, `asm_clamp`) |
| `setcc` | écrit **0 ou 1** dans un registre 8 bits selon le flag testé, sans branche |
| `movss` / `movsd` | déplace **un seul** flottant simple / double précision (*scalar single/double*, `xmm`) |
| `comiss` / `ucomiss` | **compare** deux flottants et positionne les flags entiers (`ZF`, `CF`...) pour un `jcc` derrière ; `u` = *unordered* accepté (ne lève pas d'exception sur NaN, voir 3.3) |
| `cvtsi2ss` | convertit un entier vers flottant (*convert signed integer to scalar single*) |
| `cvttss2si` | convertit un flottant vers entier avec **troncature** (*convert with truncation*) |
| `cpuid` | interroge le CPU (vendeur, extensions disponibles) — base du CPU dispatch (partie 3) |
| `rdtsc` | lit le compteur de cycles du CPU (*read time-stamp counter*), pour un micro-benchmark grossier |
| `adc` / `sbb` | addition / soustraction **avec retenue** (intègre aussi `CF` dans le calcul) : sert à chaîner une arithmétique au-delà de 64 bits (un nombre 128 bits s'additionne en `add` puis `adc` sur les deux moitiés), ou dans certains checksums |
| `bt` / `bts` / `btr` / `btc` | *bit test* [*and set* / *reset* / *complement*] : teste, puis positionne/efface/inverse, un bit précis d'un registre ou d'une zone mémoire — apparaît dans du code de bitset ou de champs de flags compacts |
| `pxor xmm, xmm` | idiome très courant pour mettre un registre `xmm` à zéro (équivalent SIMD entier de `xor eax, eax`) : aussi rapide, sans dépendance sur l'ancienne valeur du registre |
| `movd` / `movq` | transfère une valeur entre un registre général et un registre `xmm` (32 ou 64 bits) — apparaît chaque fois qu'un entier scalaire doit « entrer » dans le monde SIMD ou en ressortir |
| `cvtss2sd` / `cvtsd2ss` | convertit `float` ↔ `double` (à ne pas confondre avec `cvtsi2ss`/`cvttss2si`, qui convertissent entier ↔ flottant) |
| `ud2` | instruction délibérément **invalide** (2 octets), utilisée pour marquer un point réellement inatteignable ; c'est concrètement ce que génère `__assume(0)`/`std::unreachable()` (partie 4) quand le compilateur ne peut pas juste supprimer le test de bornes |

## Instructions de chaîne : `movs`/`stos`/`cmps`/`scas` et le préfixe `rep`

Ces instructions manipulent `rsi`/`rdi` comme des pointeurs **auto-incrémentés** (ou décrémentés, voir `DF` plus bas), et sont **très fréquentes** dans du code généré par le compilateur pour des copies ou remplissages de mémoire de taille connue — bien plus souvent que ce que leur discrétion dans les cours d'ASM laisse penser :

| Instruction | Rôle |
|-------------|------|
| `movs` (`movsb`/`movsw`/`movsd`/`movsq`) | copie `[rsi]` → `[rdi]`, puis avance `rsi` **et** `rdi` |
| `stos` (`stosb`/`stosw`/`stosd`/`stosq`) | écrit `al`/`ax`/`eax`/`rax` dans `[rdi]`, puis avance `rdi` — un remplissage |
| `cmps` | compare `[rsi]` et `[rdi]`, puis avance les deux — une comparaison mémoire |
| `scas` | compare `al`/`eax`/`rax` à `[rdi]`, puis avance `rdi` — une recherche d'octet/mot |
| `lods` | charge `[rsi]` dans `al`/`eax`/`rax`, puis avance `rsi` (rare seule, surtout historique) |

Précédées du préfixe **`rep`** (répète `rcx` fois), ou **`repe`/`repz`**, **`repne`/`repnz`** (répète tant que égal/pas égal, pour `cmps`/`scas`), elles condensent une boucle entière en une seule instruction encodée. C'est très exactement le mécanisme derrière le remplissage `0xCCCCCCCC` des variables locales en Debug, et c'est aussi souvent ce que MSVC choisit pour un `memcpy`/`memset`/`memcmp` de taille connue à la compilation, plutôt que d'appeler la fonction de la CRT.

Le **`DF`** (*direction flag*, dans `rflags`) contrôle le sens : `cld` le met à 0 (incrémente `rsi`/`rdi`, le cas normal et quasi systématique dans du code généré par compilateur), `std` le met à 1 (décrémente — rare, et une source classique de bugs si on oublie de le remettre à 0 après).

**Un exemple concret, pour fixer les idées** : `rsi` et `rdi` portent ces noms depuis les tout premiers x86 (*Source Index*, *Destination Index* — retrouvez-les dans le tableau des registres en 2.2), précisément parce que ce sont eux qu'utilisent ces instructions. Copier 256 octets de `src` vers `dst`, et remplir un buffer de zéros, s'écrivent ainsi à la main :

```asm
; void copy_256(const void* src, void* dst)      -- rcx = src, rdx = dst (convention Windows x64)
copy_256 PROC
    push    rsi                 ; rsi et rdi sont non-volatiles (2.2) : à sauvegarder
    push    rdi
    mov     rsi, rcx            ; rsi = SOURCE   (d'où son nom)
    mov     rdi, rdx            ; rdi = DESTINATION (d'où le sien)
    mov     rcx, 256            ; rcx = compteur pour rep
    cld                         ; sens croissant (DF = 0)
    rep     movsb               ; répète 256 fois : [rdi++] = [rsi++]
    pop     rdi
    pop     rsi
    ret
copy_256 ENDP

; void zero_1024(void* dst)                      -- rcx = dst
zero_1024 PROC
    push    rdi
    mov     rdi, rcx            ; rdi = DESTINATION
    xor     eax, eax            ; la valeur à écrire (stosd écrit eax)
    mov     rcx, 256            ; 256 x 4 octets = 1024 octets
    cld
    rep     stosd               ; répète 256 fois : [rdi] = eax ; rdi += 4
    pop     rdi
    ret
zero_1024 ENDP
```

`rep movsb` avec `rcx = 256` fait très exactement ce que ferait une boucle manuelle de 256 tours (`mov al, [rsi]` / `mov [rdi], al` / `inc rsi` / `inc rdi` / `dec rcx` / `jnz ...`), mais tient en une seule instruction encodée sur 2 octets (`F3 A4`). C'est pour ça que le compilateur les choisit spontanément pour un `memcpy`/`memset` de taille connue (revoyez plus haut) : moins d'instructions à décoder, moins de branches à prédire.

**Signé ou non signé ?** Le CPU ne connaît pas les types : `cmp` positionne tous les flags, et **c'est le saut choisi qui interprète**. `jl` lit `SF != OF` (signé), `jb` lit `CF` (non signé). Le compilateur choisit en fonction du type C++ d'origine.

## Table complète des conditions (`jcc` / `setcc` / `cmovcc`)

Le tableau plus haut n'en montrait qu'une partie. Le même jeu de suffixes de condition s'applique **identiquement** aux trois familles — seul le préfixe change (`j`, `set`, `cmov`) :

| Suffixe | Alias | Vrai si (flags) | Sens |
|---------|-------|------------------|------|
| `e` / `z` | — | `ZF=1` | égal / nul |
| `ne` / `nz` | — | `ZF=0` | différent / non nul |
| `l` / `nge` | — | `SF≠OF` | `<` **signé** |
| `ge` / `nl` | — | `SF=OF` | `>=` **signé** |
| `le` / `ng` | — | `ZF=1` ou `SF≠OF` | `<=` **signé** |
| `g` / `nle` | — | `ZF=0` et `SF=OF` | `>` **signé** |
| `b` / `nae` | `c` | `CF=1` | `<` **non signé** (ou retenue posée) |
| `ae` / `nb` | `nc` | `CF=0` | `>=` **non signé** (pas de retenue) |
| `be` / `na` | — | `CF=1` ou `ZF=1` | `<=` **non signé** |
| `a` / `nbe` | — | `CF=0` et `ZF=0` | `>` **non signé** |
| `s` | — | `SF=1` | négatif |
| `ns` | — | `SF=0` | positif ou nul |
| `o` | — | `OF=1` | dépassement signé |
| `no` | — | `OF=0` | pas de dépassement |
| `p` / `pe` | — | `PF=1` | parité paire (rare : checksums, ASCII) |
| `np` / `po` | — | `PF=0` | parité impaire |

Exemples : `jl` = *jump if less* (signé), `setb` = écrit 1 si `<` non signé, `cmovge` = déplace si `>=` signé. C'est exactement la même table qui explique la ligne « `jl` lit `SF != OF`... » ci-dessus — elle est juste complète cette fois, alias compris.

## Structures de contrôle

```cpp
if (a < b) x = 1; else x = 2;
```
```asm
    cmp  ecx, edx
    jge  else_branch        ; condition INVERSÉE : on saute si "pas a < b"
    mov  eax, 1
    jmp  end_if
else_branch:
    mov  eax, 2
end_if:
```

```cpp
for (int i = 0; i < n; ++i) body;
```
```asm
    xor  eax, eax           ; i = 0
    test edx, edx
    jle  done               ; garde : n <= 0 -> on ne rentre pas
top:
    ; body
    inc  eax
    cmp  eax, edx
    jl   top                ; test en BAS de boucle : un seul saut par itération
done:
```

---

# 2.4 La pile et la convention d'appel Windows x64

## La pile

La pile grandit **vers les adresses basses**. `rsp` pointe sur le dernier élément empilé.

- `push rax` fait `rsp -= 8` puis `[rsp] = rax`.
- `pop rax` fait `rax = [rsp]` puis `rsp += 8`.
- `call f` fait `push rip_suivant` puis `jmp f`.
- `ret` fait `pop rip`.

## `rbp`, le frame pointer : pourquoi il existe, pourquoi nos exemples ne s'en servent pas

Le tableau des registres (2.2) nomme `rbp` *base pointer (frame)* — mais aucun exemple MASM de ce cours ne s'en sert : ils adressent tous directement via `rsp`. Pourquoi la case existe-t-elle quand même ?

**Le problème que `rbp` résout** : `rsp` **bouge** en permanence pendant l'exécution d'une fonction — chaque `push`, chaque `call` imbriqué le déplace. Adresser une variable locale par rapport à `rsp` oblige donc à recalculer l'offset à chaque endroit du code où `rsp` a une hauteur différente. `rbp`, lui, est **délibérément figé** une fois pour toutes à l'entrée de la fonction (`mov rbp, rsp`, juste après avoir sauvegardé l'ancien) : toute variable locale s'adresse alors par un offset **constant** depuis `rbp` (`[rbp-8]`, `[rbp-16]`...), quel que soit ce que fait `rsp` ensuite dans le corps de la fonction.

**Un exemple réel** : cette fonction triviale, compilée en **Debug** (`/Od`) —

```cpp
void add() { int a = 0; int b = 1; int c = a + b; }
```

```asm
push    rbp
push    rdi
sub     rsp, 128h
mov     rbp, rsp        ; <- rbp figé ICI, pour tout le reste de la fonction
mov     dword ptr [a], 0     ; VS résout [a] en [rbp-XX], un offset FIXE
mov     dword ptr [b], 1     ; idem pour [b]
...
```

VS affiche les noms symboliques `[a]`, `[b]`, `[c]` plutôt que les vrais offsets `[rbp-4]`, `[rbp-8]`..., mais ce sont bien des adresses **rbp-relatives**, calculées une fois et valables partout dans la fonction. C'est précisément ce qui permet au débogueur de retrouver une variable locale de façon fiable à n'importe quel point d'arrêt.

**Pourquoi nos exemples MASM ne l'utilisent pas** : en Release, et dans la plupart du code écrit à la main, la taille du frame est connue à l'avance et fixe du début à la fin de la fonction — `rsp` ne bouge qu'au prologue et à l'épilogue, jamais au milieu. Adresser directement via `rsp` (comme tout le code de 2.5) marche donc tout aussi bien, et évite de sacrifier un registre supplémentaire (`rbp` redevient un registre général utilisable). C'est l'option *frame pointer omission*, activée par défaut en `/O2`. `rbp` garde son utilité dans deux cas précis : les **builds Debug** (fiabilité du débogueur, exemple ci-dessus), et les fonctions dont la **pile bouge de façon imprévisible en cours de route** (allocation dynamique sur la pile façon `alloca`) — là, seul un point de référence figé comme `rbp` permet de retrouver ses propres variables locales de façon fiable.
- `ret` fait `pop rip`.

## Convention d'appel Windows x64 (Microsoft x64 ABI, *Application Binary Interface*)

Documentation officielle : [x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention).

| Élément | Règle |
|---------|-------|
| Arguments entiers / pointeurs | `rcx`, `rdx`, `r8`, `r9`, puis sur la pile |
| Arguments flottants | `xmm0`, `xmm1`, `xmm2`, `xmm3` (**positionnels** : le 2e argument va dans `rdx` OU `xmm1`) |
| Retour | `rax` (entier), `xmm0` (flottant) ; structs > 8 octets via un pointeur caché dans `rcx` |
| Volatiles (*caller-saved*) | `rax rcx rdx r8 r9 r10 r11`, `xmm0`-`xmm5` |
| Non-volatiles (*callee-saved*) | `rbx rbp rdi rsi rsp r12`-`r15`, `xmm6`-`xmm15` |
| **Shadow space** | l'appelant réserve **32 octets** sur la pile pour les 4 premiers arguments, même s'ils passent par registres |
| Alignement | `rsp` **multiple de 16** au moment du `call` (donc `8 mod 16` à l'entrée de la fonction) |
| Red zone | **aucune** (contrairement à System V) |
| Unwind info | toute fonction non-leaf (ou qui touche `rsp` ou un registre non-volatile) doit déclarer son prologue (`.pdata` / `.xdata`) |

Il existe aussi `__vectorcall` (MSVC), qui passe jusqu'à 6 vecteurs `__m128`/`__m256` dans `xmm0`-`xmm5`. DirectXMath l'utilise.

**Piège très classique** : la plupart des tutoriels ASM trouvés en ligne utilisent **NASM sous Linux**, avec une convention totalement différente (`rdi`, `rsi`, `rdx`, `rcx` pour les 4 premiers arguments, pas de shadow space). Copier ce code tel quel sous Windows plante. Vérifiez toujours que vous lisez de la documentation **Windows x64**.

## Le cadre de pile (stack frame) d'une fonction non-leaf

```
adresses hautes
   +-------------------------+
   | arguments 5, 6...       |  [rsp+40] vus depuis le callee (après son call)
   +-------------------------+
   | shadow space (32 oct.)  |  réservé par l'appelant pour rcx, rdx, r8, r9
   +-------------------------+
   | adresse de retour       |  <- rsp à l'entrée du callee (= 8 mod 16)
   +-------------------------+
   | registres sauvegardés   |  push rbx, push rsi...
   +-------------------------+
   | variables locales       |
   +-------------------------+
   | shadow space pour MES   |
   | propres appels (32)     |  <- rsp pendant le corps (= 0 mod 16)
   +-------------------------+
adresses basses
```

**Sortie MSVC** (`/O2`), une fonction qui appelle `external(x, 1, 2, 3, 4)` :

```asm
int caller(int) PROC
        sub     rsp, 56                 ; zone des arguments sortants (5 x 8 = 40, arrondie à 48) + 8 pour l'alignement
        mov     edx, 1                  ; 2e argument
        mov     DWORD PTR [rsp+32], 4   ; 5e argument : SUR LA PILE, juste après le shadow space
        mov     r9d, 3                  ; 4e
        mov     r8d, 2                  ; 3e
        call    int external(int,int,int,int,int)   ; 1er argument : x est déjà dans ecx !
        inc     eax
        add     rsp, 56
        ret     0
```

Remarques : `x` arrive dans `ecx` et repart dans `ecx`, donc le compilateur n'a rien à faire. Le 5e argument est écrit à `[rsp+32]`, juste au-dessus des 32 octets de shadow space. `56 + 8` (adresse de retour) `= 64`, un multiple de 16 : la pile est bien alignée au moment du `call`. `sub rsp, 40` aurait suffi, mais MSVC arrondit la zone d'arguments à un multiple de 16.

### Pourquoi « 8 mod 16 » et pas simplement « 16 » ?

C'est le point qui prête le plus à confusion : la règle ABI dit que `rsp` doit être multiple de 16 **au moment du `call`**, mais la ligne « Alignement » du tableau ci-dessus parle de fonctions dont l'**entrée** est à « 8 mod 16 ». Les deux sont vraies en même temps, à des instants différents : `call` pousse 8 octets (l'adresse de retour), ce qui décale l'alignement d'une demi-ligne de 16. Suivez `rsp` sur un appel imbriqué, en partant d'une adresse hypothétique déjà alignée sur 16 (« 0 mod 16 ») :

```
avant `call foo`                        : rsp = ...1000   (0 mod 16 -- requis par l'ABI pour CE call)
call foo   -> push l'adresse de retour (8 octets)
entrée de foo                           : rsp = ...0FF8   (8 mod 16 -- décalé par le call)

foo:
    push rbx                             -> encore 8 octets
                                         : rsp = ...0FF0   (0 mod 16 -- ré-aligné !)
    sub rsp, 32   ; shadow space          -> 32 est multiple de 16, ne change pas le "mod 16"
                                         : rsp = ...0FD0   (0 mod 16 -- toujours aligné)
    call bar   -> push l'adresse de retour (8 octets)
    entrée de bar                       : rsp = ...0FC8   (8 mod 16 -- décalé, comme foo à son entrée)
```

Le schéma se répète à l'identique à chaque niveau d'appel : `call` décale toujours de 8, donc l'entrée d'**une** fonction est toujours à 8 mod 16 — c'est la trace directe du `push` de l'adresse de retour, rien de plus mystérieux. Le rôle du prologue est de **ré-aligner** avant le prochain `call` : soit avec un nombre impair de `push` de 8 octets (comme `push rbx` ci-dessus, qui à lui seul ramène à 0 mod 16), soit avec un `sub rsp, N` où `N` compense ce qui a déjà été poussé. C'est exactement ce qui se passe dans `caller()` juste au-dessus : `sub rsp, 56` ramène l'ensemble (`56` + les `8` de l'adresse de retour déjà poussée par l'appelant de `caller`) à `64`, multiple de 16 — la pile est de nouveau prête pour le `call external(...)` qui suit.

**Piège si vous oubliez ce détail** : un prologue qui pousse un **nombre pair** de registres (2, 4...) sans compenser par un `sub rsp` de taille impaire par rapport à 16 laisse `rsp` à 8 mod 16 au moment d'un `call` interne — l'appelée reçoit alors une pile mal alignée. Sur x86-64, ça ne plante pas toujours immédiatement (beaucoup de code scalaire tolère un `rsp` non aligné), mais **`movaps`/`movdqa`** et certaines instructions SSE (partie 3) exigent un opérande mémoire aligné sur 16 et lèvent une exception `#GP` sinon — une des causes classiques de crash « aléatoire », qui n'apparaît qu'en présence de SIMD.

## Comparaison : System V AMD64 (Linux, macOS)

| | Windows x64 | System V AMD64 |
|---|---|---|
| Arguments entiers | `rcx rdx r8 r9` | `rdi rsi rdx rcx r8 r9` |
| Arguments flottants | `xmm0`-`xmm3` (positionnels) | `xmm0`-`xmm7` (indépendants) |
| Shadow space | 32 octets | aucun |
| Red zone | non | 128 octets sous `rsp` |
| `rsi`, `rdi` | non-volatiles | volatiles |
| `xmm6`-`xmm15` | non-volatiles | **tous volatiles** |

---

# 2.5 Écrire de l'assembleur avec Visual Studio

## Assembleur pur ou assembleur « dans » le C++ ?

| Méthode | MSVC x64 | Commentaire |
|---------|----------|-------------|
| `__asm { ... }` (inline asm) | **NON** | uniquement en x86 32 bits. Supprimé en x64 |
| Fichier `.asm` séparé assemblé par **MASM** (`ml64.exe`) | **oui** | **la méthode conventionnelle sous Windows**, livrée avec VS |
| **Intrinsics** (`<intrin.h>`, `<immintrin.h>`) | oui | fonctions C++ qui correspondent presque 1:1 à des instructions (`__cpuid`, `_mm_add_ps`, `_BitScanForward`...) |
| clang-cl (fourni dans VS) + `__asm` | oui | Clang accepte les blocs `__asm` style Microsoft **même en x64** (vérifié). Pratique, mais peu conventionnel |
| NASM / UASM | oui | assembleurs tiers : NASM est très répandu (portable), UASM est compatible MASM avec macros de haut niveau |
| Générer les octets à l'exécution (**JIT**) | oui | `VirtualAlloc` + `VirtualProtect`, ou des bibliothèques comme [asmjit](https://github.com/asmjit/asmjit) et [xbyak](https://github.com/herumi/xbyak) |

Pour ce cours : **MASM** pour écrire, le *Disassembly* de VS et Compiler Explorer pour lire, les **intrinsics** pour le SIMD (partie 3), le **JIT** en bonus.

## Mettre en place MASM dans un projet Visual Studio

1. Projet C++ (Console App).
2. Clic droit sur le projet → **Build Dependencies** → **Build Customizations...** → cocher **masm (.targets, .props)**.
3. Ajouter un fichier `functions.asm`. Dans ses propriétés : *Item Type* = **Microsoft Macro Assembler** (normalement automatique après l'étape 2).
4. Côté C++, déclarer les fonctions en `extern "C"` (pas de *name mangling*) :
   ```cpp
   extern "C" int64_t asm_add(int64_t a, int64_t b);
   ```
5. Compiler en **x64** (pas Win32 : les registres 64 bits n'existent pas en 32 bits).

Avec CMake (VS ouvre les dossiers CMake nativement) :

```cmake
enable_language(ASM_MASM)
add_executable(asm_demo main.cpp asm_basics.asm)
```

## Squelette d'un fichier MASM x64

```asm
option casemap:none     ; noms sensibles à la casse (comme en C++)

.data                   ; variables globales initialisées
counter QWORD 0

.const                  ; données en lecture seule
message BYTE "hello", 0

.code                   ; code
asm_add PROC
    lea     rax, [rcx + rdx]
    ret
asm_add ENDP

END
```

**Limites de `ml64` à connaître** : contrairement à `ml` 32 bits (et à UASM), **`ml64` ne supporte pas `INVOKE`, `ADDR`, ni les paramètres nommés dans `PROC`**. Tout se fait « à la main » avec les registres de la convention d'appel. Pédagogiquement, c'est plutôt une bonne chose.

## Exemples commentés (testés)

Fichier complet : [code/02_asm/asm_basics.asm](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/02_asm/asm_basics.asm), appelé depuis [main.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/02_asm/main.cpp).

### Leaf function : addition

```asm
; int64_t asm_add(int64_t a, int64_t b)
asm_add PROC
    lea     rax, [rcx + rdx]        ; LEA calcule une adresse... ou n'importe quelle somme
    ret
asm_add ENDP
```

### Boucle : somme d'un tableau

```asm
; int64_t asm_sum_array(const int32_t* data, uint64_t count)
asm_sum_array PROC
    xor     eax, eax                ; rax = 0
    test    rdx, rdx                ; count == 0 ?
    jz      sum_done
sum_loop:
    movsxd  r8, DWORD PTR [rcx]     ; lit un int32, l'étend en int64
    add     rax, r8
    add     rcx, 4                  ; data++
    dec     rdx
    jnz     sum_loop
sum_done:
    ret
asm_sum_array ENDP
```

### Sans branchement : `cmov`

```asm
; int64_t asm_max(int64_t a, int64_t b)
asm_max PROC
    mov     rax, rcx
    cmp     rcx, rdx
    cmovl   rax, rdx                ; si a < b (signé) : rax = b
    ret
asm_max ENDP
```

### Non-leaf : appeler du C++ depuis l'ASM

```asm
; int64_t asm_apply_twice(int64_t (*fn)(int64_t), int64_t x)   -> fn(fn(x))
asm_apply_twice PROC FRAME
    push    rbx                     ; entrée : RSP = 8 mod 16 -> après push : 0 mod 16
    .pushreg rbx
    sub     rsp, 32                 ; shadow space pour nos appels
    .allocstack 32
    .endprolog

    mov     rbx, rcx                ; RBX survit aux appels (non-volatile)
    mov     rcx, rdx                ; argument = x
    call    rbx                     ; rax = fn(x)
    mov     rcx, rax
    call    rbx                     ; rax = fn(fn(x))

    add     rsp, 32                 ; épilogue = inverse exact du prologue
    pop     rbx
    ret
asm_apply_twice ENDP
```

**`PROC FRAME`, `.pushreg`, `.allocstack`, `.endprolog`** génèrent les *unwind infos*. Sans elles, le programme fonctionne tant que tout va bien, mais une exception C++ qui traverse la fonction, le debugger (call stack) ou un crash dump deviennent incohérents. C'est une spécificité Windows x64 qui n'existe pas sous Linux.

### Un piège sur « leaf » : toucher `rsp` suffit, même sans appel

`asm_apply_twice` ci-dessus est non-leaf au sens intuitif : elle appelle autre chose (`call rbx`). Mais relisez la ligne « Unwind info » du tableau 2.4 : « toute fonction non-leaf (**ou** qui touche `rsp` **ou** un registre non-volatile) ». Le *ou* est important — une fonction qui n'appelle **jamais rien** peut quand même perdre son statut « leaf » au sens de l'ABI, simplement parce qu'elle modifie `rsp` :

```asm
; int64_t asm_sum_last3(const int64_t* data, int64_t count)   -- copie 3 valeurs sur la pile puis les additionne
asm_sum_last3 PROC FRAME
    sub     rsp, 24                 ; scratch local -- AUCUN call, AUCUN push de registre non-volatile
    .allocstack 24
    .endprolog

    mov     rax, [rcx + rdx*8 - 8]
    mov     [rsp],    rax
    mov     rax, [rcx + rdx*8 - 16]
    mov     [rsp+8],  rax
    mov     rax, [rcx + rdx*8 - 24]
    mov     [rsp+16], rax

    mov     rax, [rsp]
    add     rax, [rsp+8]
    add     rax, [rsp+16]

    add     rsp, 24
    ret
asm_sum_last3 ENDP
```

Cette fonction ne fait **aucun `call`** — au sens « arbre d'appel », c'est bien une feuille. Mais elle bouge `rsp` (`sub rsp, 24`), ce qui suffit à lui retirer le statut *leaf* au sens précis de l'ABI Windows x64 : sans `.allocstack`/`.endprolog`, un déroulement d'exception (ou un simple stack walk par le debugger) ne saurait plus retrouver l'adresse de retour, puisqu'elle ne se trouve plus à `[rsp]` mais à `[rsp+24]`. Retenez « leaf » comme « ne touche à rien de ce qui doit être déroulé », pas comme « n'appelle personne ».

### Voir les unwind infos pour de vrai : `.pdata` / `.xdata`

`.pdata` et `.xdata` ne sont pas des directives que vous écrivez vous-même — ce sont les **sections du fichier objet/exécutable** que `.pushreg`, `.allocstack` et `.endprolog` remplissent pour vous à l'assemblage :

- **`.xdata`** contient, pour chaque fonction `PROC FRAME`, une structure `UNWIND_INFO` : la liste des « unwind codes » (un par `.pushreg`/`.allocstack`/`.savexmm128`...), chacun associé à l'offset dans le prologue où il s'applique. C'est l'équivalent binaire du commentaire `; entrée : RSP = 8 mod 16 -> après push : 0 mod 16` que vous écrivez à côté de chaque instruction — sauf que là, c'est la machine qui le lit pour dérouler la pile.
- **`.pdata`** contient une table de `RUNTIME_FUNCTION` (un triplet début/fin/pointeur-vers-`.xdata`) par fonction : l'index qui permet à Windows, pour une adresse `rip` donnée pendant une exception ou un stack walk, de retrouver instantanément la bonne `UNWIND_INFO`.

Pour les voir vous-même sur un `.obj` compilé par MASM : `dumpbin /unwindinfo functions.obj` (même outil en ligne de commande VS que `/disasm`, voir 2.7). Vous devriez y retrouver, pour `asm_apply_twice`, deux codes — un `ALLOC_SMALL` pour `sub rsp, 32` et un `PUSH_NONVOL` pour `push rbx` — listés dans l'ordre **inverse** du prologue (le dérouleur défait le prologue en remontant du dernier au premier). Pour `asm_add` (`PROC` simple, sans `FRAME`), la même commande ne produira **aucune** entrée : sans `.pdata`/`.xdata`, une exception qui traverserait cette fonction ne saurait pas comment la « dérouler » — sans conséquence ici, puisqu'une fonction qui ne touche ni `rsp` ni un registre non-volatile n'a, par construction, rien à défaire.

### `cpuid` : interroger le CPU

```asm
; void asm_cpu_vendor(char out[13])   -> "GenuineIntel" / "AuthenticAMD"
asm_cpu_vendor PROC FRAME
    push    rbx                     ; CPUID écrase RBX, qui est non-volatile
    .pushreg rbx
    .endprolog
    mov     r8, rcx                 ; CPUID écrase aussi ECX
    xor     eax, eax                ; leaf 0
    cpuid
    mov     DWORD PTR [r8], ebx
    mov     DWORD PTR [r8 + 4], edx
    mov     DWORD PTR [r8 + 8], ecx
    mov     BYTE PTR [r8 + 12], 0
    pop     rbx
    ret
asm_cpu_vendor ENDP
```

C'est la base du **CPU dispatch** de la partie 3 : savoir si AVX2 est disponible avant de l'utiliser.

### Premier contact SIMD

```asm
; float asm_dot4(const float* a, const float* b)
asm_dot4 PROC
    movups  xmm0, XMMWORD PTR [rcx] ; 4 floats d'un coup
    movups  xmm1, XMMWORD PTR [rdx]
    mulps   xmm0, xmm1              ; 4 multiplications en UNE instruction
    movhlps xmm1, xmm0              ; somme horizontale...
    addps   xmm0, xmm1
    movaps  xmm1, xmm0
    shufps  xmm1, xmm1, 1
    addss   xmm0, xmm1              ; résultat dans XMM0 = valeur de retour
    ret
asm_dot4 ENDP
```

### Une machine virtuelle à registres... en assembleur

C'est la **passerelle vers la partie 4**. Instructions de 32 bits `[op:8][a:8][b:8][c:8]`, registres dans un tableau `int64_t regs[256]`, dispatch par **jump table** :

```asm
asm_vm_run PROC
    lea     r11, vm_table
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

op_add::
    movzx   r8d, r10b               ; b
    shr     eax, 24                 ; c
    mov     r10, QWORD PTR [rdx + r8*8]
    add     r10, QWORD PTR [rdx + rax*8]
    mov     QWORD PTR [rdx + r9*8], r10
    jmp     vm_dispatch
    ; ... op_halt, op_loadi, op_sub, op_mul, op_jnz
asm_vm_run ENDP

.const
vm_table QWORD op_halt, op_loadi, op_add, op_sub, op_mul, op_jnz
```

---

# 2.6 Lire le code généré par MSVC

## Outils

- **Visual Studio, en debug** : clic droit → *Go To Disassembly* (`Ctrl+Alt+D`) ; fenêtre *Registers* (`Ctrl+Alt+G`) ; fenêtres *Memory* (`Ctrl+Alt+M`, `1`). On peut exécuter pas à pas **instruction par instruction**. Pour lire du code optimisé : configuration Release avec `/Zi`.
- **[Compiler Explorer](https://godbolt.org)** : choisir *x64 msvc v19.latest* et les options `/O2`. C'est l'outil n°1 du cours. Il permet de comparer MSVC, Clang et GCC, et d'afficher le rapport de vectorisation.
- `dumpbin /disasm fichier.obj` (en ligne de commande VS).
- Pour le reverse : [x64dbg](https://x64dbg.com), [Ghidra](https://ghidra-sre.org) (NSA, gratuit), IDA Free.

## Debug (`/Od`) vs Release (`/O2`)

```cpp
int64_t sum(const int32_t* data, uint64_t count)
{
    int64_t s = 0;
    for (uint64_t i = 0; i < count; ++i) s += data[i];
    return s;
}
```

**`/Od`** : chaque variable vit **sur la pile** et chaque ligne recharge tout depuis la mémoire.

```asm
        mov     QWORD PTR [rsp+16], rdx         ; copie les arguments dans le shadow space
        mov     QWORD PTR [rsp+8], rcx
        sub     rsp, 24
        mov     QWORD PTR s$[rsp], 0
        mov     QWORD PTR i$1[rsp], 0
        jmp     SHORT $LN4@sum
$LN2@sum:
        mov     rax, QWORD PTR i$1[rsp]         ; i++ : load, inc, store
        inc     rax
        mov     QWORD PTR i$1[rsp], rax
$LN4@sum:
        mov     rax, QWORD PTR count$[rsp]
        cmp     QWORD PTR i$1[rsp], rax
        jae     SHORT $LN3@sum
        mov     rax, QWORD PTR data$[rsp]
        mov     rcx, QWORD PTR i$1[rsp]
        movsxd  rax, DWORD PTR [rax+rcx*4]
        mov     rcx, QWORD PTR s$[rsp]
        add     rcx, rax
        mov     rax, rcx
        mov     QWORD PTR s$[rsp], rax
        jmp     SHORT $LN2@sum
```

**`/O2`** : le compilateur **vectorise** avec une vérification à l'exécution de la disponibilité de SSE4.1, puis traite la fin de tableau en scalaire.

```asm
        cmp     rdx, 4
        jb      SHORT $LN23@sum                 ; moins de 4 éléments -> scalaire
        cmp     DWORD PTR __isa_available, 2    ; le CPU supporte-t-il SSE4.x ? (détecté au démarrage par la CRT)
        jl      SHORT $LN23@sum
        ...
$LL4@sum:
        pmovsxdq xmm1, QWORD PTR [r8+rax*4]     ; 2 int32 -> 2 int64 (SSE4.1)
        paddq   xmm3, xmm1
        pmovsxdq xmm1, QWORD PTR [r8+rax*4+8]
        add     rax, 4                          ; 4 éléments par itération
        paddq   xmm2, xmm1
        cmp     rax, rcx
        jb      SHORT $LL4@sum
        ...
$LL17@sum:                                      ; boucle de "reste" (tail), déroulée par 2
        movsxd  rcx, DWORD PTR [r8+rax*4]
        add     rdx, rcx
```

Cet exemple montre à lui seul quatre choses : (1) l'écart Debug/Release, (2) l'auto-vectorisation (teaser de la partie 3), (3) le **dispatch CPU automatique** via `__isa_available`, (4) la gestion du « reste » (*tail*). Voir le code Debug est aussi la meilleure façon de comprendre pourquoi vous ne devez jamais mesurer une performance en Debug.

## Idiomes du compilateur à reconnaître

```asm
; int mul9(int x) { return x * 9; }
        lea     eax, DWORD PTR [rcx+rcx*8]      ; LEA comme multiplication rapide

; unsigned div8(unsigned x) { return x / 8; }
        shr     ecx, 3                          ; division par puissance de 2 = décalage
        mov     eax, ecx

; int abs_branch(int x) { if (x < 0) return -x; return x; }
        mov     eax, ecx
        neg     eax
        cmovs   eax, ecx                        ; le "if" a disparu : branchless

; int get_id(const Entity* array, int index) { return array[index].id; }   (sizeof(Entity) = 28)
        movsxd  rax, edx
        imul    rax, rax, 28                    ; index * sizeof(Entity)
        mov     eax, DWORD PTR [rax+rcx]
```

Un `switch` dense devient une **jump table** — le mécanisme exact que vous utiliserez pour le dispatch de votre VM (partie 4) :

```asm
int op(int,int,int) PROC
        cmp     ecx, 6
        ja      SHORT $LN11@op                  ; hors bornes -> default
        movsxd  rax, ecx
        lea     r9, OFFSET FLAT:__ImageBase
        mov     ecx, DWORD PTR $LN13@op[r9+rax*4]   ; table d'offsets 32 bits relatifs à l'image
        add     rcx, r9
        jmp     rcx                             ; saut indirect
$LN4@op:
        lea     eax, DWORD PTR [rdx+r8]         ; case 0: return a + b;
        ret     0
```

Le `cmp ecx, 6` / `ja` est le **bounds check** du `switch`. Dans un interpréteur qui contrôle la totalité de son bytecode, on peut le supprimer sciemment avec `default: __assume(0);` (MSVC) ou `std::unreachable()` (C++23) — un gain mesurable sur le chemin le plus chaud du code, au prix d'un comportement indéfini si un opcode invalide apparaît jamais.

Une petite fonction flottante sur une struct (`pos += vel * dt`) montre un autre mécanisme : MSVC charge `x, y` avec `movsd` (8 octets = 2 floats) et fait du **SLP vectorization** (*Superword-Level Parallelism*, qui regroupe des instructions scalaires consécutives sans qu'il y ait de boucle) :

```asm
void integrate(Entity *,float) PROC
        movsd   xmm0, QWORD PTR [rcx+4]         ; pos.x, pos.y
        movaps  xmm2, xmm1
        movsd   xmm3, QWORD PTR [rcx+16]        ; vel.x, vel.y
        mulss   xmm1, DWORD PTR [rcx+24]        ; vel.z * dt (scalaire)
        shufps  xmm2, xmm2, 0                   ; dt dupliqué 4 fois
        mulps   xmm3, xmm2                      ; (vel.x, vel.y) * dt
        addss   xmm1, DWORD PTR [rcx+12]
        addps   xmm3, xmm0
        movss   DWORD PTR [rcx+12], xmm1
        movsd   QWORD PTR [rcx+4], xmm3
        ret     0
```

---

# 2.7 Encodage des instructions : sous le capot de l'assembleur

C'est **la** section utile pour le projet : un assembleur transforme du texte en octets, et un JIT fait la même chose à l'exécution.

## Format d'une instruction x86-64

```
+----------+-------+----------+---------+-------+--------------+-------------+
| Prefixes |  REX  |  Opcode  |  ModRM  |  SIB  | Displacement |  Immediate  |
| 0-4 oct. | 0-1   | 1-3 oct. |  0-1    |  0-1  |  0,1,2,4 oct.| 0,1,2,4,8   |
+----------+-------+----------+---------+-------+--------------+-------------+
                              max 15 octets au total
```

- **REX** (`0100WRXB`) : `W=1` pour les opérandes 64 bits ; `R`, `X`, `B` étendent les champs pour atteindre `r8`-`r15`. `0x48` = REX.W.
- **ModRM** (`[mod:2][reg:3][rm:3]`) : `mod=11` registre-registre ; `mod=00/01/10` mémoire sans, avec 8 ou avec 32 bits de déplacement. `reg` désigne un registre **ou** une extension d'opcode (`/7`...).
- **SIB** (`[scale:2][index:3][base:3]`) : pour `[base + index*scale]`.
- Numéros de registres : `rax=0, rcx=1, rdx=2, rbx=3, rsp=4, rbp=5, rsi=6, rdi=7`, puis `r8`-`r15` = 0-7 avec le bit REX.
- **VEX** (AVX) et **EVEX** (AVX-512) sont des préfixes plus compacts qui remplacent REX pour le SIMD.

## Exemple décortiqué

`mov rax, QWORD PTR [rdx + 0x18]` s'encode `48 8B 82 18 00 00 00` :

| Octet(s) | Signification |
|----------|---------------|
| `48` | REX.W : opérande 64 bits |
| `8B` | opcode `MOV r64, r/m64` |
| `82` | ModRM = `10 000 010` : mod=10 (disp32), reg=000 (`rax`), rm=010 (`rdx`) |
| `18 00 00 00` | déplacement 32 bits little-endian = 0x18 |

Un vrai assembleur choisirait `48 8B 42 18` avec un disp8, plus court. Un JIT écrit pour rester simple prend souvent toujours disp32 : c'est un compromis classique entre simplicité d'implémentation et taille du code généré.

## Démonstration : un « template JIT » en 150 lignes

[code/02_asm/jit.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/02_asm/jit.cpp) prend **le même bytecode** que `asm_vm_run` et :

1. l'**interprète** avec un `switch` en C++ ;
2. le **compile** en x86-64 en émettant les octets à la main : chaque instruction VM devient un « template » de 1 à 3 instructions machine, et les sauts sont résolus par **backpatching** ;
3. copie les octets dans une page `VirtualAlloc`, la passe en exécutable (`VirtualProtect`, jamais RWX : politique **W^X**) et l'appelle comme une fonction.

```cpp
void mov_rax_mem(int reg)  { u8(0x48); u8(0x8B); modrm_rdx_disp32(0, reg * 8); }   // mov rax, [rdx + reg*8]
void add_rax_mem(int reg)  { u8(0x48); u8(0x03); modrm_rdx_disp32(0, reg * 8); }   // add rax, [..]
void mov_mem_rax(int reg)  { u8(0x48); u8(0x89); modrm_rdx_disp32(0, reg * 8); }   // mov [..], rax
size_t jne_rel32()         { u8(0x0F); u8(0x85); size_t at = pos(); i32(0); return at; }

case ADD: e.mov_rax_mem(b); e.add_rax_mem(c); e.mov_mem_rax(a); break;
```

Programme : somme de 1 à 100 000 000 (boucle de 3 instructions VM).

```
JIT : 9 instructions VM -> 130 octets de machine code
interpréteur (switch)                      287.022 ms
JIT (template)                               69.148 ms
résultats : 5000000050000000 / 5000000050000000
```

Le code produit, relu par un désassembleur :

```asm
  42:  48 8b 82 00 00 00 00   mov    rax,QWORD PTR [rdx+0x0]
  49:  48 03 82 08 00 00 00   add    rax,QWORD PTR [rdx+0x8]
  50:  48 89 82 00 00 00 00   mov    QWORD PTR [rdx+0x0],rax
  57:  48 8b 82 08 00 00 00   mov    rax,QWORD PTR [rdx+0x8]
  5e:  48 03 82 10 00 00 00   add    rax,QWORD PTR [rdx+0x10]
  65:  48 89 82 08 00 00 00   mov    QWORD PTR [rdx+0x8],rax
  6c:  48 83 ba 08 00 00 00   cmp    QWORD PTR [rdx+0x8],0x0
  73:  00
  74:  0f 85 c8 ff ff ff      jne    0x42
```

On voit tout de suite l'optimisation suivante : les registres VM restent en mémoire (`[rdx+...]`) ; les garder dans de vrais registres CPU (`r0` → `rax`, `r1` → `rcx`) serait de l'**allocation de registres**, le cœur d'un vrai JIT. C'est un excellent bonus pour un groupe avancé — référence : LuaJIT (Mike Pall), et le *copy-and-patch* de CPython 3.13.

---

# 2.8 Les autres architectures

## ARM64 (AArch64)

Smartphones, Nintendo Switch (1 et 2), Apple Silicon, Windows on ARM (Snapdragon X), serveurs (AWS Graviton).

| | x86-64 | ARM64 |
|---|---|---|
| Type | CISC | RISC, *load/store* |
| Taille d'instruction | 1 à 15 octets | **4 octets fixes** |
| Registres généraux | 16 (`rax`...) | **31** (`x0`-`x30`, `w0`-`w30` en 32 bits) + `sp` + `xzr` (zéro) |
| Retour de fonction | adresse sur la pile | registre **`x30` (LR, link register)** |
| Arguments (**AAPCS64**, *Arm Architecture Procedure Call Standard*) | `rcx rdx r8 r9` (Windows) | `x0`-`x7`, flottants `v0`-`v7` |
| Instructions à 3 opérandes | non (`add rax, rbx`) | oui (`add x0, x1, x2`) |
| SIMD | SSE / AVX / AVX-512 | **NEON** (128 bits, `v0`-`v31`), **SVE/SVE2** (largeur variable) |
| Flags | modifiés par presque tout | seulement par les instructions en `s` (`adds`, `subs`, `cmp`) |

```asm
// int64_t add(int64_t a, int64_t b)  -- ARM64
add:
    add     x0, x0, x1      // x0 = x0 + x1 (destination, source1, source2)
    ret                     // saute à x30
```

Pour Windows on ARM, il existe **ARM64EC**, une ABI qui permet de mêler code ARM64 natif et code x64 émulé dans le même processus.

## RISC-V

Jeu d'instructions **ouvert et libre** (UC Berkeley, 2010). Modulaire : `RV64I` de base, plus des extensions (`M` multiplication, `F`/`D` flottants, `V` vecteurs). Très utilisé dans l'enseignement et l'embarqué (ESP32-C3). Pour un projet de VM, c'est une **excellente cible à émuler**, car le jeu d'instructions de base est petit et régulier.

## Architectures « de VM » historiques (pour l'inspiration projet)

- **CHIP-8** (Joseph Weisbecker, 1977) : VM pour jeux vidéo sur le COSMAC VIP. 35 opcodes, 16 registres `V0`-`VF`. *L'émulateur CHIP-8 est le « Hello World » de l'émulation.*
- **6502** : simple, documenté à l'extrême (NES, C64, Apple II).
- **Fantasy consoles** : PICO-8 (VM Lua), TIC-80, [Uxn](https://100r.co/site/uxn.html) (VM à pile de 100 Rabbits).

---

# 2.9 Pièges et questions fréquentes

- **« Mon .asm n'est pas compilé »** : Build Customizations masm non coché, ou fichier ajouté *avant* de cocher (vérifiez *Item Type*).
- **« unresolved external symbol asm_add »** : `extern "C"` oublié côté C++, ou nom différent (casse : ajoutez `option casemap:none`).
- **« Ça marche en Debug mais crashe en Release »** : registre non-volatile écrasé (`rbx`, `rsi`, `rdi`, `r12`-`r15`, `xmm6`+). En Debug, le compilateur n'y garde rien d'important, alors qu'en Release il y met des variables.
- **« Crash dans printf appelé depuis mon ASM »** : pile mal alignée ou shadow space manquant.
- **« Pourquoi `mov eax, ...` et pas `mov rax, ...` dans le code MSVC ? »** : l'encodage est plus court (pas de REX), et les 32 bits hauts sont mis à zéro automatiquement.
- **« `ret 0` ? »** : notation MASM historique, identique à `ret`.
- **« Mon ASM est plus lent que le C++ »** : c'est normal. Le compilateur connaît mieux le CPU que vous. L'ASM à la main se justifie pour des instructions que le compilateur n'utilise pas, pour des conventions spéciales (JIT, coroutines, dispatch d'interpréteur) ou pour apprendre.

---

# 2.10 Références

**Documentation officielle**

- Microsoft, [x64 software conventions](https://learn.microsoft.com/en-us/cpp/build/x64-software-conventions) et [x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- Microsoft, [MASM for x64 (ml64.exe)](https://learn.microsoft.com/en-us/cpp/assembler/masm/masm-for-x64-ml64-exe) et [directives de prologue/épilogue](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64)
- Intel, *Intel 64 and IA-32 Architectures Software Developer's Manual* (SDM), vol. 2 : référence de toutes les instructions et de leur encodage
- ARM, *Procedure Call Standard for the Arm 64-bit Architecture* (AAPCS64)

**Livres et cours**

- Bryant & O'Hallaron, *Computer Systems: A Programmer's Perspective*, chap. 3 *Machine-Level Representation of Programs*. Le meilleur point de départ (syntaxe AT&T et Linux, mais les concepts sont identiques).
- Daniel Kusswurm, *Modern X86 Assembly Language Programming*, 3e éd. (Apress, 2023) : **MASM + Visual Studio + AVX2/AVX-512**. C'est le livre le plus proche de ce cours.
- Michael Abrash, *Graphics Programming Black Book* : [gratuit](https://www.jagregory.com/abrash-black-book/)
- Agner Fog, [*Optimizing subroutines in assembly language*](https://www.agner.org/optimize/optimizing_assembly.pdf) et [*Calling conventions*](https://www.agner.org/optimize/calling_conventions.pdf)
- Randall Hyde, *The Art of 64-Bit Assembly* (No Starch, 2021) : MASM x64

**Outils en ligne**

- [Compiler Explorer](https://godbolt.org)
- [defuse.ca online x86 assembler](https://defuse.ca/online-x86-assembler.htm) : texte ↔ octets
- [x86 instruction reference (felixcloutier.com)](https://www.felixcloutier.com/x86/) : le SDM en HTML
- [Zydis](https://github.com/zyantific/zydis) (désassembleur), [asmjit](https://github.com/asmjit/asmjit) et [xbyak](https://github.com/herumi/xbyak) (JIT)

**Conférences**

- Matt Godbolt, *What Has My Compiler Done for Me Lately? Unbolting the Compiler's Lid*, CppCon 2017
- Chandler Carruth, *Going Nowhere Faster*, CppCon 2017 (benchmarks et assembleur)

**Jeux**

- *Human Resource Machine*, *TIS-100*, *Shenzhen I/O*, *EXAPUNKS*
