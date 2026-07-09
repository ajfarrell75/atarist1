# libcmini — copie vendorisée (NeoST)

Copie **versionnée directement dans le dépôt NeoST** (« vendorisée », plus de
`.git`) de **libcmini**, une **libC minimaliste** pour Atari ST / m68k-atari-mint,
qui remplace la MiNTLib pour produire des exécutables **très petits** (évite
l'overhead POSIX sur 68000).

C'est la **fondation C commune** aux libs de jeu type **AGT** (`dev/agt`) et **SGDL**
(OrionSoft) : toutes deux se compilent avec GCC `m68k-atari-mint` + libcmini.

## Provenance (vérifiée)

| | |
|---|---|
| **Amont**   | `https://github.com/freemint/libcmini` (projet FreeMiNT, libre) |
| **Release** | **`v0.54`** — dernière **release officielle** (`releases/latest` GitHub) |
| **Commit**  | `be8f0a6` (tag `v0.54`) |
| **Date**    | 2020-05-15 |
| **Licence** | **GNU LGPL** (cf. `LICENSE.txt`) — libre, redistribuable |

> `master` amont est bien plus récent (dev 2026) mais **non tagué** ; on fige la
> dernière **release** stable `v0.54` (choix « à jour ET vérifié »). Pour passer au
> dev : `git clone … && rsync` depuis `master`, puis mettre à jour commit/date ici.

## Contenu

`sources/` (implémentations libc), `include/` (headers), `Makefile`, `tests/`,
`libcmini.*` (config du projet). Se compile en `libcmini.a` + startup (`crt0.o`)
à lier avec `-nostdlib`.

## Chaîne de compilation

**GCC `m68k-atari-mint`** (cross-compilateur), pas vbcc. Installé via les binaires
de Thomas Otto (`https://tho-otto.de/crossmint.php`) — cf. le README du toolchain
NeoST (binutils 2.45 + gcc 13.4.0 + mintlib + fdlibm, préfixe local). Build type :

```sh
export PATH="$HOME/opt/crossmint/usr/bin:$PATH"
make            # → build/**/libcmini.a (68000/020-60/ColdFire/mshort/mfastcall) + crt0.o
```

**Piège GCC 13** : v0.54 (2020) casse sur `-Werror` durci de GCC 13
(`isblank.c` infinite-recursion, etc.). Réglé **sans patcher la source** par
`Make.config.local` (déjà `-include`é par le Makefile) : `CFLAGS += -Wno-error`.
Ce fichier est conservé dans le dossier vendorisé. Détails → `../TOOLCHAIN_M68K_MINT.md`.

## Mettre à jour

```sh
git clone https://github.com/freemint/libcmini.git /tmp/libcmini
cd /tmp/libcmini && git checkout v0.54   # ou master pour le dev
rsync -a --exclude='.git' /tmp/libcmini/ dev/libcmini/   # puis MAJ commit/date ci-dessus
```
