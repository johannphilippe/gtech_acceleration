---
title: 3 - SIMD (SSE, AVX2, AVX-512)
author: Johann Philippe
---

# 3. SIMD : Single Instruction, Multiple Data

> Après la mémoire et l'assembleur, la troisième brique matérielle du cours : faire calculer le CPU sur plusieurs valeurs **en une seule instruction**. On commence toujours par regarder ce que le compilateur sait faire **seul** (l'auto-vectorisation) avant d'écrire quoi que ce soit à la main — sinon vous risquez d'écrire du SIMD plus lent que ce que MSVC aurait produit tout seul.
> Code de démonstration : [code/03_simd](https://github.com/johannphilippe/hardware_acceleration/tree/main/code/03_simd). Des exercices accompagnent ce chapitre (distribués séparément en cours). Les benchmarks ont été mesurés sur un Ryzen 9 8940HX (Zen 4, AVX-512), avec g++ 13 en `-O2`. Tous les fichiers `.cpp` **compilent aussi avec MSVC** (`/O2 /std:c++20 /W4`, vérifié). Les rapports de vectorisation et les extraits d'assembleur viennent du vrai MSVC.
> Ordre suivi : **auto-vectorisation d'abord** (ce que le compilateur fait seul, et quand il échoue), puis **SSE 128 bits** en profondeur, puis **AVX2** et **AVX-512** pour les subtilités.

## Ce que vous devez savoir faire à la fin

1. Comprendre le principe du SIMD et son histoire, du Cray aux registres `zmm`.
2. Savoir **vérifier** et **provoquer** l'auto-vectorisation sous MSVC (`/Qvec-report:2`), et connaître les cas où elle **échoue** ou **n'apporte rien**.
3. Écrire du SIMD à la main avec les **intrinsics SSE** : chargement et alignement, arithmétique, masques, *branchless*, shuffles, gestion du *tail*.
4. Connaître les spécificités d'**AVX2** (VEX, lanes, FMA, gather, `vzeroupper`) et d'**AVX-512** (masques `k`, tail masqué, throttling, fragmentation).
5. Faire du **CPU dispatch** (détecter à l'exécution ce que le CPU sait faire).
6. Relier tout ça au projet : types `vec4` natifs dans la VM, exécution en *batch*, lexer.

---

# 3.1 Préambule historique

## La taxonomie de Flynn (1966)

Michael J. Flynn classe les architectures selon le nombre de flux d'instructions et de données (*Very High-Speed Computing Systems*, Proceedings of the IEEE, 1966) :

|                         | Une donnée | Plusieurs données |
|-------------------------|------------|--------------------|
| **Une instruction**     | SISD : le CPU scalaire classique | **SIMD** : vectoriel (SSE, GPU) |
| **Plusieurs instructions** | MISD : rare (tolérance aux pannes) | MIMD : multi-cœur, clusters |

```
SISD : a0+b0 -> r0      (4 instructions, 4 cycles)
       a1+b1 -> r1
       a2+b2 -> r2
       a3+b3 -> r3

SIMD : [a0 a1 a2 a3]
     + [b0 b1 b2 b3]      (1 instruction : addps)
     = [r0 r1 r2 r3]
```

## Les supercalculateurs vectoriels

- **ILLIAC IV** (Université de l'Illinois, années 1970) : l'un des premiers « array processors » SIMD.
- **Cray-1** (1976) : le calculateur vectoriel emblématique, avec des registres vectoriels et un pipeline dédié. Son concepteur, Seymour Cray, doutait pourtant de l'intérêt du calcul massivement parallèle sur de petites unités :

> « If you were plowing a field, which would you rather use: two strong oxen or 1024 chickens? »
> — Seymour Cray

La formule sous-entend clairement sa réponse : deux bœufs puissants valent mieux que mille poulets. L'histoire lui a donné à la fois raison et tort. Un cœur CPU moderne avec ses registres `zmm` de 512 bits, c'est un bœuf plus costaud à chaque génération — exactement ce que Cray défendait. Mais les GPU, avec des milliers de cœurs simples exécutant la même instruction en parallèle sur des données différentes, sont littéralement les 1024 poulets — et ils dominent aujourd'hui le calcul massivement parallèle (rendu, deep learning). Les deux modèles cohabitent, chacun sur son terrain : le SIMD sur CPU que voit ce chapitre est la version « bœuf » du parallélisme de données.

## SWAR : le parallélisme « fait maison » avant le vrai SIMD

Pendant que Cray construisait de vrais registres vectoriels, les CPU grand public n'avaient que des registres généraux (partie 2.2) — et on trouvait déjà le moyen d'y empaqueter un peu de parallélisme. La technique porte un nom, **SWAR** (*SIMD Within A Register*) : empaqueter plusieurs petites valeurs dans un seul registre 64 bits, puis les manipuler d'un coup avec une instruction ordinaire. Ça fonctionne « gratuitement » pour les opérations **logiques** (`and`, `or`, `xor`) — aucune retenue à gérer, chaque bit est indépendant. Ça casse pour l'arithmétique (`add`, `sub`) : une addition classique propage sa retenue **bit à bit sur tout le registre**, donc si un octet déborde, il contamine l'octet voisin. C'est très exactement ce que le vrai SIMD résout : dans un `xmm`, chaque lane est isolée **au niveau du silicium**, `paddb` ne laisse jamais une retenue franchir une frontière d'octet.

L'exemple canonique, tiré de *Hacker's Delight* (Henry Warren) et utilisé historiquement dans des implémentations de `strlen`/`memchr` (glibc, avant que SSE2 devienne une base universelle sous x86-64) : détecter si un mot de 64 bits contient un octet nul, en masquant soigneusement les bits de poids fort de chaque lane pour empêcher la retenue de « fuir » vers la lane voisine :

```c
#define haszero(v) (((v) - 0x0101010101010101ULL) & ~(v) & 0x8080808080808080ULL)
```

Aujourd'hui, le SWAR reste un outil de niche — une fonction minuscule où monter un état SSE/AVX coûterait plus cher que le gain (voir 3.2, les petites boucles), ou du code historique/embarqué sans SIMD disponible. Mais c'est un excellent rappel de *pourquoi* le matériel vectoriel a été inventé : même idée de traiter plusieurs valeurs à la fois, mais avec des lanes réellement isolées par le matériel, pas simulées à coups de masques.

## Le SIMD dans nos PC

| Année | Extension | Registres | Apport principal |
|-------|-----------|-----------|--------------------|
| 1997 | **MMX** (Pentium MMX) | 8 × `mm` 64 bits | entiers seulement ; partage l'état x87 (il faut appeler `EMMS`) |
| 1998 | **3DNow!** (AMD K6-2) | `mm` | flottants, pensé pour la 3D des jeux |
| 1999 | **SSE** (Pentium III) | 8 × `xmm` 128 bits | 4 `float` simple précision |
| 2000 | **SSE2** (Pentium 4) | `xmm` | `double` et entiers 128 bits. **Base obligatoire de x86-64** |
| 2004-06 | SSE3, SSSE3 | | `hadd`, `pshufb` (shuffle d'octets), `abs` |
| 2007-08 | **SSE4.1 / SSE4.2** (Penryn, Nehalem) | | `blendv`, `dp_ps`, `pmulld`, `round` ; `popcnt`, CRC32, comparaison de chaînes |
| 2011 | **AVX** (Sandy Bridge, Bulldozer) | 16 × `ymm` 256 bits | 8 `float`, encodage **VEX**, instructions à 3 opérandes |
| 2013 | **AVX2** + **FMA** (Haswell ; AMD Zen en 2017) | `ymm` | entiers 256 bits, gather, `a*b+c` fusionné |
| 2016-17 | **AVX-512** (Xeon Phi, Skylake-SP) | 32 × `zmm` 512 bits + 8 masques `k` | masques, scatter, compress ; **famille fragmentée** |
| 2022 | AVX-512 sur AMD Zen 4 | | arrivée « grand public » côté AMD |
| 2023-25 | **AVX10** (spec Intel) | | réunification d'AVX-512 ; depuis la révision de mars 2025, le 512 bits est obligatoire |

Chez ARM : **NEON** (ARMv7, Cortex-A8, 2005 ; obligatoire en ARMv8) sur 128 bits, puis **SVE/SVE2** (*Scalable Vector Extension*), dont la largeur de vecteur n'est connue qu'à l'exécution. Chez RISC-V : l'extension **V** (ratifiée en 2021), elle aussi à longueur variable.

## Et dans les consoles ?

- **PS2** : l'*Emotion Engine* possède deux coprocesseurs vectoriels (VU0, VU1) pour la transformation des vertex.
- **Xbox 360** : PowerPC avec **VMX128**. **PS3** : le **Cell**, avec 8 cœurs SPU très orientés SIMD.
- **PS4 / Xbox One** : AMD Jaguar (SSE4.2 et AVX, pas d'AVX2). **PS5 / Xbox Series** : AMD Zen 2 (**AVX2**).
- **Switch / Switch 2** : ARM Cortex-A57 / A78C (**NEON**).

Une bibliothèque SIMD de moteur doit donc souvent viser SSE, AVX2 et NEON à la fois : c'est ce que fait DirectXMath.

## Ce que peut viser un jeu PC en 2026

[Steam Hardware Survey, août 2026](https://store.steampowered.com/hwsurvey) (section *Other Settings*) :

| Jeu d'instructions | % des machines Steam |
|----------------------|-------------------------|
| SSE2 / SSE3 / SSSE3 | ~98,1 % |
| SSE4.1 / SSE4.2 | ~98,0 % |
| AVX | ~97,2 % |
| **AVX2** / FMA | **~95,4 %** / 95,6 % |
| **AVX-512F** | **~23,9 %** |

**Conclusion pratique** : SSE4.2 est une base sûre. AVX2 est quasiment universel, mais il faut un chemin de secours (*fallback*) ou un message d'erreur clair. AVX-512 ne s'utilise qu'avec du **dispatch** à l'exécution.

Ce déséquilibre de support matériel a nourri un vrai débat dans l'industrie. Linus Torvalds, créateur de Linux, l'a résumé sans détour en juillet 2020, à propos de l'usage d'AVX-512 dans le noyau :

> « I hope AVX512 dies a painful death, and that Intel starts fixing real problems instead of trying to create magic instructions to then create benchmarks that they can look good on. I'm 100% convinced that AVX512 is not a good thing to use, but is instead something that should be actively avoided. »
> — Linus Torvalds, message sur la mailing list linux-kernel, juillet 2020

Ce n'est pas une opinion isolée ni un simple coup de gueule : à l'époque, activer AVX-512 sur certains CPU Intel (Skylake-SP) faisait **baisser la fréquence** de tout le cœur pendant plusieurs millisecondes (*AVX-512 license levels*), ralentissant le code voisin qui n'avait rien demandé — voir 3.6. Ce problème de throttling s'est beaucoup atténué depuis, mais la leçon reste valable : une instruction plus large n'est **jamais automatiquement** une bonne idée, il faut mesurer sur le CPU réellement ciblé, pas supposer. C'est très exactement le genre de débat que vous devez savoir avoir avec vous-même avant de choisir une cible matérielle pour un jeu PC.

---

# 3.2 Commencer par l'auto-vectorisation

Avant d'écrire un seul intrinsic, vous devez savoir ce que le compilateur fait **tout seul**. Sinon, vous risquez d'écrire du SIMD à la main plus lent que ce que MSVC aurait produit.

## Activer et lire le rapport sous MSVC

- L'auto-vectorisation est **active par défaut en `/O2`**. Elle est désactivée en `/Od` (Debug), `/O1` et `/Os`.
- Rapport : `/Qvec-report:1` (boucles vectorisées) ou **`/Qvec-report:2`** (toutes les boucles, avec un **reason code**).
- Dans VS : *Project Properties → C/C++ → Command Line → Additional Options* : `/Qvec-report:2`. Les messages apparaissent dans la fenêtre *Output*.
- Documentation des codes : [Vectorizer and parallelizer messages](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/vectorizer-and-parallelizer-messages).

Messages :

- `info C5001: loop vectorized`
- `info C5002: loop not vectorized due to reason 'XXXX'`
- `info C5003: block vectorized` (SLP : instructions consécutives regroupées, sans boucle)

## Le laboratoire : [code/03_simd/autovec.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/autovec.cpp)

15 boucles, compilées avec MSVC `v19.latest` :

| # | Boucle | `/O2` | `/O2 /fp:fast` | Raison MSVC |
|---|--------|-------|-----------------|--------------|
| 01 | `y[i] += a*x[i]` avec `__restrict` | ✅ | ✅ | |
| 02 | idem **sans** `__restrict` (aliasing possible) | ✅ | ✅ | vectorisée **avec un test de chevauchement à l'exécution** |
| 03 | somme de `float` | ❌ **1105** | ✅ | *unrecognized reduction* : réassociation interdite sans `/fp:fast` |
| 04 | somme d'`int32_t` | ✅ | ✅ | l'addition entière est associative |
| 05 | `a[i] = a[i-1] + a[i]` | ❌ **1200** | ❌ | *loop-carried data dependency* |
| 06 | recherche avec `return` anticipé | ❌ **506** | ❌ | forme de boucle non canonique (sortie anticipée) |
| 07 | appel d'une fonction opaque | ❌ **1200** | ❌ | l'appel peut modifier le tableau |
| 08 | `out[i] = in[i] > 0 ? in[i] : 0` | ✅ | ✅ | devient `maxps` (branchless) |
| 09 | `out[i] = table[idx[i]] * 2` (gather) | ✅ | ✅ | gather émulé |
| 10 | `std::sqrt` | ✅ | ✅ | `sqrtps` |
| 11 | AoS `p[i].x += p[i].vx * dt` | ✅ | ✅ | MSVC s'en sort sur ce cas simple |
| 12 | SoA | ✅ | ✅ | |
| 13 | `i += 3` | ❌ **1301** | ❌ | *stride isn't +1* |
| 14 | 3 itérations fixes | ❌ **1303** + ✅ C5003 | | trop peu d'itérations, mais *block vectorized* (SLP) |
| 15 | multiplication `int64` | ✅ | ✅ | émulée (pas d'instruction dédiée avant AVX-512DQ) |

**Autres codes à connaître** (doc Microsoft) :

- **1100** : flot de contrôle (`if`) non vectorisable ;
- **1104** : variable scalaire utilisée après la boucle ;
- **1203** : accès mémoire non contigus ;
- **1300** : trop peu de calcul, le compilateur appelle `memcpy` à la place (typiquement `A[i] = B[i]`) ;
- **1304** : affectations de tailles différentes dans la même boucle (`int` et `short`) ;
- **500-502** : variable d'induction non reconnue (globale, borne qui change, pas multiple) ;
- **1400-1405** : option ou pragma qui désactive la vectorisation (`#pragma loop(no_vector)`, `/O1`).

## Pourquoi ça échoue vraiment : quatre cas expliqués

Un code de raison MSVC dit *que* la boucle échoue, pas toujours *pourquoi* en profondeur. Quatre cas reviennent sans cesse et méritent d'être compris au niveau du matériel, pas juste mémorisés comme des règles :

**Cas 03 — réduction flottante non reconnue (1105)**. `sum += x[i]` en boucle semble trivial à paralléliser, mais vectoriser une somme, c'est **changer l'ordre des additions** : au lieu de `((a+b)+c)+d`, une version à 4 voies calcule `(a+c)+(b+d)`. En entiers, l'addition est associative, l'ordre ne change jamais le résultat (cas 04, vectorisé sans problème). En flottant IEEE 754, l'addition **n'est pas associative** : chaque addition arrondit son résultat, et arrondir dans un ordre différent donne un résultat différent au bit près (démonstration chiffrée plus haut, « Pourquoi `/fp:fast` change tout »). Le compilateur, en `/fp:precise` (le défaut), n'a **pas le droit** de changer ce résultat sans autorisation explicite — d'où le refus, corrigé uniquement par `/fp:fast`/`/fp:contract`, ou en acceptant consciemment le changement d'ordre avec des accumulateurs écrits à la main (voir les 4 accumulateurs du benchmark en 3.5).

**Cas 05 — dépendance portée par la boucle (1200)**. `a[i] = a[i-1] + a[i]` (une *prefix sum*) : pour calculer `a[i]`, il **faut** déjà connaître `a[i-1]`, qui vient d'être calculé à l'itération précédente. Contrairement à la réduction (où l'ordre peut changer), ici c'est la **donnée elle-même** qui dépend séquentiellement d'elle-même : il n'existe littéralement pas de version qui calculerait 4 `a[i]` en même temps sans déjà connaître les 3 précédents. Ce n'est pas une limite du compilateur, c'est une limite de l'algorithme — une prefix sum a un algorithme parallèle dédié (*scan* parallèle, hors programme), mais ce n'est pas une simple vectorisation de la boucle naïve.

**Cas 06 — sortie anticipée (506)**. Une boucle `for (i=0; i<n; ++i) if (data[i]==target) return i;` s'arrête **dès qu'elle trouve** ce qu'elle cherche — mais une version SIMD calcule 4 (ou 8, ou 16) comparaisons *en même temps*, avant de savoir laquelle aurait dû arrêter la boucle. Le compilateur refuse par prudence : si `data[3]` correspond à la cible mais que la boucle scalaire se serait arrêtée à `data[1]` (à cause d'un effet de bord ailleurs), les deux versions divergeraient. La solution manuelle contourne ce refus : comparer un bloc entier avec `cmpeq` + `movemask`, PUIS regarder si le masque est non nul, et SEULEMENT alors chercher l'index exact avec `countr_zero` — c'est-à-dire assumer soi-même la responsabilité que le compilateur refuse de prendre à votre place.

**Cas 07 — appel de fonction opaque (1200)**. Un appel à une fonction dont le compilateur ne peut pas voir le corps (déclarée dans un autre `.cpp`, un pointeur de fonction, une fonction virtuelle) est traité comme une **boîte noire qui pourrait faire n'importe quoi** — y compris modifier le tableau qu'on est en train de parcourir, lancer une exception, ou avoir des effets de bord dont l'ordre compte. Le compilateur doit alors supposer le pire et garder l'ordre séquentiel exact. La correction la plus fréquente (rendre la fonction `inline`, ou visible dans le même fichier/header) fonctionne précisément parce qu'elle donne au compilateur de quoi **prouver** que la fonction n'a pas ces effets de bord dangereux — ce n'est pas un contournement arbitraire, c'est lui fournir l'information qui lui manquait.

## Ce que montre le code généré

**Cas 02, aliasing** : sans `__restrict`, MSVC vérifie à l'exécution que `y` et `x` ne se chevauchent pas, puis choisit la version vectorisée ou la version scalaire.

```asm
        cmp     r9, 16
        jb      $LN27                           ; moins de 16 éléments : scalaire
        lea     rax, QWORD PTR [r9-1]
        lea     rax, QWORD PTR [rdx+rax*4]      ; fin de x
        cmp     rcx, rax
        ja      SHORT $LN11                     ; y commence après la fin de x : OK
        lea     rax, QWORD PTR [r9-1]
        lea     rax, QWORD PTR [rcx+rax*4]      ; fin de y
        cmp     rax, rdx
        jae     $LN27                           ; chevauchement : version scalaire
$LN11:
        movups  xmm0, XMMWORD PTR [rcx+r8*4]    ; version vectorisée (16 éléments / itération)
        movups  xmm1, XMMWORD PTR [rdx+r8*4]
        ...
```

**Cas 08, ReLU** : le ternaire disparait.

```asm
$LL4@v08_relu:
        movups  xmm0, XMMWORD PTR [rdx+r9*4]
        movups  xmm1, XMMWORD PTR [rdx+r9*4+16]
        maxps   xmm0, xmm2                      ; max(x, 0)
        maxps   xmm1, xmm2
```

**`/arch:AVX2`** : la même boucle saxpy passe sur `ymm` (8 floats), avec des instructions VEX à 3 opérandes, **et les accès mémoire sont fusionnés dans les opérations** (VEX ne demande pas que la mémoire soit alignée, contrairement à `mulps xmm, [mem]` en SSE classique) :

```asm
$LL4@saxpy:
        vmulps  ymm0, ymm3, YMMWORD PTR [rdx+r8*4]
        vaddps  ymm1, ymm0, YMMWORD PTR [rcx+r8*4]
        vmovups YMMWORD PTR [rcx+r8*4], ymm1
```

Remarque : MSVC n'utilise **pas** FMA ici (`vfmadd`), car depuis VS 2022, `/fp:precise` n'autorise plus la *contraction* `a*b+c`. Il faut `/fp:contract` ou `/fp:fast`.

## Pourquoi `/fp:fast` change tout pour les flottants

Vectoriser une somme revient à changer l'ordre des additions : `((a+b)+c)+d` devient `(a+c)+(b+d)`. En flottant, **ce n'est pas le même résultat au bit près**. Par défaut (`/fp:precise`), MSVC refuse. Dans notre benchmark :

```
résultats : -465.3771 -465.3771 -465.3754 -465.3731 -465.3741  (différents : ordre des additions !)
```

C'est un point sensible pour le jeu vidéo, à cause du **déterminisme flottant**. Un jeu en *lockstep* (RTS, jeux de combat avec rollback netcode) ou avec des *replays* exige que deux machines calculent **exactement** la même chose. `/fp:fast`, FMA, ou un chemin AVX2 sur une machine et SSE sur l'autre peuvent **désynchroniser** la partie. Voir le GDC talk *8 Frames in 16ms: Rollback Networking in Mortal Kombat and Injustice 2* (Michael Stallone, 2018), et les articles de Glenn Fiedler sur le *floating point determinism*.

## Les cas où l'auto-vectorisation n'est pas efficace

Même quand le compilateur dit « vectorized », ce n'est pas forcément plus rapide :

1. **Petites boucles** (< 8-16 itérations) : les tests d'entrée (alignement, aliasing, taille) coûtent plus que le gain.
2. **Aliasing** : le code en double et les tests à l'exécution grossissent le code (pression sur le cache d'instructions) sans garantie de gain.
3. **Branches complexes** : le compilateur calcule *les deux branches* et mélange avec un masque. Si une branche est rare et coûteuse, c'est **plus lent** que le scalaire.
4. **Gather / scatter** : accès mémoire indirects, souvent **plus lents que le scalaire**. Sur Intel, la mitigation *Gather Data Sampling* (« Downfall », 2023) a encore dégradé `vpgather`.
5. **Accès mémoire limitant** : si le goulot est la RAM (tableaux > L2/L3), multiplier la largeur SIMD ne sert à rien. Voir le benchmark *saxpy* ci-dessous : SSE ≈ AVX2.
6. **Données AoS non alignées** : beaucoup de `movups`, de chargements et de shuffles. SoA d'abord.
7. **Code non inlinable** : un appel de fonction dans une boucle bloque tout. `__forceinline` ou `[[msvc::forceinline]]` peuvent aider.
8. **Réductions flottantes sous `/fp:precise`** : pas de vectorisation (code 1105).

## Aider le compilateur

- **SoA** et tableaux contigus (`std::vector`, `std::span`).
- `__restrict` sur les pointeurs (MSVC ; `__restrict__` pour GCC/Clang).
- Variables locales plutôt que membres ou globales dans les boucles chaudes. Par exemple, `size_t n = v.size();` avant la boucle.
- Boucle `for` canonique : `i` local, `++i`, borne invariante.
- Remplacer `if` par `std::min`, `std::max` ou un ternaire simple.
- `#pragma loop(ivdep)` (MSVC) : « fais-moi confiance, il n'y a pas de dépendance ». **À utiliser avec prudence.**
- `/fp:fast` ou `/fp:contract` par fichier, **seulement** si le déterminisme n'est pas requis.
- `/arch:AVX2` (voir 3.5) sur les fichiers ou modules concernés.

---

# 3.3 SSE en profondeur (128 bits)

## Headers et types

```cpp
#include <immintrin.h>     // inclut tout (SSE -> AVX-512). Sous MSVC, <intrin.h> inclut aussi le reste.
```

| Type | Contenu | Registre |
|------|---------|-----------|
| `__m128` | 4 × `float` | `xmm` |
| `__m128d` | 2 × `double` | `xmm` |
| `__m128i` | 16 × `int8`, 8 × `int16`, 4 × `int32` ou 2 × `int64` (c'est la fonction qui décide) | `xmm` |
| `__m256`, `__m256d`, `__m256i` | ×2 | `ymm` |
| `__m512`, `__m512d`, `__m512i` | ×4 | `zmm` |
| `__mmask8/16/32/64` | masques de bits AVX-512 | `k` |

## Convention de nommage

```
_mm_add_ps          _mm256_cmp_ps_mask
 |   |   |            |     |   |  |
 |   |   |            |     |   |  +-- retourne un masque de bits (AVX-512)
 |   |   +-- suffixe  |     |   +-- type de donnée
 |   +-- opération    |     +-- opération
 +-- _mm (128)        +-- _mm256 (256) / _mm512 (512)
```

| Suffixe | Signification |
|---------|----------------|
| `ps` / `pd` | *packed single* / *packed double* : sur tous les éléments |
| `ss` / `sd` | *scalar single* / *double* : uniquement l'élément 0 |
| `epi8/16/32/64` | entiers signés (*extended packed integer*) |
| `epu8/16/32/64` | entiers non signés |
| `si128`, `si256` | registre entier « brut » (load, store, and, or...) |

**LA référence** : l'[Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html). Il permet de filtrer par jeu d'instructions et donne, pour chaque intrinsic, l'instruction ASM correspondante et une *latence* indicative.

## Table de référence : les intrinsics SSE les plus utilisés

Pas besoin de mémoriser l'Intel Intrinsics Guide en entier : voici les intrinsics `__m128`/`__m128i` qui reviennent dans **presque tout** le code SIMD d'un moteur. Chaque ligne donne l'équivalent `ps` (float) ; le principe est identique pour `pd` (double) et `epi32` (entiers), en changeant le suffixe.

**Chargement et stockage**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_load_ps(ptr)` | `movaps` | charge 4 `float` ; **l'adresse doit être alignée sur 16**, sinon crash |
| `_mm_loadu_ps(ptr)` | `movups` | charge 4 `float`, adresse quelconque |
| `_mm_load_ss(ptr)` | `movss` | charge 1 `float` dans l'élément 0, met les 3 autres à 0 |
| `_mm_load_si128(ptr)` | `movdqa` | charge 128 bits entiers, alignés |
| `_mm_loadu_si128(ptr)` | `movdqu` | charge 128 bits entiers, non alignés |
| `_mm_store_ps(ptr, v)` | `movaps` | stocke 4 `float`, alignés |
| `_mm_storeu_ps(ptr, v)` | `movups` | stocke 4 `float`, non alignés |
| `_mm_stream_ps(ptr, v)` | `movntps` | stocke **sans polluer le cache** (*non-temporal store* : pour de gros volumes écrits une seule fois, jamais relus tout de suite) |

**Construire des constantes**

| Intrinsic | Instruction MASM (typique) | Ce que ça fait |
|-----------|-------------------------------|-----------------|
| `_mm_set_ps(e3,e2,e1,e0)` | plusieurs `mov`/`unpck` | remplit le registre — **ordre inversé**, voir « Piège n°1 » ci-dessous |
| `_mm_setr_ps(e0,e1,e2,e3)` | idem | même chose, **ordre naturel** (*set-reversed*) |
| `_mm_set1_ps(x)` | `movss` + `shufps` (ou `vbroadcastss` en AVX) | diffuse (*broadcast*) une seule valeur dans les 4 éléments |
| `_mm_setzero_ps()` | `xorps xmm, xmm` | registre à zéro — la façon la **plus rapide** de « vider » un registre (aucune dépendance sur une valeur précédente) |

**Arithmétique flottante**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_add_ps` / `_mm_sub_ps` / `_mm_mul_ps` / `_mm_div_ps` | `addps` / `subps` / `mulps` / `divps` | les 4 opérations de base, sur les 4 éléments |
| `_mm_add_ss` / ... | `addss` / ... | idem, mais **seulement l'élément 0** (les 3 autres du 1er opérande passent inchangés) |
| `_mm_sqrt_ps` | `sqrtps` | racine carrée exacte (lente, ~12-15 cycles) |
| `_mm_rsqrt_ps` | `rsqrtps` | **approximation** de `1/sqrt(x)` (~12 bits de précision, très rapide) — voir l'itération de Newton dans les exercices pour la raffiner |
| `_mm_rcp_ps` | `rcpps` | approximation de `1/x` |
| `_mm_min_ps` / `_mm_max_ps` | `minps` / `maxps` | minimum/maximum élément par élément — **asymétriques avec NaN**, voir « NaN » plus bas |

**Arithmétique entière**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_add_epi32` / `_mm_sub_epi32` | `paddd` / `psubd` | addition/soustraction sur 4 `int32` |
| `_mm_mullo_epi32` (SSE4.1) | `pmulld` | multiplication 32×32→32 bits (les 32 bits bas du résultat) |
| `_mm_add_epi8` / `_mm_adds_epu8` | `paddb` / `paddusb` | addition sur 16 `int8` ; la version `adds`/`us` **sature** au lieu de déborder (utile pour un canal de couleur 0-255) |

**Comparaison**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_cmpeq_ps` / `_mm_cmplt_ps` / `_mm_cmple_ps` / `_mm_cmpgt_ps` / `_mm_cmpge_ps` | `cmpeqps` / `cmpltps` / ... | produit un **masque** (tout à 1 ou tout à 0) par élément, voir « Les masques » ci-dessous. `cmpgt`/`cmpge` inversent simplement les opérandes de `cmplt`/`cmple` en interne |
| `_mm_cmpeq_epi32` / `_mm_cmpgt_epi32` | `pcmpeqd` / `pcmpgtd` | équivalent entier — **`pcmpgt` est toujours signé**, voir le piège du test d'intervalle en octets plus bas |
| `_mm_cmpunord_ps` | `cmpunordps` | vrai là où **l'un des deux opérandes est NaN** (voir « NaN ») |

**Logique bit à bit**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_and_ps` / `_mm_or_ps` / `_mm_xor_ps` / `_mm_andnot_ps(a,b)` | `andps` / `orps` / `xorps` / `andnps` | opérations bit à bit — base de la sélection branchless (`andnot(a,b)` calcule `(~a) & b`, dans cet ordre précis) |

**Sélection / branchless**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_blendv_ps` (SSE4.1) | `blendvps` | `r[i] = mask[i] ? b[i] : a[i]` en **une seule instruction** (le bit de poids fort de chaque élément du masque décide) |
| `_mm_blend_ps` (SSE4.1) | `blendps` | comme `blendv`, mais le masque est un **immédiat** connu à la compilation (pas un registre) |

**Conversion**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_cvtps_epi32` | `cvtps2dq` | `float` → `int32`, **arrondi** selon le mode courant de `MXCSR` (par défaut : au plus proche) |
| `_mm_cvttps_epi32` | `cvttps2dq` | `float` → `int32`, **troncature** (le `tt` = *truncate*, comme un cast C++ `(int)x`) |
| `_mm_cvtepi32_ps` | `cvtdq2ps` | `int32` → `float` |
| `_mm_cvtss_f32` | (extraction, souvent gratuite) | lit l'élément 0 comme un `float` C++ ordinaire |

**Shuffle et mouvement de données**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_shuffle_ps(a,b,imm)` | `shufps` | réarrange 2 éléments de `a` (bas) + 2 de `b` (haut) selon un masque `_MM_SHUFFLE` |
| `_mm_shuffle_epi32(a,imm)` | `pshufd` | comme `shuffle_ps`, mais **un seul opérande** (les 4 éléments de `a`, réarrangés librement) |
| `_mm_shuffle_epi8(a,idx)` (SSSE3) | `pshufb` | réarrangement **arbitraire** octet par octet, la table d'indices est elle-même un `__m128i` (pas un immédiat) — la brique de base de beaucoup de parseurs SIMD |
| `_mm_unpacklo_ps` / `_mm_unpackhi_ps` | `unpcklps` / `unpckhps` | entrelace les 2 éléments bas (ou hauts) de `a` et `b` |
| `_mm_movehl_ps` / `_mm_movelh_ps` | `movhlps` / `movlhps` | déplace la moitié haute/basse d'un registre vers l'autre — utilisé dans la réduction horizontale (plus bas) |
| `_mm_movemask_ps` | `movmskps` | extrait le bit de signe de chaque élément dans un `int` (4 bits) |

**Horizontal (au-delà de SSE2)**

| Intrinsic | Instruction MASM | Ce que ça fait |
|-----------|-------------------|-----------------|
| `_mm_hadd_ps` (SSE3) | `haddps` | additionne les éléments adjacents (souvent plus lent que la réduction manuelle, voir plus bas) |
| `_mm_dp_ps` (SSE4.1) | `dpps` | produit scalaire (*dot product*) avec un masque immédiat pour choisir les éléments — pratique, rarement le plus rapide |

## Visite guidée : [code/03_simd/intrinsics_tour.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/intrinsics_tour.cpp)

Sortie réelle :

```
a = set(4,3,2,1)             [   1.00    2.00    3.00    4.00]
b = setr(10,-20,30,-40)      [  10.00  -20.00   30.00  -40.00]
load (aligné 16)             [   1.00    2.00    3.00    4.00]
loadu (non aligné)           [   5.00    6.00    7.00    8.00]
a + b   (addps)              [  11.00  -18.00   33.00  -36.00]
a * k   (mulps)              [   0.50    1.00    1.50    2.00]
sqrt(a) (sqrtps)             [   1.00    1.41    1.73    2.00]
min(a, b) (minps)            [   1.00  -20.00    3.00  -40.00]
a + b   (addss, scalaire)    [  11.00    2.00    3.00    4.00]
mask = b > 0                 [FFFFFFFF 00000000 FFFFFFFF 00000000]  movemask = 0b0101
select and/andnot/or         [  10.00    2.00   30.00    4.00]
select blendv (SSE4.1)       [  10.00    2.00   30.00    4.00]
shuffle(a,a, 0,1,2,3)        [   4.00    3.00    2.00    1.00]
shuffle(a,a, 3,0,2,1) (yzx)  [   2.00    3.00    1.00    4.00]
_mm_cvtss_f32(a)             1.00
epi32 add                    [101 102 103 104]
find '(' dans 16 octets      movemask = 0x1000 -> index 12
```

### Piège n°1 : l'ordre de `_mm_set_ps`

`_mm_set_ps(e3, e2, e1, e0)` prend les éléments **du plus haut au plus bas**. `_mm_setr_ps` (*reversed*) prend l'ordre naturel. **C'est un piège dans lequel tout le monde tombe au moins une fois.** Conseil : n'utilisez que `setr` ou `load`.

### Chargement : `load` vs `loadu`

- `_mm_load_ps` / `movaps` : l'adresse **doit** être alignée sur 16, sinon **crash** (access violation).
- `_mm_loadu_ps` / `movups` : n'importe quelle adresse. Depuis Nehalem (2008), **aussi rapide** que `load` quand la donnée est alignée.
- Recommandation actuelle : **`loadu` partout**, et alignez vos données quand c'est possible (le gain vient de l'alignement réel, pas de l'instruction).

**`load`/`loadu` ne « retournent » jamais l'ordre**, contrairement à `_mm_set_ps` ci-dessus : `ptr[0]` va dans l'élément 0, `ptr[1]` dans l'élément 1, etc. — l'ordre naturel, celui de la mémoire. C'est précisément pour ça que `_mm_setr_ps` existe : il donne, à partir de valeurs littérales, le **même** ordre que `_mm_loadu_ps` donnerait à partir d'un tableau. Le piège ne touche que `_mm_set_ps` (et sa famille `_mm_set_epi32`...), jamais `load`/`loadu`.

### Les masques et le *branchless*

**Pourquoi c'est le cœur du SIMD, pas un détail** : en scalaire, un `if` compile en un saut conditionnel (`jcc`), et un CPU moderne **prédit** l'issue du saut pour continuer à exécuter en avance (partie 2.6). Sur des données aléatoires, cette prédiction se trompe environ une fois sur deux, et chaque erreur coûte 15-20 cycles (vider le pipeline). En SIMD, il n'y a **pas de saut par élément possible** : une instruction `addps` traite ses 4 lanes, point final, elle ne peut pas « sauter » sur 2 d'entre elles et pas les 2 autres. La solution : calculer **les deux issues** (le `if` et le `else`) pour **tous** les éléments, puis choisir le bon résultat par élément avec un masque — sans aucun saut. C'est exactement ce que montre le benchmark « clamp » en 3.5 (`x22`, gain qui vient de la suppression des mauvaises prédictions, pas de la largeur SIMD).

Une comparaison SIMD ne produit donc pas un booléen : elle produit un **masque**, un registre où chaque élément vaut soit **tous bits à 1** (`0xFFFFFFFF` pour un `float` sur 32 bits — qui, réinterprété comme flottant, serait un NaN, mais peu importe : on ne le lit jamais comme un nombre, seulement comme un motif de bits), soit **tous bits à 0**. On s'en sert pour **sélectionner** sans branchement, avec de l'algèbre booléenne pure :

```cpp
// pour chaque i : r[i] = cond[i] ? b[i] : a[i]
__m128 mask = _mm_cmpgt_ps(b, zero);                                  // mask[i] = 0xFFFFFFFF si b[i] > 0, sinon 0
__m128 r = _mm_or_ps(_mm_and_ps(mask, b), _mm_andnot_ps(mask, a));    // SSE2
__m128 r = _mm_blendv_ps(a, b, mask);                                 // SSE4.1 : la même chose, en une instruction
```

**Pourquoi ça marche, bit à bit** : `and_ps(mask, b)` garde `b` là où `mask` vaut tout-1 (`x AND 1...1 = x`), et met à 0 là où `mask` vaut tout-0 (`x AND 0...0 = 0`). Symétriquement, `andnot_ps(mask, a)` calcule `(~mask) AND a` : ça garde `a` exactement là où `mask` valait 0 (donc là où le premier terme a mis des zéros), et vice-versa. Les deux termes ne se chevauchent jamais (un bit de `mask` est soit 1 soit 0, jamais les deux), donc le `or_ps` final les recombine sans collision : chaque élément du résultat vient entièrement de `b`, ou entièrement de `a`, jamais d'un mélange des deux. `_mm_blendv_ps` fait exactement ce calcul en microcode, en une seule instruction — préférez-le dès que SSE4.1 est disponible (2007+, quasi universel aujourd'hui).

**Exemple numérique**, avec `a = [1, 2, 3, 4]`, `b = [10, -20, 30, -40]`, `mask = (b > 0)` :

```
mask   = [0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000]
and(mask, b)    = [10, 0, 30, 0]          -- b gardé où mask=1
andnot(mask, a) = [0, 2, 0, 4]            -- a gardé où mask=0
or(...)         = [10, 2, 30, 4]          -- = "cond ? b : a", élément par élément
```

`_mm_movemask_ps` extrait le bit de signe de chaque élément dans un `int` (4 bits — c'est justement le bit qui vaut 1 dans un masque tout-1, et 0 dans un masque tout-0, d'où son utilité ici). Ça permet, une fois qu'on a réduit un masque de 128 bits à un entier de 4 bits :

- des **tests globaux** : `if (_mm_movemask_ps(mask) == 0)` (aucun élément ne vérifie la condition — utile pour un *early-out* : sauter carrément le traitement d'un bloc de 4 si rien ne s'applique) ;
- de **compter** : `std::popcount(movemask)` (combien d'éléments vérifient la condition) ;
- de **trouver l'index** : `std::countr_zero(movemask)` (C++20, `<bit>`) donne l'indice du **premier** élément qui vérifie la condition — c'est exactement la technique utilisée pour `scan_text.cpp` plus bas (trouver la position d'un caractère dans un bloc de 16 octets).

### Shuffles

```cpp
// _MM_SHUFFLE(d, c, b, a) : sortie[3] <- d, [2] <- c, [1] <- b, [0] <- a
// Les 2 éléments bas viennent du 1er argument, les 2 hauts du 2e.
__m128 yzx = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 0, 2, 1));
```

Un shuffle **réarrange** les éléments d'un ou deux registres — il ne calcule rien, il redistribue des valeurs déjà là. C'est la base du *swizzling* des langages de shader (`v.zyx` en GLSL/HLSL, exactement la même opération), et c'est ce qui rend possible le produit vectoriel (*cross product*) en SIMD — voir 3.4, où la formule et le rôle exact du shuffle `yzx` sont détaillés pas à pas.

## Gérer le « tail »

Quand `n` n'est pas multiple de la largeur SIMD `W`, il reste de 1 à `W-1` éléments après la dernière itération SIMD complète — c'est le ***tail*** (la « queue »). Exemple concret avec `n = 13` et `W = 4` (SSE, 4 `float` par registre) :

```
indices :  0  1  2  3 | 4  5  6  7 | 8  9  10 11 | 12
           \___________/\___________/\____________/  \_/
             itération 0   itération 1   itération 2   tail (1 élément)
             (4 éléments)  (4 éléments)  (4 éléments)
```

`13 / 4 = 3` reste `1` : trois itérations SIMD traitent les indices `0..11` (12 éléments), puis **il reste l'indice 12**, un seul élément — dans l'intervalle `[1, W-1] = [1, 3]` annoncé plus haut. Le code correspondant :

```cpp
size_t i = 0;
for (; i + 4 <= n; i += 4) {                  // boucle SIMD : s'arrête dès qu'il reste < 4 éléments
    __m128 v = _mm_loadu_ps(&data[i]);
    // ... traitement ...
}
for (; i < n; ++i) {                          // tail scalaire : de 0 à W-1 = 3 tours, ici 1 seul (i=12)
    // ... même traitement, élément par élément ...
}
```

La condition `i + 4 <= n` (et non `i < n`) est ce qui garantit que la boucle SIMD ne lit **jamais** au-delà du tableau : dès qu'il reste moins de 4 éléments valides, elle s'arrête et laisse la boucle scalaire finir le travail un par un. Les stratégies possibles pour ce dernier bout :

1. **Boucle scalaire** après la boucle SIMD (ci-dessus) : la plus simple, utilisée dans tous nos exemples.
2. **Padding** : allouer les tableaux à un multiple de 4/8/16 (ici, `16` au lieu de `13`) et remplir les 3 éléments en trop avec des valeurs neutres (`0` pour une somme, `+∞` pour un minimum...). Plus besoin de code de tail du tout — la boucle SIMD traite tout, y compris le padding, dont le résultat est ignoré ou sans effet. Solution préférée dans un moteur **qui contrôle ses données** (buffers de particules, de vertex...).
3. **Dernier bloc recouvrant** : au lieu d'un tail scalaire, retraiter les `W` derniers éléments avec **une itération SIMD de plus**, qui chevauche partiellement la précédente (ici, un 4e passage sur les indices `9..12` recouvre `9,10,11` déjà traités). Valable **seulement** si l'opération est idempotente (refaire `min`/`max`/`clamp` une 2e fois sur la même donnée ne change rien) — **faux** pour une somme ou un compteur, qui compteraient certains éléments deux fois.
4. **Masque AVX-512** : `_mm512_maskz_loadu_ps(mask, ptr)` ne lit (et ne peut crasher) que les éléments désignés par le masque — un seul chemin de code pour la boucle complète ET le tail, voir l'exemple chiffré du benchmark en 3.5 (« tail masqué »).

## Réductions horizontales

```cpp
// somme des 4 éléments d'un __m128
__m128 hi = _mm_movehl_ps(v, v);                                      // (v2, v3, v2, v3)
__m128 s  = _mm_add_ps(v, hi);                                        // (v0+v2, v1+v3, ...)
s = _mm_add_ss(s, _mm_shuffle_ps(s, s, _MM_SHUFFLE(1, 1, 1, 1)));
float sum = _mm_cvtss_f32(s);
```

Il existe `_mm_hadd_ps` (SSE3) et `_mm_dp_ps` (SSE4.1), mais ils sont souvent plus lents que la version ci-dessus.

## SSE sur des octets : l'exemple du lexer

[code/03_simd/scan_text.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/scan_text.cpp) : 16 octets comparés d'un coup, `movemask` pour obtenir 1 bit par octet, puis `popcount` / `countr_zero`. C'est le principe de **simdjson** (Geoff Langdale & Daniel Lemire, *Parsing Gigabytes of JSON per Second*, VLDB Journal 2019).

```cpp
__m128i chunk = _mm_loadu_si128((const __m128i*)(s + i));
unsigned bits = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, _mm_set1_epi8('\n')));
count += std::popcount(bits);
```

**Tester un intervalle** (`'a' <= c <= 'z'`) est un peu moins direct qu'il n'y paraît en SIMD sur des octets. Le test naturel serait « `c - 'a'` est un entier non signé inférieur à `26` » (26 lettres) : si `c` est bien une minuscule, `c - 'a'` tombe dans `[0, 25]` ✓ ; si `c` est **avant** `'a'` dans la table ASCII (par exemple `'0'` = 0x30), `c - 'a'` est négatif, et en non signé ça **déborde** vers une énorme valeur positive (`0x30 - 0x61 = -0x31`, soit `0xCF = 207` en octet non signé) — largement supérieure à 26, donc exclue à raison. Le test « non signé `< 26` » fonctionne exactement comme voulu.

Le problème : **SSE ne fournit qu'une comparaison signée sur les octets**, `pcmpgtb` (`_mm_cmpgt_epi8`). Il n'existe pas de `pcmpgtub` pour du non signé. Si on compare `(uint8_t)(c - 'a')` avec une instruction **signée**, une valeur comme `207` (`0xCF`) est interprétée comme `-49` (complément à deux) — **négative**, donc `< 26` serait vrai à tort. Le test signé direct est donc **faux** dans ce cas précis.

La correction : ajouter `0x80` (128) aux deux côtés de la comparaison avant de comparer en signé. Sur un octet, ajouter `0x80` a **exactement le même effet binaire** qu'inverser le bit de poids fort (`XOR 0x80`) — puisque `0x80` n'a que ce bit-là à 1, l'additionner ne peut pas produire de retenue vers les bits plus bas. Or inverser le bit de signe d'un octet est précisément la transformation qui fait correspondre l'**ordre non signé** de `[0, 255]` à l'**ordre signé** de `[-128, 127]` : `0` (le plus petit non signé) devient `-128` (le plus petit signé), `255` (le plus grand non signé) devient `127` (le plus grand signé), et l'ordre relatif de toutes les valeurs intermédiaires est préservé. Une comparaison **signée** après ce biais donne donc exactement le même résultat qu'une comparaison **non signée** sans biais — on simule une instruction qui n'existe pas avec celle qui existe :

```
(c - 'a') + 0x80 < 26 + 0x80
```

**Vérification avec deux exemples concrets** (`'a'` = 0x61) :

```
c = 'm' (0x6D, dans l'intervalle a-z) :
  c - 'a' = 0x0C (12, déjà positif)
  + 0x80  = 0x8C, interprété en signé  = -116
  26 + 0x80 = 0x9A, interprété en signé = -102
  -116 < -102 ?  OUI -> 'm' est accepté (correct : 'm' est bien entre 'a' et 'z')

c = '0' (0x30, avant 'a' dans la table ASCII) :
  c - 'a' = 0x30 - 0x61 = 0xCF (deborde, = -49 en signé, = 207 en non signé)
  + 0x80  = 0xCF + 0x80 = 0x14F, tronqué sur 8 bits = 0x4F, interprété en signé = 79
  26 + 0x80 = 0x9A, interprété en signé = -102
  79 < -102 ?  NON -> '0' est rejeté (correct : '0' n'est pas entre 'a' et 'z')
```

Cette astuce est utilisée telle quelle dans des scanners SIMD réels (voir Wojciech Muła, [0x80.pl](http://0x80.pl/), en référence 3.12) : chaque fois qu'une extension SIMD ne propose qu'une comparaison signée là où le problème est naturellement non signé (fréquent sur les octets), le biais `+ 0x80` (ou `XOR 0x80`, strictement équivalent) permet de simuler l'instruction manquante sans passer par une boucle scalaire.

```
--- compter les '\n' dans 64 Mo ---
scalaire                                     26.798 ms
SSE2 (16 octets)                              2.223 ms
AVX2 (32 octets)                              1.230 ms
std::count (STL)                             26.826 ms     (libstdc++ ; la STL MSVC vectorise std::count/std::find à la main)
--- lexer : sauter tous les identifiants ---
scalaire                                     75.962 ms
SSE2                                         44.168 ms
```

Deux leçons à retenir de ce benchmark. D'abord, sans autorisation explicite du jeu d'instructions, `std::popcount` peut redevenir un simple appel de fonction logiciel plutôt qu'un `popcnt` matériel : sur ce test, la version SSE2 compilée sans cette autorisation mettait **8,4 ms** au lieu de 2,2 ms. Toujours vérifier le code généré, jamais le supposer. Ensuite, le lexer SIMD ne gagne « que » 1,7x : les identifiants sont courts (souvent moins de 16 caractères), donc on sort vite de la boucle SIMD. **Le SIMD rapporte sur les longues séquences homogènes**, pas sur des données fragmentées.

## Denormals : un piège classique (audio, physique)

**Pourquoi un problème de nombres flottants a sa place dans un chapitre SIMD** : les denormals et les NaN (ci-dessous) ne sont pas des curiosités mathématiques isolées — ce sont des comportements **matériels** de l'unité flottante qui interagissent directement avec tout ce qui précède dans ce chapitre. Un denormal ralentit **silencieusement** une boucle vectorisée entière (tout le registre `xmm`/`ymm` paye le chemin lent dès qu'**une seule** lane contient un denormal) : c'est un problème de **débit SIMD**, pas juste de précision numérique. Un NaN, lui, casse les **masques** et le *branchless* vus plus haut (une comparaison avec NaN ne se comporte pas comme attendu, donc un `select`/`clamp` écrit avec `min`/`max` peut laisser passer un NaN sans le vouloir) : c'est un problème de **correction** du code branchless, pas de vitesse. Les deux méritent donc d'être traités ici, pas relégués à un chapitre « flottants » séparé qui n'existe pas dans ce cours.

[code/03_simd/denormals.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/denormals.cpp). Les nombres **subnormaux** (plus petits que `FLT_MIN` ≈ 1,17e-38) sont traités par des chemins lents du CPU (microcode). Ils apparaissent dès qu'un filtre, une réverbe ou un amortissement physique décroît vers zéro :

```
sans FTZ/DAZ                                  9.805 ms
avec FTZ/DAZ                                  2.713 ms        (Zen 4 : x3,6 ; historiquement x10 à x100 sur Intel)
```

```cpp
_mm_setcsr(_mm_getcsr() | 0x8040);   // FTZ (Flush To Zero, bit 15) + DAZ (Denormals Are Zero, bit 6)
// ou : _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
```

`MXCSR` est **par thread** : à régler au début de chaque thread de calcul (thread audio, jobs physiques).

## NaN : un autre piège flottant

**Quand rencontre-t-on un NaN (*Not a Number*) ?** L'IEEE 754 en produit dans un petit nombre de cas précis, tous fréquents en jeu vidéo :

- `0.0 / 0.0`, `∞ - ∞`, `∞ * 0` : formes indéterminées ;
- `std::sqrt(x)` avec `x < 0` (physique : pénétration négative avant clamp, normale mal orientée) ;
- `std::asin`/`std::acos` avec un argument hors `[-1, 1]` (erreurs d'arrondi qui font déborder légèrement) ;
- `normalize()` d'un vecteur nul (division par une longueur de 0 — voir les exercices) ;
- lecture d'une donnée non initialisée ou d'un fichier corrompu qui contient déjà un NaN.

**Pourquoi c'est dangereux ?** Un NaN est **contagieux** : `NaN + x`, `NaN * x`, `min(NaN, x)`... redonnent presque toujours NaN (les intrinsics `min`/`max` font exception, voir plus bas), donc une seule valeur invalide contamine tout un calcul en aval — une position de particule, un buffer audio entier, un vertex. Deuxième piège, **toute comparaison ordonnée avec un NaN est fausse**, y compris `NaN == NaN` :

```cpp
float x = std::numeric_limits<float>::quiet_NaN();
x == x;      // false !
x < 1.0f;    // false
x >= 1.0f;   // false aussi (pas le contraire de <)
```

Un code qui suppose `!(x < y) == (x >= y)` (vrai pour tout flottant normal) se trompe silencieusement dès qu'un NaN apparaît : des tests de collision, des tris ou des clamps peuvent laisser passer des valeurs invalides sans jamais lever d'erreur.

**Comment s'en protéger ?**

- **Détecter** : `std::isnan(x)` (scalaire) ; en SIMD, un NaN ne vérifie **aucune** comparaison *ordered* : `_mm_cmpeq_ps(x, x)` renvoie un masque à 0 exactement là où `x` est NaN (c'est l'astuce classique pour un `isnan` vectorisé).
- **Choisir le bon prédicat de comparaison** : `_CMP_GT_OQ` (*ordered, quiet* : faux si un opérande est NaN) contre `_CMP_GT_UQ` (*unordered* : vrai si un opérande est NaN). Le choix du suffixe `_OQ`/`_UQ` décide si un NaN doit « échouer » ou « passer » le test — à choisir consciemment, jamais par défaut.
- **`min`/`max` et NaN ne sont pas symétriques** : `_mm_min_ps(a, b)` renvoie `b` si `a` est NaN, mais renvoie `a` (donc le NaN) si c'est `b` qui est NaN — l'opérande qui « gagne » dépend de sa position. Un `clamp` écrit avec `min`/`max` peut donc laisser passer un NaN selon l'ordre des arguments : à tester explicitement si l'entrée n'est pas garantie valide.
- **Nettoyer aux frontières** : valider/clamper les entrées venant de l'extérieur du système (fichiers, réseau, script) plutôt que de propager un NaN et le découvrir trois systèmes plus loin.
- **En dernier recours, détecter au runtime** : positionner l'exception FPU *invalid operation* (`_MM_SET_EXCEPTION_MASK` / `_controlfp_s` sous MSVC) pendant le développement pour lever une exception matérielle au premier NaN produit, puis la désactiver en build finale (le coût et les faux positifs — bibliothèques tierces qui produisent des NaN transitoires « inoffensifs » — la rendent peu utilisable en production).

Contrairement aux denormals (un problème de **vitesse**), un NaN est un problème de **correction** silencieuse — il ne ralentit rien, il fausse le résultat sans crasher. C'est souvent plus difficile à déboguer : un rendu qui « disparaît » d'un coup (un vertex NaN casse le bounding box de tout un mesh), une physique qui explose, un panning audio qui saute à fond à droite.

---

# 3.4 Bibliothèque vec4 / mat4 en SSE

[code/03_simd/vec4.hpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/vec4.hpp), testée contre une référence scalaire dans [vec4_test.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/vec4_test.cpp).

```cpp
struct alignas(16) vec4 { __m128 m; /* ... */ };

inline vec4 VECCALL operator+(vec4 a, vec4 b) { return vec4(_mm_add_ps(a.m, b.m)); }

// cross(a, b) = (a * b.yzx - a.yzx * b).yzx   -- 3 shuffles au lieu de 4
inline vec4 VECCALL cross3(vec4 a, vec4 b)
{
    const int YZX = _MM_SHUFFLE(3, 0, 2, 1);
    __m128 a_yzx = _mm_shuffle_ps(a.m, a.m, YZX);
    __m128 b_yzx = _mm_shuffle_ps(b.m, b.m, YZX);
    __m128 c = _mm_sub_ps(_mm_mul_ps(a.m, b_yzx), _mm_mul_ps(a_yzx, b.m));
    return vec4(_mm_shuffle_ps(c, c, YZX));
}

// M * v = c0*x + c1*y + c2*z + c3*w   (matrice en 4 colonnes __m128)
```

**Pourquoi le produit vectoriel a besoin d'un shuffle** : la définition mathématique de `cross(a,b)` est

```
cross.x = a.y*b.z - a.z*b.y
cross.y = a.z*b.x - a.x*b.z
cross.z = a.x*b.y - a.y*b.x
```

Chaque composante de sortie multiplie des composantes **différentes** de l'entrée — la sortie n'est pas un simple produit lane par lane comme `add`/`mul`. Une multiplication SIMD ordinaire (`_mm_mul_ps(a, b)`) ne calcule que `a.x*b.x, a.y*b.y, a.z*b.z, a.w*b.w` : rien de tout ça n'apparaît dans les formules ci-dessus. Il faut donc d'abord **réarranger** les lanes pour amener les bonnes valeurs en face les unes des autres, *puis* multiplier — c'est le rôle du shuffle `yzx` : il fait tourner les 3 composantes d'un cran, si bien que **chaque lane affiche la valeur de la composante suivante** dans le cycle `x → y → z → x` — la lane `x` du résultat affiche l'ancien `y`, la lane `y` affiche l'ancien `z`, la lane `z` affiche l'ancien `x`.

Vérifions avec `a = (x=1, y=0, z=0)`, `b = (x=0, y=1, z=0)` (attendu : `cross(a,b) = (0,0,1)`) :

```
a     = (x=1, y=0, z=0)          b     = (x=0, y=1, z=0)
a_yzx = (x=0, y=0, z=1)          b_yzx = (x=1, y=0, z=0)     -- x<-y, y<-z, z<-x, à partir de a et b ci-dessus

a * b_yzx  = (1*1, 0*0, 0*0) = (x=1, y=0, z=0)
a_yzx * b  = (0*0, 0*1, 1*0) = (x=0, y=0, z=0)
c = (a*b_yzx) - (a_yzx*b)    = (x=1, y=0, z=0)
```

`c` contient déjà les 3 bonnes valeurs, mais **dans le mauvais ordre** : sa lane `x` vaut `a.x*b_yzx.x - a_yzx.x*b.x = a.x*b.y - a.y*b.x`, qui est en réalité la formule de `cross.z` (comparez avec les 3 formules ci-dessus) — pas de `cross.x`. Un dernier shuffle `yzx` sur `c` remet tout à sa place : `c_yzx = (x=old.y=0, y=old.z=0, z=old.x=1) = (0, 0, 1)` ✓, exactement `cross(a,b)` attendu. D'où les 3 shuffles de `cross3` : un sur chaque opérande d'entrée pour aligner les bonnes composantes avant la multiplication, un dernier sur le résultat pour corriger l'ordre. C'est exactement le même geste que le *swizzling* `v.zyx` d'un shader — sauf qu'ici il sert à calculer quelque chose, pas seulement à réordonner un affichage.

- **`__vectorcall`** (MSVC) : passe les `__m128` dans les registres `xmm0`-`xmm5` au lieu de la pile. DirectXMath l'utilise via la macro `XM_CALLCONV`.
- **Benchmark** : 1 M de `M * v` : scalaire 0,95 ms, SSE 0,76 ms. **Écart faible** : la version scalaire (4 × 4 multiplications de taille fixe) est **SLP-vectorisée** par le compilateur. Encore une preuve qu'il faut mesurer avant d'écrire du SIMD à la main.

Pour un vrai moteur, la référence à utiliser est **DirectXMath** (Microsoft, header-only, SSE/AVX/NEON, [GitHub](https://github.com/microsoft/DirectXMath)) — c'est le meilleur code à *lire* pour apprendre. Votre `vec4.hpp` sert avant tout à comprendre le mécanisme, et à alimenter les registres `vec4` de votre VM.

---

# 3.5 AVX et AVX2 (256 bits)

## Ce qui change

1. **Largeur** : 8 `float` / 4 `double` / 4 `int64` par registre `ymm`.
2. **Encodage VEX** (*Vector EXtension*, le préfixe d'instruction qui remplace REX pour AVX — détaillé en 2.7) :
   - instructions **à 3 opérandes** (`vaddps ymm0, ymm1, ymm2` : la source n'est pas écrasée) ;
   - accès mémoire **non alignés** autorisés dans les opérations ;
   - pas de pénalité de transition... **si tout le code est VEX**.
3. **AVX2** : entiers 256 bits (`_mm256_add_epi32`...), **gather** (`_mm256_i32gather_ps`), décalages variables, permutations sur toute la largeur.
4. **FMA** (*Fused Multiply-Add*) : `_mm256_fmadd_ps(a, b, c)` calcule `a*b + c` en une instruction, plus rapide **et plus précise** (un seul arrondi). Mais le résultat diffère donc du calcul non fusionné : attention au déterminisme.

## Sous MSVC : `/arch`

| Option (x64) | Effet |
|---------------|--------|
| (défaut) | SSE2 pour le code généré automatiquement. **Les intrinsics AVX/AVX2/AVX-512 restent utilisables** |
| `/arch:SSE4.2` | SSE4.2 autorisé pour le code généré |
| `/arch:AVX` | VEX partout, auto-vectorisation 256 bits flottants |
| `/arch:AVX2` | + entiers 256 bits, FMA possible (avec `/fp:contract`), BMI |
| `/arch:AVX512` | 512 bits par défaut (`/vlen:256` pour rester en 256) |
| `/arch:AVX10.1` (VS 2022 17.13), `/arch:AVX10.2` (VS 2026) | 256 bits par défaut, `/vlen:512` possible |

Dans VS : *Project Properties → C/C++ → Code Generation → Enable Enhanced Instruction Set*. MSVC définit les macros `__AVX__`, `__AVX2__`, `__AVX512F__`... selon l'option choisie.

**Différence MSVC / GCC-Clang (piège si vous testez ailleurs)** : MSVC accepte n'importe quel intrinsic dans n'importe quelle fonction. GCC et Clang refusent (`inlining failed in call to 'always_inline' ... target specific option mismatch`) sans `-mavx2` ou `__attribute__((target("avx2")))`. Notre en-tête [common/simd_config.hpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/common/simd_config.hpp) masque cette différence (`TARGET_AVX2`).

**Bonne pratique MSVC** : mettre le code AVX2 dans un **`.cpp` séparé compilé avec `/arch:AVX2`** (propriété du fichier dans VS). Tout le code de ce fichier est alors VEX, l'auto-vectorisation y utilise AVX2, et on l'appelle via le dispatch (3.7).

## Table de référence : ce qui change par rapport à SSE

**La majorité des intrinsics SSE ont un équivalent AVX2 qui suit exactement le même nom**, juste avec `_mm256_` au lieu de `_mm_` (`_mm_add_ps` → `_mm256_add_ps`, `_mm_mul_ps` → `_mm256_mul_ps`, `_mm_cmpgt_ps` → `_mm256_cmp_ps` avec un prédicat en argument...). Ce qui mérite une attention particulière, ce sont les intrinsics **vraiment nouveaux** ou dont le comportement diffère :

| Intrinsic AVX/AVX2 | Instruction MASM | Ce que ça fait |
|---------------------|-------------------|-----------------|
| `_mm256_broadcast_ss(ptr)` | `vbroadcastss` | diffuse **un seul** `float` depuis la mémoire dans les 8 lanes — plus direct que `set1` |
| `_mm256_permute_ps(a, imm)` | `vpermilps` | comme `shuffle_ps`, mais **un seul opérande** et applicable indépendamment sur chaque lane de 128 bits |
| `_mm256_permute2f128_ps(a, b, imm)` | `vperm2f128` | réarrange des **blocs de 128 bits entiers** entre `a` et `b` — un des rares moyens de traverser les deux lanes en SSE/AVX (pas AVX2) |
| `_mm256_permutevar8x32_ps(a, idx)` (AVX2) | `vpermps` | permutation **arbitraire et indexée** des 8 éléments, **à travers les deux lanes** — celle qui manque le plus souvent quand on porte du code SSE vers AVX |
| `_mm256_i32gather_ps(base, idx, scale)` (AVX2) | `vgatherdps` | charge 8 `float` à 8 adresses différentes (`base + idx[i]*scale`) — accès mémoire indirect, souvent plus lent que prévu (voir 3.2, limites de l'auto-vectorisation) |
| `_mm256_fmadd_ps(a,b,c)` (AVX2+FMA) | `vfmadd213ps` (ou 132/231 selon l'ordre des opérandes) | `a*b + c` en une instruction, un seul arrondi (plus rapide **et** plus précis qu'un `mul` + `add` séparés — mais résultat différent, voir 3.2 sur le déterminisme) |
| `_mm256_blendv_ps` | `vblendvps` | identique à SSE4.1, désormais standard en AVX |
| `_mm256_zeroupper()` | `vzeroupper` | remet à zéro la moitié haute de tous les `ymm` — voir la pénalité de transition ci-dessous |
| `_mm256_testz_ps(a, b)` | `vtestps` + `jz` | teste un masque et fixe les flags **directement**, sans passer par `movemask` puis comparer — utile pour un test « masque vide » rapide |

## La pénalité de transition SSE/AVX et `vzeroupper`

Mélanger des instructions SSE « legacy » (non VEX) avec des registres `ymm` dont la moitié haute est « sale » provoque, selon les CPU, une pénalité (sauvegarde et restauration de l'état, ou fausse dépendance). La règle : **appeler `_mm256_zeroupper()` avant de retourner vers du code SSE**. Les compilateurs l'ajoutent automatiquement dans le code compilé en AVX, mais pas forcément dans du code intrinsics compilé sans `/arch:AVX`.

## Les *lanes* : le piège d'AVX

**L'idée reçue dangereuse** : penser qu'un `ymm` de 256 bits est « comme un `xmm` de 128 bits, mais deux fois plus large », et que le code qui manipule ses 8 éléments se comporte comme s'il n'y en avait qu'un seul bloc continu. C'est faux pour une bonne partie des instructions : électriquement, beaucoup d'unités d'exécution AVX sont construites comme **deux unités 128 bits accolées**, chacune ne « voyant » que sa propre moitié du registre. Un `ymm` se comporte alors comme **deux `xmm` indépendants côte à côte**, chacun une *lane* :

```
                     lane basse (128 bits)          lane haute (128 bits)
ymm  = [    élément 0  1  2  3    |    élément 4  5  6  7    ]
         \______________________/    \______________________/
              lane 0 (comme un xmm)      lane 1 (comme un xmm)
```

Les opérations **arithmétiques** (`vaddps`, `vmulps`...) ne posent aucun problème : `+`, `*`, `min`, `max` sont appliqués élément par élément, la frontière entre lanes n'a aucune importance puisque chaque lane ne dépend que de ses propres éléments. Le piège concerne les opérations qui **redistribuent** des éléments — shuffle, unpack, permute — où la frontière devient très visible :

```
a = [ 0  1  2  3 |  4  5  6  7 ]      b = [ 10 11 12 13 | 14 15 16 17 ]

_mm256_unpacklo_ps(a, b)            = [ 0 10  1 11 |  4 14  5 15 ]   <- PAS [0 10 1 11 2 12 3 13]
_mm256_shuffle_ps(a, b, (1,0,3,2))  = [ 2  3 10 11 |  6  7 14 15 ]   <- le masque s'applique à chaque lane
_mm256_permutevar8x32_ps(a, 7..0)   = [ 7  6  5  4 |  3  2  1  0 ]   <- AVX2 : traverse les lanes
```

(Sortie vérifiée.) Sur `_mm256_unpacklo_ps(a, b)`, l'intuition « entrelacer tout le tableau » donnerait `[0 10 1 11 2 12 3 13]` — mais l'instruction traite `[0 1 2 3]`/`[10 11 12 13]` (lane basse) et `[4 5 6 7]`/`[14 15 16 17]` (lane haute) **séparément**, donnant deux entrelacements indépendants de 4 éléments chacun : `[0 10 1 11]` puis `[4 14 5 15]`. Même chose pour `_mm256_shuffle_ps` : le masque `_MM_SHUFFLE` s'applique **identiquement aux deux lanes**, il ne peut pas faire venir un élément de la lane haute dans la lane basse. Tout code SSE « porté » naïvement en AVX en changeant `_mm_` en `_mm256_` est donc **faux** dès qu'il utilise `unpack` ou `shuffle` — le résultat compile, tourne, ne crashe pas, et donne simplement le mauvais nombre.

**Comment traverser les lanes quand c'est nécessaire** : seules certaines instructions le permettent, et il faut les demander explicitement (tableau ci-dessus) — `_mm256_permutevar8x32_ps`/`vpermps` (AVX2, indexation arbitraire sur les 8 éléments), `_mm256_permute2f128_ps`/`vperm2f128` (échanger des blocs de 128 bits entiers entre deux registres), ou, pour une réduction horizontale sur les 8 éléments (par exemple une somme complète d'un `__m256`), le geste classique consiste à **rabattre** la lane haute sur la lane basse avec `_mm256_extractf128_ps` (extraire les 128 bits hauts dans un `__m128`), additionner ce résultat avec la lane basse (`_mm256_castps256_ps128`), puis terminer avec la réduction horizontale SSE déjà vue plus haut (`movehl` + `shuffle`) — on retombe sur du 128 bits classique pour la toute dernière étape.

## Benchmarks : [code/03_simd/bench_kernels.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/bench_kernels.cpp)

1 048 579 `float` (≈ 4 Mo, un peu plus que le L2), chaque mesure couvre 20 exécutions :

```
--- somme de 1048579 floats ---
scalaire (no_vector)                         12.209 ms
auto-vectorisation                           12.126 ms     (réduction flottante : non vectorisée)
SSE                                           3.075 ms     x4
AVX2 (4 accumulateurs)                        0.667 ms     x18 !
AVX-512 (tail masqué)                        0.775 ms
--- saxpy ---
scalaire (no_vector)                          4.768 ms
auto-vectorisation                            4.724 ms     (GCC -O2 : pas de versioning d'aliasing ; x3 en -O3)
SSE                                           1.547 ms
AVX2 + FMA                                    1.523 ms     = SSE : limité par la bande passante mémoire
--- clamp [-0.5, 0.5] (données aléatoires = branches imprévisibles) ---
scalaire avec if                             47.602 ms
SSE min/max                                   2.128 ms     x22 : les mispredictions disparaissent
--- compter x > 0.25 ---
scalaire                                      5.933 ms
AVX2 movemask + popcount                      1.068 ms
AVX-512 mask + popcount                       0.733 ms
```

**Trois lectures à en tirer** :

- **Somme x18 avec AVX2 (> 8 !)** : la somme scalaire est limitée par la **latence** de l'addition flottante. Chaque `addss` attend le résultat du précédent (~3-4 cycles), et le CPU ne peut pas paralléliser cette chaîne : `s = s + x[0]`, puis `s = s + x[1]` dépend du résultat de la ligne précédente, etc. — une seule longue chaîne de dépendances, quelle que soit la largeur du registre utilisé. **4 accumulateurs indépendants** cassent cette chaîne :

  ```cpp
  __m256 acc0 = _mm256_setzero_ps(), acc1 = acc0, acc2 = acc0, acc3 = acc0;
  for (; i + 32 <= n; i += 32) {
      acc0 = _mm256_add_ps(acc0, _mm256_loadu_ps(&x[i]));       // 4 chaînes de dépendances
      acc1 = _mm256_add_ps(acc1, _mm256_loadu_ps(&x[i + 8]));   // INDÉPENDANTES : le CPU
      acc2 = _mm256_add_ps(acc2, _mm256_loadu_ps(&x[i + 16]));  // out-of-order peut les
      acc3 = _mm256_add_ps(acc3, _mm256_loadu_ps(&x[i + 24]));  // exécuter en même temps
  }
  __m256 sum = _mm256_add_ps(_mm256_add_ps(acc0, acc1), _mm256_add_ps(acc2, acc3));  // fusion finale
  ```

  `acc0` ne dépend jamais de `acc1`/`acc2`/`acc3` : le CPU *out-of-order* peut faire progresser les 4 chaînes **en parallèle**, en profitant du fait qu'il a plusieurs unités d'exécution flottantes disponibles à chaque cycle. Résultat : le débit n'est plus limité par la latence d'une seule chaîne, mais par le **débit** (throughput) du CPU — d'où un gain qui dépasse le simple facteur de largeur (x8 attendu pour AVX2 sur des `float`, x18 mesuré, parce que la version scalaire elle-même était limitée par la latence, pas seulement plus étroite). AVX-512 avec **un seul** accumulateur est plus lent que AVX2 avec 4 : la largeur du registre ne compense pas une chaîne de dépendance non brisée. **La largeur ne fait pas tout : la structure du calcul compte.**

  **« Tail masqué » (ligne AVX-512)** : `1 048 579` n'est pas un multiple de 16 (largeur d'un `zmm` en `float`). Plutôt que de séparer une boucle SIMD + une boucle scalaire pour les 3 derniers éléments (stratégie 1 de « Gérer le tail » plus haut), la version AVX-512 calcule un masque `__mmask16` qui n'a que les 3 bits correspondant aux éléments restants activés, et fait `_mm512_maskz_loadu_ps(mask, ptr)` : **une seule** instruction charge (et additionne) exactement les éléments valides, zéro pour le reste — pas de deuxième boucle, pas de risque de lire hors tableau. C'est la stratégie 4 de « Gérer le tail », appliquée ici en vrai.
- **saxpy : SSE ≈ AVX2**. Deux tableaux de 4 Mo lus et un écrit : c'est la RAM qui limite. Retour à la partie 1.
- **clamp x22** : c'est surtout la suppression des branches imprévisibles qui paye, pas la largeur.

**Piège de benchmark à connaître** : lors de l'écriture de ces tests, GCC calculait à un moment **une seule fois** la fonction scalaire « pure » appelée 20 fois avec les mêmes arguments, et affichait un temps 20x trop bon. D'où la fonction `opaque()` de `bench.hpp` (appel via un pointeur `volatile`, qui empêche cette optimisation). Retenez la leçon : **un benchmark trop beau est un benchmark faux**.

## Démo « moteur » : frustum culling

[code/03_simd/culling.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/culling.cpp) : 1 M de bounding spheres en SoA alignée sur 32, 6 plans.

```
scalaire (early-out)                         12.424 ms
SSE (4 sphères / itération)                   1.730 ms      x7
AVX2 + FMA (8 / itération)                    0.713 ms      x17
visibles : 131620 / 131620 / 131620 sur 1000003 - résultats identiques : oui
```

Le scalaire utilise un **early-out** (on arrête dès qu'un plan rejette), qui semble plus malin mais cause des mispredictions. Les versions SIMD testent **toujours les 6 plans** : c'est plus de calcul, mais sans branche.

---

# 3.6 AVX-512

## Les nouveautés qui comptent

1. **32 registres** `zmm0`-`zmm31` (512 bits = 16 `float`).
2. **Registres de masque `k0`-`k7`** : les comparaisons produisent directement des **bits** (`__mmask16`), et presque toutes les opérations acceptent un masque :
   ```cpp
   __mmask16 m = _mm512_cmp_ps_mask(x, t, _CMP_GT_OQ);     // 1 bit par élément
   __m512 r = _mm512_mask_add_ps(src, m, a, b);             // r[i] = m[i] ? a[i]+b[i] : src[i]
   __m512 z = _mm512_maskz_loadu_ps(tail_mask, ptr);        // tail sans boucle scalaire ni lecture hors limites
   ```
3. **Instructions nouvelles** : `vpcompressd` (filtrer un tableau selon un masque), scatter, `vpternlog`, `reduce_add`, VNNI (réseaux de neurones), VBMI (octets)...
4. **Versions 128 et 256 bits de tout ça** (AVX-512VL) : on profite des masques sans passer à 512 bits.

## Les subtilités (pourquoi c'est compliqué)

- **Fragmentation** : AVX-512 est une famille (F, CD, BW, DQ, VL, VNNI, VBMI, VBMI2, IFMA, BITALG, FP16...). Chaque génération de CPU en supporte un sous-ensemble différent.
- **Disponibilité** : Intel l'a retiré de ses CPU grand public hybrides (Alder Lake en 2021 : les E-cores ne le supportaient pas), alors qu'AMD l'a ajouté (Zen 4, 2022). Résultat : environ 24 % du parc Steam.
- **Throttling** : sur Skylake-SP/X, les instructions 512 bits « lourdes » faisaient **baisser la fréquence** du cœur (*AVX-512 license levels*) pendant plusieurs millisecondes. Un petit bout de code AVX-512 pouvait **ralentir tout le reste**. C'est très atténué depuis Ice Lake, et absent sur Zen 4.
- **AVX10** : la réponse d'Intel. Une numérotation par version (10.1, 10.2) plutôt que par sous-ensemble, avec le 512 bits obligatoire depuis la révision de 2025. MSVC propose `/arch:AVX10.1` depuis VS 2022 17.13.

Pour un jeu PC en 2026, le discours raisonnable est le suivant : **AVX2 comme cible haute, AVX-512 en bonus derrière un dispatch, et seulement si un benchmark le justifie**. Une bonne partie du parc n'a tout simplement pas de CPU compatible.

---

# 3.7 CPU dispatch

[code/03_simd/cpu_dispatch.cpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/03_simd/cpu_dispatch.cpp) et [common/simd_config.hpp](https://github.com/johannphilippe/hardware_acceleration/blob/main/code/common/simd_config.hpp).

**Deux vérifications sont nécessaires** :

1. **Le CPU** supporte l'extension : `CPUID` leaf 1 (ECX : SSE4.1 bit 19, SSE4.2 bit 20, AVX bit 28) et leaf 7 (EBX : AVX2 bit 5, AVX-512F bit 16).
2. **L'OS** sauvegarde les registres étendus lors des changements de contexte : bit OSXSAVE, puis `XGETBV(0)` (bits 1-2 pour AVX, 5-7 en plus pour AVX-512).

```cpp
CpuFeatures f = detect_cpu();                    // __cpuidex + _xgetbv sous MSVC
SumFn sum = f.avx2 ? sum_avx2 : sum_scalar;       // choisi UNE fois au démarrage
```

```
SSE4.1 1 | SSE4.2 1 | AVX 1 | AVX2 1 | FMA 1 | AVX-512F 1 BW 1 VL 1
implémentation choisie : AVX2
```

Stratégies :

- **Pointeur de fonction** choisi au démarrage (ci-dessus). Simple, avec un appel indirect par appel : à placer **autour** des boucles, jamais dedans.
- **Plusieurs DLL** compilées avec des `/arch` différents, chargées selon le CPU.
- **Vérification au lancement** avec un message clair (« ce jeu nécessite AVX2 ») : c'est ce que font beaucoup de jeux AAA récents.
- **Le CRT MSVC fait déjà du dispatch** : `__isa_available`, vu dans le code de la partie 2. MSVC fournit aussi l'intrinsic [`__check_isa_support`](https://learn.microsoft.com/en-us/cpp/intrinsics/check-isa-arch-support) pour tester les extensions courantes sans décoder `CPUID` soi-même.

---

# 3.8 Écosystème : bibliothèques et abstractions

| Outil | Nature | Remarque |
|-------|--------|-----------|
| **DirectXMath** | maths 3D SIMD (Microsoft) | header-only, SSE/AVX/NEON, `XMVECTOR`, `__vectorcall` |
| **STL MSVC** | algorithmes vectorisés à la main (SSE4.2/AVX2) | `std::find`, `std::count`, `std::min_element`... sur types triviaux : [doc](https://learn.microsoft.com/en-us/cpp/standard-library/vectorized-stl-algorithms) |
| **`std::simd`** | STL, **C++26** (P1928) | issu de `std::experimental::simd` (Parallelism TS 2, implémenté dans libstdc++ depuis GCC 11). **Pas disponible dans la STL MSVC** au moment d'écrire ce cours |
| **xsimd** | wrappers C++ | utilisé par xtensor, Apache Arrow |
| **Google Highway** | wrappers + dispatch | utilisé par Chrome, JPEG XL, libvips |
| **EVE** | C++20 | très expressif, algorithmes SIMD |
| **SIMDe** | émulation portable | permet de compiler du code SSE/AVX sur ARM (et inversement) |
| **ISPC** | compilateur SPMD (Intel) | « shader-like » pour le CPU ; **utilisé dans Unreal Engine** (Chaos, animation) |
| **Unity Burst** | compilateur C# → SIMD | exemple industriel du modèle « job + SoA » |
| Agner Fog **VCL** | wrappers C++ | simple, bien documenté |

Pourquoi ne pas simplement utiliser `std::simd` si vous aimez la STL ? C'est la bonne direction — mais la STL MSVC ne le fournit pas encore, seulement libstdc++ (GCC). Vous pouvez en observer un exemple sur Compiler Explorer pour la culture, sans compter dessus dans le projet :

```cpp
// std::experimental::simd (GCC/libstdc++) - pour la culture
#include <experimental/simd>
namespace stdx = std::experimental;
stdx::native_simd<float> a([](int i) { return float(i); });
auto b = a * 2.0f + 1.0f;               // opérateurs naturels, largeur choisie selon la cible
float s = stdx::reduce(b);
```

**Décortiquons ces 4 lignes**, car la syntaxe cache beaucoup de mécanique :

- `stdx::native_simd<float>` est un **type dont la largeur n'est pas fixée par le code**, mais par les options de compilation cibles : sur une machine compilée `-msse2` (128 bits), `native_simd<float>` contient 4 `float` (l'équivalent d'un `__m128` derrière le rideau) ; compilé `-mavx2`, il en contient 8 (comme un `__m256`) ; `-mavx512f`, 16. **Le même code source** produit un binaire différent selon la cible, sans qu'aucune ligne ne change — c'est tout l'intérêt face à `__m128`/`__m256` qui figent la largeur dans le type lui-même.
- `stdx::native_simd<float> a([](int i) { return float(i); })` est un **constructeur générateur** : la lambda est appelée une fois par lane, avec son indice `i`, pour produire la valeur initiale de cette lane. Avec une largeur de 4 (SSE), ça construit `a = [0.0, 1.0, 2.0, 3.0]` — équivalent de `_mm_setr_ps(0,1,2,3)` écrit à la main, mais qui s'adapterait tout seul à une largeur de 8 en AVX2 (`a = [0,1,2,3,4,5,6,7]`).
- `auto b = a * 2.0f + 1.0f;` : les opérateurs `*` et `+` sont surchargés pour agir **lane par lane**, exactement comme `_mm_mul_ps`/`_mm_add_ps`, mais avec la syntaxe naturelle du C++ au lieu d'appeler des intrinsics. Avec `a = [0,1,2,3]` (largeur 4) : `b = [1.0, 3.0, 5.0, 7.0]`.
- `stdx::reduce(b)` fait la **réduction horizontale** (la somme de toutes les lanes) — exactement ce que le code fait à la main plus haut (`movehl` + `shuffle` + `add`), mais implémenté une fois pour toutes dans la bibliothèque, avec la meilleure séquence d'instructions connue pour la cible. Avec `b = [1,3,5,7]` : `s = 1+3+5+7 = 16.0f`.

En clair : `std::experimental::simd` fait exactement ce que fait ce chapitre à la main (charger/construire un vecteur, l'opérer, le réduire), mais avec une largeur **portable** choisie automatiquement, et sans jamais écrire `_mm_` explicitement. C'est la direction que prend le C++ standard (P1928, C++26) — mais pas encore disponible dans la STL MSVC au moment d'écrire ce cours, d'où son statut de « pour la culture » ici.

---

# 3.9 Et sur ARM : NEON (pour la culture)

| SSE | NEON (`<arm_neon.h>`) |
|-----|--------------------------|
| `__m128` | `float32x4_t` |
| `__m128i` (type unique) | `int8x16_t`, `uint32x4_t`... (**types distincts**) |
| `_mm_add_ps(a, b)` | `vaddq_f32(a, b)` |
| `_mm_mul_ps` | `vmulq_f32` |
| `_mm_cmpgt_ps` | `vcgtq_f32` (retourne un `uint32x4_t`) |
| `_mm_movemask_ps` | **pas d'équivalent direct** (astuces nécessaires) |
| masques + `and`/`or` | `vbslq_f32(mask, a, b)` (*bitwise select*) |
| FMA | `vmlaq_f32` / `vfmaq_f32` |

**SVE/SVE2** : on n'écrit pas « 4 » ou « 8 », le code demande la largeur à l'exécution (`svcntw()`). C'est le modèle *vector-length agnostic*, repris par RISC-V V.

---

# 3.10 Lien avec le projet

| Notion SIMD | Dans l'interpréteur |
|--------------|------------------------|
| `vec4` SSE | un **type `vec4` natif** dans le langage, avec des registres `__m128` et des opcodes `VADD`, `VDOT`, `VCROSS`... comme le `vector` natif de **Luau** (Roblox) |
| Masques / branchless | opcodes `SELECT`, `MIN`, `MAX`, `CLAMP` sans branche |
| SoA + boucles SIMD | **batch mode** : exécuter le même bytecode sur N entités, un registre VM devenant un tableau de N valeurs (modèle des shaders et d'ISPC) |
| Scan d'octets | **lexer** accéléré (espaces, commentaires, identifiants) |
| CPU dispatch | la VM choisit sa table d'opcodes (SSE / AVX2) au démarrage |
| Auto-vectorisation | vérifier que les boucles internes des opcodes batch sont vectorisées (`/Qvec-report:2`) |

---

# 3.11 Pièges et questions fréquentes

- **Crash `movaps` / access violation** : `_mm_load_ps` (ou `_mm_store_ps`) sur de la mémoire non alignée. Passer à `loadu` ou aligner la donnée (`alignas`, `operator new` aligné).
- **Résultats « à l'envers »** : `_mm_set_ps` vs `_mm_setr_ps`.
- **« AVX2 plus lent que SSE »** : bande passante mémoire, transitions SSE/AVX (manque de `vzeroupper`), throttling, ou petites boucles.
- **« Ça compile sous MSVC mais pas chez moi (GCC/Clang) »** : il faut `-mavx2` ou `__attribute__((target))`.
- **« Illegal instruction » au lancement sur la machine d'un ami** : binaire compilé avec `/arch:AVX2`, lancé sur un CPU sans AVX2. D'où le dispatch.
- **`std::vector<__m128>`** : fonctionne depuis C++17 (new aligné), mais attention aux vieux allocateurs custom.
- **Comparer des résultats flottants SIMD / scalaire au bit près** : faux (ordre des opérations, FMA). Toujours utiliser une tolérance.
- **NaN dans les masques** : `_CMP_GT_OQ` (*ordered, quiet*) renvoie faux si un NaN est présent. Le choix du prédicat compte.
- **Benchmark trop beau** : fonction pure calculée une seule fois, résultat inutilisé et supprimé, mesure en Debug.

---

# 3.12 Références

**Documentation**

- [Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
- Microsoft : [/Qvec-report](https://learn.microsoft.com/en-us/cpp/build/reference/qvec-report-auto-vectorizer-reporting-level), [Vectorizer messages](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/vectorizer-and-parallelizer-messages), [/arch (x64)](https://learn.microsoft.com/en-us/cpp/build/reference/arch-x64), [Auto-vectorization](https://learn.microsoft.com/en-us/cpp/parallel/auto-parallelization-and-auto-vectorization), [blog AVX-512 auto-vectorization in MSVC](https://devblogs.microsoft.com/cppblog/avx-512-auto-vectorization-in-msvc/)
- ARM : [NEON Intrinsics Reference](https://developer.arm.com/architectures/instruction-sets/intrinsics/)
- [DirectXMath](https://github.com/microsoft/DirectXMath) : lire `DirectXMathVector.inl`

**Articles et papiers**

- Agner Fog, [*Optimizing software in C++*](https://www.agner.org/optimize/optimizing_cpp.pdf) (chap. SIMD) et [*Instruction tables*](https://www.agner.org/optimize/instruction_tables.pdf) (latences par CPU)
- Geoff Langdale, Daniel Lemire, *Parsing Gigabytes of JSON per Second*, VLDB Journal, 2019 : [arXiv](https://arxiv.org/abs/1902.08318)
- Blog de Daniel Lemire ([lemire.me](https://lemire.me/blog/)) : SIMD pratique, AVX-512
- Travis Downs, *Gathering Intel on Intel AVX-512 Transitions* (throttling) : [blog](https://travisdowns.github.io/blog/2020/01/17/avxfreq1.html)
- Wojciech Muła : [0x80.pl](http://0x80.pl/) (algorithmes SIMD sur les octets : base64, UTF-8)
- Intel, [AVX10.2 Architecture Specification](https://www.intel.com/content/www/us/en/content-details/856721/intel-advanced-vector-extensions-10-2-intel-avx10-2-architecture-specification.html)
- P1928 : [std::simd, merge data-parallel types from the Parallelism TS 2](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p1928r15.pdf)

**Conférences**

- Andreas Fredriksson, *SIMD at Insomniac Games: How We Do the Shuffle*, GDC 2015 : [slides avec notes](https://deplinenoise.wordpress.com/2015/03/06/slides-simd-at-insomniac-games-gdc-2015/), [GDC Vault](https://www.gdcvault.com/play/1022248/SIMD-at-Insomniac-Games-How). **La** conférence SIMD orientée jeu vidéo.
- Matthias Kretz, *std::simd: How to Express Inherent Parallelism Efficiently Via Data-parallel Types*, CppCon 2023 : [YouTube](https://www.youtube.com/watch?v=LAJ_hywLtMA)
- Michael Stallone, *8 Frames in 16ms: Rollback Networking in Mortal Kombat and Injustice 2*, GDC 2018 (déterminisme)
- Chandler Carruth, *Going Nowhere Faster*, CppCon 2017

**Livres**

- Daniel Kusswurm, *Modern X86 Assembly Language Programming* (AVX2/AVX-512 en ASM et intrinsics)
- Jason Gregory, *Game Engine Architecture*, chap. 6 (SIMD, maths 3D)
