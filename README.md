---
title: Cours ASM / SIMD / Interpréteur
author: Johann Philippe
---

# Cours ASM / SIMD / Interpréteur

> Bachelor 3 GTECH (spécialité moteur). Mémoire → assembleur x86-64 → SIMD → interpréteur, jusqu'à un projet de machine virtuelle accélérée par le matériel.

## Contenu

| Chapitre | Sujet |
|----------|-------|
| [1_Memoire.md](1_Memoire.md) | histoire, hiérarchie mémoire, caches, alignement et padding, registres, DOD (AoS/SoA), prédiction de branchement |
| [2_ASM.md](2_ASM.md) | x86-64 depuis zéro : registres, instructions, pile, convention Windows x64, MASM dans VS, encodage des instructions, ARM64 |
| [3_SIMD.md](3_SIMD.md) | histoire, auto-vectorisation d'abord, SSE en profondeur, AVX2, AVX-512, CPU dispatch, NEON |
| [4_Interpreteur.md](4_Interpreteur.md) | lexer, Pratt, sema, stack vs registres (Lua 5), représentation des valeurs, dispatch, LuaJIT, assembleur maison |
| [5_Projet.md](5_Projet.md) | cahier des charges, planning, grille d'évaluation, pièges |

## Compiler les exemples

**Visual Studio (recommandé)** : *File → Open → Folder* sur `code/`, sélectionner **x64-Release**, puis *Build All*.

**Ligne de commande** :

```
cmake -S code -B build
cmake --build build --config Release
```

## Organisation

- Chaque chapitre est un cours autonome : histoire, théorie, démonstrations mesurées, pièges fréquents, références.
- Les exercices sont distribués séparément en cours : chaque défi fournit un programme qui tourne déjà et une mission de correction/optimisation sous contrainte, jamais une page blanche. Le score, quand il y en a un, se compare à votre propre mesure de départ — jamais à celle des autres.
- La partie 5 est le cahier des charges du projet de groupe : les exercices des parties 1 à 4 en sont les briques.
