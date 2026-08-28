#!/usr/bin/env python3
# =============================================================================
#  run_megaste_diag.py — L'OBJECTIF DU PROJET, gardé par une machine (A25).
#
#  POURQUOI. L'objectif de tête du TODO — « émuler proprement un MegaSTE » — a été
#  déclaré ATTEINT le 2026-08-27 sur la foi de la suite Q du diagnostic Atari Field
#  Service (12/12)… validée À LA MAIN, non rejouable, non gardée : une régression
#  MegaSTE (SCU, SCC, DMA, RTC…) serait restée invisible de toute la pyramide de
#  tests, `etalons.json` l'avouait lui-même. Ce runner rejoue la recette exacte et
#  rend le 12/12 vérifiable à chaque palier full.
#
#  LA RECETTE (redécouverte le 2026-08-27, la session d'origine ne l'avait pas
#  consignée) : boot MegaSTE 1 Mo + cartouche `MegaSTE_Diagnostic_v1.5.bin`, TOS
#  2.06 US ; le menu du diag est à l'écran avant la trame 300 ; « Q » + Return
#  (scancodes 10,90,1c,9c) à la trame 320 ; bouclages série/MIDI/imprimante
#  branchés à 330 (--loopback-at, APRÈS l'injection datée — OUTIL-1) ; boîtier de
#  test DMA du kit armé (--dma-fixture). La suite complète (R,O,M,S,T,D,I,L,F,P,
#  Y,V) se termine entre les trames 5000 et 6500 avec --fastfdc (mesuré) ; on
#  roule 8000 trames de marge. Les disquettes A ET B sont des COPIES sacrificielles
#  de disks/diskA.st : le test F FORMATE les deux lecteurs (piège A14, déjà payé
#  deux fois — ne JAMAIS monter une image du dépôt en écriture ici).
#
#  LE VERDICT est lu dans le dump série, qui ÉCHOIT le texte de l'écran :
#    · exactement 11 lignes « Pass » (R,O,M,T,D,I,L,P, F×2 — un par lecteur —, Y),
#    · « No VME board » (fidèle : Hatari n'émule pas le VME, NeoST non plus),
#    · « Q Tests Completed »,
#    · ZÉRO « Fail ».
#  ⚠ « No loopback connector » apparaît dans le dump MÊME quand le test S passe :
#  la routine série du diag émet son message d'erreur COMME DONNÉES DE SONDE
#  (chaque caractère doit revenir par le bouclage). Ce n'est PAS un verdict — le
#  prendre pour tel a déjà coûté une enquête (cf. CHANGELOG 2026-08-27).
#
#  DÉPENDANCES PROPRIÉTAIRES, assumées : le TOS 2.06 ET la cartouche sont du
#  copyright Atari (§ BLOQUANT RELEASE). Absents → SKIP RECENSÉ (dit, jamais un
#  faux vert), même politique que rom_is_free() de run_etalons.py. Sur EmuTOS la
#  suite rend 11/12 : le test O (O.S. ROM) échoue LÉGITIMEMENT (CRC EmuTOS hors
#  table Atari) — c'est pourquoi l'étalon reste sur le TOS d'époque.
#
#  Usage : run_megaste_diag.py [--frames N] [--keep]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"
# Code de sortie « sauté, recensé » : ni succès ni échec — il manque des données
# NON REDISTRIBUABLES, et run_all.py doit le DIRE dans son bilan de fin.
EXIT_SKIPPED = 77
ROM = ROOT / "roms" / "tos206us.img"
CART = ROOT / "carts" / "MegaSTE_Diagnostic_v1.5.bin"
DISK = ROOT / "disks" / "diskA.st"

EXPECT_PASS = 11          # R,O,M,T,D,I,L,P,F(A),F(B),Y — un « Pass » chacun


def main() -> int:
    ap = argparse.ArgumentParser(description="Suite Q du diagnostic MegaSTE en headless")
    ap.add_argument("--frames", type=int, default=8000,
                    help="trames émulées (fin de suite mesurée entre 5000 et 6500)")
    ap.add_argument("--keep", action="store_true", help="garde le répertoire de travail")
    a = ap.parse_args()

    if not HEADLESS.exists():
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2
    missing = [p for p in (ROM, CART) if not p.exists()]
    if missing:
        # Fichiers Atari propriétaires : leur absence est un état LÉGITIME du dépôt
        # (post-purge § BLOQUANT RELEASE) — SKIP recensé, jamais un échec ni un
        # silence. La perte de couverture est DITE.
        for p in missing:
            print(f"  SKIP recensé : {p.relative_to(ROOT)} absent (copyright Atari — "
                  "cf. TODO § BLOQUANT RELEASE)")
        print("  ⚠ la garde MegaSTE 12/12 n'a PAS tourné — couverture amputée, pas verte.")
        # 77 (et non 0) depuis le 2026-08-28 : un 0 rendait ce SKIP invisible dans le
        # bilan de run_all.py, qui concluait « TOUS LES PALIERS OK » sur une étape qui
        # n'avait rien vérifié. Cf. EXIT_SKIPPED, même convention dans run_selftests.py
        # et run_midi_sequencer.py.
        return EXIT_SKIPPED
    if not DISK.exists():
        print(f"  ÉCHEC : {DISK.relative_to(ROOT)} absent (libre, régénérable par "
              "tools/make_floppy.py — son absence est une casse du dépôt)", file=sys.stderr)
        return 1

    work = Path(tempfile.mkdtemp(prefix="neost-msdiag-"))
    rc = 1
    try:
        sac_a, sac_b = work / "sacA.st", work / "sacB.st"
        shutil.copy(DISK, sac_a)
        shutil.copy(DISK, sac_b)
        dump = work / "serial.txt"
        # A14 (2026-08-28) — « --disk-ro » : le test F FORMATE les deux disquettes.
        # Les copies sacrificielles ci-dessus protégeaient déjà l'arbre git ; l'option
        # coupe le write-through vers le fichier, ce qui rend la protection
        # STRUCTURELLE au lieu d'être une discipline d'appelant. Vérifié le même jour :
        # avec et sans l'option, le dump série est BYTE-IDENTIQUE (11 Pass, 0 Fail,
        # « Q Tests Completed », « No VME board ») — la machine invitée ne voit rien,
        # elle relit ce qu'elle a écrit depuis l'image en RAM. Sans l'option les deux
        # fichiers changent de md5 ; avec, ils sont intacts. Les copies restent (une
        # ceinture ET des bretelles : elles couvrent tout chemin d'écriture hôte que
        # --disk-ro ne couvrirait pas, ACSI compris).
        cmd = [str(HEADLESS), str(ROM), "--machine", "megaste", "--mem", "1m",
               "--cart", str(CART), "--disk", str(sac_a), "--diskb", str(sac_b),
               "--fastfdc", "--disk-ro", "--scancode-at", "320", "10,90,1c,9c",
               "--loopback-at", "330", "--dma-fixture",
               "--frames", str(a.frames), "--serial-dump", str(dump)]
        print("  $", " ".join(cmd))
        try:
            r = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=600)
        except subprocess.TimeoutExpired:
            print("  TIMEOUT après 600s : " + " ".join(cmd), file=sys.stderr)
            return 1
        if r.returncode != 0 or not dump.exists():
            print(f"  ÉCHEC : neost-headless a rendu {r.returncode}", file=sys.stderr)
            sys.stderr.buffer.write(r.stderr[-2000:])
            return 1

        # GARDE A14 : les images montées doivent être INTACTES. C'est le seul endroit
        # de la pyramide où un programme invité formate vraiment une disquette — donc
        # le seul qui puisse prouver « --disk-ro » de bout en bout. Si un jour le
        # write-through revient, ce test le dit ICI, pas trois semaines plus tard dans
        # un `git status` surpris.
        for sac, orig in ((sac_a, DISK), (sac_b, DISK)):
            if sac.read_bytes() != orig.read_bytes():
                print(f"  ÉCHEC A14 : {sac.name} a été MODIFIÉ malgré --disk-ro — "
                      "le write-through vers le fichier hôte est revenu", file=sys.stderr)
                return 1

        text = dump.read_bytes().decode("latin-1")     # le dump porte des octets bruts
        n_pass = text.count("Pass")
        n_done = text.count("Q Tests Completed")
        n_fail = text.count("Fail")
        n_vme = text.count("No VME board")
        ok = (n_pass == EXPECT_PASS and n_done == 1 and n_fail == 0 and n_vme == 1)
        print(f"  suite Q : {n_pass}/{EXPECT_PASS} Pass, VME absent (fidèle) : "
              f"{'oui' if n_vme else 'NON'}, terminée : {'oui' if n_done else 'NON'}, "
              f"Fail : {n_fail}")
        if ok:
            print("PASS")
            rc = 0
        else:
            print("  ÉCHEC — le MegaSTE ne passe plus le banc Field Service.",
                  file=sys.stderr)
            rc = 1
        if rc != 0 or a.keep:
            print(f"  répertoire de travail : {work}  (serial.txt)")
        return rc
    finally:
        if not a.keep and rc == 0:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
