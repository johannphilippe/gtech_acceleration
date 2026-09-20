---
title: 4 - Interpréteur, machine virtuelle, assembleur maison
author: Johann Philippe
---

# 4. Interpréteur : de la chaîne de caractères au registre

> C'est le chapitre qui relie tout : mémoire, ASM et SIMD s'y retrouvent dans la conception d'une petite machine virtuelle et de son assembleur maison — exactement ce que vous allez construire pour le projet.
> Le code est dans [code/04_interpreteur](https://github.com/johannphilippe/gtech_acceleration/tree/main/code/04_interpreteur) :
> - `showdown.cpp` *(distribué comme corrigé après les défis 2 et 5, voir exercices)* : le même programme en tree-walking, stack VM et register VM, avec 3 techniques de dispatch ;
> - [batch_demo.cpp](https://github.com/johannphilippe/gtech_acceleration/blob/main/code/04_interpreteur/batch_demo.cpp) : interprétation par entité vs par lot (SIMD) ;
> - `vektor/` : **l'interpréteur de référence complet** (lexer, parser Pratt, sema typée, compilateur vers une VM à registres typés `int64` / `__m128`, assembleur et désassembleur maison, tests). Certains de ses fichiers vous seront distribués **par morceaux**, au fil des exercices, pour servir de corrigés partiels.
> Des exercices accompagnent ce chapitre (distribués séparément en cours).

## Objectifs de la partie

1. Connaître la chaîne complète : **lexer → parser → AST (*Abstract Syntax Tree*, arbre de syntaxe abstraite) → analyse sémantique → compilation → exécution**.
2. Comprendre les trois grands modèles d'exécution : **tree-walking**, **stack VM**, **register VM** (Lua 5). Savoir *mesurer* et *expliquer* leurs différences en termes de mémoire, de cache et de branchements.
3. Connaître les techniques de **dispatch** (switch, computed goto, tail calls) et leurs limites sous MSVC.
4. Savoir représenter les valeurs (**tagged union**, NaN-boxing, **registres typés**).
5. Écrire un **assembleur maison** (texte → bytecode, deux passes, labels) et son désassembleur.
6. Brancher les optimisations matérielles dans la VM : **registres SIMD, opcodes vectoriels, exécution par lot**.

---

# 4.1 Préambule historique

## Interpréteurs et machines virtuelles

| Année | Jalon |
|-------|-------|
| 1958-60 | **LISP** (John McCarthy) : `eval` décrit en LISP lui-même, le premier interpréteur « méta-circulaire » |
| 1964 | **BASIC** (Dartmouth) : langage interprété pour débutants, puis sur tous les micro-ordinateurs |
| 1970 | **Forth** (Chuck Moore) : *threaded code*, machine à pile minimale |
| 1973-78 | **Pascal P-code** (Wirth), puis **UCSD Pascal** : un compilateur vers une VM à pile portable |
| 1979 | **Z-machine** (Infocom) : VM des jeux d'aventure textuels (*Zork*), portée sur toutes les machines de l'époque |
| 1980 | Smalltalk-80 : bytecode et VM documentés dans le « Blue Book » |
| 1987 | **SCUMM** (LucasFilm Games, *Maniac Mansion*) : langage de script et VM des jeux d'aventure LucasArts |
| 1993 | **Lua** (Roberto Ierusalimschy, Luiz Henrique de Figueiredo, Waldemar Celes, PUC-Rio) |
| 1995 | **JVM** : VM à pile, bytecode, puis JIT |
| 1996 | **QuakeC** (id Software) : la logique de jeu de *Quake* tourne dans une VM |
| 1998 | **UnrealScript** (Tim Sweeney) ; *Grim Fandango* (LucasArts), l'un des premiers grands jeux scriptés en **Lua** |
| 1999 | *Quake III* : **QVM**, du C compilé (LCC) vers un bytecode, puis JIT x86 |
| 2003 | **Lua 5.0** : première VM *register-based* largement utilisée |
| 2004 | *World of Warcraft* : l'interface utilisateur est scriptée en Lua |
| 2005 | LuaJIT (Mike Pall) ; son interpréteur est écrit en assembleur (DynASM) |
| 2008 | V8 (Chrome) : JavaScript compilé en machine code |
| 2014 | Unreal Engine 4 : UnrealScript disparaît au profit des **Blueprints** (VM de graphe) |
| 2019-21 | **Luau** (Roblox) : dérivé de Lua, avec typage graduel, **type `vector` natif** et interpréteur très optimisé |
| 2023+ | Godot 4 (GDScript réécrit), CPython 3.13 (JIT *copy-and-patch*), 3.14 (interpréteur *tail-call*) |

> « Any sufficiently complicated C or Fortran program contains an ad hoc, informally-specified, bug-ridden, slow implementation of half of Common Lisp. »
> — Philip Greenspun, *dixième règle* (années 90)

Côté jeu vidéo, cette liste raconte une même histoire répétée : tout moteur finit tôt ou tard par avoir un langage de script, un système de *data-driven behaviour* ou un graphe de nœuds — autant savoir le construire correctement dès la première fois. Une lecture qui prolonge bien ce chapitre : Robert Nystrom, *Game Programming Patterns*, chapitre [*Bytecode*](https://gameprogrammingpatterns.com/bytecode.html), gratuit en ligne.

## Pourquoi un interpréteur dans un jeu ?

- **Itération** : modifier le gameplay sans recompiler le moteur (*hot reload*).
- **Sécurité** et *sandboxing* : les mods et l'**UGC** (*User-Generated Content*, contenu créé par les joueurs — Roblox, *Garry's Mod*) ne doivent pas accéder à la mémoire du moteur.
- **Portabilité** : le même bytecode tourne sur PC, console et mobile, **sans JIT**. iOS et les consoles interdisent souvent la génération de code exécutable (pages W^X), d'où l'importance d'un **interpréteur rapide**.
- **Données** : IA (arbres de comportement), dialogues, quêtes, *shaders* de particules, graphes audio.

---

# 4.2 La chaîne de compilation

```
 source ──► LEXER ──► tokens ──► PARSER ──► AST ──► SEMA ──► AST typé ──► COMPILER ──► bytecode ──► VM
 "let x"     │         [let][x]    │        Let(x)   │       + scopes      │           LOADI r0, 1   │
             │                     │                 │       + types       │                          │
          erreurs              erreurs           erreurs                 (optimisations)       erreurs
          lexicales            syntaxiques       sémantiques                                   d'exécution
```

Trois grandes stratégies d'exécution :

1. **Tree-walking** : on exécute l'AST directement. C'est simple, mais lent (voir 4.6).
2. **Bytecode + VM** : l'AST est compilé en instructions compactes, exécutées par une boucle. C'est le choix de Lua, Python, Java, C#, GDScript, Luau.
3. **JIT** (*Just-In-Time*, compilation à la volée pendant l'exécution) **/ AOT** (*Ahead-Of-Time*, compilation à l'avance, avant l'exécution) : le bytecode est traduit en machine code (voir le JIT de la partie 2).

---

# 4.3 Lexer

Le lexer transforme le texte en **tokens** (type, texte, position). Les règles :

- **Maximal munch** : on prend le plus long token possible (`<=` et non `<` suivi de `=`). Il y a des exceptions à gérer : dans Vektor, `0..10` doit donner `0` puis `..`, et non le flottant `0.` suivi de `.10`.
- **Positions** (ligne, colonne) dans chaque token, pour des messages d'erreur utiles.
- **Mots-clés** : lire un identifiant, puis le chercher dans une table.
- **Vues** (`std::string_view`) sur le source plutôt que des copies : zéro allocation.

Sortie réelle de `vektor tokens` :

```
  1:1   'fn'           'fn'
  1:4   identifiant    'update'
  1:10  '('            '('
  1:11  identifiant    'pos'
  1:14  ':'            ':'
  1:16  identifiant    'vec4'
```

**Lien SIMD** : sur de gros fichiers, sauter les espaces, les commentaires et les identifiants se vectorise (voir [scan_text.cpp](https://github.com/johannphilippe/gtech_acceleration/blob/main/code/03_simd/scan_text.cpp), et simdjson). Pour des scripts de jeu de quelques Ko, le lexer n'est jamais le goulot. C'est un bon exercice, mais ce n'est pas une priorité.

---

# 4.4 Parser : descente récursive et Pratt

## Grammaire de Vektor

```
program   := function*
function  := "fn" IDENT "(" (param ("," param)*)? ")" ("->" type)? block
param     := IDENT ":" type
block     := "{" stmt* "}"
stmt      := "let" IDENT (":" type)? "=" expr ";"
           | "if" expr block ("else" (block | ifstmt))?
           | "while" expr block
           | "for" IDENT "in" expr ".." expr block
           | "return" expr? ";"
           | block
           | expr ("=" expr)? ";"
type      := "int" | "float" | "bool" | "vec4"
```

- **Instructions** : descente récursive classique, une fonction par règle.
- **Expressions** : **Pratt parsing** (Vaughan Pratt, *Top Down Operator Precedence*, POPL 1973). À chaque opérateur binaire correspond une *binding power*. On parse un préfixe, puis tant que l'opérateur suivant est « plus fort » que le minimum courant, on l'absorbe :

```cpp
ExprPtr expression(int min_bp = 1)
{
    ExprPtr left = prefix();                 // littéral, variable, appel, (expr), -x, !x
    for (;;)
    {
        Tok op = peek().kind;
        int bp = binding_power(op);          // || 1, && 2, == != 3, < <= > >= 4, + - 5, * / % 6
        if (bp < min_bp || bp == 0) break;
        advance();
        auto rhs = expression(bp + 1);       // bp + 1 : associativité à gauche
        left = make_binary(op, std::move(left), std::move(rhs));
    }
    return left;
}
```

Pratt parsing est un algorithme particulièrement élégant à connaître : il tient en une trentaine de lignes, gère nativement précédence et associativité, et s'étend facilement — postfix (`v.x`, appels), opérateurs ternaires, associativité à droite (`bp` au lieu de `bp + 1`). Référence très lisible : Bob Nystrom, [*Pratt Parsers: Expression Parsing Made Easy*](https://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/), et le chapitre 17 de *Crafting Interpreters*.

## Erreurs

Vektor s'arrête à la première erreur (exception avec position). Pour aller plus loin : la *panic mode recovery* (on saute jusqu'au prochain `;` ou `}`) permet de signaler plusieurs erreurs d'un coup. C'est un exercice bonus.

```
10_error_type.vk: 5:13: erreur: opérateur '+' invalide entre int et float (pas de conversion implicite : utilisez float(x) ou int(x))
15_error_syntax.vk: 4:5: erreur: attendu ';' après la déclaration, trouvé 'print'
```

---

# 4.5 AST et analyse sémantique

## Représenter l'AST : lien avec la partie 1

| Représentation | Avantages | Inconvénients |
|----------------|-----------|---------------|
| Classes + héritage + `virtual eval()` | naturel en C++, idéal pour débuter | une allocation par nœud, des pointeurs partout, des appels virtuels |
| `std::variant` + `std::visit` | pas d'héritage, type sûr | taille du variant = taille du plus gros nœud |
| Struct unique + `kind` + `unique_ptr` enfants (**Vektor**) | simple à parcourir avec un `switch` | reste « pointer-chasing » |
| **AST data-oriented** : nœuds dans un `std::vector`, enfants = **index 32 bits**, données en SoA | compact, *cache-friendly*, désallocation en bloc | moins lisible |

La dernière option est celle du compilateur Zig : Andrew Kelley, *A Practical Guide to Applying Data-Oriented Design*, Handmade Seattle 2021. **C'est la partie 1 appliquée au compilateur**, et un bon bonus pour un groupe avancé.

## Analyse sémantique

Le parser ne sait pas si `x` existe, ni si `a + b` a un sens. La sema (`sema.cpp`) :

1. **Résout les noms** avec une pile de portées (`vector<unordered_map<string, int>>`). Chaque déclaration reçoit un `local_id` unique.
2. **Type** chaque expression, sans conversion implicite : `int + float` est une erreur avec un conseil.
3. **Résout les appels** : fonction utilisateur (index), **fonction intégrée** (`sqrt`, `dot`, `vec4`... transformées en **opcodes**, pas en appels) ou erreur.
4. **Vérifie** les `return` (type, et « tous les chemins retournent » pour une fonction non `void`), l'existence de `main`, le nombre et le type des arguments.

**Pourquoi un typage statique dans un langage de script ?** Parce que **la VM n'a alors plus besoin de tester les types à l'exécution**. `a + b` devient `IADD`, `FADD` ou `VADD` dès la compilation. C'est la plus grande optimisation matérielle du projet : zéro branche de type dans la boucle chaude (voir 4.7).

---

# 4.6 Modèles d'exécution : tree-walking, pile, registres

Programme de référence (`showdown.cpp` *(distribué comme corrigé après les défis 2 et 5, voir exercices)*) :

```
s = 0; i = 0;
while (i < N) { s = s + i * 3 + 1; i = i + 1; }
```

## Tree-walking

```cpp
struct Binary : Node {
    BinOp op; NodePtr l, r;
    int64_t eval(int64_t* vars) const override {
        int64_t a = l->eval(vars), b = r->eval(vars);   // 2 appels virtuels + 2 déréférencements
        switch (op) { case BinOp::Add: return a + b; /* ... */ }
    }
};
```

Chaque itération visite **16 nœuds**. Chaque visite coûte un appel virtuel (branche indirecte), un saut vers un nœud alloué ailleurs dans le heap (cache) et un cadre de pile C++ (récursion).

## Stack VM (Lua 4, JVM, CPython, clox de *Crafting Interpreters*)

```
LOAD 0        ; s
LOAD 1        ; i
PUSHK 3
MUL
ADD
PUSHK 1
ADD
STORE 0       ; s = ...
```

Les opérandes sont **implicites** (le sommet de pile) : les instructions sont courtes (1 octet + opérandes), et le compilateur est très simple. En contrepartie, il faut **beaucoup d'instructions**, et chacune déplace des valeurs entre la pile et les variables.

## Register VM (Lua 5)

```
MUL  t, i, K(3)     ; t = i * 3
ADD  t, s, t        ; t = s + t
ADD  s, t, K(1)     ; s = t + 1
ADD  i, i, K(1)     ; i = i + 1   <- UNE instruction
```

Les opérandes sont **explicites** : des numéros de registres dans l'instruction. Les instructions sont plus grosses (32 bits), mais **bien moins nombreuses**, et les variables locales **sont** des registres, donc on ne copie rien.

## Registres VM vs registres matériels : ne pas confondre

**Question à anticiper, tout le monde se la pose en arrivant ici** : est-ce qu'une VM « à registres » alloue une arène mémoire, alignée, puis laisse le compilateur (celui qui compile la VM elle-même, ici MSVC/GCC) placer ces registres dans de **vrais** registres matériels ? Réponse courte : **non, pas directement**, et c'est justement ce qu'il faut désambiguïser.

**Ce qu'est concrètement un « registre » de VM** : une case dans un **tableau contigu** en mémoire. L'instruction `ADD t, s, t` ne contient donc pas trois registres CPU, mais **trois petits entiers** (indices dans ce tableau) : à l'exécution, `R[A(i)] = R[B(i)] + R[C(i)]` est un calcul d'adresse (`base + index * 8`) suivi d'un chargement, exactement comme un accès à un tableau C++ ordinaire.

```
registre VM "t"  =  I[3]  =  *(int64_t*)(base_I + 3 * 8)      <- une case dans un tableau en RAM
registre CPU rax =  la case physique "rax" du cœur             <- pas la même chose du tout
```

**Une seule allocation pour tout le programme, puis des fenêtres par appel.** Chez Vektor (`vm.cpp`), les deux banques — `istack` (`int64_t`) et `xstack` (`__m128`, **allouée alignée sur 16**, sinon `_mm_load_ps` crashe, partie 1) — sont allouées **une seule fois**, à la construction de la VM, assez grandes pour toute la profondeur d'appel possible :

```cpp
VM::VM(const Module& m, size_t size)
    : istack(size),
      xstack(::operator new[](size * sizeof(__m128), std::align_val_t{16})) { ... }
```

Un appel de fonction **ne réalloue rien** : il fait juste avancer un pointeur de base dans ce même tableau, exactement comme la pile C++ avance `rsp` sans jamais faire de `malloc` :

```cpp
// case CALL :
int64_t* ni = I + A(i);   // "registre 0" de l'appelé = une case plus loin dans LE MÊME tableau
__m128*  nx = X + B(i);
...
I = ni; X = nx;            // le frame de l'appelé est une FENÊTRE (register window) sur le tableau partagé
```

Chaque appel « fragmente » le tableau au sens où il en réserve une **tranche contiguë** pour ses propres variables, mais rien n'est coupé ni recopié — c'est de l'arithmétique de pointeur, pas une allocation.

**Le compilateur hôte ne « place » pas les 256 registres de la VM dans les 16 registres généraux du CPU** : il n'y a tout simplement pas la place (256 contre 16), et rien ne le lui demande — `I` est un tableau ordinaire à ses yeux. Ce que le compilateur *fait* mettre en vrais registres CPU, ce sont les quelques variables **de l'interpréteur lui-même** qui restent vivantes tout au long de la boucle chaude : le pointeur d'instruction `pc`, le pointeur de base `base_I`/`base_X`, le pointeur de constantes `k`. Ces **pointeurs** tiennent dans des registres physiques, pas le contenu du tableau qu'ils désignent (voir le tableau en 4.10, « état chaud en variables locales »).

**`pc` pointe dans un troisième tableau, encore différent.** Ni `istack`/`xstack` (les registres) ni `code` (le bytecode) ne sont la même chose, et il y en a même un quatrième : chez Vektor (`bytecode.hpp`), chaque `Function` possède son propre `std::vector<Instr> code` (le flux d'instructions, `pc` s'y promène) **et** ses propres pools de constantes séparés `k_int` / `k_float` / `k_vec` (un `LOADKI r, K[0]` va lire `k_int[0]`, pas `code`). Quatre familles de tableaux, quatre rôles, aucun partagé :

| Tableau | Contenu | Portée | Qui le pointe |
|---------|---------|--------|----------------|
| `code` | le bytecode (`uint32_t` par instruction) | un par fonction | `pc` |
| `k_int` / `k_float` / `k_vec` | pools de constantes | un par fonction | `KI` / `KF` / `KV` |
| `istack` / `xstack` | les registres (banque entière / SIMD) | un pour toute la VM, fenêtré par appel | `I` / `X` |

Sur un vrai CPU (von Neumann, partie 1), code et données partagent un seul espace d'adressage. Ici, au niveau de la VM, ils vivent dans des `std::vector` complètement distincts : c'est presque une séparation « Harvard », simplement parce que ce sont des allocations C++ indépendantes.

### Une instruction, en mémoire : qui lit quoi, qui écrit quoi

Rendons ça concret avec `s = s + i * 1000000;`, en supposant `s` déjà alloué dans `I[0]`, `i` dans `I[1]`, et un registre temporaire `I[2]` (allocation décidée par le compilateur, voir 4.9). Le compilateur ne peut **pas** utiliser `LOADI` (immédiat) pour la constante : `LOADI` encode sa valeur sur `sBx`, un **entier signé 16 bits** (-32768 à 32767, voir `bytecode.hpp`), et 1 000 000 déborde largement. Il doit donc la ranger dans le pool de constantes et émettre `LOADKI` à la place :

```asm
    loadki  r2, K[0]        ; temp = k_int[0]        (= 1 000 000)
    imul    r2, r1, r2      ; temp = i * temp
    iadd    r0, r0, r2      ; s = s + temp
```

`pc`, la base de `I` et la base de `KI` vivent dans de vrais registres CPU (voir plus haut). Voici, instruction par instruction, ce qui se passe réellement **en mémoire** (RAM/cache — pas les registres CPU eux-mêmes) :

| Instruction | FETCH | DECODE | EXECUTE — lit | EXECUTE — écrit |
|-------------|-------|--------|----------------|-------------------|
| `loadki r2, K[0]` | lit `code[pc]` (4 octets), `pc++` | décalages/masques sur le mot déjà en registre CPU (op=LOADKI, A=2, Bx=0) — **aucun accès mémoire** | `KI[0]` | `I[2]` |
| `imul r2, r1, r2` | lit `code[pc]`, `pc++` | idem, zéro accès mémoire | `I[1]`, `I[2]` | `I[2]` |
| `iadd r0, r0, r2` | lit `code[pc]`, `pc++` | idem | `I[0]`, `I[2]` | `I[0]` |

Trois idées à en retenir :

1. **Le FETCH est le seul accès mémoire garanti par instruction** : un `uint32_t` lu dans `code[]`. Le DECODE ne touche **jamais** la mémoire — ce ne sont que des `>>`/`&` sur une valeur déjà en registre CPU (`A()`, `B()`, `C()` dans `bytecode.hpp`).
2. **`code[]`, `KI[]` et `I[]` sont des tableaux séparés**, à des adresses potentiellement très éloignées. C'est pourquoi une instruction encode des **indices**, jamais des adresses : `KI[Bx]` et `I[A]` sont deux calculs d'adresse indépendants faits à l'exécution.
3. **`LOADI` (immédiat) vs `LOADKI` (pool de constantes)** : `LOADI r, 3` n'a **aucune** lecture supplémentaire, la valeur est *dans l'instruction elle-même*, déjà en registre CPU après le FETCH. `LOADKI` coûte une lecture mémoire de plus (`KI[Bx]`), précisément parce que la valeur ne tient pas dans les 16 bits disponibles. C'est le même compromis taille-d'instruction vs nombre-d'accès-mémoire qu'en x86 entre un immédiat embarqué (`mov eax, 42`) et une lecture `[rip + constante]` (partie 2).

### Pourquoi `Instr = uint32_t`, et pas `uint8_t` pour le seul opcode ?

Question naturelle : un opcode tient dans un octet (moins de 256 opcodes), alors pourquoi `using Instr = uint32_t;` ? Parce que le mot d'instruction **n'encode pas que l'opcode** : il embarque aussi les opérandes, opcode et registres au coude à coude dans le même mot de 32 bits :

```
bit :   31          24 23          16 15           8 7            0
        +-------------+-------------+-------------+-------------+
        |      C      |      B      |      A      |     OP      |    forme "ABC" (3 registres, ex. IADD)
        +-------------+-------------+-------------+-------------+
        |            Bx / sBx       |      A      |     OP      |    forme "ABx"/"AsBx" (1 registre + 1 valeur 16 bits, ex. LOADKI, JMP)
        +----------------------------+-------------+-------------+
```

Selon le format de l'opcode (`"iii"` vs `"iK"`/`"iI"`/`"J"`, voir l'X-macro en 4.9), les deux derniers octets sont lus soit comme deux registres séparés (`B()`, `C()`), soit comme **un seul** champ 16 bits (`Bx()`/`sBx()`). Un opcode ne tient que dans le premier octet (`op_of(i) = i & 0xFF`) : le reste du mot est déjà réservé aux opérandes.

**Pourquoi 32 bits précisément, ni moins ni plus ?**

- **8 bits** (opcode seul) ne fonctionnerait que pour une **stack VM** : là, les opérandes sont implicites (le sommet de pile), donc pas besoin de place pour eux dans l'instruction. Une register VM doit au contraire réserver de la place *dans l'instruction* pour des opérandes explicites (4.6) — elle est nécessairement plus large qu'un octet.
- **16 bits** donnerait `op(8) + A(8)` et plus aucune place pour `B`/`C` : impossible d'encoder `ADD r, r, r` à 3 opérandes explicites en une seule instruction, ce qui viderait l'intérêt même des registres.
- **32 bits** est le compromis choisi (comme Lua 5.0, voir plus bas) : assez large pour `op + 3 registres` (jusqu'à 255 chacun — c'est très exactement pourquoi `MAX_REGISTERS = 255`, un index de registre doit tenir dans un octet), **ou** `op + 1 registre + 1 valeur 16 bits` (immédiat, offset de saut, index de pool de constantes).
- **64 bits** permettrait d'embarquer un immédiat 32 bits complet sans jamais passer par un pool de constantes, mais **doublerait** l'empreinte du bytecode en cache L1i (partie 1) pour un cas rare — c'est justement le rôle du pool de constantes que d'absorber les valeurs trop grandes (`LOADKI` ci-dessus) sans agrandir *toutes* les instructions.

C'est exactement le design du papier Lua 5.0 cité plus bas, qui nomme ces trois formes **`iABC`**, **`iABx`** et **`iAsBx`** — toutes tenant elles aussi sur 32 bits, avec un découpage légèrement différent (`OP` 6 bits, `A` 8, `B`/`C` 9 chacun, donc jusqu'à 511 registres contre 255 pour Vektor). C'est aussi exactement le problème que résout ARM64 (2.8) avec ses instructions fixes de 4 octets : une constante trop grande pour tenir dans l'instruction part dans un pool littéral chargé en RIP-relatif, plutôt que d'agrandir l'instruction elle-même.

**Alors, qu'apporte vraiment le concept de « registre » à un interpréteur, si ce n'est pas un accès direct au matériel ?** Deux choses, toutes deux visibles dans les mesures ci-dessous et le papier Lua 5.0 :

1. **Moins de copies** : dans une stack VM, chaque variable locale doit être `PUSH`ée sur la pile avant usage et re`POP`ée après — deux accès mémoire et deux instructions par usage. Dans une register VM, une variable locale **est** une case du tableau `R[]`, utilisée directement en opérande : zéro copie.
2. **Moins d'instructions dispatchées** : une instruction register VM encode 3 opérandes explicites (`ADD a, b, c`), contre plusieurs instructions stack VM à un seul opérande implicite (le sommet de pile). Moins de dispatches, c'est moins de branches indirectes (partie 1, prédiction de branchement) — le vrai goulot d'un interpréteur (partie 4.8).

**Comment c'est mis en place concrètement ?** C'est le travail du **compilateur de la VM** (pas celui de la machine hôte) : à la compilation du *script*, `compiler.cpp` décide quelle case du tableau `R[]` représente quelle variable, avec un algorithme d'allocation très simple (pile LIFO de « sommet libre », détaillé en 4.9 « L'allocation de registres »). C'est un problème entièrement différent, et beaucoup plus simple, que l'allocation de registres d'un vrai compilateur (qui doit, elle, choisir parmi un nombre *fixe et petit* de registres matériels, avec *spill* vers la mémoire quand ils manquent — voir la remarque sur le JIT en 2.7).

## Mesures (Ryzen 9 8940HX, N = 50 000 000)

```
Pour N = 1000 : noeuds AST visités 16008 | instructions stack VM 17009 | instructions register VM 6005 (-65%)
Taille du bytecode : stack 86 octets | register 40 octets

                                    g++ 13 -O2        clang 18 -O2
1)  tree-walking (AST virtuel)       938 ms            1002 ms
2)  stack VM (switch)                659 ms             555 ms
3a) register VM (switch)             446 ms             283-308 ms
3b) register VM (computed goto)      357 ms             380 ms
3c) register VM (tail calls)           -                466 ms
```

## Ce que dit la littérature

**Le papier Lua 5.0** : R. Ierusalimschy, L. H. de Figueiredo, W. Celes, *The Implementation of Lua 5.0*, Journal of Universal Computer Science 11(7), 2005 ([PDF](https://www.lua.org/doc/jucs05.pdf)).

- « *For ten years (since 1993, when Lua was first released), Lua used a stack-based virtual machine [...] Since 2003, with the release of Lua 5.0, Lua uses a register-based virtual machine.* »
- « *Register-based code avoids several "push" and "pop" instructions that stack-based code needs to move values around the stack. Those instructions are particularly expensive in Lua, because they involve the copy of a tagged value.* »
- **Format** : instructions de 32 bits, **OP 6 bits, A 8 bits, B et C 9 bits** (ou Bx/sBx sur 18 bits). **35 instructions** (contre 49 pour la VM à pile de Lua 4.0).
- **RK(X)** : un opérande désigne un registre si `X < k` (k = 250), sinon la constante `K[X-k]`. `a = a + 1` compile en **une** instruction `ADD x x 250`.
- **Comparaisons** : `LT A B C` saute l'instruction suivante (un `JMP`) si le test ne correspond pas à A. L'interpréteur exécute les deux ensemble.
- **Appels** : *register window*. Les arguments sont évalués dans les registres libres suivants, qui deviennent les premiers registres de l'appelé.
- **Benchmarks** (Lua 4.0 → Lua 5.0, Pentium 4) : `sum` 1,23 s → **0,54 s (44 %)**, `fibo` 0,95 s → 0,69 s (73 %), `sieve` 0,93 s → 0,57 s (61 %).

La figure 8/9 du papier illustre parfaitement la différence :

```
local a,t,i          Lua 5.0 (registres)          Lua 4.0 (pile)
a=a+i                ADD      0 0 2               GETLOCAL 0 ; GETLOCAL 2 ; ADD ; SETLOCAL 0
a=a+1                ADD      0 0 250 ; 1         GETLOCAL 0 ; ADDI 1 ; SETLOCAL 0
a=t[i]               GETTABLE 0 1 2               GETLOCAL 1 ; GETINDEXED 2 ; SETLOCAL 0
```

**Lua 5.4** : 83 opcodes, opcode sur **7 bits**, champ `k` d'un bit, et des instructions spécialisées comme `OP_ADDI` (`R[A] := R[B] + sC`) et `OP_ADDK` (`R[A] := R[B] + K[C]`). Voir [lopcodes.h](https://www.lua.org/source/5.4/lopcodes.h.html).

**Stack vs Register, étude systématique** : Y. Shi, D. Gregg, A. Beatty, M. A. Ertl, *Virtual Machine Showdown: Stack Versus Registers*, VEE 2005 ([PDF](https://www.scss.tcd.ie/David.Gregg/papers/vee05-ShiGreggBeattyErtl.pdf)) :

> « *we eliminate an average of more than 47% of executed VM instructions, with the register machine bytecode size only 25% larger than that of the corresponding stack bytecode [...] the register architecture requires an average of 32.3% less time to execute standard benchmarks if dispatch is performed using a C switch statement. Even if more efficient threaded dispatch is available [...] the reduction in running time is still approximately 26.5%.* »

## Pourquoi c'est plus rapide : les explications matérielles

| Coût | Tree-walking | Stack VM | Register VM |
|------|--------------|----------|-------------|
| Branches indirectes par itération | 16 (virtuel) | 17 (dispatch) | 6 (dispatch) |
| Accès mémoire | nœuds épars dans le heap | code + pile + variables | code + registres contigus |
| Copies de valeurs | retours de fonctions | push/pop | aucune pour les locaux |
| Décodage | aucun | opcode + lecture d'opérandes (1 à 5 octets) | opcode + opérandes par **décalages** sur un mot de 32 bits |

## Et les systèmes « plus rudimentaires » ?

- **Machine à accumulateur** : un seul registre implicite (`LOAD`, `ADD x`, `STORE`). C'est le modèle des tout premiers ordinateurs et de *Human Resource Machine*.
- **Machine mémoire-à-mémoire** (QuakeC) : chaque instruction à trois adresses pointe dans un **tableau global** de valeurs. Très simple et assez rapide, mais sans vraie pile ni récursion propre.
- **Stack machine pure** (Forth, Uxn, clox) : le compilateur le plus simple, la VM la plus compacte.
- **Tree-walking** : suffisant pour un langage de configuration, pas pour une boucle de jeu.

Une nuance importante à garder en tête : les registres ne sont pas « toujours meilleurs » en absolu. La stack VM produit un code plus compact (utile en mémoire contrainte, ou pour du bytecode transmis sur le réseau) et son compilateur est trivial à écrire. Le vrai message à retenir est **« moins d'instructions dispatchées et moins de copies = moins de branches indirectes et moins d'accès mémoire »** — soit exactement les leçons des parties 1 et 2 appliquées ici.

---

# 4.7 Représenter les valeurs

## Tagged union (Lua, Vektor si dynamique)

```cpp
struct Value {              // 16 octets
    uint8_t type;           // tag + 7 octets de padding
    union { double number; bool boolean; Object* obj; };
};
```

Le papier Lua 5.0 le dit : « *the size of a TObject is 12 bytes (or 16 bytes, if doubles are aligned on 8-byte boundaries) and so copying a value requires copying 3 (or 4) machine words* ». **Chaque opération teste le tag** avant de calculer.

## NaN-boxing / NaN-tagging

**Attention, ce n'est pas le même NaN que le piège de la partie 3.3.** Là-bas, un NaN est un **accident de calcul** (`0.0/0.0`, `sqrt` négatif...) qu'il faut détecter et éviter. Ici, c'est l'inverse : on **choisit délibérément** de représenter des valeurs qui n'ont *rien à voir* avec des flottants — des entiers, des booléens, des pointeurs — en réutilisant l'espace binaire qu'IEEE 754 réserve à NaN, parce que cet espace est immense et presque toujours vide en pratique.

### Pourquoi cet espace existe

Un `double` (64 bits) se découpe en signe (1 bit) + exposant (11 bits) + mantisse (52 bits). Il vaut **NaN** exactement quand l'exposant est à 1 partout (`0x7FF`) et la mantisse est **non nulle** — l'exposant à 1 partout avec une mantisse **nulle**, c'est `±∞`, pas NaN :

```
 63  62          52 51                                              0
 +---+-------------+----------------------------------------------+
 | S |   exposant   |                    mantisse                  |
 +---+-------------+----------------------------------------------+
       11111111111    n'importe quoi SAUF tout à zéro   <- NaN si exposant tout à 1 et mantisse != 0
```

Ça fait 2 × (2^52 − 1) motifs binaires distincts qui décodent tous en « pas un nombre » — largement plus qu'il n'en faut pour coder un tag (quelques bits) et un payload (le reste). Et comme x86-64/ARM64 n'utilisent que 48 bits d'adresse virtuelle avec des **adresses canoniques** (partie 1, TLB) — les bits hauts d'un vrai pointeur sont prévisibles —, un pointeur complet tient dans la mantisse avec de la place en trop pour un tag.

### Pourquoi s'embêter avec ça (le lien avec la `Value` ci-dessus)

Le `struct Value` tagged union vu plus haut fait **16 octets** (tag + padding + union de 8), et chaque opération doit **tester le tag** avant de calculer. Une valeur NaN-boxée tient en **8 octets** (la taille d'un `double` tout court), et un flottant s'y lit **sans aucun décodage** : c'est le chemin le plus chaud (le calcul numérique) qui devient gratuit, au prix d'un décodage pour tout le reste. C'est un pari délibéré : la plupart du temps chaud d'un script de jeu est arithmétique.

### Deux variantes, à ne pas confondre

**1. NaN boxing par masquage de pointeur** (JavaScriptCore, et le chapitre optionnel *NaN Boxing* de *Crafting Interpreters*, en référence 4.13) : on traite la valeur comme **un seul entier 64 bits**. Un motif de base (`QNAN`, un NaN silencieux canonique) plus le bit de signe marquent « ceci est un objet, pas un double » ; quelques bits bas de la mantisse encodent un petit tag (`nil`, `true`, `false`...) ; le reste porte un pointeur 48 bits ou un entier. Schéma illustratif (à revérifier au compilateur avant de vous en servir tel quel — reprend la forme du livre, non retesté ici) :

```cpp
using Value = uint64_t;

constexpr uint64_t QNAN     = 0x7ffc000000000000ULL;   // NaN silencieux + 2 bits de marge
constexpr uint64_t SIGN_BIT = 0x8000000000000000ULL;
constexpr uint64_t TAG_NIL = 1, TAG_FALSE = 2, TAG_TRUE = 3;

inline bool   is_number(Value v) { return (v & QNAN) != QNAN; }                 // pas dans l'espace réservé -> vrai double
inline bool   is_obj(Value v)    { return (v & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT); }
inline Value  number_val(double d) { Value v; std::memcpy(&v, &d, 8); return v; }  // std::bit_cast en C++20
inline double as_number(Value v)   { double d; std::memcpy(&d, &v, 8); return d; }
inline Value  obj_val(void* p)   { return SIGN_BIT | QNAN | reinterpret_cast<uint64_t>(p); }
inline void*  as_obj(Value v)    { return reinterpret_cast<void*>(v & ~(SIGN_BIT | QNAN)); }
```

**2. NaN tagging par demi-mots** (LuaJIT, Mike Pall — voir le « Zoom : LuaJIT » en 4.8) : on traite la valeur comme **deux mots de 32 bits** (`hi:lo`, l'agencement mémoire naturel d'un `double`). Le mot haut, s'il tombe dans la plage réservée à NaN une fois réinterprété comme le haut d'un double, sert de **tag** (une petite poignée de valeurs réservées, une par type) ; le mot bas porte directement un pointeur ou un `int32_t`, **sans masquage ni décalage** — juste une lecture. C'est particulièrement direct en LuaJIT 32 bits, où un pointeur tient exactement dans le mot bas ; en 64 bits, LuaJIT a besoin d'un mode dédié (`GC64`) pour les pointeurs plus larges.

### Le piège qui relie les deux parties du cours

**C'est là que revient le NaN « accidentel » de la partie 3.3.** Un calcul flottant légitime peut produire un vrai NaN (`sqrt(-1)`, `0.0/0.0`...). Si ce NaN calculé tombe *par hasard* dans le motif de bits réservé aux tags (`QNAN | SIGN_BIT | ...`), le décodeur le prendra pour un pointeur ou un entier valide — un **pointeur sauvage**, pas juste un résultat faux. La protection : **ne jamais faire confiance à la sortie brute du FPU** dans un système NaN-boxé. Deux approches : (a) **canoniser** systématiquement tout NaN produit par un calcul flottant vers un motif fixe qui n'entre jamais en collision avec l'espace des tags (souvent en testant `std::isnan` après chaque opération sensible), ou (b), plus simple et plus sûr, **choisir l'espace des tags dans une zone que le FPU ne produit jamais naturellement** (c'est le rôle du bit de signe dans le schéma JSC ci-dessus : le FPU ne met quasiment jamais ce bit à 1 sur un NaN qu'il génère lui-même). C'est un vrai piège de sécurité mémoire, pas qu'un détail de correction.

**Portabilité** : `std::bit_cast` (C++20, partie 1) plutôt que `reinterpret_cast` pour rester dans les clous du *strict aliasing* (1.5) ; attention aux adresses non canoniques sur certaines plateformes ; technique non portée nativement au 32 bits (moins de place) ni à toutes les architectures. Utilisée par LuaJIT, JavaScriptCore (WebKit) et Wren.

## Registres typés (choix de Vektor)

Vektor est **typé statiquement**. Aucune valeur n'a donc besoin de tag. La VM possède **deux banques de registres par frame**, exactement comme le CPU :

| Banque Vektor | Contenu | Équivalent CPU |
|---------------|---------|----------------|
| `r0`, `r1`... : `int64_t` | `int`, `bool` | `rax`, `rbx`... (GPR) |
| `x0`, `x1`... : `__m128` aligné 16 | `float` (voie 0), `vec4` | `xmm0`, `xmm1`... |

`float` scalaire dans un registre 128 bits ? C'est **exactement ce que fait l'ABI x64** : les flottants scalaires vivent dans `xmm` et se calculent avec `addss`. Dans la VM, `FADD` fait `_mm_add_ss` et `VADD` fait `_mm_add_ps`. **Un opcode = une instruction SIMD.**

Luau (Roblox) a fait un choix proche pour son type `vector` : une valeur native de 3 floats SIMD, sans allocation, avec des opérations dans la VM (« *essentially means native 3-wide SIMD support* », doc Luau).

---

# 4.8 Le dispatch

La boucle `fetch → decode → execute` de la VM est une **branche indirecte** exécutée des centaines de millions de fois. C'est là que se jouent les performances d'un interpréteur.

## 1. `switch` (portable, MSVC)

```cpp
for (;;) {
    Instr i = *pc++;
    switch (op_of(i)) {
    case Op::IADD: I[A(i)] = I[B(i)] + I[C(i)]; break;
    // ...
    default: __assume(0);          // MSVC : supprime le test de bornes du switch
    }
}
```

Le compilateur génère une jump table (vu en partie 2), **un seul** site de saut indirect partagé par tous les opcodes, précédé d'un test de bornes (supprimé par `__assume(0)` / `std::unreachable()`).

## 2. Computed goto / threaded code (GCC, Clang, pas MSVC)

```cpp
static const void* labels[] = { &&L_MOVI, &&L_MOVX, /* ... */ };
#define DISPATCH() do { i = *pc++; goto *labels[uint8_t(i)]; } while (0)
L_IADD: I[A(i)] = I[B(i)] + I[C(i)]; DISPATCH();
```

**Un site de saut indirect par opcode** : le prédicteur de branchement apprend alors des séquences (« après `ILT` vient souvent `JMPF` »). CPython, Ruby et Lua (sous GCC) l'utilisent. **MSVC ne supporte pas les *labels as values*.**

## 3. Tail calls (Clang `[[clang::musttail]]`, MSVC `[[msvc::musttail]]`)

Chaque opcode devient une **fonction**, qui se termine par un appel *garanti* en tail call vers le handler suivant (un `jmp`, pas un `call`) :

```cpp
#define NEXT() [[msvc::musttail]] return handlers[TOP(*pc)](pc, k, R)
static int64_t h_ADD(const uint32_t* pc, const int64_t* k, int64_t* R)
{ uint32_t i = *pc++; R[TA(i)] = RK(TB(i)) + RK(TC(i)); NEXT(); }
```

Avantages : de petites fonctions que le compilateur optimise bien, et `pc`, `R`, `k` restent dans des registres CPU (convention d'appel). CPython 3.14 a introduit un interpréteur tail-call (Clang 19). Sous Windows, avec **Visual Studio 2026 / MSVC 14.50** et `[[msvc::musttail]]`, le blog de Ken Jin annonce **15 à 20 % de gain** (moyenne géométrique pyperformance).

**Pièges MSVC vérifiés en écrivant `showdown.cpp`** (erreur `C4737: Unable to perform required tail call`) :

- l'attribut est **expérimental**, **x64 uniquement**, et exige `/O2` ;
- `do { [[msvc::musttail]] return f(...); } while (0)` **échoue** : le `return` doit être la dernière instruction ;
- un **appel à une fonction `inline`** dans le handler (par exemple `A(i)` écrit en fonction) **fait échouer** le tail call, même si la fonction est finalement inlinée. **Il faut des macros.**

## Mesures et folklore

Sur notre VM à registres : GCC `switch` 446 ms → computed goto 357 ms, **mais** Clang `switch` 283 ms, **plus rapide** que son computed goto (380 ms) et que ses tail calls (466 ms).

C'est exactement la conclusion de E. Rohou, B. N. Swamy, A. Seznec, *Branch Prediction and the Performance of Interpreters — Don't Trust Folklore* (CGO 2015) : les prédicteurs modernes (type ITTAGE) prédisent très bien le `switch` unique, et « l'avantage » du threaded code, énorme en 2003 (M. A. Ertl, D. Gregg, *The Structure and Performance of Efficient Interpreters*, JILP 2003), s'est beaucoup réduit. Le message à retenir : **mesurez, sur votre compilateur et votre CPU**. Et avant de toucher au dispatch, réduire le **nombre** d'instructions (superinstructions, constantes en opérandes) est en général plus rentable.

## Superinstructions et spécialisation

- `IADDI r, r, imm8` au lieu de `LOADI t, 1` + `IADD r, r, t` : c'est le `ADDI` de Lua 5.4. Vektor le génère pour `i = i + 1` et pour les boucles `for`.
- Comparaison + saut fusionnés (`JLT a, b, offset`).
- Constantes en opérandes (RK) : évite le `loadki r3, 50000000` que Vektor recharge **à chaque tour** de `while`.
- *Quickening* / *inline caching* : réécrire l'instruction à l'exécution une fois le type observé (CPython 3.11+). Ça ne sert à rien dans Vektor, puisque le typage est statique.

## Zoom : LuaJIT

LuaJIT (Mike Pall, 2005) mérite un détour, parce qu'il illustre **deux** idées du cours en même temps : un interpréteur extrêmement soigné, et un JIT construit par-dessus. À ne pas confondre : « LuaJIT » désigne l'ensemble, mais les deux moitiés répondent à des questions différentes.

**1. L'interpréteur.** Même sans jamais compiler la moindre ligne à la volée, l'interpréteur seul de LuaJIT est déjà l'un des plus rapides qui existent pour un langage dynamique — souvent aussi rapide que du bytecode Lua 5.1 exécuté par un JIT plus naïf. Le bytecode est **register-based**, largement compatible avec celui de Lua 5.1 (35+ opcodes, héritier direct du modèle vu plus haut). Ce qui le distingue :

- **Écrit entièrement en assembleur**, un par plateforme (x86, x64, ARM, ARM64, MIPS, PPC...), généré via **DynASM**, un préprocesseur maison de Mike Pall qui permet d'écrire de l'ASM avec des macros et des labels dans un fichier `.dasc`, assemblé en table d'octets à la compilation de LuaJIT lui-même. C'est un « assembleur maison » au sens de la partie 2, mais utilisé pour construire l'interpréteur plutôt qu'un langage.
- **Dispatch en threaded code pur** (voir plus haut) : chaque opcode se termine par un saut direct vers le suivant, sans jamais repasser par une boucle centrale — l'idée du computed goto, poussée au niveau de l'assembleur, avec un contrôle total sur l'allocation des registres CPU (contrairement à un `switch` en C, où c'est le compilateur qui décide).
- **NaN-tagging** pour la représentation des valeurs (voir 4.7) : c'est justement la référence historique de cette technique.

**2. Le JIT à traces (*tracing JIT*).** Une fois qu'une boucle est identifiée comme « chaude » (exécutée assez de fois), LuaJIT enregistre la **trace** : la séquence linéaire d'opérations réellement exécutée lors d'un passage (et non le graphe de contrôle complet de la fonction, à la différence d'un *method JIT* comme le C2 de la JVM ou TurboFan de V8). Cette trace est :

- traduite dans une **IR** (*Intermediate Representation*) interne en forme **SSA** (*Static Single Assignment* : chaque variable n'est affectée qu'une seule fois, ce qui simplifie énormément les optimisations d'un compilateur) ;
- optimisée (élimination de code mort, *allocation sinking* — éviter d'allouer un objet qui ne « s'échappe » jamais de la trace, propagation de constantes...) ;
- compilée en **machine code natif**, avec de vrais registres CPU cette fois (allocation de registres classique, la différence avec la question précédente) ;
- protégée par des **guards** : des tests rapides qui vérifient que les hypothèses de la trace (types observés, branches prises) restent vraies ; si un guard échoue, on retombe dans l'interpréteur (*trace exit*), qui peut éventuellement enregistrer une nouvelle trace pour ce chemin.

**Pourquoi c'est pertinent pour le projet** : même si un JIT est hors périmètre (voir 4.12), l'interpréteur seul de LuaJIT est la preuve qu'on peut aller très loin sans jamais générer de code à l'exécution — c'est exactement la philosophie de ce cours (« l'interpréteur rapide est le vrai sujet »). Les techniques de dispatch en assembleur pur et le NaN-tagging sont, elles, directement transposables à un groupe très motivé, en bonus.

Mike Pall documente ses choix dans des messages très denses sur la liste de diffusion `lua-l`, jamais dans un vrai papier — c'est une lecture exigeante mais unique en son genre (référence en 4.13). La page [*LuaJIT 2.0 IR*](https://luajit.org/ext_ir.html) et [*How LuaJIT Works*](https://web.archive.org/web/2020/https://wiki.luajit.org/) (wiki archivé) sont plus digestes pour une première approche.

---

# 4.9 L'interpréteur de référence : Vektor

## Le langage

```rust
fn update(pos: vec4, vel: vec4, dt: float) -> vec4 {
    return pos + vel * dt;
}

fn main() {
    let pos = vec4(0.0, 10.0, 0.0, 1.0);
    let vel = vec4(1.0, 0.0, 0.0, 0.0);
    let hits = 0;
    for frame in 0..3 {
        pos = update(pos, vel, 0.016);
        if pos.x > 0.03 && hits < 10 {
            hits = hits + 1;
        }
    }
    print(pos);     // (0.048, 10, 0, 1)
    print(hits);    // 2
}
```

- Types : `int` (64 bits), `float` (32 bits), `bool`, `vec4`. Pas de conversion implicite.
- Instructions : `let`, affectation, `if`/`else if`/`else`, `while`, `for i in a..b`, `return`, blocs.
- Opérateurs : arithmétique, comparaisons, `&&`/`||` avec court-circuit, `vec4 * float`, `v.x/y/z/w`.
- Intégrées : `print`, `vec4(a,b,c,d)` / `vec4(s)`, `float()`, `int()`, `sqrt`, `abs`, `floor`, `sin`, `cos`, `min`, `max`, `dot`, `dot3`, `cross`, `length`, `normalize`.

## Le bytecode (sortie réelle de `vektor dis`)

```asm
func update(vec4, vec4, float) -> vec4   frame(r=0, x=5)
    vscale  x4, x1, x2          ; vel * dt   -> mulps + shuffle
    vadd    x3, x0, x4          ; pos + ...  -> addps
    retx    x3
    ret
end

func main()   frame(r=5, x=5)
    loadkv  x0, (0.0, 10.0, 0.0, 1.0)     ; vec4 de littéraux : constant folding
    loadkv  x1, (1.0, 0.0, 0.0, 0.0)
    loadi   r0, 0                          ; hits
    loadi   r1, 0                          ; frame
    loadi   r2, 3                          ; borne cachée du for
L5:
    ilt     r3, r1, r2
    jmpf    r3, L22
    movx    x2, x0                         ; fenêtre d'appel : arguments dans x2, x3, x4
    movx    x3, x1
    loadkf  x4, 0.0160000008
    call    r3, x2, update                 ; résultat dans x2
    movx    x0, x2
    vget    x2, x0, 0                      ; pos.x
    loadkf  x3, 0.0299999993
    flt     r3, x3, x2                     ; 0.03 < pos.x
    jmpf    r3, L18                        ; court-circuit du &&
    loadi   r4, 10
    ilt     r3, r0, r4
L18:
    jmpf    r3, L20
    iaddi   r0, r0, 1                      ; superinstruction
L20:
    iaddi   r1, r1, 1
    jmp     L5
L22:
    printv  x0
    printi  r0
    ret
end
```

## Architecture du code

| Fichier | Rôle | Lignes |
|---------|------|--------|
| `common.hpp` | positions, erreurs, types, banque de registre d'un type | ~55 |
| `bytecode.hpp` | **X-macro des opcodes**, encodage 32 bits, `Function`, `Module` | ~170 |
| `lexer.cpp` | tokens | ~130 |
| `parser.cpp` | descente récursive + Pratt | ~280 |
| `sema.cpp` | portées, types, builtins, retours | ~300 |
| `compiler.cpp` | allocation des registres façon Lua, émission, patch des sauts | ~450 |
| `vm.cpp` | boucle d'interprétation (switch **ou** computed goto), opcodes SSE | ~270 |
| `asm.cpp` | **assembleur maison** et désassembleur | ~360 |
| `main.cpp` | CLI + testeur (`// expect:`) | ~160 |

*(Fichiers de `vektor/`, distribués progressivement comme corrigés au fil des exercices — voir l'en-tête du chapitre.)*

```
vektor run    script.vk      compile et exécute
vektor dis    script.vk      désassemble
vektor asm    prog.vasm      assemble et exécute un programme écrit à la main
vektor tokens script.vk      affiche les tokens
vektor bench  script.vk      mesure (avec -DVK_COUNT_INSTRUCTIONS=ON : compte les instructions)
vektor test   scripts/       tests : sortie attendue + aller-retour dis -> asm -> bytecode identique
```

Options CMake : `-DVK_COMPUTED_GOTO=ON` (GCC/Clang), `-DVK_COUNT_INSTRUCTIONS=ON`.

## L'X-macro : une liste, quatre usages

```cpp
#define VK_OPCODES(X)            \
    X(IADD,   "iii")             \
    X(LOADKF, "xF")              \
    X(JMPF,   "iJ")              \
    ...

enum class Op : uint8_t {              // 1) l'enum
#define X(name, fmt) name,
    VK_OPCODES(X)
#undef X
    COUNT
};

inline constexpr const char* op_names[] = {     // 2) les noms (désassembleur)
#define X(name, fmt) #name,
    VK_OPCODES(X)
#undef X
};
// 3) op_formats[] : même principe avec "fmt" (assembleur)
// 4) labels[]     : même principe avec &&L_##name (computed goto, dans vm.cpp)
```

Le **format d'opérandes** (`i` registre entier, `x` registre SIMD, `J` saut, `F` constante float...) suffit à l'assembleur et au désassembleur pour traiter **tous** les opcodes sans code spécifique.

## L'allocation de registres (compiler.cpp)

C'est le cœur du modèle Lua :

1. Chaque banque a un **sommet libre** (`free_i`, `free_x`).
2. `let x = expr;` : on alloue le registre au sommet, on compile `expr` **directement dedans**, puis on lie `x`.
3. Une expression intermédiaire alloue un temporaire au sommet, qu'on **libère dès l'instruction émise** (LIFO, pas de fragmentation).
4. **Une variable locale utilisée en opérande n'est jamais copiée** : `expr_any(Var)` renvoie son registre. Ainsi `a = b + c` donne `IADD ra, rb, rc`.
5. **Appel** : les arguments sont évalués dans les registres libres suivants ; `CALL rA, xB, f` décale la base des registres (`I += A; X += B`) ; le résultat revient dans le premier registre de la fenêtre.
6. **Piège d'aliasing** : `ok = cond && ok;` compilé naïvement écrit `cond` dans `ok` **avant** de relire `ok`. Le compilateur détecte ce cas (`reads_register`) et passe par un temporaire.

## Assembleur maison (`asm.cpp`)

```asm
; Programme écrit directement en assembleur Vektor (scripts/hand_written.vasm)
func seglen(vec4, vec4) -> float
    vsub    x2, x1, x0
    vlen3   x2, x2
    retx    x2
end

func main()
    loadi   r0, 0               ; somme
    loadi   r1, 1               ; i
    loadi   r2, 11
loop:
    ilt     r3, r1, r2
    jmpf    r3, done            ; label défini PLUS BAS : d'où les deux passes
    imul    r4, r1, r1
    iadd    r0, r0, r4
    iaddi   r1, r1, 1
    jmp     loop
done:
    printi  r0                  ; 385
    ret
end
```

- **Passe 0** : collecter les noms de fonctions (appels vers l'avant).
- **Passe 1** (par fonction) : associer chaque label à un index d'instruction.
- **Passe 2** : encoder. Pour chaque opérande, le format dit quoi parser (registre, entier, label, constante) ; les pools de constantes sont dédupliqués ; la taille de frame est déduite des registres utilisés.
- **Validation** : registres < 255, sBx dans [-32768, 32767], labels et fonctions connus, nombre d'opérandes.
- **Test d'aller-retour** : `compile → disassemble → assemble` doit redonner un bytecode **identique au bit près**. C'est la meilleure garantie de cohérence entre assembleur et désassembleur.

## Mesures Vektor

```
bench/sum.vk (même boucle que showdown, 50 M itérations, 9 instructions / tour)
    switch          353 ms
    computed goto   321 ms
    instructions exécutées : 450 000 007

bench/particles.vk (balle qui rebondit, 2 M de pas en vec4)
    switch           24-29 ms
    computed goto    27 ms
    instructions exécutées : 30 721 544
```

---

# 4.10 Optimisations matérielles de la VM

Ce qui fait le lien avec **tout** le cours. Chaque point est une piste de projet :

| Optimisation | Partie du cours | Dans Vektor |
|--------------|-----------------|-------------|
| Instructions de taille fixe (32 bits), décodage par décalages | 2 (encodage) | ✅ |
| Registres contigus, `__m128` alignés 16 | 1 (alignement, cache) | ✅ |
| État chaud (`pc`, `I`, `X`, `K`) en variables locales → vrais registres CPU | 2 (registres) | ✅ |
| Typage statique → opcodes spécialisés, zéro test de tag | 1, 2 (branches) | ✅ |
| Opcodes = instructions SIMD (`FADD`=`addss`, `VADD`=`addps`) | 3 | ✅ |
| `__assume(0)` / computed goto / tail calls | 2 (jump tables) | ✅ switch + goto |
| Superinstructions (`IADDI`), constant folding (`LOADKV`) | 4 | ✅ partiel |
| Constantes en opérandes (RK), comparaison + saut fusionnés | 4 | ❌ exercice |
| **Exécution par lot** (SoA × N entités) | 1 (SoA), 3 (SIMD) | ❌ démo séparée, projet |
| CPU dispatch : table d'opcodes AVX2 si disponible | 3 | ❌ projet |
| JIT template (partie 2, `jit.cpp`) | 2 | ❌ bonus |
| Bytecode binaire (header, endianness, `bit_cast`) | 1 | ❌ projet |

## L'exécution par lot : la démo qui relie tout

[batch_demo.cpp](https://github.com/johannphilippe/gtech_acceleration/blob/main/code/04_interpreteur/batch_demo.cpp). Un script de comportement de particule (17 instructions), exécuté sur **100 000 entités** pendant 100 frames :

```
(a) interprétation par entité             234.971 ms     17 x 100 000 dispatches par frame
(b) interprétation par lot (auto-vec)      39.873 ms     17 dispatches par frame, boucles vectorisées  (-O3 : 20.8 ms)
(c) interprétation par lot (AVX2)          10.737 ms     x22
    somme des y = 199772.514  (identique pour les trois)
```

**Principe** : chaque registre de la VM devient un **tableau de N valeurs** (SoA). Chaque instruction est une boucle SIMD sur N éléments. Le coût du dispatch est **amorti** sur N. **Contrainte** : pas de saut dépendant des données. Les `if` deviennent des **masques** (`LT` → masque, `SELECT` → `blendv`), comme dans un shader.

C'est le modèle des **shaders** (GPU), d'**ISPC**, des graphes audio (traitement par blocs de 64 à 1024 échantillons, si vous suivez le cours audio) et des *systems* d'un **ECS**.

C'est **la** fonctionnalité qui justifie de combiner SIMD et interpréteur dans le même projet : implémenter un mode batch pour un sous-ensemble *branchless* de votre langage (particules, audio, post-process CPU, boids) touche aux trois parties du cours à la fois.

---

# 4.11 Pièges et questions fréquentes

- **« Mon parser boucle à l'infini »** : un `while (!check(RBrace))` qui n'avance pas sur un token inattendu. Il faut toujours tester `End`.
- **« `for i in 0..10` ne marche pas »** : le lexer lit `0.` comme un flottant. Il faut regarder deux caractères en avant.
- **« Les sauts partent n'importe où »** : offset relatif calculé depuis la mauvaise instruction (la convention est `pc` **après** l'instruction de saut : `cible - (at + 1)`).
- **« Récursion : les variables de l'appelant sont écrasées »** : la fenêtre d'appel commence **sous** des registres encore utilisés (mauvais sommet libre).
- **« Crash dans `_mm_load_ps` »** : pool de constantes `vec4` non aligné. Vektor utilise `alignas(16)` sur `Vec4Const`.
- **« Le switch est plus lent en Debug »** : toujours mesurer en Release. En Debug MSVC, la VM est 10 à 30 fois plus lente.
- **« Pourquoi pas de GC ? »** (*Garbage Collector*, ramasse-miettes : récupération automatique de la mémoire des objets devenus inutilisés) : hors périmètre du cours. Vektor n'a que des types valeur. Des chaînes ou des tableaux imposeraient une stratégie mémoire (arena par frame, comptage de références, GC).
- **« Faut-il un JIT ? »** : non. L'interpréteur rapide est le vrai sujet. Le JIT template de la partie 2 est un bonus.

---

# 4.12 Références

**Livres**

- Robert Nystrom, [*Crafting Interpreters*](https://craftinginterpreters.com/), 2021 : gratuit en ligne. **Référence principale de ce cours.** Partie II : tree-walker Java ; partie III : **clox**, une stack VM en C (dispatch, NaN-boxing, GC).
- Robert Nystrom, [*Game Programming Patterns*](https://gameprogrammingpatterns.com/), chapitre *Bytecode*
- Thorsten Ball, *Writing an Interpreter in Go* et *Writing a Compiler in Go*
- Aho, Lam, Sethi, Ullman, *Compilers: Principles, Techniques, and Tools* (Dragon Book)
- Keith Cooper, Linda Torczon, *Engineering a Compiler*

**Papiers**

- R. Ierusalimschy, L. H. de Figueiredo, W. Celes, *The Implementation of Lua 5.0*, J.UCS 2005 : [PDF](https://www.lua.org/doc/jucs05.pdf)
- Kein-Hong Man, *A No-Frills Introduction to Lua 5.1 VM Instructions*, 2006
- Y. Shi, D. Gregg, A. Beatty, M. A. Ertl, *Virtual Machine Showdown: Stack Versus Registers*, VEE 2005 : [PDF](https://www.scss.tcd.ie/David.Gregg/papers/vee05-ShiGreggBeattyErtl.pdf)
- M. A. Ertl, D. Gregg, *The Structure and Performance of Efficient Interpreters*, JILP 2003
- E. Rohou, B. N. Swamy, A. Seznec, *Branch Prediction and the Performance of Interpreters — Don't Trust Folklore*, CGO 2015
- V. Pratt, *Top Down Operator Precedence*, POPL 1973

**Sources à lire**

- Lua 5.4 : [lvm.c](https://www.lua.org/source/5.4/lvm.c.html), [lopcodes.h](https://www.lua.org/source/5.4/lopcodes.h.html), [lcode.c](https://www.lua.org/source/5.4/lcode.c.html) (allocation des registres)
- [Luau](https://github.com/luau-lang/luau) et sa page [*How we make Luau fast*](https://luau.org/performance)
- [Wren](https://github.com/wren-lang/wren) : petite VM très lisible, NaN-tagging, computed goto
- CPython `Python/ceval.c` et `Python/generated_cases.c.h`
- [LuaJIT](https://luajit.org/) et [*LuaJIT 2.0 Intermediate Representation*](https://luajit.org/ext_ir.html) : le JIT à traces

**Articles et conférences**

- Bob Nystrom, [*Pratt Parsers: Expression Parsing Made Easy*](https://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/)
- Ken Jin, [*Python 3.15's interpreter for Windows x86-64 should hopefully be 15% faster*](https://fidget-spinner.github.io/posts/no-longer-sorry.html) : tail calls MSVC
- Microsoft, [`[[msvc::musttail]]`](https://learn.microsoft.com/en-us/cpp/cpp/attributes#msvcmusttail)
- Andrew Kelley, *A Practical Guide to Applying Data-Oriented Design*, Handmade Seattle 2021
- Mike Pall, messages sur la mailing list lua-l concernant l'interpréteur LuaJIT 2 en assembleur (2011)
