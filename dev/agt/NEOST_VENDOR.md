# AGT (Atari Game Tools) — copie vendorisée (NeoST)

Ce dossier est une copie **versionnée directement dans le dépôt NeoST**
(« vendorisée », plus de `.git`) d'**AGT — Atari Game Tools**, moteur de
prototypage 2D pour Atari **STE** de **dml (Doug Little)**.

C'est la **seconde bibliothèque de codage Atari** du projet, à côté de GODLIB
(Reservoir Gods, dans `dev/reservoir-gods`). Objectif : disposer d'un second
corpus de code/jeux réels pour tester et faire progresser l'émulation NeoST.

## Provenance (vérifiée)

| | |
|---|---|
| **Amont**   | `https://bitbucket.org/d_m_l/agtools` (repo public officiel de l'auteur) |
| **Auteur**  | dml — Doug Little (`doug694@gmail.com`) |
| **Branche** | `master` (stable/vérifiée) |
| **Commit**  | `1139b0993c0afda2b4bb8f24df26520c2fcddad6` |
| **Date**    | 2022-09-10 (dernier commit `master`) |

> ⚠ Une branche `wip-integration` (`e6e4f6a…`) existe en amont mais est
> *work-in-progress* (non vérifiée) — **non retenue**. On fige `master`.
>
> Récupéré par **`git clone` direct** (HTTPS anonyme du repo public), pas par
> scraping : un dépôt git public donne une provenance **vérifiable au hash de
> commit**, plus fiable qu'un download de page. (Le fork Bitbucket
> `logronoide/agtools` est, lui, privé → 404/auth.)

## Licence

**Aucun fichier `LICENSE` explicite** dans l'amont. L'auteur publie AGT en
**open-source** (« all source … no secret libraries, black-box .bin files or
other magic sauce ») pour apprendre et faire des jeux STE (cf. fil officiel
Atari-Forum `viewtopic.php?t=31558`). Usage NeoST = étude / test d'émulation.
**Vérifier les conditions de l'auteur avant toute redistribution.**

## Contenu

Arbre amont **complet** conservé (rien d'élagué) :

| Dossier | Contenu |
|---------|---------|
| `agtsys/`     | le **moteur** AGT (C/C++ + 68k) |
| `tools/`      | outils spécifiques AGT (conversion assets, etc.) |
| `examples/` `demos/` `tutorials/` | échantillons/jeux (sources + assets) |
| `data/` `3rdparty/` | données runtime + dépendances tierces |
| `bin/{Linux,Darwin,win}` | **prebuilts des outils hôtes** (Linux aarch64, macOS x86_64, Windows x86) |
| `makedefs*` `makerules*` `config.sh` | configuration de build |

## Chaîne de compilation (≠ GODLIB)

AGT se compile avec **GCC `m68k-atari-mint`** (cf. `makedefs.mintgcc` /
`makedefs.mikrocc` / `makedefs.browncc`) — **pas la chaîne vbcc** utilisée pour
GODLIB (`dev/etalons/build.sh`). Pour bâtir AGT nativement il faudrait un
cross-GCC `m68k-atari-mint` (ou réutiliser les outils hôtes de `bin/`).
Portage vers notre chaîne = travail séparé, non fait ici (vendorisation seule).

## Mettre à jour

```sh
git clone --depth 1 -b master https://bitbucket.org/d_m_l/agtools.git /tmp/agt
rsync -a --exclude='.git' /tmp/agt/ dev/agt/     # puis mettre à jour commit/date ci-dessus
```
