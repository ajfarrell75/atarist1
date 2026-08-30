# TinySoundFont — copie vendorisée

Copie versionnée directement dans le dépôt NeoST (« vendorisée ») de
**TinySoundFont** ([schellingb/TinySoundFont](https://github.com/schellingb/TinySoundFont)),
synthétiseur SoundFont2 en un seul header de Bernhard Schelling, d'après SFZero
de Steve Folta. Licence **MIT** (`LICENSE`).

## Pourquoi vendorisé

C'est le synthé **General MIDI intégré** des plateformes sans DLSMusicDevice
(Linux, Windows) — cf. `src/audio/GmSynth`. Même politique que Moira et Munt :
une dépendance système (FluidSynth…) aurait rendu la fonctionnalité absente en
silence de tous les binaires livrés ; un header MIT dans l'arbre compile partout.
La banque de sons, elle, est une donnée : NeoST livre TimGM6mb dans `roms/gm/`
(cf. son `README.md`), remplaçable par n'importe quelle `.sf2`.

## Version reprise

Amont `TinySoundFont` `790a219810cb0fca5defa8cdbd88e2487e5efc7a` (2025-06-05),
`tsf.h` **v0.9** (récupéré le 2026-08-30).

## Contenu conservé

`tsf.h` et `LICENSE` uniquement — pas les exemples ni `tml.h` (parseur de
fichiers .mid, inutile ici : le flux MIDI vient de l'ACIA émulée).

## Modifications NeoST

Aucune. Le header est repris tel quel ; toute divergence future doit être
consignée ici.
