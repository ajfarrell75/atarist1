#!/usr/bin/env python3
# =============================================================================
#  run_perfbench.py — Barrière de DÉBIT (chantier A6).
#
#  POURQUOI. Le projet avait un `cycle-bench` qui garde le MODÈLE de cycle, mais
#  rien qui garde le TEMPS MUR. Constaté le 2026-08-25 : le chantier BL4 a ajouté
#  un `syncTo` par accès bus du blitter — de l'ordre de 370 000 appels de plus sur
#  6000 trames — et personne ne l'a mesuré, ni la CI vue. Or le mode borne sur
#  Raspberry Pi est une cible déclarée du projet : un émulateur temps réel doit
#  avoir une barrière de débit, pas seulement de justesse.
#
#  ⚠ LE PIÈGE DE CE GENRE DE BANC, et comment il est contourné. Un seuil ABSOLU en
#  trames/s est ingérable : un runner de CI est 2 à 3 fois plus lent qu'un poste de
#  travail, et sa charge varie d'un run à l'autre. Un seuil serré serait donc
#  instable (donc désarmé au bout de trois faux rouges), et un seuil lâche ne
#  verrait rien. On garde donc des RATIOS entre charges mesurées DANS LE MÊME RUN :
#  le boot EmuTOS nu sert d'étalon de vitesse machine, et les charges lourdes sont
#  exprimées par rapport à lui. Un ratio est INDÉPENDANT de la vitesse de la
#  machine — il ne bouge que si le coût RELATIF d'un chemin change, ce qui est
#  précisément ce qu'on veut attraper.
#
#  Les débits absolus sont quand même AFFICHÉS à chaque run : ils ne gardent rien,
#  mais ils rendent une dérive visible à l'œil sur la durée.
#
#  MESURE DE RÉFÉRENCE (2026-08-26, poste macOS Silicon) — coût réel du chantier
#  blitter, la question laissée ouverte à l'époque :
#    boot EmuTOS sans blitter : 1733 → 1707 tr/s   (+1,5 % de temps)
#    blitter non-hog STE      : 1504 → 1523 tr/s   (−1,3 % de temps)
#    poll MFP Timer A         : 1319 → 1317 tr/s   (+0,2 % de temps)
#  Autrement dit : BL3+BL4 ne coûtent RIEN de mesurable. `Scheduler::syncTo` est
#  O(1) quand rien n'est dû (cache `nextDue_`), donc les appels supplémentaires
#  sont gratuits. L'inquiétude était légitime ; la mesure la lève.
#
#  Usage : run_perfbench.py [--update-ratios]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suffixe .exe sous Windows : sans lui, le binaire est cherché sous un nom qui
# n'existe pas et la suite se déclare « non bâtie » — la seule plateforme livrée
# où les tests ne pouvaient pas tourner du tout.
_EXE = ".exe" if sys.platform == "win32" else ""
HEADLESS = ROOT / "build" / ("neost-headless" + _EXE)
RATIOS = Path(__file__).resolve().parent / "perfbench_ratios.json"

FRAMES = 600
REPS = 3          # on garde le MEILLEUR temps : le bruit d'ordonnancement ne fait
                  # que RALENTIR, jamais accélérer — le min est donc l'estimateur
                  # le moins bruité du coût réel.

# La première charge est l'ÉTALON DE VITESSE MACHINE (ratio 1 par construction).
LOADS = [
    ("boot", "boot EmuTOS nu (aucun blitter, aucun disque)",
     ["roms/etos192fr.img", "--machine", "st"]),
    ("blitter", "blitter non-hog STE (~400 tranches/run)",
     ["roms/etos256us.img", "--machine", "ste", "--mem", "512k",
      "--disk", "disks/etalons/blitter_timer.st"]),
    ("mfp", "poll IPRA du MFP pendant Timer A",
     ["roms/etos192fr.img", "--machine", "st", "--mem", "512k",
      "--disk", "disks/etalons/mfp_poll.st"]),
]

# Tolérance sur les ratios. Large À DESSEIN : ce banc n'est pas là pour détecter
# 5 % de mieux ou de moins — il est là pour attraper un chemin qui devient DEUX
# FOIS plus cher sans que personne ne le remarque. Un seuil serré serait désarmé
# à son troisième faux rouge, et ne garderait alors plus rien du tout.
TOLERANCE = 0.25

# Garde-fou A18 (audit 2026-08-27) : AUCUN appel à l'émulateur n'avait de timeout —
# un 68000 qui boucle (la régression même que ces tests cherchent) consommait les
# 45 min du job CI sans diagnostic. Verdict 124 (convention de timeout(1)) pour les
# appels à code de retour ; sortie 2 immédiate pour les appels check=True (leur
# échec est de toute façon fatal au run).
def run_timed(cmd, limit_s, **kw):
    try:
        return subprocess.run(cmd, timeout=limit_s, **kw)
    except subprocess.TimeoutExpired:
        print(f"  TIMEOUT après {limit_s}s : " + " ".join(map(str, cmd)),
              file=sys.stderr, flush=True)
        if kw.get("check"):
            sys.exit(2)
        return subprocess.CompletedProcess(cmd, 124)



def measure(args) -> float:
    ts = []
    for _ in range(REPS):
        t0 = time.perf_counter()
        run_timed([str(HEADLESS)] + args + ["--frames", str(FRAMES)],
                  300, cwd=ROOT, capture_output=True)
        ts.append(time.perf_counter() - t0)
    return FRAMES / min(ts)


def measure_all() -> dict:
    """Un passage complet du banc → {clé: ratio vs boot}."""
    print(f"Banc de débit ({FRAMES} trames, meilleur de {REPS}) :")
    rates = {}
    for key, label, args in LOADS:
        rates[key] = measure(args)
        print(f"  {label:44s} {rates[key]:7.0f} tr/s")
    base = rates[LOADS[0][0]]
    return {k: v / base for k, v in rates.items()}


def compare(measured: dict, ref: dict) -> list:
    """Affiche les ratios et renvoie la liste des clés hors tolérance."""
    print(f"\nRatios (indépendants de la vitesse machine, tolérance ±{TOLERANCE:.0%}) :")
    bad = []
    for k, v in measured.items():
        if k not in ref:
            print(f"  ⚠ {k}/boot = {v:.3f} — pas de référence (ajouter via --update-ratios)")
            continue
        drift = (v - ref[k]) / ref[k]
        mark = "OK  " if abs(drift) <= TOLERANCE else "✗   "
        print(f"  {mark}{k}/boot = {v:.3f} (réf {ref[k]:.3f}, {drift:+.1%})")
        if abs(drift) > TOLERANCE:
            bad.append(k)
    return bad


def main() -> int:
    if not HEADLESS.exists():
        print(f"neost-headless absent ({HEADLESS}) — bâtir d'abord.")
        return 1
    update = "--update-ratios" in sys.argv

    measured = measure_all()

    if update or not RATIOS.exists():
        RATIOS.write_text(json.dumps(measured, indent=2) + "\n", encoding="utf-8")
        print(f"\nRatios de référence écrits dans {RATIOS.name} :")
        for k, v in measured.items():
            print(f"  {k}/boot = {v:.3f}")
        return 0

    ref = json.loads(RATIOS.read_text(encoding="utf-8"))
    bad = compare(measured, ref)

    # SECONDE PASSE avant de rougir (2026-08-28). Le message d'échec disait déjà
    # « relancer avant de conclure » — il le disait à l'humain, et la CI, elle,
    # rougissait. Observé ce jour-là : un `--tier full` a rendu blitter/boot à
    # −32,0 % (donc ÉCHEC) alors que deux passages isolés du même binaire, aussitôt
    # après, donnaient −2,4 % et −6,8 % : la machine bâtissait un oracle Hatari en
    # parallèle. Les charges sont mesurées SÉQUENTIELLEMENT — une bouffée de charge
    # qui couvre une charge mais pas l'étalon de vitesse fausse le ratio, et le
    # « meilleur de REPS » n'y peut rien si la bouffée dure plus que les REPS.
    # La seconde passe ne coûte QUE dans le cas qui allait échouer, et un vrai
    # surcoût de chemin la franchit deux fois. Un garde-fou qui crie au loup finit
    # désarmé — c'est la leçon du 2026-08-25 sur les grandeurs dépendantes de la
    # charge, appliquée à l'outil qui la mesure.
    if bad:
        print(f"\n⚠ hors tolérance au 1ᵉʳ passage : {', '.join(bad)} — SECONDE PASSE "
              "(une bouffée de charge fausse un ratio ; un vrai surcoût, non).")
        measured2 = measure_all()
        bad2 = compare(measured2, ref)
        confirmed = [k for k in bad if k in bad2]
        if not confirmed:
            print("\nDÉBIT OK (le 1ᵉʳ passage était bruité — écart NON reproduit)")
            return 0
        print(f"\nÉCHEC — débit relatif hors tolérance aux DEUX passages : "
              f"{', '.join(confirmed)}.")
        print("Un chemin est devenu nettement plus cher. Si le changement est VOULU "
              "et mesuré, réétalonner avec --update-ratios.")
        return 1
    print("\nDÉBIT OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
