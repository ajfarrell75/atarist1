# Discipline de release — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qu'il faut faire, dans l'ordre, pour poser une version.**
Écrit dans le cadre du chantier **A37** (2026-08-28).

## Pourquoi ce fichier

L'audit du 2026-08-27 relevait trois symptômes d'une seule cause — rien n'écrivait la
procédure, donc rien ne pouvait la vérifier :

- **trois tags le même jour** (0.5 → 0.5.1 → 0.5.2, le 2026-08-10) ;
- **0.5.3 sautée sans une ligne pour le dire** (elle est désormais consignée au
  `CHANGELOG.md`, § *Numéros de version sautés*) ;
- **le travail majeur depuis le 2026-08-23 n'est pas tagué** — MegaSTE 12/12,
  CAB/theoldnet, l'audit et le plan A16-A37, puis A10/A14/A16b/A28-A36 et les pas 2 et 5
  de la purge.

`tools/check_release.py` (palier `fast`) garde ce qui est vérifiable par une machine ;
le reste est ci-dessous.

## La procédure

1. **Décider le numéro.** `MAJOR.MINOR.PATCH`. Un correctif de paquet qui ne change pas
   l'émulation → PATCH ; une fonctionnalité → MINOR. Ne pas sauter de numéro ; si un
   numéro est brûlé malgré tout, l'écrire au § *Numéros de version sautés* du
   `CHANGELOG.md` — le contrôle échoue tant que ce n'est pas fait.
2. **Bumper les DEUX endroits, ensemble** : `project(NeoST VERSION x.y.z)` dans
   `CMakeLists.txt` et « Version courante : **x.y.z** » en tête du `CHANGELOG.md`.
   `check_release.py` exige qu'ils soient égaux **et** égaux à la dernière en-tête
   `## x.y.z — …`.
3. ⚠ **Reconfigurer une fois avec `-DNEOST_VERSION_STR=x.y.z`.** C'est une variable de
   **cache** CMake : sans ce passage, le binaire continue d'annoncer l'ancienne version
   et `--version` **ment**. (Déjà écrit dans `CLAUDE.md` ; répété ici parce que c'est
   l'étape qu'on saute.)
4. **Écrire l'entrée de release** dans le `CHANGELOG.md` : ce que l'utilisateur gagne,
   pas la liste des commits. Les chantiers datés vivent déjà en dessous.
5. **`python3 tools/run_all.py --tier full`** — vert, sur un poste au repos. Le palier
   `full` porte les étalons pixel : c'est le seul qui interdit de publier une régression
   de rendu.
6. **Taguer** : un tag annoté par release, `git tag -a x.y.z -m "…"`. Un seul par jour,
   sauf raison écrite.
7. **Publier** : la CI construit les paquets. Vérifier que chacun porte ses licences
   (`GPL-3.0.txt`, `GPL-2.0.txt`, `THIRD-PARTY.txt` — huit jobs le vérifient) et que
   `check_licenses.py` est vert : tout composant livré doit être nommé avec sa licence.

## Ce qui BLOQUE encore une release publique

- ~~Le § BLOQUANT du `TODO.md`~~ **Purgé le 2026-08-30** : l'historique est réécrit
  (`git filter-repo`), le dépôt ne suit plus ni ROM Atari, ni jeux, ni cartouches, ni
  Cubase Lite. Reste le pas **4** : basculer `NEOST_PACKAGE_NO_ATARI_TOS=1` par défaut,
  et supprimer ou re-couper les **assets des releases 0.5.2/0.5.4** (leurs paquets
  bureau contiennent `tos102uk.img` + `tos162uk.img` — vérifié).
- **Signature / notarisation** : le `.dmg` macOS n'est ni signé ni notarisé (Gatekeeper
  affiche « NeoST est endommagé »), le `.zip` Windows n'est pas signé. À traiter **après**
  la purge : signer un paquet qui contient des ROM Atari n'aurait pas de sens.

## Ce que la machine vérifie déjà

| Contrôle | Ce qu'il garde |
|---|---|
| `tools/check_release.py` | les trois numéros de version disent la même chose ; aucun numéro sauté en silence |
| `tools/check_licenses.py` | tout composant livré est nommé avec sa licence, README **et** `THIRD-PARTY.txt` |
| `tools/check_doc_claims.py` | les chiffres cités dans la doc se recomptent |
| `tools/check_doc_anchors.py` | les symboles cités existent encore |
| 8 jobs de CI | les fichiers de licence accompagnent chaque paquet |
