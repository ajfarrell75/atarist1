# `hd/` — disques durs

Ce dossier est ce que scanne la fenêtre **Hard Disks** de NeoST (barre d'outils « HD »,
ou *Fenêtres → Hard Disks*). La convention est volontairement tenue par la seule nature
de l'entrée :

| Ce que vous mettez ici | Ce que NeoST en fait |
|------------------------|----------------------|
| un **dossier** (`hd/JEUX/`) | un lecteur **GEMDOS** monté en **C:** — les appels GEMDOS du TOS sont redirigés vers l'hôte, façon Hatari. Rien à formater : on y dépose des fichiers depuis l'hôte. |
| un **fichier** image (`hd/disque20mo.img`) | une image de disque dur **ACSI** sur la cible 0 — le TOS lit sa table de partitions et monte C:, D:… C'est un vrai disque bloc, à partitionner/formater depuis le TOS (ou à récupérer déjà formaté). |

Extensions reconnues pour les images ACSI : `.img`, `.hd`, `.acsi`, `.vhd`, `.raw`.

Le scan des **images** n'est **pas récursif** : seuls les fichiers posés à plat dans `hd/`
sont proposés. C'est délibéré — sans ça, les `.img` rangés *à l'intérieur* d'un lecteur
GEMDOS (`hd/JEUX/DEMOS/truc.img`) seraient offerts comme disques durs. Les **dossiers**,
eux, ne sont regardés qu'au premier niveau : `hd/JEUX/` est un lecteur, pas `hd/JEUX/AUTO/`.

Le dossier `gemdos/` du dépôt reste proposé en tête de la liste GEMDOS : c'est le lecteur
livré avec NeoST (`gemdos/DEMOS`, les `DESKTOP.INF`/`NEWDESK.INF`).

## Trois choses à savoir

- **Monter relance la machine.** Le TOS ne sonde les disques qu'au boot ; NeoST enchaîne
  donc un *hard reset* après chaque montage ou éjection. Ce qui tournait est perdu.
- **GEMDOS est exclusif avec la cartouche.** Le lecteur GEMDOS occupe le port `$FA0000`
  (il y installe une cartouche système) : monter un dossier éjecte la cartouche externe.
- **GEMDOS et ACSI revendiquent C: tous les deux.** NeoST ne décale pas le lecteur GEMDOS
  derrière les partitions ACSI (contrairement à Hatari) — la fenêtre affiche un
  avertissement si les deux sont montés en même temps. N'en monter qu'un.

## En ligne de commande

Les mêmes montages existent côté headless et sont mémorisés dans `neost.cfg`
(`gemdos=`, `acsi=`) :

```sh
./build/neost-headless <rom> --gemdos hd/JEUX          # dossier hôte → C:
./build/neost-headless <rom> --acsi   hd/disque20mo.img # image ACSI (alias --hd)
```

Le contenu de ce dossier (hors ce README) est **ignoré par git** : les images de disque
dur sont volumineuses et souvent chargées de logiciels sous copyright.
