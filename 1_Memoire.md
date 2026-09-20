---
title: 1 - Mémoire, caches, alignement, registres
author: Johann Philippe
---

# 1. Mémoire, caches, alignement, registres

> Ce cours porte sur l'assembleur, le SIMD et l'écriture d'un petit interpréteur — trois façons différentes d'exploiter le matériel. On commence par la mémoire, parce que c'est **le** problème de performance n°1 dans un moteur, et parce que tout le reste (ASM, SIMD, VM à registres) n'a de sens qu'une fois qu'on a compris pourquoi.
> Code de démonstration : [code/01_memoire](https://github.com/johannphilippe/gtech_acceleration/tree/main/code/01_memoire). Exercices : [exercices/1_Memoire.md](exercices/1_Memoire.md). Les mesures citées ont été faites sur un Ryzen 9 8940HX (Zen 4), avec g++ 13 en `-O2` ; sous MSVC `/O2`, les ordres de grandeur sont les mêmes.

## Objectifs de la partie

À la fin, vous devez pouvoir expliquer :

1. Pourquoi **la mémoire est plus lente que le CPU**, et pourquoi c'est *le* problème de performance n°1 dans un moteur.
2. Ce que sont la **hiérarchie mémoire** et les **caches**, la *cache line*, et la **localité** spatiale et temporelle.
3. Comment une `struct` est disposée en mémoire : **alignement**, **padding**, `alignof`, `alignas`, allocation alignée.
4. Ce qu'est un **registre** (préambule à l'ASM), et pourquoi une VM « à registres » s'appelle ainsi.
5. Ce que sont **AoS et SoA**, et pourquoi le data-oriented design est apparu dans le jeu vidéo.

Gardez une phrase en tête tout au long de ce cours, elle en est le fil rouge : **le matériel a des préférences, et le code rapide est celui qui les respecte**. La mémoire, l'ASM, le SIMD puis l'interpréteur ne sont que quatre angles différents sur cette même idée.

---

# 1.1 Préambule historique

## L'architecture de von Neumann (1945)

En 1945, John von Neumann rédige le *First Draft of a Report on the EDVAC*. Il y décrit une machine où **le programme et les données partagent la même mémoire** : c'est le *stored-program computer*. Presque tous les ordinateurs actuels en descendent :

```
          +-------------------------------+
          |              CPU              |
          |  +-------------+  +---------+ |
          |  | Unité de    |  |   ALU   | |      ALU = Arithmetic Logic Unit
          |  | contrôle    |  | (calcul)| |
          |  +-------------+  +---------+ |
          |        [ registres ]          |
          +---------------+---------------+
                          |   bus  <== le "von Neumann bottleneck"
          +---------------+---------------+
          |   Mémoire (code + données)    |
          +-------------------------------+
```

En 1977, dans sa conférence du prix Turing, **John Backus** (le créateur de FORTRAN) nomme le goulot d'étranglement de cette architecture :

> « Surely there must be a less primitive way of making big changes in the store than by pushing vast numbers of words back and forth through the von Neumann bottleneck. »
> — John Backus, *Can Programming Be Liberated from the von Neumann Style?*, 1977

On pourrait croire le problème réglé depuis. C'est l'inverse : il s'est aggravé.

## Les mémoires d'avant

- **Lignes à retard au mercure** (EDSAC, 1949) : les bits circulent sous forme d'ondes acoustiques dans un tube de mercure. L'accès est *séquentiel* : il faut attendre que le bit repasse.
- **Tambours magnétiques**, puis **tores de ferrite** (*core memory*, années 50-70). Un anneau magnétisé par bit. La mémoire des ordinateurs d'Apollo en était une variante (*core rope memory*). Le mot anglais « core » (*core dump*) vient de là.
- **1970 : Intel 1103**, première DRAM commerciale (1 kibibit). Les semi-conducteurs remplacent les tores.

## Le « memory wall » (1995)

À partir des années 80, la vitesse des CPU progresse bien plus vite que celle de la DRAM. En 1995, deux chercheurs, Wulf et McKee, formalisent le problème dans une courte note restée célèbre :

> « The difference between diverging exponentials also grows exponentially. »
> — Wm. A. Wulf & Sally A. McKee, *Hitting the Memory Wall: Implications of the Obvious*, ACM SIGARCH Computer Architecture News, 1995

Leur argument tient en une observation simple mais dérangeante : à l'époque, la vitesse des microprocesseurs progressait d'environ 55 à 60 % par an, quand la latence d'accès à la DRAM ne s'améliorait que de 7 à 9 % par an. Deux courbes exponentielles qui n'avancent pas au même rythme finissent, mathématiquement, par diverger de plus en plus vite. Les auteurs projetaient qu'un accès mémoire, qui coûtait environ 1,5 cycle CPU en 2000, pourrait en coûter près de 99 en 2010 si rien ne changeait — un ordinateur qui passerait l'essentiel de son temps à attendre la RAM plutôt qu'à calculer. Dans les faits, ce scénario ne s'est pas réalisé aussi brutalement : des caches plus grands, un prefetching plus intelligent et l'exécution *out-of-order* ont largement amorti le choc. Mais l'alerte a eu l'effet recherché — c'est très exactement ce qui a poussé toute l'industrie à concevoir du matériel et des logiciels **autour** du cache plutôt qu'en l'ignorant.

Un CPU moderne exécute une instruction simple en moins d'une nanoseconde. Aller chercher une donnée en RAM prend environ **100 ns**. Sans cache, le CPU passerait l'essentiel de son temps à attendre.

## L'invention du cache (1965)

**Maurice Wilkes** (déjà père de l'EDSAC) décrit en 1965 une petite mémoire rapide placée devant la mémoire principale. Il l'appelle *slave memory* (M. V. Wilkes, *Slave Memories and Dynamic Storage Allocation*, IEEE Trans. Electronic Computers, 1965). Le premier cache commercial arrive en 1968 sur l'**IBM System/360 Model 85**. Avec l'Intel **80486** (1989), le cache L1 entre dans la puce du CPU.

## La fin du « free lunch » (2005)

Jusqu'au milieu des années 2000, il suffisait d'attendre la génération de CPU suivante pour qu'un programme aille plus vite. Vers 2005, le *Dennard scaling* s'arrête : réduire la taille des transistors ne permet plus d'augmenter la fréquence sans faire fondre la puce. Les fabricants se tournent alors vers :

- le **multi-cœur**, sujet de votre cours multithreading ;
- les **instructions vectorielles**, c'est le **SIMD** (*Single Instruction, Multiple Data*, partie 3) ;
- un usage plus malin du **cache** et de la **prédiction**.

> « The free lunch is over. »
> — Herb Sutter, *The Free Lunch Is Over: A Fundamental Turn Toward Concurrency in Software*, Dr. Dobb's Journal, 2005

## Et dans le jeu vidéo ?

Les consoles ont toujours été des machines contraintes. L'Atari 2600 avait **128 octets** de RAM, la NES 2 Ko, la PS2 32 Mo. La PS3 imposait en plus son processeur **Cell**, dont les cœurs SPU travaillaient sur une mémoire locale de 256 Ko : il fallait *penser en flux de données* pour en tirer quelque chose.

C'est de cette contrainte permanente qu'est né le **Data-Oriented Design** (DOD), une manière de penser les données avant de penser les objets :

- Noel Llopis, *Data-Oriented Design (Or Why You Might Be Shooting Yourself in the Foot With OOP)*, Game Developer Magazine, 2009.
- Tony Albrecht (Sony), *Pitfalls of Object Oriented Programming*, GCAP 2009.
- **Mike Acton** (à l'époque Engine Director chez Insomniac Games), *Data-Oriented Design and C++*, CppCon 2014 — une conférence devenue culte, à regarder en entier si vous avez une heure :

> « The purpose of all programs, and all parts of those programs, is to transform data from one form to another. »
> — Mike Acton, *Data-Oriented Design and C++*, CppCon 2014

Le reste de la conférence est une attaque en règle contre trois habitudes qu'il qualifie de « mensonges » que la programmation orientée objet nous a fait intérioriser :

1. *« Software is the platform »* — en réalité non : c'est le **matériel** qui est la plateforme. Un design qui ignore le CPU, le cache, la mémoire n'est pas « portable » ou « propre », il est juste indifférent à ce sur quoi il tourne réellement.
2. *« Code should be designed around a model of the world »* — modéliser le monde avec des hiérarchies de classes (`Animal → Chien → Labrador`) semble naturel, mais ce n'est pas ce que le CPU exécute : il transforme des octets, pas des concepts.
3. *« Code is more important than data »* — pour Acton, c'est l'inverse : les données (leur forme, leur volume, la fréquence à laquelle elles changent) devraient dicter le code à écrire, jamais le contraire.

---

# 1.2 Le modèle mental : la hiérarchie mémoire

## Ordres de grandeur

Les chiffres ci-dessous sont des ordres de grandeur. Ils sont inspirés de *Latency Numbers Every Programmer Should Know* (Jeff Dean, Peter Norvig, mis à jour par Colin Scott). La dernière colonne ramène tout à l'échelle humaine : *1 ns devient 1 seconde*.

| Niveau                   | Taille typique                 | Latence       | « 1 ns = 1 s »   |
|--------------------------|---------------------------------|---------------|------------------|
| Registre                 | 16 GPR x 64 bits + 16/32 SIMD  | ~0 (dans le cœur) | instantané   |
| Cache L1d                | 32 à 48 Ko **par cœur** (+ L1i séparé, en général 32 Ko) | ~1 ns | 1 s |
| Cache L2                 | 0,5 à 2 Mo par cœur            | ~4 ns         | 4 s              |
| Cache L3                 | 16 à 128 Mo (jusqu'à ~192 Mo sur les puces « 3D V-Cache » les plus extrêmes), **partagé** — souvent par *groupe* de cœurs (un CCD/CCX chez AMD) plutôt que par tout le CPU, voir plus bas | ~10-20 ns | 15 s |
| RAM (DDR5)               | 16 à 64 Go                     | ~80-100 ns    | 1 min 40         |
| SSD NVMe (lecture random)| To                              | ~20-100 µs    | 1 jour           |
| HDD (seek)               | To                              | ~5-10 ms      | 4 mois           |
| Paquet réseau Europe → US| -                               | ~80-150 ms    | 3 à 5 ans        |

```
                  /\
                 /  \        registres       <- le CPU calcule ICI
                /----\
               /  L1  \      ~1 ns
              /--------\
             /    L2    \    ~4 ns
            /------------\
           /      L3      \  ~15 ns          (partagé entre cœurs)
          /----------------\
         /       RAM        \ ~100 ns
        /--------------------\
       /     SSD / disque     \  µs .. ms
      /------------------------\
  plus petit, plus rapide, plus cher  <----->  plus grand, plus lent, moins cher
```

**Un exemple réel**, mesuré sur un Ryzen 9 8940HX (Zen 4, 16 cœurs) :

```
$ lscpu -C
NAME ONE-SIZE ALL-SIZE WAYS TYPE        LEVEL  SETS PHY-LINE COHERENCY-SIZE
L1d       32K     512K    8 Data            1    64        1             64
L1i       32K     512K    8 Instruction     1    64        1             64
L2         1M      16M    8 Unified         2  2048        1             64
L3        32M      64M   16 Unified         3 32768        1             64
```

**Comment lire ce tableau `lscpu`** : `ONE-SIZE` est la taille **par cœur**, `ALL-SIZE` la somme sur toute la puce (16 cœurs ici). L1d et L1i : 32K × 16 = 512K, cohérent avec « 32 à 48 Ko par cœur ». L2 : 1M × 16 = 16M, cohérent avec « par cœur » aussi. **L3 est le cas piège** : `ONE-SIZE` vaut 32M mais `ALL-SIZE` ne vaut que 64M, pas 32 × 16 = 512M. Cette puce (deux CCD Zen 4, 8 cœurs chacun) a en réalité **deux pools de L3 de 32 Mo**, un par CCD — pas un seul pool de 64 Mo partagé par les 16 cœurs. Un thread sur le CCD A ne voit **pas** dans son L3 ce qu'un thread du CCD B y a mis : c'est un effet proche d'un mini-NUMA, à garder en tête pour le placement des threads d'un moteur (cours multithreading), et une bonne raison de ne jamais supposer qu'un CPU « à 64 Mo de L3 » offre un seul cache uniforme de cette taille.

Sous Windows : *Gestionnaire des tâches > Performance > Processeur* affiche L1, L2 et L3. Pour le détail par cœur, utiliser [Coreinfo](https://learn.microsoft.com/en-us/sysinternals/downloads/coreinfo) (Sysinternals) ou CPU-Z. Regardez les chiffres de votre propre machine avant le prochain cours.

**32 Ko de L1d, c'est ridicule** — à peine la taille d'une texture 90×90 en RGBA. Tout le reste de la frame vit plus loin, plus lentement. Le jeu (au sens propre) consiste à faire en sorte que *ce qui sert maintenant* tienne dans ces 32 Ko.

## SRAM vs DRAM

- **SRAM** (*Static RAM*) : 6 transistors par bit, sans rafraîchissement. Très rapide, peu dense, chère. Elle sert pour les caches.
- **DRAM** (*Dynamic RAM*) : 1 transistor et 1 condensateur par bit, rafraîchie toutes les ~64 ms. Dense et bon marché, mais lente. C'est la RAM principale.

Référence incontournable, un peu datée mais toujours juste : **Ulrich Drepper, *What Every Programmer Should Know About Memory*, 2007** ([PDF](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)).

---

# 1.3 Les registres (avant-goût de l'ASM)

Un **registre** est une petite case mémoire *à l'intérieur du cœur du CPU*, faite de bascules (*flip-flops*). C'est la seule mémoire sur laquelle l'ALU calcule directement. Additionner deux nombres en mémoire se fait donc en trois temps :

1. **charger** les valeurs dans des registres (*load*) ;
2. **calculer** (registre ⊕ registre → registre) ;
3. **écrire** le résultat en mémoire (*store*).

Sur x86-64 on dispose de :

- **16 registres généraux** (*GPR*, General Purpose Registers) de 64 bits : `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `rsp`, `r8` à `r15`. Intel **APX** (*Advanced Performance Extensions*), annoncé en 2023 et déjà supporté par MSVC (`/feature:APX`), en ajoute 16 autres ;
- `rip` (*instruction pointer*) et `rflags` ;
- des **registres vectoriels** : 16 `xmm` (128 bits, SSE), `ymm` (256 bits, AVX), puis 32 `zmm` (512 bits, AVX-512).

Tout cela est détaillé dans la partie 2.

**Pourquoi c'est important pour le projet :** une VM « *register-based* » comme Lua 5 imite ce modèle. Les instructions du bytecode disent « prends le registre 1 et le registre 2, mets la somme dans le registre 0 ». Une VM « *stack-based* » (JVM, CPython, Lua 4) empile et dépile à la place. La différence de performance vient de la mémoire : moins de copies et moins d'instructions à décoder (voir partie 4).

Une distinction utile à connaître : les registres *architecturaux* (`rax`...) sont une abstraction. Un cœur moderne en possède en réalité des centaines de *physiques* grâce au **register renaming**, au service de l'exécution *out-of-order*. Ça n'a pas d'impact direct sur votre code, mais ça explique pourquoi écrire `xor eax, eax` est si efficace : le CPU le reconnaît comme une *zeroing idiom* et casse la dépendance sur l'ancienne valeur du registre.

---

# 1.4 Les caches CPU

## Cache line, sets, associativité

Le cache ne stocke pas des octets isolés mais des **cache lines** (ou *cache blocks*) de **64 octets** sur toutes les machines x86 et ARM courantes. Accéder à 1 octet charge toute la ligne de 64 octets qui le contient.

Une adresse est découpée ainsi :

```
 adresse (virtuelle ou physique)
 +-------------------------------+-----------+----------+
 |              tag              |  index    |  offset  |
 +-------------------------------+-----------+----------+
                                   (quel set)  (6 bits = 64 octets)
```

- **Direct-mapped** : chaque adresse n'a qu'une seule place possible dans le cache.
- **N-way set-associative** : une adresse a N places possibles dans son *set*. Le L1d ci-dessus : 64 sets × 8 ways × 64 octets = 32 Ko.
- **Fully associative** : n'importe où. C'est trop coûteux en matériel, sauf pour de très petites structures comme le TLB.

Quand un set est plein, une ligne est évincée (en gros *LRU*, **Least Recently Used** : on évince la ligne la moins récemment utilisée).

## Les « 3 C » des cache misses (Mark Hill, 1987)

- **Cold / Compulsory** : premier accès, la donnée n'a jamais été chargée.
- **Capacity** : le *working set* est plus grand que le cache.
- **Conflict** : deux données se battent pour le même set (*cache thrashing*).

## Localité

- **Localité temporelle** : ce qui vient d'être utilisé le sera probablement de nouveau (variables de boucle, pile).
- **Localité spatiale** : ce qui est *à côté* sera probablement utilisé (tableaux).

## Le prefetcher matériel

Le CPU observe les accès mémoire. Quand il détecte un motif régulier (*stride* : +8, +8, +8...), il charge les lignes suivantes *avant* qu'on les demande. D'où la règle d'or : **accès séquentiels = prefetcher content**. À l'inverse, suivre des pointeurs éparpillés (liste chaînée, arbre, graphe d'objets `new`) le rend aveugle.

## TLB et pages

Les adresses manipulées par le programme sont **virtuelles**. L'OS et la **MMU** (*Memory Management Unit*, l'unité de traduction d'adresses intégrée au CPU) les traduisent en adresses physiques par **pages** (4 Ko par défaut, 2 Mo ou 1 Go en *large pages*). La traduction est mise en cache dans le **TLB** (*Translation Lookaside Buffer*), qui ne compte que quelques centaines à quelques milliers d'entrées. Un *TLB miss* coûte un *page walk*, soit plusieurs accès mémoire.

- x86-64 n'utilise que 48 bits d'adresse virtuelle (57 avec la pagination à 5 niveaux). Les bits hauts doivent recopier le bit 47 : ce sont les *canonical addresses*. Certaines VM s'en servent pour cacher des tags dans les pointeurs (partie 4, NaN-boxing).
- Sous Windows : `VirtualAlloc(..., MEM_LARGE_PAGES, ...)` exige le privilège `SeLockMemoryPrivilege`.

## Démonstration 1 : parcours d'une grille

[code/01_memoire/cache.cpp](code/01_memoire/cache.cpp)

```cpp
const size_t N = 4096;
std::vector<int32_t> grid(N * N, 1);

// row-major : accès séquentiel
for (size_t y = 0; y < N; ++y)
    for (size_t x = 0; x < N; ++x)
        sum += grid[y * N + x];

// column-major : saute de N * 4 octets = 16 Ko à chaque accès
for (size_t x = 0; x < N; ++x)
    for (size_t y = 0; y < N; ++y)
        sum += grid[y * N + x];
```

```
grid row-major    (y puis x)                  1.815 ms
grid column-major (x puis y)                 70.636 ms
```

**Facteur ~40.** Le calcul et la complexité algorithmique sont identiques. Attention, une partie de l'écart vient de l'**auto-vectorisation** : la version row-major est vectorisée, l'autre non. C'est un bon teaser pour la partie 3.

## Démonstration 2 : `std::vector` vs `std::list`

```
std::vector sum                               1.084 ms
std::list   sum                             473.850 ms
```

La liste a été « fragmentée » en mélangeant l'ordre de chaînage des nœuds, ce qui simule un heap après des heures de jeu. **Facteur ~400.** Chaque nœud coûte un cache miss probable (~100 ns), et le prefetcher ne peut rien anticiper.

**Attention à un piège classique de mesure** : si on construit la liste d'un coup avec `push_back` sans rien fragmenter, l'allocateur place souvent les nœuds côte à côte en mémoire, et l'écart tombe à ~6x seulement — on pourrait en conclure à tort que « la liste, ça va ». C'est la version fragmentée qui reflète la réalité d'un `std::list` qui vit longtemps dans un moteur (insertions/suppressions répétées). Voir Bjarne Stroustrup, *Why you should avoid Linked Lists* (GoingNative 2012), ou *CPU Caches and Why You Care* de Scott Meyers (code::dive 2014).

## Démonstration 3 : AoS vs SoA

```cpp
struct ParticleAoS {           // 80 octets : ~1 particule par cache line
    float px, py, pz, vx, vy, vz;
    float color[4];
    float life, size;
    char  name[32];            // données froides
};

struct ParticlesSoA {          // un tableau par champ
    std::vector<float> px, py, pz, vx, vy, vz;
};
```

```
sizeof(ParticleAoS) = 80 octets
AoS update position                          12.734 ms
SoA update position                           4.850 ms
```

La mise à jour des positions ne lit que 24 octets sur 80. En AoS, on charge quand même les 56 autres dans le cache. En SoA, chaque boucle parcourt un tableau contigu de `float`, ce qui est idéal pour le cache et **pour le SIMD**.

- **AoS** (*Array of Structures*) : naturel, orienté objet.
- **SoA** (*Structure of Arrays*) : orienté données.
- **AoSoA** (*Array of Structures of Arrays*) : paquets de 4, 8 ou 16 éléments en SoA, alignés sur la largeur SIMD. On le retrouve dans ISPC et dans certains moteurs de physique ou de particules.
- **Hot/cold splitting** : séparer les champs lus à chaque frame de ceux lus rarement.

Les ECS (Entity Component System), comme [EnTT](https://github.com/skypjack/entt), [flecs](https://github.com/SanderMertens/flecs) ou Unity DOTS, sont une industrialisation de l'idée SoA.

## Comment réduire les cache misses : récapitulatif

Chaque démonstration ci-dessus illustre une technique. En résumé, du plus simple au plus spécialisé :

- **Accès séquentiel** (démo 1) : parcourir la mémoire dans l'ordre où elle est rangée exploite le prefetcher et la localité spatiale. C'est le levier le plus rentable, et il est gratuit.
- **Structures compactes et contiguës** (démo 2) : préférer `std::vector` à une structure chaînée (`std::list`, arbre, graphe d'objets alloués séparément un par un) dès que c'est possible.
- **SoA** (démo 3) : ne charger que les champs réellement lus par la boucle chaude, pas toute la struct.
- **Réduire le working set par blocs** (*loop tiling* / *cache blocking*) : traiter les données par petits paquets qui tiennent en L1/L2 avant de passer au paquet suivant. Inutile sur un parcours déjà séquentiel (démo 1), mais indispensable pour des accès 2D « voisins » (convolution, flou, jeu de la vie, simulation de fluide).
- **Prefetch logiciel** : donner un indice explicite au CPU quand le prochain accès est *connu à l'avance* mais *non séquentiel* (parcours de graphe de scène, de BVH, de liste triée par distance) :

  ```cpp
  #include <xmmintrin.h>
  _mm_prefetch(reinterpret_cast<const char*>(next_node), _MM_HINT_T0);   // MSVC et GCC/Clang
  // équivalent portable : __builtin_prefetch(next_node) sous GCC/Clang
  ```

  `_MM_HINT_T0` charge dans tous les niveaux de cache (jusqu'à L1) ; `_MM_HINT_T1`/`_MM_HINT_T2` visent L2/L3 seulement, pour une donnée qui ne servira que plus tard. **C'est un outil de dernier recours** : le prefetcher matériel couvre déjà l'immense majorité des cas (tout accès à *stride* régulier), et un mauvais indice gaspille de la bande passante sans bénéfice. À réserver aux parcours véritablement irréguliers.
- **Mesurer plutôt que deviner** : `perf stat -e cache-misses,cache-references` (Linux), *Memory Access analysis* (VTune) — voir plus bas.

## False sharing (lien avec le cours multithreading)

Deux threads écrivent dans deux variables *différentes* situées sur la *même cache line*. Chaque écriture invalide la ligne dans le cache de l'autre cœur, et les performances s'effondrent :

```cpp
#include <new>
struct alignas(std::hardware_destructive_interference_size) PerThreadCounter
{
    std::atomic<uint64_t> value;
};
```

`std::hardware_destructive_interference_size` vaut 64 sur x86-64 (MSVC, GCC et Clang).

**Est-ce un problème de correction (data race), et un mutex protège-t-il contre ça ?** Non, sur les deux points. C'est uniquement un problème de **performance**. Deux threads qui écrivent chacun dans *sa propre* variable ne se marchent jamais dessus au sens de la mémoire (pas de *data race*, pas de comportement indéfini) : le résultat du calcul est correct, avec ou sans false sharing. Le protocole de cohérence de cache (MESI/MOESI) raisonne à la granularité de la **ligne entière**, pas de la variable individuelle : quand le cœur A écrit dans `counters[0].value`, le protocole invalide la copie de **toute la ligne de 64 octets** dans le cache du cœur B, même si B ne touche que `counters[1].value` sur cette même ligne. B doit alors relire la ligne (depuis un cache plus lent, ou via le bus inter-cœurs) avant de pouvoir écrire à son tour — et inversement au prochain tour. La ligne « ping-pong » entre les cœurs à chaque écriture, purement à cause du **placement physique**, indépendamment de toute synchronisation logicielle.

Un `std::mutex` ne règle donc rien ici : il protège une variable *effectivement partagée* contre un accès concurrent incorrect, mais dans le false sharing, chaque thread a déjà sa **propre** variable — il n'y a rien d'incorrect à protéger. Ajouter un verrou par variable ne ferait qu'empiler le coût du lock **par-dessus** le ping-pong de cache line, sans le supprimer (et le verrou lui-même, quelque part en mémoire, peut être victime du même phénomène s'il est mal placé). La seule vraie solution est le **placement mémoire** : séparer physiquement les données sur des lignes de cache différentes (`alignas` ci-dessus), typiquement en regroupant les données **par thread** plutôt que par « type logique ».

## Mesurer les caches

- **Intel VTune Profiler** : gratuit, fonctionne aussi sur AMD pour les analyses génériques ; *Memory Access analysis*.
- **AMD uProf** : compteurs matériels sur AMD.
- **Superluminal**, **Tracy** : profilers très utilisés dans le jeu vidéo (Tracy est open source).
- Linux, pour les curieux : `perf stat -e cache-misses,cache-references`, `valgrind --tool=cachegrind`.

---

# 1.5 Alignement et padding

## Les règles

1. Chaque type a un **alignement** `alignof(T)` (une puissance de 2) : ses adresses doivent être multiples de cette valeur. Sur x86-64 : `char` 1, `short` 2, `int` et `float` 4, `double`, `int64_t` et pointeur 8, `__m128` 16, `__m256` 32.
2. Les membres d'une `struct` sont placés **dans l'ordre de déclaration**, chacun à l'offset multiple de son alignement suivant. Le compilateur n'a pas le droit de les réordonner.
3. `alignof(struct)` est le plus grand alignement de ses membres.
4. `sizeof(struct)` est arrondi au multiple de `alignof(struct)`, avec du **padding de fin**, pour que les tableaux fonctionnent.

## Exemple

[code/01_memoire/alignment.cpp](code/01_memoire/alignment.cpp)

```cpp
struct Bad  { bool active; double x; bool visible; int32_t id; bool dirty; };
struct Good { double x; int32_t id; bool active; bool visible; bool dirty; };
```

```
Bad      sizeof=32 alignof= 8          Good     sizeof=16 alignof= 8
    offsetof(Bad, active) = 0              offsetof(Good, x) = 0
    offsetof(Bad, x) = 8                   offsetof(Good, id) = 8
    offsetof(Bad, visible) = 16            offsetof(Good, active) = 12
    offsetof(Bad, id) = 20                 offsetof(Good, visible) = 13
    offsetof(Bad, dirty) = 24              offsetof(Good, dirty) = 14
```

```
Bad (32 octets)            a=active x=x v=visible i=id d=dirty .=padding
octet :  0       8       16      24      32
         a.......xxxxxxxxv...iiiid.......

Good (16 octets)
octet :  0       8       16
         xxxxxxxxiiiiavd.
```

**Même contenu, deux fois plus petit** : deux fois plus d'éléments par cache line.

## Voir le layout sous Visual Studio

- **VS 2022 17.8+** : survoler un type affiche `sizeof` et `alignof` dans le Quick Info.
- **VS 2022 17.9+** : clic droit, puis *Memory Layout*, pour une vue graphique avec offsets et padding ([blog VS](https://devblogs.microsoft.com/visualstudio/size-alignment-and-memory-layout-insights-for-c-classes-structs-and-unions/)).
- Option du compilateur (non documentée mais très connue) : `/d1reportSingleClassLayoutBad` :

```
class Bad	size(32):
	+---
 0	| active
  	| <alignment member> (size=7)
 8	| x
16	| visible
  	| <alignment member> (size=3)
20	| id
24	| dirty
  	| <alignment member> (size=7)
	+---
```

## Représentation mémoire : aligné vs mal aligné

Un bloc est « aligné sur N octets » quand son adresse de départ est un multiple de N. Sur 64 octets consécutifs (une cache line), voici où tombent les multiples de 16, 32 et 64 — et ce qui se passe quand une donnée ne tombe pas dessus :

```
adresse :   0        16        32        48        64
            |---------|---------|---------|---------|
            ^         ^         ^         ^         ^
        mult.16/32/64 mult.16   mult.16/32 mult.16   mult.16/32/64
                                                       (= 1 cache line entière)

__m128 (16 octets) ALIGNÉ, adresse 16 :
            .........[XXXXXXXX]..............
                      16      32
            -> tient entièrement dans la ligne [0,64). movaps fonctionne.

__m128 (16 octets) MAL ALIGNÉ, adresse 20 (décalé de 4) :
            ............[XXXXXXXX]...........
                        20      36
            -> ne commence pas à un multiple de 16 : movaps/_mm_load_ps LÈVE UNE EXCEPTION (#GP).
               movups/_mm_loadu_ps fonctionne, mais reste décalé.

__m256 (32 octets) ALIGNÉ, adresse 32 :
            ....................[XXXXXXXXXXXXXXXX]
                                 32              64
            -> tient exactement dans la moitié haute de la ligne.

donnée de 16 octets à l'adresse 60 (fin de ligne) :
            ..........................[XXXX|XXXX]  ...[XXXX]...
                                       60   64(fin ligne 1)  64+12 (début ligne 2)
            -> CHEVAUCHE deux cache lines : deux lectures au lieu d'une, même avec loadu.
```

Deux conséquences concrètes :

1. **Chevauchement de cache line** : une donnée qui déborde sur la ligne suivante coûte une lecture de ligne supplémentaire. Depuis Nehalem (2008), le surcoût par accès isolé est faible, mais il s'accumule en boucle chaude.
2. **Instructions qui exigent l'alignement** (`movaps`, `_mm_load_ps`, et plus généralement tout accès SSE « aligned ») : le CPU vérifie l'adresse **au niveau matériel** et déclenche une exception (*general protection fault*, `#GP`) si elle n'est pas multiple de 16. `movups`/`_mm_loadu_ps` n'ont pas cette contrainte, mais ne suppriment pas le coût d'un chevauchement de ligne.

## Pourquoi aligner ?

- **Historiquement** : sur certaines architectures (SPARC, anciens ARM), lire un `int` à une adresse impaire provoque une *bus error*. x86 tolère, ARM64 aussi pour les données scalaires.
- **SIMD** : `_mm_load_ps` / `movaps` **crashent** (*general protection fault*) si l'adresse n'est pas alignée sur 16. `_mm_loadu_ps` / `movups` acceptent tout. Sur les CPU récents, ils sont aussi rapides quand la donnée *est* alignée (partie 3).
- **Cache** : aligner une structure fréquemment lue sur 64 octets garantit qu'elle ne chevauche pas deux lignes.

## Contrôler l'alignement

```cpp
struct alignas(16) Vec4 { float x, y, z, w; };      // imposé

#pragma pack(push, 1)                               // supprime le padding
struct FileHeader { char magic[4]; uint32_t version; uint16_t flags; };
#pragma pack(pop)
```

`#pragma pack` sert à coller à un **format de fichier ou réseau** : header de `.wav` (cours audio), bytecode sérialisé. À éviter pour les données de calcul.

## Allouer de la mémoire alignée (MSVC)

| Méthode | MSVC | Remarque |
|---------|------|----------|
| `new T[n]` avec `alignof(T) > 16` | oui (C++17) | l'`operator new` aligné est appelé automatiquement |
| `::operator new(size, std::align_val_t{64})` | oui | libérer avec `::operator delete(p, std::align_val_t{64})` |
| `_aligned_malloc(size, align)` / `_aligned_free` | oui | spécifique MSVC |
| `_mm_malloc` / `_mm_free` | oui | fourni avec les intrinsics |
| `std::aligned_alloc` (C++17) | **NON** | incompatible avec le `free` de la **CRT** (*C RunTime*, la bibliothèque C standard liée par défaut) Windows |

**Piège très fréquent** : `std::aligned_alloc` apparaît partout sur cppreference, mais ne compile pas sous MSVC. La bonne réponse : `_aligned_malloc`, ou mieux, `operator new` avec `std::align_val_t`, ou un `std::vector` avec un allocateur aligné.

## Aliasing

**Qu'est-ce que l'aliasing, en programmation ?** Le mot est le même qu'en traitement du signal (repliement de spectre), mais le sens n'a **rien à voir**. Ici, l'*aliasing* désigne simplement le fait que **deux expressions — pointeurs, références, vues — peuvent désigner la même zone mémoire** :

```cpp
void add(const int* a, const int* b, int* out) { *out = *a + *b; }
add(&x, &y, &x);   // out ALIASE a : la fonction écrit dans la variable qu'elle vient de lire
```

Tant que le compilateur ne peut pas **prouver** que deux pointeurs ne se recouvrent jamais, il doit rester prudent : il ne peut pas charger `*a` une fois en registre et le réutiliser après avoir écrit `*out`, il doit relire à chaque usage — par précaution, au cas où l'écriture aurait modifié la valeur. C'est un manque à gagner pur pour l'optimisation, et en particulier pour la **vectorisation** (voir 3.2 : sans `__restrict`, MSVC génère un test de chevauchement à l'exécution avant de choisir la version vectorisée). D'où le mot-clé `__restrict` (MSVC) / `__restrict__` (GCC/Clang) : une **promesse du programmeur** au compilateur — « ces pointeurs ne se recouvrent jamais » — à ne faire que si c'est vrai, sous peine de comportement indéfini.

## Strict aliasing et type punning

Un cas particulier et plus sournois : lire un `float` comme un `uint32_t` via un cast de pointeur est un **comportement indéfini** (*strict aliasing rule* : le compilateur suppose que deux pointeurs de types différents et non apparentés ne peuvent pas aliaser, et optimise sur cette base — même si à l'exécution c'est faux). Les méthodes correctes :

```cpp
float f = 1.0f;
uint32_t bits  = std::bit_cast<uint32_t>(f);   // C++20, <bit>
uint32_t bits2; std::memcpy(&bits2, &f, 4);    // C++ classique : le compilateur optimise en un simple mov
```

C'est indispensable pour le projet : encoder et décoder des instructions, faire du NaN-boxing, lire du bytecode binaire.

## Endianness

x86 et ARM (en pratique) sont **little-endian** : l'octet de poids faible est stocké en premier. `0x11223344` s'écrit `44 33 22 11` en mémoire. En C++20 : `std::endian::native`, et `std::byteswap` en C++23. Ça compte dès qu'on écrit un **fichier de bytecode** ou qu'on lit un dump mémoire dans Visual Studio.

---

# 1.6 Au-delà du cache : pipeline et prédiction de branchement

Un CPU moderne est un **pipeline** : *fetch → decode → rename → schedule → execute → retire*, avec des dizaines d'étages et une exécution *out-of-order*. Pour ne pas attendre le résultat d'un `if`, le CPU **prédit** la branche et exécute de manière spéculative. En cas d'erreur (*branch misprediction*), il jette le travail en cours : **~15 à 20 cycles perdus**.

La question Stack Overflow la plus célèbre : [*Why is processing a sorted array faster than processing an unsorted array?*](https://stackoverflow.com/questions/11227809) (2012). Même boucle, même données, mais une fois triées, le `if (data[i] >= 128)` devient prévisible.

Ça compte pour la suite du cours :

- **SIMD** : les branches n'existent pas en SIMD. On les remplace par des **masques** (*branchless*).
- **Interpréteur** : la boucle `switch(opcode)` est une branche indirecte *très* difficile à prédire. C'est le cœur des optimisations de *dispatch* (partie 4).
- Culture : **Spectre** (2018) exploite justement l'exécution spéculative.

---

# 1.7 Lien avec le projet

| Notion | Où elle réapparaît dans l'interpréteur |
|--------|----------------------------------------|
| Hiérarchie mémoire | le bytecode et les registres de la VM doivent tenir dans L1/L2 |
| Registres | VM *register-based* (Lua 5) vs *stack-based* |
| Alignement / padding | taille de la `Value` (tagged union 16 octets), registres `vec4` alignés 16 |
| AoS / SoA | exécuter un script sur N entités (*batch mode*) |
| Endianness / `bit_cast` | encodage des instructions sur 32 bits, fichier de bytecode |
| Prédiction de branchement | dispatch du `switch`, instructions *branchless* |

---

# 1.8 Pièges et questions fréquentes

- **« Pourquoi mon benchmark donne 0 ms ? »** Le compilateur a supprimé le calcul inutilisé : utilisez un `do_not_optimize` ou affichez le résultat. Et toujours mesurer **en Release** (en Debug MSVC, avec `_ITERATOR_DEBUG_LEVEL`, les conteneurs STL sont 10 à 100x plus lents).
- **« Pourquoi les résultats changent à chaque run ? »** Turbo boost, scheduler, throttling thermique sur portable. Prenez le **minimum** de plusieurs runs, gardez l'ordinateur sur secteur.
- **« `sizeof` d'une struct vide vaut 1 ? »** Oui : deux objets distincts doivent avoir des adresses distinctes. Voir `[[no_unique_address]]` (MSVC : `[[msvc::no_unique_address]]`).
- **« x86 tolère le mauvais alignement, pourquoi s'embêter ? »** À cause de SIMD (`movaps` crashe), des perfs du cache et de la portabilité ARM.
- **« Les classes virtuelles ? »** Le `vptr` ajoute 8 octets en tête d'objet, et chaque appel virtuel est une indirection + une branche indirecte (lien avec le dispatch de l'interpréteur, partie 4).

---

# 1.9 Références

**Articles et papiers**

- Ulrich Drepper, *What Every Programmer Should Know About Memory*, 2007 : [PDF](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- Wm. A. Wulf, Sally A. McKee, *Hitting the Memory Wall: Implications of the Obvious*, 1995 : [ACM](https://dl.acm.org/doi/10.1145/216585.216588)
- John Backus, *Can Programming Be Liberated from the von Neumann Style?*, 1977 : [PDF](https://www.cs.miami.edu/home/odelia/teaching/csc419_spring20/syllabus/backusPaper.pdf)
- M. V. Wilkes, *Slave Memories and Dynamic Storage Allocation*, 1965 : [PDF](https://safari.ethz.ch/digitaltechnik/spring2022/lib/exe/fetch.php?media=wilkes.pdf)
- Herb Sutter, *The Free Lunch Is Over*, 2005 : [gotw.ca](http://www.gotw.ca/publications/concurrency-ddj.htm)
- Colin Scott, *Latency Numbers Every Programmer Should Know* (interactif) : [lien](https://colin-scott.github.io/personal_website/research/interactive_latency.html)

**Conférences (vidéos)**

- Mike Acton, *Data-Oriented Design and C++*, CppCon 2014 : [YouTube](https://www.youtube.com/watch?v=rX0ItVEVjHc)
- Scott Meyers, *CPU Caches and Why You Care*, code::dive 2014 : [YouTube](https://www.youtube.com/watch?v=WDIkqP4JbkE)
- Chandler Carruth, *Efficiency with Algorithms, Performance with Data Structures*, CppCon 2014 : [YouTube](https://www.youtube.com/watch?v=fHNmRkzxHWs)
- Andrei Alexandrescu, *Speed Is Found In The Minds of People*, CppCon 2019

**Livres**

- Randal Bryant, David O'Hallaron, *Computer Systems: A Programmer's Perspective* (CS:APP), 3e éd. Chapitres 1, 3 (machine-level), 5 (optimisation), 6 (hiérarchie mémoire). **LA référence** pour toute cette partie et pour l'ASM.
- Jason Gregory, *Game Engine Architecture*, 3e éd. : chapitre sur le hardware et l'optimisation.
- Richard Fabian, *Data-Oriented Design*, 2018 : [en ligne gratuitement](https://www.dataorienteddesign.com/dodbook/)
- Denis Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, 2e éd. : [gratuit](https://github.com/dendibakh/perf-book)

**Outils**

- Visual Studio *Memory Layout* view : [blog](https://devblogs.microsoft.com/visualstudio/size-alignment-and-memory-layout-insights-for-c-classes-structs-and-unions/)
- Intel VTune, AMD uProf, Tracy, Superluminal, Sysinternals Coreinfo
