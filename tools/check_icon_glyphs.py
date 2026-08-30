#!/usr/bin/env python3
# =============================================================================
#  check_icon_glyphs.py — Les pictogrammes de l'interface SORTENT-ILS ?
#
#  Deux façons de perdre une icône en SILENCE, toutes deux vécues :
#
#  1. Le codepoint n'existe pas dans fa-solid-900.ttf (macro tapée à la main,
#     icône « Regular » ou « Brands » qui n'est pas dans la variante Solid) →
#     ImGui rend le glyphe de repli, personne ne le voit passer.
#
#  2. La POLICE DE TEXTE revendique le même codepoint. DejaVu Sans occupe une
#     partie de la zone à usage privé (U+F000-F003 = ses ligatures ff/fi/fl/ffi
#     héritées, plus U+F400+), exactement là où vit Font Awesome. Or ImGui
#     parcourt ses sources DANS L'ORDRE et retient la PREMIÈRE qui sait fournir
#     le codepoint (ImFontBaked_BuildLoadGlyph, imgui_draw.cpp) : la police de
#     base, chargée en premier, gagne. Vécu le 2026-08-30 — ICON_FA_MUSIC
#     (U+F001), la note de la page MIDI, sortait en ligature « fi » illisible.
#     Le remède est le champ GlyphExcludeRanges d'ImFontConfig, posé sur la
#     police de TEXTE ; ce script exige qu'il soit là tant qu'un recouvrement
#     existe.
#
#  Aucune machine, aucune ROM : logique pure, palier `fast`.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ICONS_HPP = ROOT / "src" / "gui" / "UiCommon.hpp"
APPINIT    = ROOT / "src" / "gui" / "AppInit.cpp"
FONT_TEXT  = ROOT / "fonts" / "DejaVuSans.ttf"
FONT_ICONS = ROOT / "fonts" / "fa-solid-900.ttf"


def cmap_codepoints(path: Path) -> set[int]:
    """Codepoints couverts par la table cmap (format 4) d'un .ttf."""
    data = path.read_bytes()
    ntab = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(ntab):
        off = 12 + 16 * i
        tag = data[off:off + 4].decode("latin1")
        tables[tag] = struct.unpack(">II", data[off + 8:off + 16])
    if "cmap" not in tables:
        raise ValueError(f"{path.name} : pas de table cmap")
    base = tables["cmap"][0]
    nsub = struct.unpack(">H", data[base + 2:base + 4])[0]
    sub4 = None
    for i in range(nsub):
        _pid, _eid, off = struct.unpack(">HHI", data[base + 4 + 8 * i:base + 4 + 8 * i + 8])
        if struct.unpack(">H", data[base + off:base + off + 2])[0] == 4:
            sub4 = base + off
    if sub4 is None:
        raise ValueError(f"{path.name} : pas de sous-table cmap format 4")
    seg_x2 = struct.unpack(">H", data[sub4 + 6:sub4 + 8])[0]
    ends = sub4 + 14
    starts = ends + seg_x2 + 2
    codes: set[int] = set()
    for s in range(seg_x2 // 2):
        end = struct.unpack(">H", data[ends + 2 * s:ends + 2 * s + 2])[0]
        sta = struct.unpack(">H", data[starts + 2 * s:starts + 2 * s + 2])[0]
        if sta == 0xFFFF:
            continue
        codes.update(range(sta, min(end, 0xFFFF) + 1))
    return codes


def icon_macros() -> dict[str, int]:
    """ICON_FA_* → codepoint, décodé depuis les octets UTF-8 littéraux."""
    out = {}
    for m in re.finditer(r'#define\s+(ICON_FA_\w+)\s+"((?:\\x[0-9a-fA-F]{2})+)"',
                         ICONS_HPP.read_text(encoding="utf-8")):
        raw = bytes(int(b, 16) for b in re.findall(r"\\x([0-9a-fA-F]{2})", m.group(2)))
        try:
            out[m.group(1)] = ord(raw.decode("utf-8"))
        except (UnicodeDecodeError, TypeError):
            out[m.group(1)] = -1        # séquence UTF-8 invalide → signalée plus bas
    return out


def main() -> int:
    for f in (ICONS_HPP, APPINIT, FONT_TEXT, FONT_ICONS):
        if not f.exists():
            print(f"  ✗ fichier absent : {f.relative_to(ROOT)}", file=sys.stderr)
            return 1

    icons = icon_macros()
    if not icons:
        print("  ✗ aucune macro ICON_FA_* trouvée — le parseur ou le fichier a changé",
              file=sys.stderr)
        return 1
    fa = cmap_codepoints(FONT_ICONS)
    txt = cmap_codepoints(FONT_TEXT)
    bad = 0

    # (1) chaque icône existe-t-elle dans la police d'icônes ?
    for name, cp in sorted(icons.items(), key=lambda kv: kv[1]):
        if cp < 0:
            print(f"  ✗ {name} : séquence UTF-8 invalide dans la macro", file=sys.stderr)
            bad += 1
        elif cp not in fa:
            print(f"  ✗ {name} (U+{cp:04X}) ABSENT de {FONT_ICONS.name} — rendu en glyphe "
                  f"de repli, invisible à l'usage", file=sys.stderr)
            bad += 1

    # (2) la police de TEXTE revendique-t-elle des codepoints d'icônes ?
    clash = {n: c for n, c in icons.items() if c in txt}
    if clash:
        src = APPINIT.read_text(encoding="utf-8")
        if "GlyphExcludeRanges" not in src:
            print(f"  ✗ {len(clash)} icône(s) revendiquée(s) AUSSI par {FONT_TEXT.name} "
                  f"et AppInit.cpp ne pose pas GlyphExcludeRanges sur la police de texte :",
                  file=sys.stderr)
            for n, c in sorted(clash.items(), key=lambda kv: kv[1]):
                print(f"      {n} (U+{c:04X})", file=sys.stderr)
            print("    ImGui retient la PREMIÈRE source qui sait fournir le codepoint : "
                  "la police de texte gagne, l'icône disparaît.", file=sys.stderr)
            bad += 1
        else:
            names = ", ".join(sorted(clash))
            print(f"  · {len(clash)} recouvrement(s) police de texte / icônes ({names}) "
                  f"— neutralisé(s) par GlyphExcludeRanges")

    if bad:
        return 1
    print(f"ICÔNES OK — {len(icons)} macros, toutes présentes dans {FONT_ICONS.name}, "
          f"aucune masquée par la police de texte.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
