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

# Deux modes :
#   build.sh <EXEMPLE>              -> compile GODLIB.SPL/<EXEMPLE> (via son .PRJ)
#   build.sh --prog FICHIER.c NOM   -> compile un programme C autonome contre GODLIB,
#                                      les modules GODLIB necessaires etant tires par
#                                      resolution automatique au link. -> NOM.TOS
#   build.sh --game FICHIER.PRJ NOM -> compile un JEU complet (arbre CODE/ + modules
#                                      GODLIB listés dans son .PRJ Pure C). -> NOM.TOS
PROG_MODE=0
GAME_MODE=0
if [ "${1:-}" = "--prog" ]; then
    PROG_MODE=1
    PROG_SRC="${2:?usage: build.sh --prog FICHIER.c NOM}"
    SAMPLE="${3:?usage: build.sh --prog FICHIER.c NOM}"
    [ -f "$PROG_SRC" ] || { echo "!! $PROG_SRC introuvable" >&2; exit 1; }
elif [ "${1:-}" = "--game" ]; then
    GAME_MODE=1
    PRJ="${2:?usage: build.sh --game FICHIER.PRJ NOM}"
    SAMPLE="${3:?usage: build.sh --game FICHIER.PRJ NOM}"
    [ -f "$PRJ" ] || { echo "!! $PRJ introuvable" >&2; exit 1; }
    GAMEDIR="$(cd "$(dirname "$PRJ")" && pwd)"      # racine du jeu (contient CODE/)
else
    SAMPLE="${1:?usage: build.sh <EXEMPLE> | --prog FICHIER.c NOM | --game FICHIER.PRJ NOM}"
    SPL="$RG/GODLIB.SPL/$SAMPLE"
    PRJ="$SPL/$SAMPLE.PRJ"
    # Le .PRJ ne porte pas toujours le nom du dossier (BLITTER1.PRJ, CLI_TEST.PRJ,
    # SPRITE1.PRJ) : à défaut, prendre le premier .PRJ présent dans le dossier.
    [ -f "$PRJ" ] || PRJ="$(ls "$SPL"/*.PRJ 2>/dev/null | head -1)"
    [ -n "$PRJ" ] && [ -f "$PRJ" ] || { echo "!! aucun .PRJ dans $SPL" >&2; exit 1; }
fi

export PATH="$TOOLS:$PATH" VBCC="$TARGET"

# --- 1. Arbre de build : GODLIB + l'exemple, #include \ → / -------------------
mkdir -p "$BUILD" "$OUT"
rsync -a --delete --include='*/' --include='*.[CHSI]' --exclude='*' "$RG/GODLIB/" "$BUILD/GODLIB/"
if [ "$PROG_MODE" = 1 ]; then
    mkdir -p "$BUILD/$SAMPLE"
    cp "$PROG_SRC" "$BUILD/$SAMPLE/$SAMPLE.C"
elif [ "$GAME_MODE" = 1 ]; then
    # tout l'arbre du jeu (CODE/*.C/.H/.S), sources locales incluses par "quote"
    rsync -a --delete --include='*/' --include='*.[CHSI]' --exclude='*' "$GAMEDIR/" "$BUILD/$SAMPLE/"
else
    rsync -a --delete "$SPL/" "$BUILD/$SAMPLE/"
fi
find "$BUILD" -type f \( -name '*.C' -o -name '*.H' \) \
    -exec sed -i -E '/^[[:space:]]*#[[:space:]]*include/ s#\\#/#g' {} +
# Dialecte Devpac : `OFFSET` sans argument REPART À 0 ; vasm, lui, CONTINUE le
# compteur du bloc offset précédent → tous les offsets de struct des blocs 2+
# étaient décalés (prouvé : DrawBox lisait sGraphicRect_mX à +16 au lieu de 0,
# 3e bloc de GRAPHIC.I). On force la sémantique Devpac avec `offset 0`.
find "$BUILD" -type f \( -name '*.S' -o -name '*.I' \) \
    -exec sed -i -E 's/^([[:space:]]*)[Oo][Ff][Ff][Ss][Ee][Tt][[:space:]]*(\r?)$/\1offset\t0\2/' {} +
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
        # ABI fastcall : le C compilé -fastcall référence `@X` (mangling vbcc) ;
        # on exporte un alias @X sur chaque label exporté par l asm.
        if (l ~ /^[ \t]*(export|xdef)[ \t]+/) {
            nom = $0; sub(/\r$/, "", nom); sub(/;.*/, "", nom)
            match(nom, /^[ \t]*[A-Za-z]+[ \t]+/); nom = substr(nom, RSTART + RLENGTH)
            gsub(/[ \t]/, "", nom)
            # Normalise indentation : export/xdef en COLONNE 0 (certains fichiers
            # RG ne les indentent pas, ex. AMIXER_S.S) est lu par vasm comme un
            # label, son operande devient un mnemonique inconnu. Force une tab.
            norm = out; gsub(/\r/, "", norm); sub(/^[ \t]+/, "", norm); norm = "\t" norm
            if (nom != "" && nom !~ /^@/) { exported[nom] = 1; print norm; print "\texport\t@" nom; next }
            print norm; next
        }
        if (match(out, /^[A-Za-z_][A-Za-z0-9_]*:/)) {
            lab = substr(out, 1, RLENGTH - 1)
            if (lab in exported) print "@" lab ":"
        }
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

# --- 2. Sources initiales : le .PRJ (mode exemple) OU juste le programme (--prog).
#     Dans les deux cas, le link auto-résout les modules GODLIB manquants (étape 3).
if [ "$PROG_MODE" = 1 ]; then
    SRCS=("$SAMPLE.C")                 # résolu en $BUILD/$SAMPLE/$SAMPLE.C (cas *) )
else
    mapfile -t SRCS < <(awk '/^=/{go=1;next} go' "$PRJ" \
        | sed -E 's/;.*//; s/[[:space:]]+$//; s#\\#/#g' \
        | grep -E '\.(C|S)$' || true)
    [ ${#SRCS[@]} -gt 0 ] || { echo "!! aucune source .C/.S dans $PRJ" >&2; exit 1; }
fi

# GODLIB_ST.MF + -fastcall : les stubs asm GEMDOS_S attendent l ABI registres Pure C.
# ⚠ -d2scratch OBLIGATOIRE : l asm GODLIB (Pure C) écrase d1/d2 sans les sauver ;
# sans ce flag vbcc garde des valeurs vives en d2 à travers les appels (prouvé :
# Memory_Calloc rendait NULL, son lpMem en d2 rasé par Memory_Clear).
CC_FLAGS=(-cpu=68000 -c -c99 -fastcall -d2scratch -O2)
AS_FLAGS=(-m68000 -Felf -noesc -nowarn=2049 -guess-ext -align)       # + guess-ext, -align (ds.w pairs, sémantique PureBot)

OBJDIR="$BUILD/obj/$SAMPLE"; rm -rf "$OBJDIR"; mkdir -p "$OBJDIR"   # build complet à chaque run
C_OBJS=(); S_OBJS=()

# En mode jeu : -I sur chaque sous-dossier du jeu pour les #include "LOCAL.H"
GAME_INCS=()
if [ "$GAME_MODE" = 1 ]; then
    while IFS= read -r d; do GAME_INCS+=("-I$d"); done \
        < <(find "$BUILD/$SAMPLE" -type d)
fi

# Shim _main → @main : le startup vbcc (ABI pile) appelle _main ; le main de
# GODLIB est compilé -fastcall (@main, argc en d0 / argv en a0).
# Ponts ABI fastcall (@X, args en REGISTRES) → libc/libm vbcc (ABI PILE, _X mappé
# C_X par -Cvbcc). La libc/libm vbcc est stack-only : tout appel standard depuis du
# C -fastcall a besoin d'un pont. Convention fastcall mesurée : pointeurs en a0/a1,
# entiers en d0/d1 (ordre des args) ; DOUBLE passé SUR LA PILE (→ jmp direct) ;
# retour en d0. Les helpers flottants (_ieee*) ont une ABI interne fixe (→ jmp).
cat > "$OBJDIR/mainshim.s" <<'EOF'
	export	_main
	xref	@main
_main:
	move.l	4(a7),d0
	movea.l	8(a7),a0
	jmp	@main

; --- 1 pointeur (a0) : arg poussé sur la pile ---
	xref	_malloc
	xref	_free
	xref	_time
	xref	_gmtime
	export	@malloc
@malloc:
	move.l	d0,-(a7)
	jsr	_malloc
	addq.l	#4,a7
	rts
	export	@free
@free:
	move.l	a0,-(a7)
	jsr	_free
	addq.l	#4,a7
	rts
	export	@time
@time:
	move.l	a0,-(a7)
	jsr	_time
	addq.l	#4,a7
	rts
	export	@gmtime
@gmtime:
	move.l	a0,-(a7)
	jsr	_gmtime
	addq.l	#4,a7
	rts

; --- qsort( base=a0, nmemb=d0, size=d1, compar=a1 ) → pile (droite→gauche) ---
	xref	_qsort
	export	@qsort
@qsort:
	move.l	a1,-(a7)
	move.l	d1,-(a7)
	move.l	d0,-(a7)
	move.l	a0,-(a7)
	jsr	_qsort
	lea	16(a7),a7
	rts

; --- double sin/cos : l'argument est DÉJÀ sur la pile en fastcall → jmp direct ---
	xref	_sin
	xref	_cos
	export	@sin
@sin:	jmp	_sin
	export	@cos
@cos:	jmp	_cos

; --- helpers flottants vbcc (ABI interne fixe) : simple renommage @_X → __X ---
	xref	__ieeeaddl
	xref	__ieeesubl
	xref	__ieeemull
	xref	__ieeemuld
	xref	__ieeed2s
	xref	__ieees2d
	xref	__ieeefixlsw
	export	@_ieeeaddl
@_ieeeaddl:	jmp	__ieeeaddl
	export	@_ieeesubl
@_ieeesubl:	jmp	__ieeesubl
	export	@_ieeemull
@_ieeemull:	jmp	__ieeemull
	export	@_ieeemuld
@_ieeemuld:	jmp	__ieeemuld
	export	@_ieeed2s
@_ieeed2s:	jmp	__ieeed2s
	export	@_ieees2d
@_ieees2d:	jmp	__ieees2d
	export	@_ieeefixlsw
@_ieeefixlsw:	jmp	__ieeefixlsw

; --- printf vbcc : le C fastcall référence @__v0printf, la lib fournit
; ___v0printf (même règle @→_ que les helpers ieee ci-dessus). jmp = passe-plat. ---
	xref	___v0printf
	export	@__v0printf
@__v0printf:	jmp	___v0printf

; Fin de segment code : EXCEPT (offset PC au crash) et PROFILER (dimension du
; buffer) lisent @_etext, symbole du linker préfixé @ par le C fastcall. vasm
; refuse d'aliaser un symbole importé → valeur approximative exportée (usage
; cosmétique / outil de dev, non critique à l'exécution du jeu).
	export	@_etext
@_etext	=	$80000
EOF
vasmm68k_mot -m68000 -Felf -quiet "$OBJDIR/mainshim.s" -o "$OBJDIR/mainshim.o"
C_OBJS+=("$OBJDIR/mainshim.o")
for src in "${SRCS[@]}"; do
    case "$src" in
        ../../GODLIB/*) path="$BUILD/GODLIB/${src#../../GODLIB/}" ;;
        *)              path="$BUILD/$SAMPLE/$src" ;;
    esac
    # Nom d'objet CANONIQUE (relatif à $BUILD) — DOIT coïncider avec celui que
    # l'auto-résolution calcule (${c#$BUILD/}), sinon un module listé dans le .PRJ
    # ET tiré par l'auto-résolution serait compilé DEUX fois (symboles en double).
    obj="$OBJDIR/$(echo "${path#$BUILD/}" | sed 's#[/.]#_#g').o"
    if [[ "$src" == *.C ]]; then
        echo "CC $src"
        vc "+$TARGET/vc.config" "${CC_FLAGS[@]}" -I"$BUILD" -I"$BUILD/sysinc" ${GAME_INCS[@]+"${GAME_INCS[@]}"} "$path" -o "$obj"
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
# ⚠ startup16.o EN PREMIER : un .TOS démarre au début du TEXT, sans champ
# d'entrée. GODLIB_ST.MF le mettait APRÈS les objets (bug latent du devkit) →
# le startup vbcc (Mshrink + init du tas libc) ne tournait JAMAIS : Malloc(-1)
# GEMDOS = 0, malloc = NULL, et Memory_Clear(32000, NULL) rasait la page zéro
# (écran bouillie de BOX). Prouvé à l'oracle : Hatari rendait les MÊMES zéros.
# Ensuite ordre GODLIB_ST.MF : objets C, -lvc -lm, puis -set-adduscore et
# LES OBJETS ASM EN DERNIER — le flag préfixe d'un underscore les symboles des
# fichiers suivants (réconcilie `gVidelData` asm ↔ `_gVidelData` C de vbcc ;
# nécessite le vlink patché par setup-toolchain.sh, cf. vlink-adduscore.patch).
# Les .PRJ Pure C sont en retard sur le code GODLIB : les symboles C indéfinis
# au link sont résolus automatiquement — le .H qui déclare le symbole désigne
# le module, on compile son .C et on retente (6 essais max).
do_link() {
    vlinkm68k -bataritos -tos-flags 7 -x -Bstatic -Cvbcc -P__stksize \
        -L"$TARGET/targets/m68k-atari/lib" \
        "$TARGET/targets/m68k-atari/lib/startup16.o" \
        "${C_OBJS[@]}" \
        -lvc16 -lm16 -set-adduscore \
        "${S_OBJS[@]}" \
        -o "$OUT/$SAMPLE.TOS"
}
declare -A BRIDGE_DONE      # trampolines asm→C déjà émis (évite les doublons)
for essai in 1 2 3 4 5 6 7 8; do
    echo "LD $SAMPLE.TOS (essai $essai)"
    LDERR="$(do_link 2>&1)" && { echo "$LDERR" | grep -v "^Warning 22" || true; break; }
    mapfile -t UNDEF < <(echo "$LDERR" | grep -oE 'undefined symbol [_@][A-Za-z0-9_]+' \
                         | sed 's/.*symbol [_@]//' | sort -u)
    [ ${#UNDEF[@]} -gt 0 ] || { echo "$LDERR" | tail -20; exit 1; }
    added=0
    BRIDGE_NEW=()               # trampolines à émettre cette itération
    for sym in "${UNDEF[@]}"; do
        # Le .S qui exporte le symbole prime (sémantique .PRJ Pure C : l'asm
        # remplace le repli C portable non gardé, ex. GRF_4.C vs GRF_4_S.S).
        resolved=0
        while IFS= read -r s; do
            obj="$OBJDIR/$(echo "${s#$BUILD/}" | sed 's#[/.]#_#g').o"
            [ -f "$obj" ] && { resolved=1; break; }
            echo "AS +${s#$BUILD/}  (résout $sym)"
            vasmm68k_mot "${AS_FLAGS[@]}" -quiet -I"$(dirname "$s")" "$s" -o "$obj" 2>/dev/null \
            || vasmm68k_mot "${AS_FLAGS[@]/-m68000/-m68020}" -quiet -I"$(dirname "$s")" "$s" -o "$obj"
            S_OBJS+=("$obj"); added=1; resolved=1
            break
        done < <(grep -rliE "^[[:space:]]*(export|xdef)[[:space:]]+.*\b$sym\b" "$BUILD/GODLIB" --include='*.S' 2>/dev/null)
        [ "$resolved" = 1 ] && continue
        while IFS= read -r h; do
            c="${h%.H}.C"
            [ -f "$c" ] || continue
            obj="$OBJDIR/$(echo "${c#$BUILD/}" | sed 's#[/.]#_#g').o"
            # Déjà compilé → le symbole EXISTE en @sym (C fastcall) mais l'asm le
            # référence en _sym (ABI Pure C) : pont trampoline _sym → @sym (jmp,
            # transparent aux registres). Sinon : première compilation du module.
            if [ -f "$obj" ]; then
                [ -n "${BRIDGE_DONE[$sym]:-}" ] || { BRIDGE_NEW+=("$sym"); added=1; }
                break
            fi
            echo "CC +${c#$BUILD/}  (résout $sym)"
            vc "+$TARGET/vc.config" "${CC_FLAGS[@]}" -I"$BUILD" -I"$BUILD/sysinc" ${GAME_INCS[@]+"${GAME_INCS[@]}"} "$c" -o "$obj"
            C_OBJS+=("$obj"); added=1
            break
        done < <(grep -rlE "(^|[^A-Za-z0-9_])$sym[[:space:]]*\(" "$BUILD/GODLIB" "$BUILD/$SAMPLE" --include='*.H' 2>/dev/null)
        [ "$added" = 1 ] && continue
        # Ni fonction .H ni export .S : variable globale définie dans un .C
        # (ex. gVbl de VBL.C référencée par VBL_S.S).
        while IFS= read -r c; do
            obj="$OBJDIR/$(echo "${c#$BUILD/}" | sed 's#[/.]#_#g').o"
            [ -f "$obj" ] && continue
            echo "CC +${c#$BUILD/}  (résout var $sym)"
            vc "+$TARGET/vc.config" "${CC_FLAGS[@]}" -I"$BUILD" -I"$BUILD/sysinc" ${GAME_INCS[@]+"${GAME_INCS[@]}"} "$c" -o "$obj"
            C_OBJS+=("$obj"); added=1
            break
        done < <(grep -rlE "^[A-Za-z_][A-Za-z0-9_ \*]*[^A-Za-z0-9_]$sym[[:space:]]*(\[[^]]*\])?[[:space:]]*(=|;)" "$BUILD/GODLIB" "$BUILD/$SAMPLE" --include='*.C' 2>/dev/null)
        [ "$added" = 1 ] && continue
        # Dernier recours : fonction PascalCase Module_Func référencée par de l'asm
        # en _sym mais DÉFINIE en @sym par un .C compilé (prototype absent des .H,
        # ex. AudioMixer_Slow, Except_Main, ScreenGrab_Update) → pont trampoline.
        if [[ "$sym" =~ ^[A-Z][A-Za-z0-9]*_[A-Za-z] ]] && [ -z "${BRIDGE_DONE[$sym]:-}" ] \
           && grep -rlqE "^[A-Za-z].*[^A-Za-z0-9_]$sym[[:space:]]*\(" "$BUILD/GODLIB" "$BUILD/$SAMPLE" --include='*.C' 2>/dev/null; then
            BRIDGE_NEW+=("$sym"); added=1
        fi
    done
    # Émet les trampolines asm→C collectés cette itération (dans C_OBJS, AVANT
    # -set-adduscore, donc _sym reste _sym et @sym reste le symbole C fastcall).
    if [ ${#BRIDGE_NEW[@]} -gt 0 ]; then
        br="$OBJDIR/bridges_$essai.s"
        : > "$br"
        for s in "${BRIDGE_NEW[@]}"; do
            [ -n "${BRIDGE_DONE[$s]:-}" ] && continue
            BRIDGE_DONE[$s]=1
            printf '\texport\t_%s\n\txref\t@%s\n_%s:\tjmp\t@%s\n' "$s" "$s" "$s" "$s" >> "$br"
            echo "BRIDGE _$s -> @$s"
        done
        if [ -s "$br" ]; then
            vasmm68k_mot -m68000 -Felf -quiet "$br" -o "${br%.s}.o"
            C_OBJS+=("${br%.s}.o")
        fi
    fi
    [ $added -eq 1 ] || { echo "$LDERR" | tail -20; echo "!! symboles irrésolus : ${UNDEF[*]}" >&2; exit 1; }
done
ls -la "$OUT/$SAMPLE.TOS"
