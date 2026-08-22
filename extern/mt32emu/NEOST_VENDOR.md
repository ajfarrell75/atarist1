# libmt32emu (Munt) — copie vendorisée

Copie versionnée directement dans le dépôt NeoST (« vendorisée ») de la
bibliothèque **libmt32emu** du projet [Munt](https://github.com/munt/munt) —
l'émulation Roland **MT-32 / CM-32L** de Dean Beeler, Jerome Fisher et
Sergey V. Mikayev. Licence **LGPL 2.1 ou ultérieure** (`COPYING.LESSER.txt`,
`COPYING.txt`, auteurs dans `AUTHORS.txt`).

## Pourquoi vendorisé

Le MT-32 dépendait d'un `find_package(MT32Emu CONFIG QUIET)`, donc d'un paquet
**système** : `brew install mt32emu` sur macOS, `libmt32emu-dev` ailleurs. Sans lui
la fonctionnalité disparaissait **en silence** — un simple message de configuration
« libmt32emu not found — MT-32 output disabled », et un binaire amputé. Aucun runner
de CI ni aucune image de release n'installait ce paquet : le MT-32 n'existait donc
que sur la machine du développeur qui l'avait installé à la main, et **dans aucun
binaire livré**. `TODO.md` en portait la trace (« paquet macOS », 2026-08-21 :
embarquer le `.dylib` ou compiler Munt en statique) ; c'est cette seconde branche
qui est prise ici.

Comme pour [Moira](../moira/NEOST_VENDOR.md), le code d'amont vit désormais dans
l'arbre et se compile avec NeoST.

## Version reprise

Amont `munt` `6e7c01fba7e1d50c8fa705834889fd0eac136075` (2026-06-21),
`libmt32emu` **2.8.3** (numéro repris de `mt32emu/cmake/project_data.cmake`).

## Contenu conservé

Seul `mt32emu/src/` d'amont (les sources compilées), plus les licences et
`AUTHORS.txt`. Sont **élagués** : le reste du dépôt Munt (`mt32emu-qt`,
`mt32emu_smf2wav`, `mt32emu_alsadrv`…), et `src/test/` (le runner de tests amont).

`src/srchelper/SoxrAdapter.*` et `SamplerateAdapter.*` restent dans l'arbre mais
**ne sont pas compilés** : ils exigeraient libsoxr / libsamplerate, soit exactement
la dépendance externe qu'on vient de supprimer. On garde le rééchantillonneur
interne, qui est le défaut d'amont.

## Aucun patch local

Le code d'amont est **intact**. Ce qui est écrit par NeoST se limite au
`CMakeLists.txt` de ce dossier, qui remplace celui d'amont (578 lignes de
`project()`, d'installation d'en-têtes, d'export de paquet CMake et de `.pc` —
inutiles à une bibliothèque interne). Il ne construit que ce que NeoST utilise :

- **API C++ seule** (`MT32EMU_EXPORTS_TYPE 0`), bibliothèque **statique** ;
- rééchantillonneur **interne** (`MT32EMU_WITH_INTERNAL_RESAMPLER`) ;
- `config.h` généré depuis `src/config.h.in` avec les valeurs qu'amont y
  substituerait ;
- les en-têtes recopiés dans `build/generated/mt32emu/mt32emu/`, parce que les
  sources d'amont s'incluent **à plat et entre guillemets** (`"Synth.h"`,
  `"config.h"`) alors que NeoST écrit `#include <mt32emu/mt32emu.h>`, comme avec la
  bibliothèque installée. Même schéma que Moira (`build/generated/moira/Moira`).

Les avertissements sont coupés (`-w`) sur ces sources : on ne les corrigera pas, et
leur bruit masquerait ceux de NeoST.

## Mettre à jour

1. `git clone --depth 1 https://github.com/munt/munt` ailleurs ;
2. remplacer `src/` par `mt32emu/src/` d'amont, re-supprimer `src/test/` ;
3. vérifier que les listes de sources du `CMakeLists.txt` d'ici collent encore à
   `libmt32emu_CPP_SOURCES` + `libmt32emu_INTERNAL_RESAMPLER_SOURCES` d'amont, et
   que `src/config.h.in` n'a pas gagné de variable à substituer ;
4. reporter la version (§ *Version reprise* et les `libmt32emu_VERSION_*` du
   `CMakeLists.txt`).

## Licence — ce que la LGPL impose ici

La LGPL 2.1 permet l'édition de liens **statique** à condition que le destinataire
puisse reconstruire l'ensemble avec une version modifiée de la bibliothèque. NeoST
étant publié sous GPL 3 avec ses sources, et cette copie de Munt vivant dans le même
dépôt, la condition est remplie sans démarche supplémentaire. Le § 3 de la LGPL 2.1
autorise par ailleurs explicitement le basculement vers la GPL, ce qui règle la
compatibilité avec la GPL 3 de NeoST.

⚠ **Les ROM Roland ne sont PAS incluses** et ne le seront pas : l'utilisateur fournit
les siennes dans `roms/mt32/`. Munt émule la machine, pas son micrologiciel.
