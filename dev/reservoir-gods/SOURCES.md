# Devkit Reservoir Gods — provenance

Sources **vendorisées** (copiées dans le dépôt, `.git` imbriqués retirés — même
approche que `extern/moira`) depuis https://github.com/ReservoirGods, clones
shallow du 2026-07-08 :

| Dépôt | Contenu | Commit d'origine |
|---|---|---|
| [GODLIB](https://github.com/ReservoirGods/GODLIB) | Bibliothèque C/68k pour STFM/STE/TT/Falcon (~60 modules : BLITTER, MFP, IKBD, VBL, AUDIO…) | `ae600aae192f35c8fe8a7e8164ec0e4caaa7e6e9` |
| [GODLIB.SPL](https://github.com/ReservoirGods/GODLIB.SPL) | Exemples compilables par module (JAGPAD, MIXER, BLITTER, SCRNSWAP, TRUCOLOR…) | `80f62d45c12550936f6513d3e2178b4b49b278b1` |
| [TOOLS.RG](https://github.com/ReservoirGods/TOOLS.RG) | Outils de développement RG | `6e1830ae927741043240bedd46f8034092d8f396` |
| [GAMES.RG](https://github.com/ReservoirGods/GAMES.RG) | Sources de jeux (HotPot) | `4dcd5689f41208dfd621a30c39b0069f1c1a199d` |

Intérêt pour NeoST : logiciels étalons **avec source** — quand un binaire RG diverge
de l'oracle Hatari, on lit ce que le code attend du matériel au lieu de désassembler
(JAGPAD → pads STE `$FF9200`, MIXER → son DMA, BLITTER → blitter).
