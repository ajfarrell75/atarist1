# Moira — copie vendorisée (fork NeoST)

Ce dossier n'est **plus un sous-module Git**. C'est une copie versionnée
directement dans le dépôt NeoST (« vendorisée ») du cœur 68000 cycle-exact
[Moira](https://github.com/dirkwhoffmann/Moira) de Dirk W. Hoffmann (licence MIT,
cf. `LICENSE`).

## Pourquoi vendorisé

NeoST **modifie** des fichiers internes de Moira (cf. patch ci-dessous). Avec un
sous-module, ces edits ne sont pas dans le dépôt parent et sont **écrasés au
premier `git submodule update`** — ce qui s'est déjà produit une fois (fork local
perdu, commits `e4da365`/`a1e52ec` introuvables). La vendorisation fige le code et
le patch dans l'historique NeoST, à l'épreuve du clobber.

## Contenu conservé

Seul `Moira/` (les sources compilées par NeoST), plus `LICENSE`, `README.md` et
`CMakeLists.txt` upstream (référence). L'arbre upstream complet — `Cputester/`
(~713 Mo de corpus ADF), `Documentation/`, `docs/`, `Runner/`, `Moira.xcodeproj/`
— a été **élagué** (inutile au build NeoST, qui ne compile que `Moira/*`).

## Patch local : `NEOST_IPLFETCH` (reconnaissance IPL différée)

Port fidèle de WinUAE `ipl_fetch_next` (mécanisme B). **OFF par défaut**
(`iplDelay4 == 0` ⇒ `pollIpl()` ≡ `reg.ipl = ipl`, byte-identique à l'upstream).
Activé via l'env `NEOST_IPLFETCH=1` côté NeoST. Détails et mesures :
`docs/MOIRA_WINUAE_CONVERGENCE.md` (mécanisme B).

Fichiers touchés vs upstream :
- `Moira/MoiraMacros.h` — `POLL_IPL` → `pollIpl()`
- `Moira/Moira.h` — membres `iplPrev`/`iplChangeClock`/`iplDelay4`/`iplDelay2`,
  méthodes `setIplDelay()` + `pollIpl()`
- `Moira/Moira.cpp` — historisation de la broche dans `setIPL`, règle 3-cas dans
  `pollIpl()`

Toute modif future d'un fichier `Moira/` se commit **normalement** dans NeoST.
