#!/usr/bin/env python3
# =============================================================================
#  check_headless_options.py — Auto-tests du HARNAIS lui-même (chantier A4).
#
#  POURQUOI. `neost-headless` EST le framework de test du projet : toute la preuve
#  produite ici passe par ses options. Or il avait des modes d'échec SILENCIEUX, et
#  la passe du 2026-08-25 l'a payé cher — TROIS « bloquants » sur huit venaient de
#  l'instrument et non de l'émulateur (Xenon 2, Flood, Dynamite Dux, tous jouables
#  une fois la repro corrigée), parce que `--joy-at` / `--joy-script` / `--mouse-at`
#  étaient des SCALAIRES : la dernière occurrence écrasait les précédentes sans le
#  moindre avertissement. Un verdict « confirmé à l'oracle » a même été rendu FAUX
#  par la durée d'appui (40 ms côté NeoST contre ~600 ms côté Hatari).
#
#  `neost-selftest` couvre « la logique pure » — et le parsing d'arguments EN EST.
#  Ces tests-ci sont en BOÎTE NOIRE (on lance le binaire et on lit ce qu'il fait)
#  parce que le parsing vit dans main_headless.cpp, hors de toute bibliothèque :
#  l'extraire pour le tester coûterait plus cher que la panne qu'on veut éviter.
#
#  RÈGLE : chaque test vérifie un COMPORTEMENT OBSERVABLE, jamais une implémentation.
#  Et chacun porte le bug qu'il empêche de revenir.
#
#  Usage : check_headless_options.py   (code de sortie 1 au premier échec)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"
ROM = ROOT / "roms" / "etos192fr.img"          # ROM LIBRE : ces tests survivent au
                                               # retrait des TOS Atari propriétaires.

fails = []


def run(args, frames=60):
    cp = subprocess.run([str(HEADLESS), str(ROM), "--frames", str(frames)] + args,
                        cwd=ROOT, capture_output=True, text=True, timeout=180)
    return cp.stdout + cp.stderr


def check(name, cond, why, detail=""):
    if cond:
        print(f"  OK   {name}")
    else:
        print(f"  ✗    {name}\n       {why}")
        if detail:
            print(f"       observé : {detail.strip()[:300]}")
        fails.append(name)


def main() -> int:
    if not HEADLESS.exists():
        print(f"neost-headless absent ({HEADLESS}) — bâtir d'abord.")
        return 1
    print("Auto-tests du harnais headless (options de pilotage) :")

    # --- OUTIL-1 : les options de pilotage sont RÉPÉTABLES --------------------
    # Le bug : scalaires, dernière occurrence gagnante, sans avertissement. Trois
    # faux « bloquants » en une seule passe de balayage.
    out = run(["--joy-at", "10", "0x80", "--joy-at", "30", "0x08"])
    n = out.count("joystick applied at frame")
    check("--joy-at est répétable", n == 2,
          "deux --joy-at doivent produire DEUX applications ; une seule = retour du "
          "scalaire « dernière occurrence gagnante » (OUTIL-1).",
          f"{n} ligne(s) « joystick applied »")

    # Le corollaire : c'est bien la PREMIÈRE qui a été jouée, pas seulement la dernière.
    check("--joy-at joue la PREMIÈRE occurrence",
          "frame 10: port1=$80" in out,
          "l'application de la trame 10 manque : les occurrences sont écrasées.",
          out)

    # --- Durée d'appui réglable (A4) -----------------------------------------
    # Le bug : 2 trames câblées en dur (~40 ms), incomparable au --cmd-fifo d'Hatari
    # (~600 ms). C'est ce qui a rendu FAUX un verdict « confirmé à l'oracle ».
    out = run(["--key-hold", "20", "--keys-at", "10", "a"], frames=60)
    check("--key-hold est accepté", "unknown option" not in out.lower(),
          "l'option de durée d'appui doit exister : sans elle, aucune A/B honnête "
          "contre l'oracle n'est possible.", out)

    # --- Scancodes bruts (A4) : le pavé numérique redevient atteignable -------
    out = run(["--scancode-at", "10", "70,6d,6e"], frames=60)
    check("--scancode-at est accepté", "unknown option" not in out.lower(),
          "les scancodes bruts doivent exister : stScancode() ne mappe pas le pavé "
          "numérique, ce qui rendait les menus de compilation impilotables.", out)

    # --- Garde-fou anti-« aide silencieuse » ---------------------------------
    # Piège zsh déjà consigné en mémoire projet : une variable multi-mots non quotée
    # devient UN argument, et le headless affiche son aide au lieu d'émuler. Un test
    # qui ne verrait pas la différence validerait du vide.
    out = run([])
    check("un run nominal ÉMULE (il n'affiche pas son aide)",
          "usage:" not in out.lower() and "video:" in out.lower(),
          "un run sans option doit émuler et rapporter « video: … Hz » ; s'il affiche "
          "l'aide, tous les autres tests mesureraient du vide.", out)

    print()
    if fails:
        print(f"ÉCHEC — {len(fails)} test(s) du harnais : {', '.join(fails)}")
        print("L'instrument est en panne : tant qu'il l'est, AUCUNE mesure produite "
              "avec lui ne vaut quoi que ce soit.")
        return 1
    print("HARNAIS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
