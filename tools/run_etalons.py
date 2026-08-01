#!/usr/bin/env python3
# =============================================================================
#  run_etalons.py — Suite headless des logiciels étalons (captures + régression).
#
#  Workflow :
#    1. python3 tools/fetch_etalons.py          # rapatrie les disques freeware
#    2. python3 tools/run_etalons.py --update-ref   # génère tests/reference/*.ppm
#    3. python3 tools/run_etalons.py            # compare NeoST vs références
#
#  Options :
#    --list              liste les étalons
#    --only ID[,ID…]     sous-ensemble
#    --fetch             fetch avant exécution
#    --update-ref        enregistre la capture NeoST comme référence
#    --oracle            régénère la référence via Hatari (si disque + params)
#    --no-compare        exécute seulement (pas de diff)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = Path(__file__).resolve().parent / "etalons.json"
OUT_DIR = ROOT / "tests" / "out"
REF_DIR = ROOT / "tests" / "reference"
HEADLESS = ROOT / "build" / "neost-headless"
COMPARE = ROOT / "tools" / "compare_screenshot.py"
HATARI_ORACLE = ROOT / "tools" / "hatari_oracle.sh"
BUFFER_W = 416   # largeur du buffer NeoST (overscan) ; un oracle Hatari est en 2× (≥832)


def load_manifest() -> list[dict]:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))["etalons"]


def ensure_disk(entry: dict) -> bool:
    gen = entry.get("disk_generate")
    disk = entry.get("disk")
    if not disk:
        return True
    path = ROOT / disk
    if path.exists():
        return True
    if gen:
        script = ROOT / gen
        print(f"  [gen] {script.relative_to(ROOT)} → {path.relative_to(ROOT)}")
        path.parent.mkdir(parents=True, exist_ok=True)
        out_arg = str(path)
        subprocess.run([sys.executable, str(script), out_arg], cwd=ROOT, check=True)
        return path.exists()
    return False


def run_selftest(flag: str, cpu: str) -> int:
    # Auto-tests logique pure (P0) : pas de boot, pas d'oracle. La ROM sert juste à
    # construire la machine (RAM) ; --glue-selftest/--spec512-selftest court-circuitent
    # le boot et renvoient le verdict via le code de sortie.
    cmd = [str(HEADLESS), "roms/etos256us.img", flag, "--cpu", cpu]
    print("  $", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


def run_headless_capture(entry: dict, out_ppm: Path) -> int:
    rom = entry.get("rom", "roms/etos192us.img")
    cmd = [str(HEADLESS), str(ROOT / rom), "--cpu", entry.get("cpu", "moira"),
           "--machine", entry.get("machine", "ste"),
           "--mem", entry.get("mem", "512k"),
           "--frames", str(entry.get("frames", 200)),
           "--screenshot", str(out_ppm)]
    if entry.get("fastfdc"):
        cmd.append("--fastfdc")
    disk = entry.get("disk")
    if disk:
        cmd += ["--disk", str(ROOT / disk)]
    if entry.get("keys"):
        cmd += ["--keys", entry["keys"]]
    if entry.get("keys_at"):                 # [trame, "touches"] ou liste de paires — pilotage daté
        ka = entry["keys_at"]
        pairs = ka if isinstance(ka[0], list) else [ka]
        for n, keys in pairs:
            cmd += ["--keys-at", str(n), keys]
    if entry.get("cart"):
        cmd += ["--cart", str(ROOT / entry["cart"])]
    print("  $", " ".join(cmd))
    r = subprocess.run(cmd, cwd=ROOT)
    return r.returncode


def run_hatari_oracle(entry: dict, out_png: Path) -> int:
    if not HATARI_ORACLE.exists():
        print("  [oracle] hatari_oracle.sh introuvable", file=sys.stderr)
        return 1
    rom = str(ROOT / entry.get("rom", "roms/etos192us.img"))
    disk = str(ROOT / entry["disk"])
    frames = int(entry.get("frames", 400))
    frame = int(entry.get("frame", frames - 10))
    # MARGE obligatoire : Hatari doit tourner AU-DELÀ de la trame extraite. Avec
    # run-vbls == frame, l'image demandée est la toute dernière de l'AVI — souvent
    # absente ou incomplète, et ffmpeg rend alors une image NOIRE. C'est ainsi que la
    # référence « oracle » de spectrum512_diapo était devenue entièrement noire : le
    # premier étalon spec512 du projet ne validait plus AUCUN pixel de rendu.
    vbls = str(max(frames, frame + 25))
    frame = str(frame)
    machine = entry.get("machine", "st")
    # Traduction de « mem » vers --memsize d'Hatari (0 = 512 Ko, sinon la taille en Mo).
    memTxt = str(entry.get("mem", "512k")).lower()
    memArg = "0" if memTxt.startswith("512") else memTxt.rstrip("m")
    cmd = ["bash", str(HATARI_ORACLE), rom, disk, vbls, frame, str(out_png), machine]
    # oracle_fastfdc : aligne la timeline Hatari sur le run NeoST --fastfdc (sans quoi
    # les numéros de trame divergent — le FDC réel charge chaque image bien plus tard).
    cmd.append("fastfdc" if entry.get("oracle_fastfdc") else "-")   # 7e arg positionnel
    cmd.append(memArg)                                             # 8e : taille RAM
    print("  $", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


def compare_shots(neost: Path, ref: Path, entry: dict) -> int:
    crop = entry.get("crop", "active")
    mx = entry.get("max_diff_px", 0)
    cmd = [sys.executable, str(COMPARE), str(neost), str(ref),
           "--crop", crop, "--max", str(mx), "--report"]
    print("  $", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


def resolve_ref(entry: dict, ref_ppm: Path, ref_png: Path):
    # Provenance EXPLICITE de la référence (P2) :
    #   ref_kind: "oracle"   → compare à l'oracle Hatari .png (jamais la self-capture).
    #   ref_kind: "snapshot" → compare à la self-capture NeoST .ppm (non-régression).
    # Défaut historique (absent) : .ppm sinon .png (toléré, mais un WARN invite à trancher).
    kind = entry.get("ref_kind")
    if kind == "oracle":
        # JAMAIS la self-capture : un oracle se compare au .png Hatari, point.
        return (ref_png, "oracle") if ref_png.exists() else (None, "oracle")
    if kind == "snapshot":
        # Self-capture NeoST : .ppm de préférence, sinon .png (certaines réfs V2 archivées).
        if ref_ppm.exists():
            return ref_ppm, "snapshot"
        return (ref_png, "snapshot") if ref_png.exists() else (None, "snapshot")
    if ref_ppm.exists():
        print(f"  ⚠ ref_kind absent — utilise la self-capture {ref_ppm.name} "
              f"(ajouter ref_kind: oracle|snapshot dans etalons.json)")
        return ref_ppm, "?"
    if ref_png.exists():
        return ref_png, "?"
    return None, kind or "?"


# Étalons dont la comparaison n'a PAS eu lieu (référence absente) : recensés pour
# que « TOUS OK » ne puisse jamais masquer une couverture creuse.
SKIPPED = []


def run_one(entry: dict, args) -> bool:
    eid = entry["id"]
    print(f"\n=== {eid} — {entry['name']} ===")

    selftest_flag = {"glue_selftest": "--glue-selftest",
                     "spec512_selftest": "--spec512-selftest",
                     "bus_selftest": "--bus-selftest",
                     "mfp_selftest": "--mfp-selftest",
                     "msa_selftest": "--msa-selftest"}.get(entry.get("type"))
    if selftest_flag:
        rc = run_selftest(selftest_flag, entry.get("cpu", "moira"))
        if rc != 0:
            print(f"  ÉCHEC {entry['type']} (exit {rc})")
            return False
        print(f"  OK {entry['type']}")
        return True

    if entry.get("disk") and not ensure_disk(entry):
        msg = f"  disque manquant : {entry['disk']}"
        if entry.get("optional"):
            print(msg + " (optionnel — SKIP)")
            return True
        print(msg, file=sys.stderr)
        return False

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    neost_ppm = OUT_DIR / f"{eid}_neost.ppm"
    ref_ppm = REF_DIR / f"{eid}.ppm"
    ref_png = REF_DIR / f"{eid}.png"

    if args.oracle and entry.get("disk"):
        REF_DIR.mkdir(parents=True, exist_ok=True)
        tmp_png = OUT_DIR / f"{eid}_oracle.png"
        if run_hatari_oracle(entry, tmp_png) != 0:
            return False
        shutil.copy2(tmp_png, ref_png)
        print(f"  référence oracle → {ref_png.relative_to(ROOT)}")

    rc = run_headless_capture(entry, neost_ppm)
    if rc != 0 or not neost_ppm.exists():
        print(f"  ÉCHEC capture NeoST (exit {rc})")
        return False
    print(f"  capture → {neost_ppm.relative_to(ROOT)}")

    if args.update_ref:
        REF_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copy2(neost_ppm, ref_ppm)
        print(f"  référence → {ref_ppm.relative_to(ROOT)}")
        return True

    if args.no_compare:
        return True

    ref, kind = resolve_ref(entry, ref_ppm, ref_png)
    if not ref:
        want = ".png (oracle)" if kind == "oracle" else ref_ppm.name
        print(f"  SKIP diff : pas de référence {want} (ref_kind={kind}) — "
              f"lancer {'--oracle' if kind == 'oracle' else '--update-ref'}")
        # La capture a EU LIEU mais il n'y a rien à quoi la comparer : ce n'est pas
        # la même chose qu'une donnée d'entrée absente, et le compter comme une
        # réussite transformait l'étalon en no-op parfaitement vert. On le RECENSE.
        SKIPPED.append(eid)
        # `optional` n'excuse que l'absence de DONNÉE D'ENTRÉE (disque non rapatriable).
        # Si le disque est là et que seule la référence manque, l'étalon prétend
        # surveiller quelque chose qu'il ne surveille pas : c'est un ÉCHEC. Sans cette
        # distinction, cuddly_demos — dont le disque EST suivi par git — restait un
        # no-op vert permanent, et la mutation « mauvais disque + 5 trames » passait.
        disk = entry.get("disk")
        if entry.get("optional") and disk and (ROOT / disk).exists():
            print(f"  ⚠ {eid}: disque présent mais référence absente — "
                  f"« optional » ne couvre PAS ce cas")
            return False
        return entry.get("optional", False)
    print(f"  référence : {ref.name} (ref_kind={kind})")

    if compare_shots(neost_ppm, ref, entry) != 0:
        print(f"  ÉCHEC diff {eid}")
        return False
    print(f"  OK {eid}")
    return True


def _png_dims(path: Path):
    import struct
    d = path.read_bytes()[:24]
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", d[16:24])


def _ref_content(path: Path) -> tuple[int, float] | None:
    """(nombre de couleurs plafonné à 9, part de la couleur MINORITAIRE) d'une référence.

    La part minoritaire départage une image réellement porteuse d'information d'une
    image quasi vide : un étalon de scroll est bichrome PAR CONSTRUCTION et valide
    pourtant chaque pixel, tandis qu'un écran noir avec trois pixels parasites ne
    valide rien. Renvoie None si l'image est illisible (ffmpeg absent, format
    inconnu) — l'appelant traite alors le contrôle comme non concluant.
    """
    try:
        sys.path.insert(0, str(Path(__file__).parent))
        import compare_screenshot as cs
        _w, _h, px = cs._load_image(path)
    except Exception:
        return None
    from collections import Counter
    c = Counter(px[i : i + 3] for i in range(0, len(px), 3))
    total = sum(c.values()) or 1
    return min(len(c), 9), (total - c.most_common(1)[0][1]) / total


def verify_refs(entries: list[dict]) -> int:
    # Contrôle de provenance (P2) : chaque entrée ref_kind=oracle doit avoir un .png
    # aux dimensions d'un oracle Hatari (2× le buffer NeoST, ex. 832×552) — sinon c'est
    # une self-capture renommée (piège : figerait un bug). Les snapshot doivent avoir un .ppm.
    bad = 0
    for e in entries:
        if e.get("type"):                 # selftests : pas de réf image
            continue
        eid = e["id"]; kind = e.get("ref_kind"); opt = e.get("optional")
        png = REF_DIR / f"{eid}.png"; ppm = REF_DIR / f"{eid}.ppm"
        if kind == "oracle":
            if not png.exists():
                if opt:
                    print(f"  · {eid}: oracle .png absent (optionnel — fetch/oracle à la demande)")
                else:
                    print(f"  ✗ {eid}: ref_kind=oracle mais {png.name} absent"); bad += 1
                continue
            dims = _png_dims(png)
            if not dims or dims[0] < 2 * BUFFER_W:
                print(f"  ✗ {eid}: {png.name} {dims} n'a pas la taille d'un oracle Hatari "
                      f"(≥ {2*BUFFER_W}px de large) — self-capture renommée ?"); bad += 1
            else:
                print(f"  ✓ {eid}: oracle {png.name} {dims[0]}×{dims[1]}")
        elif kind == "snapshot":
            if ppm.exists() or png.exists():
                print(f"  ✓ {eid}: snapshot {(ppm if ppm.exists() else png).name}")
            elif opt:
                print(f"  · {eid}: snapshot absent (optionnel — fetch à la demande)")
            else:
                print(f"  ✗ {eid}: ref_kind=snapshot mais aucune réf ({ppm.name}/{png.name})"); bad += 1
        else:
            print(f"  ⚠ {eid}: ref_kind absent (ajouter oracle|snapshot)")

        # GARDE-FOU DE VACUITÉ. Une référence UNIFORME (écran noir) valide zéro pixel
        # tout en affichant « OK » : c'est arrivé deux fois sur cet étalon spec512, et
        # le contrôle de provenance ne regardait que les dimensions. Une image de moins
        # de 3 couleurs n'est pas une preuve — sauf pour les étalons dont c'est le
        # signal voulu (trace_odd peint l'écran en vert/rouge selon le verdict,
        # overscan_top n'a que 2 couleurs par construction) : "uniform_ok": true.
        ref = png if png.exists() else (ppm if ppm.exists() else None)
        if ref is not None and not e.get("uniform_ok"):
            got = _ref_content(ref)
            if got is None:
                # ÉCHEC, pas avertissement : un contrôle qui ne peut pas s'exécuter ne
                # prouve RIEN, et il échouait jusqu'ici « en mode ça passe ». Sans
                # ffmpeg, les 6 références .png — dont les TROIS oracles spec512,
                # justement l'étalon dont la référence a été noire deux fois — sortaient
                # « RÉFS OK », code 0. Une référence noircie repassait donc verte sur
                # toute machine sans ffmpeg. Le remède est d'installer ffmpeg, pas de
                # laisser le garde-fou s'auto-désarmer.
                print(f"  ✗ {eid}: vacuité NON VÉRIFIABLE ({ref.name} illisible — "
                      f"ffmpeg absent ?) — contrôle non concluant, donc refusé")
                bad += 1
            else:
                nc, minor = got
                if nc <= 1 or minor < 0.005:
                    print(f"  ✗ {eid}: référence {ref.name} QUASI UNIFORME "
                          f"({nc} couleur(s), minoritaire {minor:.3%}) — "
                          f"elle ne valide aucun pixel"); bad += 1
    print("\n" + ("RÉFS OK" if bad == 0 else f"{bad} réf(s) suspecte(s)"))
    return 0 if bad == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Suite étalons NeoST (headless)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", help="IDs séparés par des virgules")
    ap.add_argument("--fetch", action="store_true", help="fetch_etalons.py d'abord")
    ap.add_argument("--update-ref", action="store_true", help="sauve la capture NeoST en référence")
    ap.add_argument("--oracle", action="store_true", help="capture Hatari comme référence PNG")
    ap.add_argument("--verify-refs", action="store_true",
                    help="contrôle la provenance des références (oracle vs snapshot)")
    ap.add_argument("--no-compare", action="store_true")
    args = ap.parse_args()

    if not HEADLESS.exists() and not args.verify_refs:
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2

    entries = load_manifest()
    if args.verify_refs:
        return verify_refs([e for e in entries
                            if not args.only or e["id"] in args.only.split(",")])
    if args.list:
        for e in entries:
            opt = " [opt]" if e.get("optional") else ""
            print(f"  {e['id']:20} {e.get('subsystem','?'):16} {e['name']}{opt}")
        return 0

    if args.fetch:
        ids = args.only.split(",") if args.only else []
        cmd = [sys.executable, str(ROOT / "tools" / "fetch_etalons.py")] + ids
        subprocess.run(cmd, cwd=ROOT, check=False)

    # strip() : « --only "a, b" » traitait « b » (avec son espace) comme un ID inconnu
    # et refusait de tourner, en DÉSIGNANT un identifiant pourtant valide.
    want = {t.strip() for t in args.only.split(",") if t.strip()} if args.only else None
    # Un ID inconnu ne doit PAS rendre « TOUS OK » sur zéro étalon exécuté : run_all.py
    # passe au palier P0 une liste d'IDs CODÉE EN DUR, donc un simple renommage dans le
    # manifeste viderait le garde-fou logique du hook pre-push sans un mot.
    if want:
        unknown = want - {e["id"] for e in entries}
        if unknown:
            print("ID inconnu(s) : " + ", ".join(sorted(unknown)))
            return 2
    ok = True
    ran = 0
    disabled = []
    for entry in entries:
        if want and entry["id"] not in want:
            continue
        # Étalon DÉSACTIVÉ : il ne valide rien et le dit. Mieux vaut l'assumer ainsi
        # qu'un « optional » vert qui laisse croire à une couverture inexistante — la
        # raison est obligatoire et s'affiche à chaque exécution.
        if entry.get("disabled"):
            disabled.append((entry["id"], entry["disabled"]))
            continue
        ran += 1
        if not run_one(entry, args):
            ok = False
    if ran == 0:
        print("\nAUCUN étalon exécuté — filtre trop restrictif ?")
        return 2
    if disabled:
        print(f"\n⛔ DÉSACTIVÉS ({len(disabled)}) — ne valident RIEN :")
        for eid, why in disabled:
            print(f"  · {eid} : {why}")
    if SKIPPED:
        print(f"\n⚠ NON COMPARÉS ({len(SKIPPED)}) : " + ", ".join(SKIPPED)
              + "\n  (capture faite, mais aucune référence — ces étalons ne valident RIEN)")
    print("\n" + ("TOUS OK" if ok else "ÉCHECS — voir ci-dessus"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
