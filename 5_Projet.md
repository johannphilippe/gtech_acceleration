---
title: 5 - Le projet
author: Johann Philippe
---

# 5. Le projet : un moteur de script accéléré

> Ce chapitre est votre cahier des charges. Les exercices des quatre premières parties ne sont pas des à-côtés : ce sont littéralement les briques de ce que vous allez construire ici. Vous gardez une grande liberté sur le thème.

---

# 5.1 Le sujet en une phrase

> **Concevoir et réaliser, en groupe de 2 ou 3, un DSL (*domain-specific language*, votre propre petit langage) dont l'exécution exploite le matériel (mémoire, ASM, SIMD), puis le mettre en scène dans une démo au thème libre.**

L'**architecture d'exécution est un choix qui vous appartient** : VM à registres pilotée par un assembleur maison (l'option la plus guidée par le cours), VM à pile, interpréteur AST dont les opérations coûteuses sont accélérées à la main, JIT, ou une combinaison — voir 5.2.

## Thèmes possibles (libres, à valider avec l'équipe enseignante)

| Thème | Ce que votre DSL exécute | Pourquoi c'est intéressant |
|-------|---------------------------|----------------------------|
| **Particules scriptables** | un « kernel » par particule (gravité, forces, couleur) | batch mode SoA + SIMD, rendu visuel immédiat |
| **DSL audio** | oscillateurs, filtres, enveloppes, traités par blocs | traitement par blocs = batch, denormals |
| **Fantasy console** (façon CHIP-8 / PICO-8) | un jeu écrit dans votre DSL | le langage et son moteur d'exécution sont au centre, framebuffer, input |
| **Shaders CPU** / post-process | un programme par pixel | SIMD évident, comparaison possible avec un vrai shader |
| **Boids / IA de foule** | règles de comportement par agent | vec4 natif, voisinage, SoA |
| **Génération procédurale** | bruit (Perlin/Simplex), terrain, donjon | calcul flottant intensif |
| **Scripts de gameplay** | logique d'entités, événements | appels de fonctions, hot reload |

---

# 5.2 Cahier des charges

## Socle obligatoire

1. **Un DSL** (votre propre langage, même minimal)
   - syntaxe texte, avec **lexer** et **parser** (partie 4) ;
   - au minimum : expressions, variables, un contrôle de flux (`if` et/ou `while`), et des **appels de fonction** ;
   - une représentation interne **documentée** (bytecode, AST, ou autre selon l'architecture choisie au point 2).
2. **Un moteur d'exécution qui exploite le matériel**, au choix du groupe — une seule brique suffit comme cœur, combiner plusieurs est encouragé :
   - **VM à registres** pilotée par un **assembleur maison** — c'est l'option la plus guidée par le cours : format d'instruction documenté (taille fixe recommandée, par exemple 32 bits façon Lua), registres **contigus en mémoire**, texte → bytecode avec **labels** (deux passes) et constantes, **désassembleur**, **test d'aller-retour** bytecode → texte → bytecode identique ;
   - **VM à pile** (*stack-based*), avec un dispatch soigné (jump table, threaded code, superinstructions) ;
   - **interpréteur AST** (« *tree-walking* ») dont les opérations coûteuses ne sont pas interprétées naïvement mais déléguées à des noyaux écrits à la main en ASM/SIMD ;
   - un **JIT** (template JIT façon 2.7, ou génération de code natif) ;
   - toute **combinaison** de ce qui précède.

   Quel que soit le choix : documentez la représentation interne retenue, et sachez justifier vos décisions de layout (taille des valeurs, alignement).
3. **Au moins un chemin SIMD réellement exploité** : registres `vec4` + opcodes SSE, **ou** mode batch sur N entités, **ou** noyaux SIMD appelés par votre moteur (mixage audio, culling...).
4. **Mesures**
   - benchmarks reproductibles **en Release** ;
   - au moins **deux optimisations mesurées avant et après**, avec une **explication matérielle** (cache, branches, SIMD, nombre d'instructions/nœuds exécutés).
5. **Démo** en lien avec le thème choisi.
6. **README** : architecture choisie et pourquoi, format de la représentation interne, résultats de mesure, répartition du travail.

## Bonus (au choix, aucun n'est obligatoire)

- **Comparaison d'architectures** : implémenter deux moteurs d'exécution différents pour le même DSL (par exemple VM à registres **et** tree-walking accéléré en SIMD) et comparer leurs performances, avec explication matérielle.
- **Batch mode** SoA + AVX2 écrit à la main + **CPU dispatch** (repli SSE).
- **Superinstructions** / constantes en opérandes, avec mesure du nombre d'instructions exécutées.
- **Comparatif de dispatch** : `switch` vs `[[msvc::musttail]]` (VS 2026) vs computed goto (clang-cl).
- **Handlers écrits en MASM**, ou **template JIT** (partie 2) si ce n'est pas déjà votre moteur principal.
- **Bytecode binaire** sérialisé (header, version, endianness).
- **Debugger** : pas à pas, breakpoints, affichage de l'état d'exécution (très utile... et très apprécié en soutenance).
- **Hot reload** du script pendant que la démo tourne.
- **Profiling** documenté avec VTune, Superluminal ou Tracy.

## Contraintes techniques

- C++20, **Visual Studio 2022 ou 2026, x64**, compilable en Release sans warnings bloquants (`/W4` conseillé).
- **CMake** pour le projet (voir 5.3) : `CMakeLists.txt` à la racine, ouverture directe dans VS via *File → Open → Folder*.
- Bibliothèques autorisées pour la **démo** (rendu, audio, fenêtre) : raylib, SFML, SDL, Dear ImGui...
- **Interdit pour le cœur** : bibliothèques d'interprétation ou de parsing (Lua, sol2, ANTLR, asmjit pour le JIT...). **Autorisé** : DirectXMath en comparaison, mais les opcodes/noyaux SIMD du cœur doivent être écrits par le groupe.
- Dépôt **git** avec historique réel (des commits réguliers de chaque membre).

---

# 5.3 Planning

## Semaine 1 : théorie et briques (16 h officielles, 24 h maximum)

6 séances de 4 h, les deux dernières empiétant sur la semaine 2 :

| Séance | Contenu | Exercices (briques) |
|--------|-------------------|--------------------------|
| **S1** Mémoire | Partie 1, sections 1.1 à 1.6 : histoire, hiérarchie, caches, alignement, DOD | Défis 1, 2, 3 |
| **S2** ASM 1 | Partie 2, sections 2.1 à 2.3 et 2.6 : modèle d'exécution, registres, instructions, lecture de `/Od` et `/O2` | Défis 1, 2 (début) |
| **S3** ASM 2 | Partie 2, sections 2.4, 2.5, 2.7, 2.8 : pile, convention d'appel, unwind, jump tables, encodage, ARM64 | Défis 3, 4 |
| **S4** SIMD 1 | Partie 3, sections 3.1 à 3.4 : histoire, auto-vectorisation, SSE, masques, tail | (exercices distribués en cours) |
| **S5** SIMD 2 | Partie 3, sections 3.5 à 3.8 : AVX2, lanes, AVX-512, dispatch, benchmarks | (exercices distribués en cours) |
| **S6** Interpréteur | Partie 4, sections 4.1 à 4.8 : chaîne, Pratt, stack vs registres, dispatch, batch | Défi 1 ; **lancement du projet** (groupes, thèmes) |

## Semaines 2 et 3 : projet (16 h par semaine en présentiel + temps libre)

Chaque séance combine une courte capsule théorique (20-30 min) au moment où vous en avez besoin, puis du travail en groupe. Les capsules ci-dessous suivent par défaut le chemin le plus guidé (VM à registres) — si votre groupe a choisi une autre architecture (5.2, point 2), adaptez-en l'esprit : par exemple « allocation de registres » devient « disposition des nœuds/valeurs de votre AST » pour un tree-walker.

| Moment | Capsule | Jalon attendu |
|--------|---------|---------------|
| S2-J1 | Format d'instruction, X-macro, allocation de registres (4.9) | choix du thème et de l'architecture, représentation interne écrite |
| S2-J2 | Assembleur deux passes (Défi 4) | **Jalon 1** : premier moteur d'exécution minimal (VM, interpréteur...) + boucle qui tourne |
| S2-J3 | Fenêtres d'appel, stack overflow, tests d'aller-retour | fonctions et appels |
| S2-J4 | Mesurer proprement : Release, benchmark anti-optimisation, VTune (1.9, 3.10) | **Jalon 2** : premiers benchmarks, chemin SIMD commencé |
| S3-J1 | Pratt, sema, compilation (Défis 1, 3) | langage plus riche (sucre syntaxique, plus de constructions) |
| S3-J2 | Dispatch (Défi 5), superinstructions (Défi 6) | optimisations mesurées |
| S3-J3 | Batch mode (Défi 7), CPU dispatch | **Jalon 3** : démo jouable, gel des fonctionnalités |
| S3-J4 | - | **Soutenance** : 15 min + démo + questions individuelles |

---

# 5.4 Grille d'évaluation (sur 20)

| Critère | Points | Ce qu'on regarde |
|---------|--------|------------------|
| Moteur d'exécution (VM, interpréteur, JIT...) | 7 | justesse, représentation interne documentée, outillage de vérification (désassembleur, dump, test aller-retour...), appels de fonctions, choix architectural justifié |
| Exploitation du matériel (SIMD, mémoire) | 4 | chemin SIMD réel, alignement, SoA, justification |
| Mesures et analyse | 3 | protocole (Release, répétitions), avant/après, **explication matérielle correcte** |
| Langage haut niveau | 3 | lexer, parser, sema, allocation de registres |
| Qualité : code, README, git | 2 | lisibilité, tests, historique git réparti |
| Soutenance et démo | 1 | clarté, démo qui fonctionne |
| **Bonus** | +2 max | voir la liste en 5.2 |

**Note individuelle** : pondérée selon les questions individuelles de la soutenance (« explique ce handler », « pourquoi ce registre est non-volatile ») et l'historique git.

---

# 5.5 Questions à anticiper pour les points d'étape

À chaque jalon, attendez-vous à devoir justifier vos choix à voix haute, pas seulement montrer que « ça marche ». Les exemples ci-dessous supposent une VM à registres (l'architecture la plus guidée par le cours) — adaptez l'esprit de chaque question à ce que vous avez réellement construit (VM à pile, AST, JIT...) :

**Jalon 1 (moteur d'exécution + assembleur/outillage)**

- « Montrez-moi, de bout en bout, comment une instruction (ou un nœud de votre AST) est exécutée. »
- « Montrez-moi le décodage d'une instruction. Combien de bits pour l'opcode ? Pourquoi ? » *(VM à registres/pile)*
- « Que se passe-t-il si un saut vise un label défini plus bas ? »
- « Où vivent vos registres (ou vos variables) ? Quelle est la taille d'une valeur ? Alignement ? »

**Jalon 2 (fonctions, premiers benchmarks)**

- « Quand `f` appelle `g`, où sont les arguments ? Qu'est-ce qui empêche `g` d'écraser les registres de `f` ? »
- « Votre benchmark est en Release ? Combien de répétitions ? Minimum ou moyenne ? »
- « Combien d'instructions dispatchées par tour de boucle ? »

**Jalon 3 (optimisations, démo)**

- « Votre version SIMD est plus rapide de combien ? Pourquoi pas ×4 ? » (réponses possibles : bande passante mémoire, tail, chargements, dispatch)
- « Avez-vous vérifié l'auto-vectorisation avec `/Qvec-report:2` ? »
- « Que se passe-t-il sur un CPU sans AVX2 ? »

---

# 5.6 Pièges fréquents à anticiper

| Symptôme | Cause probable | Piste |
|----------|----------------|-------|
| Le moteur d'exécution plante « au hasard » | lecture hors bornes (registres, pile, buffer AST...), fenêtre d'appel mal calculée, état mal initialisé | assertions en Debug sur les accès ; ASan (MSVC `/fsanitize=address`) |
| Le SIMD est plus lent | mesure en Debug, `loadu`/`storeu` sur des données non alignées dans une boucle trop courte, dispatch par élément | batch mode ; mesurer en Release |
| Crash `movaps` | constantes `vec4` ou registres non alignés | `alignas(16)`, `operator new` aligné |
| Résultats flottants différents entre SSE et AVX2 | FMA, ordre des additions | tolérance dans les tests ; déterminisme discuté en 3.2 |
| Envie de sauter directement à l'architecture la plus ambitieuse (JIT...) sans rien de simple qui tourne encore | enthousiasme, compréhensible | un JIT est un choix légitime dès le départ (5.2), mais faites d'abord tourner un chemin simple de bout en bout avant d'optimiser l'architecture elle-même |
| Le parser mange tout le temps | langage trop ambitieux (classes, closures, chaînes) | réduire : pas de GC, types valeur, pas de chaînes dynamiques |
| Un membre du groupe ne code que la démo | répartition du travail | exiger une brique du moteur d'exécution ou une optimisation mesurée par membre |

---

# 5.7 Vektor, l'interpréteur de référence

Vous ne recevrez jamais Vektor (l'interpréteur de référence complet, voir partie 4) en un seul bloc avant la fin du projet — ce serait une solution complète pour l'ensemble du projet. En revanche, certains de ses fichiers vous seront distribués **par morceaux, comme corrigés**, au fil des exercices de la partie 4 : `lexer.cpp` et `Parser::expression` après le défi 1, `asm.cpp` après le défi 4, `showdown.cpp` après les défis 2 et 5. Utilisez-les pour comparer votre approche, pas pour copier — le but est de comprendre pourquoi vos choix diffèrent, pas d'obtenir le même code.

Vektor suit spécifiquement le chemin **VM à registres** décrit en 5.2 : `asm.cpp` (l'assembleur maison) et `showdown.cpp` (stack vs registres) n'ont de sens directement transposable que si vous avez choisi cette même architecture. `lexer.cpp` et `Parser::expression`, en revanche, sont utiles quel que soit votre choix — un front-end (lexer/parser) est nécessaire pour n'importe quel DSL, indépendamment de comment vous l'exécutez ensuite.

**Vektor a aussi des limites volontaires**, qui peuvent vous inspirer pour délimiter le périmètre de votre propre projet : pas de tableaux, pas de chaînes, pas de variables globales, pas d'affectation de composante (`v.x = 1.0`), pas de batch mode intégré (c'est une démo séparée), constantes rechargées dans les boucles (un exercice, pas un défaut assumé). Rien ne vous oblige à couvrir plus que ça pour un socle solide.
