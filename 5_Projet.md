---
title: 5 - Le projet
author: Johann Philippe
---

# 5. Le projet : un moteur de script accéléré

> Ce chapitre est votre cahier des charges. Les exercices des quatre premières parties ne sont pas des à-côtés : ce sont littéralement les briques de ce que vous allez construire ici. Vous gardez une grande liberté sur le thème.

---

# 5.1 Le sujet en une phrase

> **Concevoir et réaliser, en groupe de 2 ou 3, une machine virtuelle à registres pilotée par un assembleur maison, dont l'exécution exploite le matériel (mémoire, SIMD), puis la mettre en scène dans une démo au thème libre.**

## Thèmes possibles (libres, à valider avec l'équipe enseignante)

| Thème | Ce que la VM exécute | Pourquoi c'est intéressant |
|-------|----------------------|----------------------------|
| **Particules scriptables** | un « kernel » par particule (gravité, forces, couleur) | batch mode SoA + SIMD, rendu visuel immédiat |
| **DSL audio** (lien avec le cours audio) | oscillateurs, filtres, enveloppes, traités par blocs | traitement par blocs = batch, denormals, XAudio2 déjà vu |
| **Fantasy console** (façon CHIP-8 / PICO-8) | un jeu écrit dans l'assembleur de la VM | l'assembleur maison est au centre, framebuffer, input |
| **Shaders CPU** / post-process | un programme par pixel | SIMD évident, comparaison possible avec un vrai shader |
| **Boids / IA de foule** | règles de comportement par agent | vec4 natif, voisinage, SoA |
| **Génération procédurale** | bruit (Perlin/Simplex), terrain, donjon | calcul flottant intensif |
| **Scripts de gameplay** | logique d'entités, événements | appels de fonctions, hot reload |

---

# 5.2 Cahier des charges

## Socle obligatoire

1. **VM à registres**
   - format d'instruction **documenté** (taille fixe recommandée, par exemple 32 bits façon Lua) ;
   - au minimum : entiers, flottants, arithmétique, comparaisons, sauts conditionnels, **appels de fonction** avec retour ;
   - registres **contigus en mémoire** ; vous devez savoir justifier vos choix de layout (taille des valeurs, alignement).
2. **Assembleur maison**
   - texte → bytecode, avec **labels** (sauts vers l'avant, donc deux passes) et constantes ;
   - **désassembleur** ;
   - **test d'aller-retour** : bytecode → texte → bytecode identique.
3. **Au moins un chemin SIMD réellement exploité** : registres `vec4` + opcodes SSE, **ou** mode batch sur N entités, **ou** noyaux SIMD appelés par la VM (mixage audio, culling...).
4. **Mesures**
   - benchmarks reproductibles **en Release** ;
   - au moins **deux optimisations mesurées avant et après**, avec une **explication matérielle** (cache, branches, SIMD, nombre d'instructions dispatchées).
5. **Démo** en lien avec le thème choisi.
6. **README** : architecture, format d'instruction, table des opcodes, résultats de mesure, répartition du travail.

## Niveau attendu pour une bonne note

7. **Un langage de haut niveau** (même minimal : expressions, variables, `if`, `while`, fonctions) compilé vers le bytecode : lexer, parser, analyse sémantique, allocation de registres.

## Bonus (au choix, aucun n'est obligatoire)

- **Batch mode** SoA + AVX2 écrit à la main + **CPU dispatch** (repli SSE).
- **Superinstructions** / constantes en opérandes, avec mesure du nombre d'instructions exécutées.
- **Comparatif de dispatch** : `switch` vs `[[msvc::musttail]]` (VS 2026) vs computed goto (clang-cl).
- **Handlers écrits en MASM**, ou **template JIT** (partie 2).
- **Bytecode binaire** sérialisé (header, version, endianness).
- **Debugger** : pas à pas, breakpoints, affichage des registres (très utile... et très apprécié en soutenance).
- **Hot reload** du script pendant que la démo tourne.
- **Profiling** documenté avec VTune, Superluminal ou Tracy.

## Contraintes techniques

- C++20, **Visual Studio 2022 ou 2026, x64**, compilable en Release sans warnings bloquants (`/W4` conseillé).
- **CMake** pour le projet (voir 5.3) : `CMakeLists.txt` à la racine, ouverture directe dans VS via *File → Open → Folder*.
- Bibliothèques autorisées pour la **démo** (rendu, audio, fenêtre) : raylib, SFML, SDL, XAudio2, Dear ImGui...
- **Interdit pour le cœur** : bibliothèques d'interprétation ou de parsing (Lua, sol2, ANTLR, asmjit pour le JIT...). **Autorisé** : DirectXMath en comparaison, mais les opcodes SIMD doivent être écrits par le groupe.
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

Chaque séance combine une courte capsule théorique (20-30 min) au moment où vous en avez besoin, puis du travail en groupe.

| Moment | Capsule | Jalon attendu |
|--------|---------|---------------|
| S2-J1 | Format d'instruction, X-macro, allocation de registres (4.9) | choix du thème, format d'instruction écrit |
| S2-J2 | Assembleur deux passes (Défi 4) | **Jalon 1** : VM minimale + assembleur + boucle qui tourne |
| S2-J3 | Fenêtres d'appel, stack overflow, tests d'aller-retour | fonctions et appels |
| S2-J4 | Mesurer proprement : Release, benchmark anti-optimisation, VTune (1.9, 3.10) | **Jalon 2** : premiers benchmarks, chemin SIMD commencé |
| S3-J1 | Pratt, sema, compilation (Défis 1, 3) | langage haut niveau (niveau 2) |
| S3-J2 | Dispatch (Défi 5), superinstructions (Défi 6) | optimisations mesurées |
| S3-J3 | Batch mode (Défi 7), CPU dispatch | **Jalon 3** : démo jouable, gel des fonctionnalités |
| S3-J4 | - | **Soutenance** : 15 min + démo + questions individuelles |

---

# 5.4 Grille d'évaluation (sur 20)

| Critère | Points | Ce qu'on regarde |
|---------|--------|------------------|
| VM et format d'instruction | 4 | justesse, format documenté, registres contigus, appels |
| Assembleur et désassembleur | 3 | labels, erreurs lisibles, test d'aller-retour |
| Exploitation du matériel (SIMD, mémoire) | 4 | chemin SIMD réel, alignement, SoA, justification |
| Mesures et analyse | 3 | protocole (Release, répétitions), avant/après, **explication matérielle correcte** |
| Langage haut niveau | 3 | lexer, parser, sema, allocation de registres |
| Qualité : code, README, git | 2 | lisibilité, tests, historique git réparti |
| Soutenance et démo | 1 | clarté, démo qui fonctionne |
| **Bonus** | +2 max | voir la liste en 5.2 |

**Note individuelle** : pondérée selon les questions individuelles de la soutenance (« explique ce handler », « pourquoi ce registre est non-volatile ») et l'historique git.

---

# 5.5 Questions à anticiper pour les points d'étape

À chaque jalon, attendez-vous à devoir justifier vos choix à voix haute, pas seulement montrer que « ça marche ». Quelques exemples des questions qui reviennent à chaque jalon :

**Jalon 1 (VM + assembleur)**

- « Montrez-moi le décodage d'une instruction. Combien de bits pour l'opcode ? Pourquoi ? »
- « Que se passe-t-il si un saut vise un label défini plus bas ? »
- « Où vivent vos registres ? Quelle est la taille d'une valeur ? Alignement ? »

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
| La VM plante « au hasard » | lecture hors de la frame (taille de frame fausse), fenêtre d'appel mal calculée | assertions en Debug sur les indices de registres ; ASan (MSVC `/fsanitize=address`) |
| Le SIMD est plus lent | mesure en Debug, `loadu`/`storeu` sur des données non alignées dans une boucle trop courte, dispatch par élément | batch mode ; mesurer en Release |
| Crash `movaps` | constantes `vec4` ou registres non alignés | `alignas(16)`, `operator new` aligné |
| Résultats flottants différents entre SSE et AVX2 | FMA, ordre des additions | tolérance dans les tests ; déterminisme discuté en 3.2 |
| Envie de « faire un JIT » dès le début | enthousiasme, compréhensible | socle d'abord ; le JIT est un bonus après le jalon 3 |
| Le parser mange tout le temps | langage trop ambitieux (classes, closures, chaînes) | réduire : pas de GC, types valeur, pas de chaînes dynamiques |
| Un membre du groupe ne code que la démo | répartition du travail | exiger un opcode ou une optimisation mesurée par membre |

---

# 5.7 Vektor, l'interpréteur de référence

Vous ne recevrez jamais Vektor (l'interpréteur de référence complet, voir partie 4) en un seul bloc avant la fin du projet — ce serait la solution complète du socle et du niveau 2. En revanche, certains de ses fichiers vous seront distribués **par morceaux, comme corrigés**, au fil des exercices de la partie 4 : `lexer.cpp` et `Parser::expression` après le défi 1, `asm.cpp` après le défi 4, `showdown.cpp` après les défis 2 et 5. Utilisez-les pour comparer votre approche, pas pour copier — le but est de comprendre pourquoi vos choix diffèrent, pas d'obtenir le même code.

**Vektor a aussi des limites volontaires**, qui peuvent vous inspirer pour délimiter le périmètre de votre propre projet : pas de tableaux, pas de chaînes, pas de variables globales, pas d'affectation de composante (`v.x = 1.0`), pas de batch mode intégré (c'est une démo séparée), constantes rechargées dans les boucles (un exercice, pas un défaut assumé). Rien ne vous oblige à couvrir plus que ça pour un socle solide.
