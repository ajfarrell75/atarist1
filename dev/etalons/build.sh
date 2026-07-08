#!/usr/bin/env bash
# =============================================================================
#  build.sh — Compile un exemple GODLIB (devkit Reservoir Gods) en .TOS 68000
#  et le dépose dans gemdos/etalon/ (monté en C: par le HD GEMDOS de NeoST).
#
#  Usage : dev/etalons/build.sh BOX          (un exemple de GODLIB.SPL/)
#
#  Chaîne : vbcc 0.9g (vc/vbccm68k) + vasmm68k_mot + vlink, cible m68k-atari —
#  binaires dans TOOLS.RG/BIN/LINUX/ (bâtis par setup-toolchain.sh), flags
#  repris de GODLIB/MAKEFILE/PLATFORM/GODLIB_ST.MF. La liste des sources est
#  extraite du .PRJ Pure C de l'exemple (le startup Pure C y est remplacé par
#  startup16.o + -lvc -lm, comme dans GODLIB_ST.MF).
#
#  Piège : les #include GODLIB utilisent des BACKSLASHES (<GODLIB\X\Y.H>) que
#  ucpp/vbcc ne résout pas sous Linux → on compile depuis un arbre de build
#  (dev/etalons/build/) copié avec les lignes #include converties \ → /. Les
#  sources vendorisées dans dev/reservoir-gods/ restent intactes.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RG="$ROOT/dev/reservoir-gods"
TOOLS="$RG/TOOLS.RG/BIN/LINUX"
TARGET="$RG/TOOLS.RG/VBCC/0.9g/vbcc_target_m68k-atari"
BUILD="$ROOT/dev/etalons/build"
OUT="$ROOT/gemdos/etalon"

SAMPLE="${1:?usage: build.sh <EXEMPLE> (ex: BOX, JAGPAD, MIXER…)}"
SPL="$RG/GODLIB.SPL/$SAMPLE"
PRJ="$SPL/$SAMPLE.PRJ"
[ -f "$PRJ" ] || { echo "!! $PRJ introuvable" >&2; exit 1; }

export PATH="$TOOLS:$PATH" VBCC="$TARGET"

# --- 1. Arbre de build : GODLIB + l'exemple, #include \ → / -------------------
mkdir -p "$BUILD" "$OUT"
rsync -a --delete --include='*/' --include='*.[CHSI]' --exclude='*' "$RG/GODLIB/" "$BUILD/GODLIB/"
rsync -a --delete "$SPL/" "$BUILD/$SAMPLE/"
find "$BUILD" -type f \( -name '*.C' -o -name '*.H' \) \
    -exec sed -i -E '/^[[:space:]]*#[[:space:]]*include/ s#\\#/#g' {} +
# Dialecte PureBot/Devpac → vasm : blocs `rept` fermés par `endm` (vasm exige
# `endr`, pile rept/macro pour ne réécrire que les bons) et directive `LOCAL`
# dans les macros (inconnue de vasm → labels suffixés `\@`, l'id unique par
# expansion, et ligne LOCAL supprimée).
find "$BUILD" -type f \( -name '*.S' -o -name '*.I' \) | while read -r f; do
    awk '{
        l = tolower($0); sub(/\r$/, "", l); sub(/;.*/, "", l); out = $0   # sources CRLF
        inmac = 0; for (i = 1; i <= d; i++) if (st[i] == "m") inmac = 1
        if      (l ~ /(^|[ \t])rept([ \t]|$)/)       st[++d] = "r"
        else if (l ~ /(^|[ \t])macro([ \t]|$)/) {
            st[++d] = "m"; nloc = 0; npar = 0
            # Forme PureBot « MACRO nom param1,param2… » : paramètres NOMMÉS,
            # inconnus de vasm (qui ne connaît que \1-\9) → on mémorise les noms
            # pour les substituer par position dans le corps, et on ne garde que
            # « macro nom » sur la ligne.
            line = $0; sub(/\r$/, "", line); sub(/;.*/, "", line)
            if (match(line, /^[ \t]*[Mm][Aa][Cc][Rr][Oo][ \t]+/)) {
                rest = substr(line, RSTART + RLENGTH)
                n = split(rest, w, /[ \t,]+/)
                for (i = 2; i <= n; i++) if (w[i] != "") par[++npar] = w[i]
                if (npar > 0) out = "\tmacro\t" w[1]
            }
        }
        else if (l ~ /^[ \t]*endr([ \t]|$)/)         { if (d > 0) d-- }
        else if (l ~ /^[ \t]*endm([ \t]|$)/) {
            if (d > 0 && st[d] == "r") { sub(/[Ee][Nn][Dd][Mm]/, "endr", out); d-- }
            else if (d > 0) { d--; nloc = 0; npar = 0 }
        }
        else if (inmac && l ~ /^[ \t]*local[ \t]/) {
            line = $0; sub(/\r$/, "", line); sub(/;.*/, "", line)
            sub(/^[ \t]*[Ll][Oo][Cc][Aa][Ll][ \t]+/, "", line); gsub(/[ \t]/, "", line)
            n = split(line, a, ","); for (i = 1; i <= n; i++) loc[++nloc] = a[i]
            next
        }
        if (inmac && nloc > 0)
            for (i = 1; i <= nloc; i++) out = gensub("\\y" loc[i] "\\y", loc[i] "\\\\@", "g", out)
        if (inmac && npar > 0)
            for (i = 1; i <= npar; i++) out = gensub("\\y" par[i] "\\y", "\\\\" i, "g", out)
        print out
    }' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
done
# Shim de casse : GODLIB inclut les en-têtes système en MAJUSCULES (<STDIO.H>,
# héritage Pure C insensible à la casse) ; la cible vbcc les fournit en
# minuscules → symlinks majuscules.
mkdir -p "$BUILD/sysinc"
for h in "$TARGET/targets/m68k-atari/include"/*.h; do
    ln -sf "$h" "$BUILD/sysinc/$(basename "$h" | tr 'a-z' 'A-Z')"
done

# --- 2. Sources listées dans le .PRJ (après la ligne '=') ---------------------
mapfile -t SRCS < <(awk '/^=/{go=1;next} go' "$PRJ" \
    | sed -E 's/;.*//; s/[[:space:]]+$//; s#\\#/#g' \
    | grep -E '\.(C|S)$' || true)
[ ${#SRCS[@]} -gt 0 ] || { echo "!! aucune source .C/.S dans $PRJ" >&2; exit 1; }

CC_FLAGS=(-cpu=68000 -c -c99 -size -O2)                       # GODLIB_ST.MF (FINAL)
AS_FLAGS=(-m68000 -Felf -noesc -nowarn=2049 -guess-ext)       # GODLIB_ST.MF + guess-ext (vasm récent strict)

OBJDIR="$BUILD/obj/$SAMPLE"; rm -rf "$OBJDIR"; mkdir -p "$OBJDIR"   # build complet à chaque run
C_OBJS=(); S_OBJS=()
for src in "${SRCS[@]}"; do
    case "$src" in
        ../../GODLIB/*) path="$BUILD/GODLIB/${src#../../GODLIB/}" ;;
        *)              path="$BUILD/$SAMPLE/$src" ;;
    esac
    obj="$OBJDIR/$(echo "$src" | sed 's#[/.]#_#g').o"
    if [[ "$src" == *.C ]]; then
        echo "CC $src"
        vc "+$TARGET/vc.config" "${CC_FLAGS[@]}" -I"$BUILD" -I"$BUILD/sysinc" "$path" -o "$obj"
        C_OBJS+=("$obj")
    else
        echo "AS $src"
        # Repli 68020 : les .PRJ Pure C assemblaient avec -2 (68020) ; certains
        # fichiers (VECTOR_S.S) utilisent des modes (bd,Dn) inexistants en 68000.
        vasmm68k_mot "${AS_FLAGS[@]}" -quiet -I"$(dirname "$path")" "$path" -o "$obj" 2>/dev/null \
        || { echo "   (repli -m68020 pour $src)";
             vasmm68k_mot "${AS_FLAGS[@]/-m68000/-m68020}" -quiet -I"$(dirname "$path")" "$path" -o "$obj"; }
        S_OBJS+=("$obj")
    fi
done

# --- 3. Édition de liens (GODLIB_ST.MF : startup16.o + -lvc -lm) --------------
# Ordre de GODLIB_ST.MF : objets C, startup, -lvc -lm, puis -set-adduscore et
# LES OBJETS ASM EN DERNIER — le flag préfixe d'un underscore les symboles des
# fichiers suivants (réconcilie `gVidelData` asm ↔ `_gVidelData` C de vbcc ;
# nécessite le vlink patché par setup-toolchain.sh, cf. vlink-adduscore.patch).
# Les .PRJ Pure C sont en retard sur le code GODLIB : les symboles C indéfinis
# au link sont résolus automatiquement — le .H qui déclare le symbole désigne
# le module, on compile son .C et on retente (6 essais max).
do_link() {
    vlinkm68k -bataritos -tos-flags 7 -x -Bstatic -Cvbcc -P__stksize \
        -L"$TARGET/targets/m68k-atari/lib" \
        "${C_OBJS[@]}" \
        "$TARGET/targets/m68k-atari/lib/startup16.o" \
        -lvc -lm -set-adduscore \
        "${S_OBJS[@]}" \
        -o "$OUT/$SAMPLE.TOS"
}
for essai in 1 2 3 4 5 6; do
    echo "LD $SAMPLE.TOS (essai $essai)"
    LDERR="$(do_link 2>&1)" && { echo "$LDERR" | grep -v "^Warning 22" || true; break; }
    mapfile -t UNDEF < <(echo "$LDERR" | grep -oE 'undefined symbol _[A-Za-z0-9_]+' \
                         | sed 's/.*symbol _//' | sort -u)
    [ ${#UNDEF[@]} -gt 0 ] || { echo "$LDERR" | tail -20; exit 1; }
    added=0
    for sym in "${UNDEF[@]}"; do
        while IFS= read -r h; do
            c="${h%.H}.C"
            [ -f "$c" ] || continue
            obj="$OBJDIR/$(echo "${c#$BUILD/}" | sed 's#[/.]#_#g').o"
            [ -f "$obj" ] && continue
            echo "CC +${c#$BUILD/}  (résout $sym)"
            vc "+$TARGET/vc.config" "${CC_FLAGS[@]}" -I"$BUILD" -I"$BUILD/sysinc" "$c" -o "$obj"
            C_OBJS+=("$obj"); added=1
            break
        done < <(grep -rlE "(^|[^A-Za-z0-9_])$sym[[:space:]]*\(" "$BUILD/GODLIB" --include='*.H' 2>/dev/null)
        # Pas trouvé côté C ? Le symbole peut vivre dans un .S qui l'exporte.
        while IFS= read -r s; do
            obj="$OBJDIR/$(echo "${s#$BUILD/}" | sed 's#[/.]#_#g').o"
            [ -f "$obj" ] && continue
            echo "AS +${s#$BUILD/}  (résout $sym)"
            vasmm68k_mot "${AS_FLAGS[@]}" -quiet -I"$(dirname "$s")" "$s" -o "$obj" 2>/dev/null \
            || vasmm68k_mot "${AS_FLAGS[@]/-m68000/-m68020}" -quiet -I"$(dirname "$s")" "$s" -o "$obj"
            S_OBJS+=("$obj"); added=1
            break
        done < <(grep -rliE "^[[:space:]]*(export|xdef)[[:space:]]+.*\b$sym\b" "$BUILD/GODLIB" --include='*.S' 2>/dev/null)
    done
    [ $added -eq 1 ] || { echo "$LDERR" | tail -20; echo "!! symboles irrésolus : ${UNDEF[*]}" >&2; exit 1; }
done
ls -la "$OUT/$SAMPLE.TOS"
