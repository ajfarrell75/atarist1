// =============================================================================
//  Shifter.cpp — Décodage planaire ST (basse/moyenne/haute) → buffer ARGB.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Shifter.hpp"
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Décalage d'alignement pixel↔couleur du re-rendu spec512, en cycles (8 MHz).
// Port de l'alignement d'Hatari (spec512.c Spec512_StartScanLine) : avant de tracer
// le 1ᵉʳ pixel affiché, Hatari fait avancer ScanLineCycleCount de (LineStartCycle/4 + 7)
// périodes de 4 cycles, soit LineStartCycle + 28 — le « +7 » étant le décalage
// pipeline du shifter documenté (« [NP] '7' is required to align pixels and colors »).
// Une écriture à la position-ligne L apparaît donc au pixel (L − LineStartCycle − 28).
// Côté NeoST, Moira date l'écriture au DÉBUT du cycle bus (~4 cyc avant la convention
// Hatari instr_end−8). Indissociable de applyShifterBusAlignment (sans le recalage des
// wait states bus, la position dérive de −2 cyc/ligne).
// **−25 (2026-07-03) = modèle spec512.c EXACT.** Hatari stocke la position d'écriture
// 4-alignée (bus) et l'applique par CELLULES de 4 cycles : premier pixel affecté =
// cyc_hatari − 84 (50 Hz : pré-spans (56−0)/4 + 7 = 21 → count 84 au 1ᵉʳ pixel visible,
// cf. Spec512_StartScanLine). Les écritures NeoST étant TOUTES ≡2 mod 4 (syncCpuBus,
// convention −2 vs Hatari), −25 place le front à c = w−82 = (w+2)−84 ≡ 0 mod 4 —
// quantification de cellule REPRODUITE par le seul offset, sans code de plus.
// VALIDÉ : diaporama étalon spectrum_512_auto_diapo — les **10** images du cycle à
// **0 px** vs oracle Hatari fastfdc aligné trame-à-trame (l'ancien −23 laissait
// 350/23/415/666/15/6 px sur 6 des 10 : marges d'index serrées autour des fronts ;
// les traces NEOST_SPEC512_TRACE ↔ TRACE_VIDEO_COLOR matchaient déjà à Δ=+2 constant
// sur les 9552 écritures/trame → seule la règle de quantification différait).
static constexpr int kSpec512AlignCyc = -25;

// Seuil de détection « image spec512 » : nombre d'écritures palette MOT par trame
// au-delà duquel on bascule sur le re-rendu intra-ligne. Bien au-dessus d'un usage
// normal (16 couleurs posées une fois = 16, ou raster-bars ~ quelques centaines) pour
// ne JAMAIS toucher les trames ordinaires. Une image Spectrum 512 réécrit ~48 couleurs/
// ligne sur 200 lignes (~9600 mots/trame). Depuis la fusion octet→mot de recordColorWrite
// (un move.w = 1 écriture, comme Hatari), on compte les MOTS : 512 ⇔ l'ancien seuil de
// 1024 (qui comptait 2 octets par mot), frontière de détection inchangée.
static constexpr int kSpec512Threshold = 512;

// Correction de datation de la LECTURE du compteur vidéo $FF8205/07/09, en cycles.
// Pendant côté lecture de kSpec512AlignCyc (qui aligne les ÉCRITURES). Port du modèle
// Hatari Video_CalculateAddress : Hatari date la lecture PLUS TÔT que le cycle de bus
// brut — FrameCycles = Video_GetCyclesSinceVbl_OnReadAccess() − 8 (le « magic 8 »),
// plus l'offset « read effective N cyc avant la fin de l'instruction » (cycles.c,
// Cycles_GetInternalCycleOnReadAccess). NeoST échantillonne au cycle de lecture brut de
// Moira (liveNow) → 2 cyc trop tard. Sans correction, la valeur tombe PILE sur la
// frontière de cellule-mot de la quantification (X−lineStart)>>1 &~1 (granularité 4 cyc) :
// les démos spec512 à auto-synchro (BEE512…) qui lisent $FF8209 puis sautent dans un
// nop-slide calculé atterrissent ±4 cyc une trame sur deux → image STATIQUE qui clignote
// à 25 Hz (~1418 px/trame). −2 cyc recentre la lecture dans la cellule. Valeur EXACTE
// (pas un simple ≡2 mod 4 anti-flicker) calée sur l'oracle Hatari (TRACE_VIDEO_COLOR) :
// 1ʳᵉ écriture palette ligne 64 datée cyc=80 stable côté Hatari ; NeoST sans correction
// oscille 76↔80, avec −2 se verrouille sur 80 (= Hatari). Flicker plein-diaporama
// (BEE512/sun/PLANET/ANIMAL, fenêtre 540..1010) : 111 paires → 0.
// Datation des LECTURES du compteur vidéo $FF8205/07/09 — VALEUR FIDÈLE
// THÉORIQUE **−6** (2026-07-03) = la dérivation « fin d'accès − 8 » de
// Video_CalculateAddrees (doc convergence §8, cible #1). L'ancien −14
// « calibré à l'oracle » (2026-07-02, banc poll) était CO-CALIBRÉ avec
// l'ancienne datation write (−6) autour d'un résidu commun de +8 : les DEUX
// datations ont été ramenées ENSEMBLE (+8 chacune) à leurs valeurs fidèles
// de la table §8 (read −6, write +2). Mesure décisive (menu robot Cuddly,
// oracle video_addr + cpu_disasm) : chemin CPU inter-trame et ancre VBL
// IDENTIQUES à Hatari, mais valeur $8209 lue 4-6 octets PLUS PETITE au même
// instant → la sortie du synchroniseur (pc=f264, octet bas > $40 en SIGNÉ)
// glissait de L34 à L36 ~1 trame/10 → clignotement vertical bistable.
// Validé ENSEMBLE : Cuddly 250/250 verrouillées (225/250 avant), EL top-trick
// 40/40, LX titre propre, SHO byte-identique, étalons 19/19 + TOUS OK.
// NEOST_VC_OFF ajuste pour le diagnostic/A/B (−8 → ancien −14).
static const int kVideoCounterReadOffsetCyc =
    -6 + [] { const char* s = std::getenv("NEOST_VC_OFF"); return s ? std::atoi(s) : 0; }();

// =============================================================================
//  Machine GLUE — retrait de bordures (port fidèle de Hatari video.c :
//  Video_Update_Glue_State + Video_StartHBL + section verticale Video_EndHBL).
//  Rejouée HORS-LIGNE en fin de trame sur les écritures freq/res datées
//  (syncWrites_) → la timeline live est INCHANGÉE (zéro régression).
// =============================================================================
namespace glue {
// BORDERMASK_* (Hatari video.c)
constexpr uint32_t LEFT_OFF        = 0x0001;   // retrait bordure gauche (hi/lo) → +26 o
constexpr uint32_t LEFT_PLUS_2     = 0x0002;   // ligne 60 Hz commence 2 o plus tôt
constexpr uint32_t STOP_MIDDLE     = 0x0004;   // fin en hi-res au cycle 160 → -106 o
constexpr uint32_t RIGHT_MINUS_2   = 0x0008;   // ligne 60 Hz finit 2 o plus tôt
constexpr uint32_t RIGHT_OFF       = 0x0010;   // retrait bordure droite → +44 o
constexpr uint32_t RIGHT_OFF_FULL  = 0x0020;   // retrait droite + gauche ligne suivante
constexpr uint32_t NO_DE           = 0x0800;   // vertical DE off pour cette ligne
constexpr uint32_t BLANK           = 0x1000;   // ligne blanche (50/60 Hz)
constexpr uint32_t NO_COUNT        = 0x2000;   // compteur ligne non incrémenté
constexpr uint32_t NO_SYNC         = 0x4000;   // pas de HSYNC (ligne vide)
constexpr uint32_t SYNC_HIGH       = 0x8000;
// V_OVERSCAN_* (Hatari includes/video.h)
constexpr uint32_t VO_NO_TOP       = 0x01;
constexpr uint32_t VO_NO_BOTTOM_50 = 0x02;
constexpr uint32_t VO_NO_BOTTOM_60 = 0x04;
constexpr uint32_t VO_BOTTOM_SHORT_50 = 0x08;
constexpr uint32_t VO_NO_DE        = 0x10;
// Timing STF nominal (ancres 56/376 cohérentes avec la géométrie/videoCounter
// validés de NeoST ; la variante "wakeup state" WS3 +1 cyc relève du sous-pixel,
// cf. TODO wait states). Cf. Hatari VideoTimings[VIDEO_TIMING_STF_WS1].
constexpr int HDE_On_Hi          =   4;
constexpr int HBlank_Off_Low_60  =  24;
constexpr int HBlank_Off_Low_50  =  28;
constexpr int HDE_On_Low_60      =  52;
constexpr int Line_Set_Pal       =  54;
constexpr int HDE_On_Low_50      =  56;
constexpr int HDE_Off_Hi         = 164;
constexpr int HDE_Off_Low_60     = 372;
constexpr int HDE_Off_Low_50     = 376;
constexpr int HSync_On_Off_Low   = -50;        // HSync_On_Offset_Low (DE_end droite = cpl-50)
constexpr int HSync_Off_Off_Low  = -10;        // HSync_Off_Offset_Low
constexpr int RemoveTopBorder_Pos    = 502;
constexpr int RemoveBottomBorder_Pos = 502;
constexpr int LINE_END_FULL      = 512;
// Canal HBL_Pos/nCyclesPerLine (video.c 977-979, VideoTimings STF) : chaque
// « Freq_match » de phase 1 fixe la POSITION de l'IRQ HBL de la ligne courante et
// sa LONGUEUR — une ligne où la freq matche en 71/60/50 fait 224/508/512 cycles.
constexpr int Hbl_Pos_Hi     = 220;            // CYCLES_PER_LINE_71HZ − 4
constexpr int Hbl_Pos_Low_60 = 504;            // CYCLES_PER_LINE_60HZ − 4
constexpr int Hbl_Pos_Low_50 = 508;            // CYCLES_PER_LINE_50HZ − 4
constexpr int CyclesLine_Hi  = 224;
constexpr int CyclesLine_60  = 508;
constexpr int CyclesLine_50  = 512;
// Positions verticales (lignes) : VDE_On/Off par fréquence.
constexpr int VDE_On_50 = 63, VDE_On_60 = 34, VDE_On_Hi = 34;
constexpr int VDE_Off_50 = 263, VDE_Off_60 = 234, VDE_Off_Hi = 434;
constexpr int VDE_Off_NoBottom_50 = 310;   // 263 + 47 (VIDEO_HEIGHT_BOTTOM_50HZ)
constexpr int VDE_Off_NoBottom_60 = 260;   // 234 + 26 (VIDEO_HEIGHT_BOTTOM_60HZ)
}  // namespace glue

Shifter::Shifter(Bus& bus) : bus_(bus) {
    resizeFor(mode);
}

// Remise à zéro au RESET machine — port fidèle de Video_Reset (video.c:810).
// Hatari remet : la base vidéo (VideoBase = 0), les registres STE (LineWidth,
// HWScrollCount) et leurs modifications DIFFÉRÉES (NewLineWidth/NewHWScrollCount
// = −1, VideoCounterDelayedOffset = 0), l'état mémorisé de la Glue
// (ShifterFrame.Freq/Res = −1) et les lignes par-trame (Video_InitShifterLines).
// Video_Reset_Glue (video.c:904) remet en plus freq $FF820A = 0 et la résolution
// à basse (moniteur couleur — le mono est piloté ailleurs, bit7 GPIP).
// ⚠ Video_Reset ne touche PAS la palette : les registres couleur survivent au
// reset (vérifié : aucun accès à HBLPalettes/IoMem $FF8240 dans Video_Reset).
void Shifter::reset() {
    videoBase = 0;                       // VideoBase = 0L
    // Video_Reset_Glue : freq et résolution à 0 (le RESET 68000 remet la Glue à
    // 60 Hz / basse rés ; TOS reprogramme aussitôt).
    sync = 0;
    mode = Mode::Low;
    // Registres STE + modifications différées annulées (« cancel pending
    // modifications set before the reset », video.c:850-853).
    lineWidth            = 0;
    hwScrollCount        = 0;
    hwScrollPrefetch     = false;
    hwScrollReg8264_     = 0;
    newLineWidth_        = -1;
    newHwScrollCount_    = -1;
    newHwScrollPrefetch_ = false;
    vcDelayedOffset_     = 0;
    // ShifterFrame.Freq/Res = −1 (video.c:824-825) : le filtre « même valeur
    // ignorée » de recordSyncWrite repart neutre — la 1ʳᵉ écriture freq/res
    // après reset est TOUJOURS traitée.
    lastGlueFreq_ = -1;
    lastGlueRes_  = -1;
    // Compteur vidéo matérialisé (≙ pVideoRaster = &STRam[0]) + restart fin de trame.
    vcFrameBase_   = 0;
    vcLineBase_    = 0;
    vcLineY_       = 0;
    vcRestartBase_ = -1;
    vcRestartLine_ = 0;
    // État glue PAR-TRAME (≙ Video_InitShifterLines + Video_ClearOnVBL) : écritures
    // datées purgées, curseurs live remis, bordures nominales, spec512 désarmé.
    // beginFrame ré-écrase tout au prochain début de trame ; on garantit ici un
    // état cohérent pour les lectures $FF8205/07/09 d'ici là.
    syncWrites_.clear();
    colorWrites_.clear();
    paletteAccesses_ = 0;
    spec512Active_   = false;
    bordersTrick_    = false;
    frameMode_ = mode;
    frameSync_ = sync;
    resizeFor(frameMode_);
    const Geometry g = geometry();
    liveStartHBL_   = g.dispStartLine;
    glueStartHBL_   = g.dispStartLine;
    glueEndHBL_     = g.dispStartLine + g.displayLines;
    glueVOverscan_  = 0;
    glueBlankLines_ = 0;
    glueHblPos_     = -1;
    glueCyclesLine_ = -1;
    nScreenRefreshRate_ = (sync & 0x02) ? 50 : 60;
    liveGlueLine_   = -1;
    liveGlueWi_     = 0;
    liveGlueRes_    = 0;
    liveGlueFreq50_ = (sync & 0x02) ? 1 : 0;
    liveGlueLen_    = g.cyclesPerLine;
    std::fill(glueLines_.begin(), glueLines_.end(), GlueLine{ -1, 0, 0, 0 });
    std::fill(glueLineStart_.begin(), glueLineStart_.end(), int64_t{0});
    std::fill(lineSnapLen_.begin(), lineSnapLen_.end(), static_cast<uint16_t>(0));
}

uint32_t Shifter::stColorToArgb(uint16_t c) {
    // Format STE : %0000 RRRR GGGG BBBB (4 bits par canal). Le ST n'utilise que
    // les 3 bits bas de chaque nibble ; le STE ajoute un 4e bit (bit3) qui pèse
    // une DEMI-marche, d'où des teintes intermédiaires. Port fidèle de Hatari
    // conv_st.c (ConvST_SetupRGBTable) : v8 = ((c4&0x7)<<1)|((c4&0x8)>>3) puis
    // v8 |= v8<<4 (réplique le nibble pour étirer 4→8 bits). Pour un coloris ST
    // (bit3=0) cela donne la même nuance qu'avant (ex. 7 → 0xEE).
    const uint8_t r4 = (c >> 8) & 0xF;
    const uint8_t g4 = (c >> 4) & 0xF;
    const uint8_t b4 = (c >> 0) & 0xF;
    auto ex = [](uint8_t c4) -> uint32_t {
        uint32_t v = ((c4 & 0x7u) << 1) | ((c4 & 0x8u) >> 3);
        v |= v << 4;
        return v;
    };
    return 0xFF000000u | (ex(r4) << 16) | (ex(g4) << 8) | ex(b4);
}

void Shifter::resizeFor(Mode m) {
    // Lignes ACTIVES (display-enable) décodées et offset de l'écran actif dans le
    // buffer. En basse rés bordée, le buffer overscan ajoute des bordures autour
    // de l'écran 320×200 (cf. kBorder*). Sinon le buffer = l'écran actif (offset 0).
    int aw = 320, ah = 200;                       // dimensions de l'écran ACTIF
    switch (m) {
        case Mode::Low:    aw = 320; ah = 200; break;   // 16 couleurs
        case Mode::Medium: aw = 640; ah = 200; break;   // 4 couleurs
        case Mode::High:   aw = 640; ah = 400; break;   // monochrome
    }
    const bool border = (m == Mode::Low) && kBordersEnabled;
    activeX_ = border ? kBorderLeftPx   : 0;
    activeY_ = border ? kBorderTopLines : 0;
    const int w = border ? (kBorderLeftPx + aw + kBorderRightPx) : aw;
    const int h = border ? (kBorderTopLines + ah + kBorderBotLines) : ah;
    curAH_ = ah;
    if (w == curW_ && h == curH_) return;
    curW_ = w; curH_ = h;
    frame_.assign(static_cast<std::size_t>(w) * h, 0xFF000000u);
}

// Verrouille la résolution ET la fréquence de la trame : le décodage ligne à
// ligne s'y tient (un changement de $FF8260 ou $FF820A en cours de trame ne prend
// effet qu'à la suivante, comme l'ancien renderFrame qui figeait la rés. au moment
// du décodage). La géométrie de la trame (cycles/ligne, lignes/trame) découle de
// ce couple — cf. geometry() — et est lue par Machine juste après cet appel.
void Shifter::beginFrame() {
    frameMode_ = mode;
    frameSync_ = sync;
    resizeFor(frameMode_);
    // Réinitialise l'enregistrement spec512 de la trame qui commence. La palette
    // courante devient la base du replay (= état de fin de trame précédente ; les
    // écritures palette de CETTE trame seront rejouées par-dessus, datées).
    colorWrites_.clear();
    paletteAccesses_ = 0;
    spec512Active_   = false;
    frameStartPalette_ = palette;
    syncWrites_.clear();
    bordersTrick_ = false;
    // VDE_On live du compteur vidéo : valeur nominale selon la fréquence verrouillée
    // (50 Hz → 63, 60 Hz → 34). En MONO (haute rés), Hatari force nStartHBL =
    // VIDEO_START_HBL_71HZ = 34 QUELLE QUE SOIT la fréquence du registre $FF820A
    // (Video_ResetShifterTimings, video.c:4498-4504 : haute rés ⇒ 71 Hz ⇒ 34) —
    // cohérent avec geometryFor(High) qui pose déjà dispStartLine = 34. Les
    // bascules freq de la trame peuvent l'avancer (retrait bordure haute) via
    // updateLiveStartHBL.
    liveStartHBL_ = (frameMode_ == Mode::High) ? 34
                                               : ((frameSync_ & 0x02) ? 63 : 34);
    // Compteur vidéo matérialisé : LATCH de la base au début de trame (port
    // Video_ClearOnVBL → Video_RestartVideoCounter : pVideoRaster = &STRam[VideoBase]).
    // Une écriture $FF8201/03 en cours de trame ne réapparaîtra qu'ici, à la trame
    // suivante — comme sur le vrai matériel. Les écritures différées (NewHWScrollCount,
    // NewLineWidth, offset compteur) ne sont PAS effacées : Hatari ne les purge qu'au
    // reset vidéo, elles s'appliquent à la prochaine fin de ligne active.
    // Base de trame : si le RESTART fin-de-trame (ligne 310/260, port
    // Video_RestartVideoCounter) a eu lieu, c'est SA base (relue à cet instant)
    // qui fait foi — une écriture $FF8201/03 entre la ligne 310 et la ligne 0
    // n'est PAS reprise (comme Hatari). Sinon (mono, ou freq inadaptée à la
    // ligne de restart), latch classique au début de trame.
    vcFrameBase_ = (vcRestartBase_ >= 0) ? static_cast<uint32_t>(vcRestartBase_)
                                         : (videoBase & 0xFFFFFFu);
    vcRestartBase_ = -1;
    vcLineBase_  = vcFrameBase_;
    vcLineY_     = 0;
    // Fond bordure : remplit tout le buffer overscan avec la couleur de bordure
    // (registre 0) au début de trame. Les lignes actives écrasent leur zone ; les
    // bordures haut/bas et les côtés non réécrits restent à cette couleur. (Phase 1 :
    // couleur de bordure figée à la trame ; les barres raster en bordure haut/bas
    // viendront avec le retrait de bordures et le suivi du registre 0 par ligne.)
    if (bordered()) {
        const uint32_t bg = stColorToArgb(palette[0]);
        std::fill(frame_.begin(), frame_.end(), bg);
    }
    // Machine Glue LIVE : prépare l'état par-ligne de la nouvelle trame. Mêmes
    // structures que replayGlue — qui ré-écrase TOUT en fin de trame à partir des
    // mêmes syncWrites_, donc live et replay donnent le même résultat ; entre-temps
    // videoCounter() peut consulter la fenêtre DE réelle de la ligne courante.
    if (frameMode_ != Mode::High) {
        const Geometry g = geometry();
        glueLines_.assign(static_cast<std::size_t>(g.linesPerFrame) + 2, GlueLine{ -1, 0, 0, 0 });
        // Capture par ligne (cf. lineSnap_) : remise à zéro des longueurs — le gros
        // tampon d'octets n'a pas besoin d'être effacé (len fait foi).
        lineSnapLen_.assign(glueLines_.size(), 0);
        lineSnap_.resize(glueLines_.size() * static_cast<std::size_t>(kLineSnapBytes));
        glueStartHBL_   = g.dispStartLine;
        glueEndHBL_     = g.dispStartLine + g.displayLines;
        glueVOverscan_  = 0;
        glueBlankLines_ = 0;
        nScreenRefreshRate_ = (frameSync_ & 0x02) ? 50 : 60;
    }
    liveGlueLine_   = -1;
    liveGlueWi_     = 0;
    liveGlueRes_    = (frameMode_ == Mode::Medium) ? 1 : (frameMode_ == Mode::High ? 2 : 0);
    liveGlueFreq50_ = (frameSync_ & 0x02) ? 1 : 0;
    // LINELEN : échelle des débuts de ligne réels (remplie au fil des avances).
    glueLineStart_.assign(glueLines_.size(), 0);
    liveGlueLen_ = geometry().cyclesPerLine;
}

// Avance la machine Glue LIVE jusqu'à la ligne `targetLine` incluse : startHBL sur
// les lignes nouvellement atteintes + consommation des écritures freq/res déjà
// enregistrées, en ordre chronologique — exactement la boucle de replayGlue, mais
// au fil de la trame (les écritures arrivent triées : recordSyncWrite est daté live).
void Shifter::liveGlueCatchUp(int targetLine) {
    if (frameMode_ == Mode::High || glueLines_.size() < 2) return;
    const int maxLine = static_cast<int>(glueLines_.size()) - 2;
    if (targetLine > maxLine) targetLine = maxLine;
    const int cpl = geometry().cyclesPerLine;
    // NEOST_LINELEN : attribution des écritures à la grille RÉELLE (échelle des
    // débuts de ligne glueLineStart_, alimentée à chaque avance de ligne ;
    // longueur courante déplacée par les « Freq_match » via glueCyclesLine_).
    static const bool lineLen = std::getenv("NEOST_LINELEN") != nullptr;
    for (;;) {
        // Écriture en attente sur une ligne déjà initialisée → consommer AVANT
        // d'avancer (elle conditionne res/freq des lignes suivantes).
        if (liveGlueWi_ < syncWrites_.size()) {
            const SyncWrite& w = syncWrites_[liveGlueWi_];
            int wl; int lc;
            if (lineLen) {
                // Ligne = position dans l'échelle réelle (recherche descendante
                // depuis la ligne courante — les écritures arrivent quasi triées).
                wl = liveGlueLine_ < 0 ? 0 : liveGlueLine_;
                while (wl > 0 && w.frameCycle < glueLineStart_[wl]) --wl;
                // L'écriture est-elle au-delà de la fin de la ligne courante ?
                // (fin = start + longueur courante) → il faut d'abord avancer.
                if (wl == liveGlueLine_ && liveGlueLine_ >= 0
                    && w.frameCycle >= glueLineStart_[wl] + liveGlueLen_ && liveGlueLine_ < targetLine) {
                    wl = liveGlueLine_ + 1;   // force l'avance ci-dessous
                }
                lc = static_cast<int>(w.frameCycle - glueLineStart_[wl]);
            } else {
                wl = static_cast<int>(w.frameCycle / cpl);
                lc = static_cast<int>(w.frameCycle % cpl);
            }
            if (wl > maxLine) { wl = maxLine; if (!lineLen) lc = static_cast<int>(w.frameCycle % cpl); }
            if (wl <= liveGlueLine_) {
                if (w.isRes) liveGlueRes_    = w.val & 0x03;
                else         liveGlueFreq50_ = (w.val & 0x02) ? 1 : 0;
                const int freqHz = (liveGlueRes_ == 2) ? 71 : (liveGlueFreq50_ ? 50 : 60);
                // DIAG (NEOST_GLUE_DIAG) : chaque écriture appliquée à la Glue, avec
                // sa datation ligne/cycle et le masque résultant — à diff'er contre
                // Hatari `video_border_h` (les « detect ... » de Video_Update_Glue_State).
                static const bool glueDiag = std::getenv("NEOST_GLUE_DIAG") != nullptr;
                updateGlueState(wl, lc, w.isRes, freqHz);
                // Canal HBL_Pos/nCyclesPerLine → Machine (reprogramme l'IRQ HBL de la
                // ligne et cumule le raccourcissement) — gated NEOST_LINELEN côté Machine.
                if (glueHblPos_ > 0 && lineGeom_) lineGeom_(wl, glueHblPos_, glueCyclesLine_);
                // La longueur de la ligne COURANTE suit le dernier « Freq_match ».
                if (lineLen && wl == liveGlueLine_ && glueCyclesLine_ > 0) liveGlueLen_ = glueCyclesLine_;
                if (glueDiag)
                    std::fprintf(stderr, "[GLUP] line=%d cyc=%d %s freq=%d -> mask=%03x de=%d..%d\n",
                                 wl, lc, w.isRes ? "res" : "sync", freqHz,
                                 glueLines_[wl].borderMask, glueLines_[wl].displayStartCycle,
                                 glueLines_[wl].displayEndCycle);
                ++liveGlueWi_;
                continue;
            }
        }
        if (liveGlueLine_ >= targetLine) break;
        // Avance d'une ligne : mémorise le début RÉEL de la nouvelle ligne
        // (start précédent + longueur réelle) et repart à la longueur nominale.
        const int64_t prevStart = (liveGlueLine_ >= 0 && lineLen) ? glueLineStart_[liveGlueLine_] : 0;
        ++liveGlueLine_;
        if (lineLen) {
            if (static_cast<std::size_t>(liveGlueLine_) < glueLineStart_.size())
                glueLineStart_[liveGlueLine_] = (liveGlueLine_ == 0) ? 0 : prevStart + liveGlueLen_;
            liveGlueLen_ = cpl;
        }
        const int freqHz = (liveGlueRes_ == 2) ? 71 : (liveGlueFreq50_ ? 50 : 60);
        startHBL(liveGlueLine_, liveGlueRes_, freqHz);
    }
}

// Décode les index de palette (ou bit mono) d'UNE scanline dans `idx`, selon la
// résolution VERROUILLÉE de la trame. Renvoie le décalage scroll fin STE.
int Shifter::decodeLineIndices(int y, uint8_t* idx) const {
    const int  W      = activeWidth();             // pixels de l'écran actif (hors bordures)
    const bool hi     = (frameMode_ == Mode::High);
    const int  planes = hi ? 1 : (frameMode_ == Mode::Medium ? 2 : 4);  // plans entrelacés
    const int  bpl    = hi ? 80 : 160;                  // octets/ligne AFFICHÉE
    const int  groupB = 2 * planes;                     // octets pour 16 px (1 mot/plan)
    const int  groups = W / 16;                         // groupes de 16 px affichés
    const int  scroll = hwScrollCount;                  // 0 hors STE scrollé ($FF8264/65)

    // Line-offset STE ($FF820F) : le shifter saute `lineWidth` MOTS en fin de ligne
    // → la ligne suivante démarre `bpl + lineWidth*2` octets plus loin (stride). Sur
    // ST/STF lineWidth=0 → stride = bpl (rendu strictement inchangé). Le scroll fin
    // avec prefetch ajoute 1 mot par plan (cf. scrollCounterAdvance).
    const uint32_t stride = static_cast<uint32_t>(bpl) + static_cast<uint32_t>(lineWidth) * 2u
                          + static_cast<uint32_t>(scrollCounterAdvance());
    // Adresse de la ligne : le compteur MATÉRIALISÉ (vcLineBase_, ≙ pVideoRaster) pour
    // le rendu live séquentiel — il accumule les strides RÉELS (lineWidth variable,
    // écritures du compteur $FF8205/07/09, scroll) — sinon repli analytique depuis la
    // base latchée de la trame (re-rendus spec512/renderFrame, où ces effets STE
    // dynamiques ne sont pas rejoués).
    const uint32_t base = (y == vcLineY_) ? vcLineBase_
                                          : vcFrameBase_ + static_cast<uint32_t>(y) * stride;

    // Deux variantes matérielles (port Video_CopyScreenLineColor) :
    //  • $FF8265 (PREFETCH) : le shifter lit un groupe de 16 px DE PLUS (1 mot par
    //    plan, juste après la ligne) pour fournir les `scroll` pixels qui entrent
    //    par la droite — la ligne entière est décalée À GAUCHE de `scroll` px.
    //  • $FF8264 (SANS prefetch) : aucun mot supplémentaire — l'affichage démarre
    //    16 px plus tard (premiers 16 px = couleur 0) et s'arrête au point normal :
    //    dst[c] = source[c-16+scroll]. On pré-transforme idx pour que l'émetteur
    //    (`idx[c + scroll]`) produise ce résultat : données décodées à partir de
    //    idx[16], puis idx[0..16+scroll) mis à l'index 0 (memmove+memset d'Hatari).
    //    En MONO, Hatari ne distingue pas le prefetch (Video_CopyScreenLineMono) →
    //    toujours le modèle prefetch.
    const bool prefetch = hwScrollPrefetch || hi;
    const int decodeGroups = (scroll && prefetch) ? groups + 1 : groups;
    int px = (scroll && !prefetch) ? 16 : 0;
    for (int g = 0; g < decodeGroups; ++g) {
        const uint32_t a  = base + static_cast<uint32_t>(g) * groupB;
        const uint16_t p0 = bus_.read16(a);
        const uint16_t p1 = planes > 1 ? bus_.read16(a + 2) : 0;
        const uint16_t p2 = planes > 2 ? bus_.read16(a + 4) : 0;
        const uint16_t p3 = planes > 3 ? bus_.read16(a + 6) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    if (scroll && !prefetch)
        std::memset(idx, 0, static_cast<std::size_t>(16 + scroll));   // bord gauche couleur 0
    return scroll;
}

// Avance SUPPLÉMENTAIRE du compteur vidéo par ligne due au scroll fin (port des
// `pVideoRaster += n*2` de Video_CopyScreenLine*) : avec PREFETCH ($FF8265) le
// shifter a consommé 1 mot PAR PLAN de plus (8 octets en basse rés, 4 en moyenne) ;
// sans prefetch ($FF8264) : rien. En mono, Hatari avance toujours d'1 mot.
int Shifter::scrollCounterAdvance() const {
    if (!hwScrollCount) return 0;
    if (frameMode_ == Mode::High) return 2;
    if (!hwScrollPrefetch) return 0;
    return frameMode_ == Mode::Medium ? 4 : 8;
}

// Décode UNE scanline active (display-enable) avec l'état COURANT des registres
// (palette, base vidéo) et la place à l'offset bordure dans le buffer overscan.
void Shifter::renderLine(int y) {
    if (y < 0 || y >= curAH_) return;
    // Commit PARESSEUX du compteur matérialisé (2026-07-02, fidèle Video_EndHBL) :
    // les lignes < y sont committées ICI (au rendu de la ligne y = cycle
    // y·cpl+376), donc APRÈS toute écriture freq/res de FIN de ligne y−1 (ex.
    // impulsion 60→50 Hz à cyc 372-376 ouvrant la bordure droite : la calibration
    // du loader d'Enchanted Land lit $FF8209 8 cycles après l'impulsion et doit
    // voir le compteur ENCORE en marche jusqu'à 460, puis la ligne créditée de
    // 204 octets). L'ancien commit à DE_end (376) de la ligne COURANTE figeait
    // la ligne à 160 octets AVANT que l'écriture datée 376 ne soit consommée →
    // valeur figée (base+160) au lieu d'avancer (base+164) = delta nul → le
    // scan de calibration EL ne détectait jamais l'ouverture (loader bloqué).
    // Les lectures sur lignes non-committées passent par l'extrapolation
    // glueLineBytes (live) de videoCounter — mêmes valeurs, mais la fenêtre DE
    // de la ligne courante est consultée EN DIRECT. Le re-rendu hors séquence
    // (spec512/renderFrame) n'avance jamais le compteur (y ≤ vcLineY_).
    while (vcLineY_ < y) endVideoLine();
    const int  W   = activeWidth();
    const bool hi  = (frameMode_ == Mode::High);

    uint8_t idx[660];                                   // max (640/16 + 1) * 16 = 656
    const int scroll = decodeLineIndices(y, idx);

    // Début de la zone ACTIVE dans le buffer (décalée des bordures gauche/haut).
    uint32_t* dst = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_ + activeX_;

    // Émet W pixels à partir de l'offset `scroll`. En haute résolution = moniteur
    // MONOCHROME : blanc (0) / noir (1), sans la palette couleur (sinon un
    // palette[1] non noir — ex. rouge sous TOS 1.02 — colore l'écran à tort).
    if (hi) {
        for (int c = 0; c < W; ++c)
            dst[c] = (idx[c + scroll] & 1) ? 0xFF000000u : 0xFFFFFFFFu;
    } else {
        for (int c = 0; c < W; ++c)
            dst[c] = stColorToArgb(palette[idx[c + scroll]]);
    }

    // Bordures latérales de CETTE ligne = couleur registre 0 courante (Phase 1).
    if (bordered()) {
        const uint32_t bg = stColorToArgb(palette[0]);
        uint32_t* row = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_;
        for (int x = 0; x < activeX_; ++x)         row[x] = bg;
        for (int x = activeX_ + W; x < curW_; ++x) row[x] = bg;
    }

}

// Octets RÉELLEMENT lus par le shifter sur la scanline `scanline` (hors line-offset
// STE et scroll, ajoutés par l'appelant) : 160 nominal, modulé par les drapeaux de
// bordure de la machine Glue (port BORDERBYTES_* / Video_CopyScreenLineColor). Sans
// écriture freq/res cette trame (cas ultra-majoritaire), renvoie le bpl nominal.
int Shifter::glueLineBytes(int scanline) const {
    const int bpl = (frameMode_ == Mode::High) ? 80 : 160;
    if (frameMode_ == Mode::High || syncWrites_.empty()) return bpl;
    if (scanline < 0 || scanline + 1 >= static_cast<int>(glueLines_.size())) return bpl;
    const GlueLine& L = glueLines_[static_cast<std::size_t>(scanline)];
    if (L.displayStartCycle < 0) return bpl;             // ligne sans état glue calculé
    if (L.borderMask & glue::NO_DE) return 0;            // ligne sans display-enable
    int bytes = 160;                                     // BORDERBYTES_NORMAL
    const uint32_t bm = L.borderMask;
    if (bm & glue::LEFT_OFF)           bytes += 26;      // BORDERBYTES_LEFT
    else if (bm & glue::LEFT_PLUS_2)   bytes += 2;
    if (bm & glue::STOP_MIDDLE)        bytes -= 106;
    else if (bm & glue::RIGHT_MINUS_2) bytes -= 2;
    else if (bm & glue::RIGHT_OFF)     bytes += 44;      // BORDERBYTES_RIGHT
    if (bm & glue::RIGHT_OFF_FULL)     bytes += 22;      // BORDERBYTES_RIGHT_FULL
    // DIAG (NEOST_GLUE_DIAG) : décision Glue par ligne à tricks — à diff'er contre
    // la trace Hatari `video_res,video_sync` (detect remove/extend border).
    static const bool glueDiag = std::getenv("NEOST_GLUE_DIAG") != nullptr;
    if (glueDiag && bytes != 160)
        std::fprintf(stderr, "[GLUE] line=%d mask=%03x de=%d..%d bytes=%d\n",
                     scanline, L.borderMask, L.displayStartCycle, L.displayEndCycle, bytes);
    return bytes;
}

// Fin de Video_CopyScreenLine (video.c:3833-3872), adaptée au modèle NeoST : la
// ligne active vient d'être décodée → le compteur vidéo avance de son stride réel,
// puis les modifications STE différées pendant la ligne s'appliquent, dans l'ordre
// d'Hatari : scroll-prefetch (+1 mot), line-offset, offset compteur ($FF8205/07/09
// écrits pendant le DE), nouveau scroll fin, nouvelle largeur de ligne.
void Shifter::endVideoLine() {
    // Stride réel de la scanline qui se termine : les retraits de bordure de CETTE
    // ligne modifient le nombre d'octets lus (accumulation inter-lignes — la
    // calibration fullscreen d'Enchanted Land mesure $FF8209 à travers des lignes
    // élargies/raccourcies). Sans trick, glueLineBytes == bpl (chemin historique).
    if (!syncWrites_.empty())
        liveGlueCatchUp(liveStartHBL_ + vcLineY_);
    // ⚠ Divergence documentée (2026-07-03) : la scanline de commit est calée sur
    // liveStartHBL_ (VDE_On STICKY — un retrait haut verrouillé ne se re-ferme pas)
    // alors que videoCounter/renderGlueFrame suivent glueStartHBL_ (machine Glue
    // live, RE-FERMABLE comme le nStartHBL d'Hatari). Les deux ne divergent que si
    // une trame ouvre PUIS re-ferme la bordure haute (rarissime) ; aligner le commit
    // sur glueStartHBL_ ré-indexerait lineSnap_/vcLineY_ en cours de trame et
    // risquerait de casser la capture par-ligne validée (Cuddly) — on documente au
    // lieu de toucher.
    const int sl  = liveStartHBL_ + vcLineY_;
    const int bpl = glueLineBytes(sl);
    // Capture les octets que le shifter vient de lire sur CETTE ligne (échantillon
    // daté au faisceau, cf. lineSnap_) : bpl + marge d'arrondi au groupe de 16 px
    // du décodage fenêtré (+ avance scroll STE). Consommé par renderGlueFrame —
    // donc capturé SEULEMENT si la trame a des écritures freq/res (candidate au
    // rendu glue) : zéro coût sur le chemin normal.
    if (!syncWrites_.empty() && bpl > 0 &&
        sl >= 0 && sl < static_cast<int>(lineSnapLen_.size())) {
        int n = bpl + scrollCounterAdvance() + 8;
        if (n > kLineSnapBytes) n = kLineSnapBytes;
        uint8_t* snap = lineSnap_.data() + static_cast<std::size_t>(sl) * kLineSnapBytes;
        for (int i = 0; i < n; ++i)
            snap[i] = bus_.read8((vcLineBase_ + static_cast<uint32_t>(i)) & 0xFFFFFFu);
        lineSnapLen_[sl] = static_cast<uint16_t>(n);
    }
    vcLineBase_ += static_cast<uint32_t>(bpl);
    vcLineBase_ += static_cast<uint32_t>(scrollCounterAdvance());   // prefetch : +1 mot PAR PLAN
    vcLineBase_ += static_cast<uint32_t>(lineWidth) * 2u;  // line-offset STE (mots sautés)
    if (vcDelayedOffset_ != 0) {                           // écriture compteur pendant le DE
        vcLineBase_ += static_cast<uint32_t>(vcDelayedOffset_ & ~1);
        vcDelayedOffset_ = 0;
    }
    if (newHwScrollCount_ >= 0) {                          // HSCROLL différé (NewHWScrollCount)
        hwScrollCount    = static_cast<uint8_t>(newHwScrollCount_);
        hwScrollPrefetch = newHwScrollPrefetch_;
        newHwScrollCount_ = -1;
    }
    if (newLineWidth_ >= 0) {                              // LINEWIDTH différé (NewLineWidth)
        lineWidth = static_cast<uint8_t>(newLineWidth_);
        newLineWidth_ = -1;
    }
    vcLineBase_ &= 0xFFFFFFu;
    ++vcLineY_;
}

// RESTART du compteur vidéo en fin de trame — port de Video_RestartVideoCounter
// (video.c, ULM DSOTS) : à la ligne 310 (50 Hz) / 260 (60 Hz), cycle 56 (STF),
// le GLUE recharge le compteur depuis $FF8201/03. La base est relue À CET INSTANT
// (≠ début de trame NeoST) : un jeu double-buffer qui pose sa nouvelle base dans
// son handler VBL (APRÈS la ligne 0) est donc bien pris en compte — c'est ce que
// mesure le stabilisateur beam-sync d'Enchanted Land en jeu (poll $FF8209).
// Les lectures des lignes ≥ restart renvoient cette base figée (videoCounter).
void Shifter::restartVideoCounter(int line) {
    vcRestartBase_ = static_cast<int64_t>(videoBase & 0xFFFFFFu);
    vcRestartLine_ = line;
}

// Position du faisceau (ligne absolue + cycle dans la ligne) depuis l'horloge de
// trame. Sert aux décisions immédiat/différé des écritures STE (port
// Video_GetPosition_OnWriteAccess). false si aucune horloge n'est branchée (tests).
bool Shifter::beamPos(int& line, int& lineCyc) const {
    if (!beamClock_) return false;
    const Geometry g = geometry();
    const int64_t fc = beamClock_();
    if (fc < 0) return false;
    line    = static_cast<int>(fc / g.cyclesPerLine);
    lineCyc = static_cast<int>(fc % g.cyclesPerLine);
    return true;
}

// Fin de trame : re-rendu spec512 (palette intra-ligne) si détecté. Port du
// modèle Hatari spec512.c — au lieu de mémoriser une palette par ligne (figée à
// DE_END), on rejoue les écritures palette datées et on met à jour une palette
// « roulante » au CYCLE où chaque pixel est balayé. Le 68000 ne peut écrire la
// palette qu'une fois tous les 4 cycles (bus 16 bits), donc au plus ~1 changement
// tous les 4 pixels en basse résolution → jusqu'à 512 couleurs à l'écran.
void Shifter::finishFrame() {
    // Commit de la DERNIÈRE ligne active : le commit paresseux de renderLine(y) ne
    // pousse dans endVideoLine que les lignes < y — la ligne curAH_−1 n'était donc
    // JAMAIS committée (pas de capture lineSnap_ pour elle → l'artefact « sprite en
    // course avec le faisceau » pouvait réapparaître sur cette unique ligne). On la
    // committe ici, AVANT le replay/rendu glue. Pas de double-commit (vcLineY_ fait
    // foi) ni de décalage des autres lignes : les lectures $FF8205/07/09 en bordure
    // basse extrapolaient déjà exactement ce stride (mêmes valeurs qu'avant).
    while (vcLineY_ < curAH_) endVideoLine();

    if (frameMode_ == Mode::High) return;                      // mono : ni spec512 ni bordures

    // Rejoue la machine Glue sur les écritures freq/res datées → état d'affichage
    // par scanline + bordures haut/bas (détection de retrait). Bon marché si aucun
    // switch (cas normal → bordersTrick_ reste faux).
    replayGlue();

    // DEBUG (NEOST_PAL_TRACE) : dump PAR TRAME des écritures palette (ligne, cyc,
    // idx, couleur, PC), même hors spec512 — diagnostic des rasters « normaux »
    // (raster bars, splits Timer B/HBL). Le fichier est réécrit à chaque trame →
    // il contient la DERNIÈRE trame rendue. Format aligné sur NEOST_SPEC512_TRACE.
    static const char* palTrace = std::getenv("NEOST_PAL_TRACE");
    if (palTrace && !colorWrites_.empty()) {
        auto ws = colorWrites_;                    // copie : ne pas perturber spec512
        std::stable_sort(ws.begin(), ws.end(),
                         [](const ColorWrite& a, const ColorWrite& b) {
                             return a.frameCycle < b.frameCycle;
                         });
        if (FILE* tf = std::fopen(palTrace, "w")) {
            const Geometry gg = geometry();
            for (const auto& w : ws)
                std::fprintf(tf, "line %d cyc=%d idx=%d col=%03x pc=%06x\n",
                             static_cast<int>(w.frameCycle / gg.cyclesPerLine),
                             static_cast<int>(w.frameCycle % gg.cyclesPerLine),
                             w.index, w.colour & 0xFFF, w.pc);
            std::fclose(tf);
        }
    }

    // Si un retrait de bordure est détecté, le rendu fenêtré (palette roulante)
    // gère TOUT : bordures ouvertes + raster par ligne + spec512 intra-ligne.
    if (bordersTrick_) { renderGlueFrame(); return; }

    if (!spec512Active_) return;                               // trame normale : rendu ligne-à-ligne conservé

    // Tri stable par cycle : les écritures d'un même move.l / movem partagent un
    // cycle ; l'ordre d'insertion (= ordre des registres) doit être conservé pour
    // que la dernière l'emporte, exactement comme sur le bus.
    std::stable_sort(colorWrites_.begin(), colorWrites_.end(),
                     [](const ColorWrite& a, const ColorWrite& b) {
                         return a.frameCycle < b.frameCycle;
                     });

    // Wait states d'alignement bus 4 cycles du shifter (cf. applyShifterBusAlignment) :
    // recale les positions intra-ligne sur le vrai HW (sans ça, dérive -2 cyc/ligne).
    applyShifterBusAlignment();

    // DEBUG (NEOST_SPEC512_TRACE) : dump des écritures palette converties en
    // (ligne, position dans la ligne) — format comparable au trace `video_color`
    // d'Hatari (« spec store col line N cyc=H idx=I col=RGB »). Diff oracle.
    static const char* spcTrace = std::getenv("NEOST_SPEC512_TRACE");
    if (spcTrace) {
        const Geometry gg = geometry();
        FILE* tf = std::fopen(spcTrace, "w");
        if (tf) {
            for (const auto& w : colorWrites_) {
                const int line = static_cast<int>(w.frameCycle / gg.cyclesPerLine);
                const int hpos = static_cast<int>(w.frameCycle % gg.cyclesPerLine);
                std::fprintf(tf, "line %d cyc=%d idx=%d col=%03x pc=%06x\n",
                             line, hpos, w.index, w.colour & 0xFFF, w.pc);
            }
            std::fclose(tf);
            std::fprintf(stderr, "[spec512] %zu écritures palette → %s\n",
                         colorWrites_.size(), spcTrace);
        }
    }

    const Geometry g   = geometry();
    const int W        = activeWidth();            // pixels de l'écran actif (hors bordures)
    const int cpl      = g.cyclesPerLine;
    const int lineStart= g.lineStartCycle;
    const int dispStart= g.dispStartLine;          // VDE_On : l'affichage actif commence à cette ligne
    const int span     = g.lineEndCycle - g.lineStartCycle;    // cycles couvrant les W pixels affichés

    // Palette roulante : démarre à l'état de début de trame puis absorbe chaque
    // écriture au cycle voulu, en avançant un curseur unique sur toute la trame
    // (les cycles-pixel sont monotones croissants ligne après ligne).
    std::array<uint16_t, 16> pal = frameStartPalette_;
    const std::size_t n = colorWrites_.size();
    std::size_t cur = 0;

    static const char* alo = std::getenv("NEOST_ALIGN_OFF");   // DEBUG : offset ADDITIONNEL (relatif à kSpec512AlignCyc) pour re-calibrer le rendu vs oracle
    const int alignOff = alo ? std::atoi(alo) : 0;

    uint8_t idx[660];
    for (int y = 0; y < curAH_; ++y) {
        const int scroll = decodeLineIndices(y, idx);
        // Zone active dans le buffer overscan (décalée des bordures gauche/haut).
        uint32_t* dst = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_ + activeX_;
        for (int c = 0; c < W; ++c) {
            // Cycle (dans la trame) où le pixel c de la ligne y sort du shifter
            // (1 cycle/pixel en basse résolution, 0,5 en moyenne). La ligne active y
            // est balayée à la scanline (dispStart + y) — l'offset VDE_On aligne les
            // écritures (datées au cycle live, ~(63+y)*cpl) sur les pixels. Le décalage
            // kSpec512AlignCyc cale le front couleur sur le front pixel.
            const int64_t pixCyc = static_cast<int64_t>(dispStart + y) * cpl + lineStart
                                 + static_cast<int64_t>(c) * span / W;
            const int64_t limit = pixCyc - kSpec512AlignCyc + alignOff;
            while (cur < n && colorWrites_[cur].frameCycle <= limit) {
                pal[colorWrites_[cur].index] = colorWrites_[cur].colour;
                ++cur;
            }
            dst[c] = stColorToArgb(pal[idx[c + scroll]]);
        }
    }
}

// Rejoue HORS-LIGNE les wait states d'alignement bus du shifter (port fidèle de
// Hatari M68000_SyncCpuBus, video.c:5382). Les registres couleur ($FF824x) ne
// s'accèdent que sur une frontière de 4 cycles : une écriture mot qui arrive à un
// cycle non multiple de 4 fait patienter le CPU jusqu'à la frontière, soit
// (4 - cyc%4) cycles de gel. Le CPU étant gelé, CE wait DÉCALE D'AUTANT toutes les
// écritures suivantes → on accumule le décalage et on le propage. Moira (68000 pur)
// ne modélise pas ces wait states ; la boucle spec512 (24 move.l (a3)+,(ax)+ + dbra
// = 510 cyc/ligne sous Moira) dérive alors de -2 cyc/ligne au lieu de +4 sur vrai HW.
// En recalant chaque écriture sur sa frontière de 4 cycles ON RECONSTRUIT le timing
// matériel, sans toucher la timeline live (zéro régression). colorWrites_ est déjà
// trié par cycle d'exécution (croissant) en entrée.
void Shifter::applyShifterBusAlignment() {
    int64_t accumWait = 0;                 // total des wait states injectés jusqu'ici
    for (auto& w : colorWrites_) {
        const int64_t arrival = static_cast<int64_t>(w.frameCycle) + accumWait;
        // Même convention que syncCpuBus : Hatari aligne la FIN de l'accès (= cycle
        // enregistré + 2, le cycle live NeoST datant le point-MILIEU de l'accès Moira).
        // syncCpuBus (LIVE) ayant déjà aligné, ceci reste un no-op de garde.
        const int64_t wait = (4 - ((arrival + 2) & 3)) & 3;   // 0..3 jusqu'à la frontière de 4
        accumWait += wait;
        w.frameCycle = static_cast<int32_t>(arrival + wait);
    }
}

// Enregistre l'écriture palette du registre `index` (valeur déjà posée dans
// palette[index]) avec son cycle live dans la trame, pour le re-rendu spec512.
void Shifter::recordColorWrite(int index) {
    if (!liveFrameClock_) return;
    const int64_t fc = liveFrameClock_();
    if (fc < 0) return;                              // hors trame courante

    // Une écriture MOT ($FF824x) passe par le bus en DEUX write8 (gros-boutiste,
    // même cycle) : le 1ᵉʳ pose l'octet haut (valeur transitoire haut-neuf/bas-vieux),
    // le 2ᵉ l'octet bas (valeur finale). Sur le vrai 68000 c'est UN seul accès mot.
    // On fusionne donc les deux : si la dernière écriture vise le MÊME registre au
    // MÊME cycle, on met simplement à jour sa couleur (valeur finale) — un seul
    // ColorWrite par mot, comme Hatari (CyclePalettes[] = 1 entrée/écriture mot).
    if (!colorWrites_.empty()) {
        ColorWrite& last = colorWrites_.back();
        if (last.frameCycle == static_cast<int32_t>(fc) &&
            last.index == static_cast<uint8_t>(index)) {
            last.colour = palette[index];
            return;
        }
    }
    const uint32_t wpc = bus_.cpu ? bus_.cpu->pc() : 0;
    colorWrites_.push_back({ static_cast<int32_t>(fc), palette[index],
                             static_cast<uint8_t>(index), wpc });
    if (++paletteAccesses_ >= kSpec512Threshold) spec512Active_ = true;
}

// Wait state de bus 4 cycles (port LIVE de Hatari M68000_SyncCpuBus, cf. .hpp). Hatari
// aligne la FIN de l'accès (Cycles_GetClockCounterOn{Read,Write}Access = currcycle+4)
// sur la frontière de 4. Le cycle live NeoST date le point-MILIEU de l'accès Moira →
// fin d'accès = fc + 2 ; on aligne CETTE fin (cohérent avec chipWait8 qui aligne le
// début d'accès, et avec les offsets de datation write +2 / read −6).
void Shifter::syncCpuBus() {
    if (!liveFrameClock_ || !bus_.cpu) return;
    const int64_t fc = liveFrameClock_();
    if (fc < 0) return;                                  // hors trame courante
    const int wait = static_cast<int>((4 - ((fc + 2) & 3)) & 3);   // fin d'accès → frontière
    if (wait) bus_.cpu->addBusWaitCycles(wait);
}

// Datation des ÉCRITURES freq/res — VALEUR FIDÈLE THÉORIQUE **+2** (2026-07-03)
// = fin d'accès Moira (fc+2, ≙ WinUAE currcycle+4, callback au point-MILIEU),
// SANS décalage d'origine (doc convergence §8, cible #2). L'« origine −8 »
// mesurée le 2026-07-02 (write −6 / read −14) était un ARTEFACT co-calibré :
// la mesure croisée à l'oracle (menu Cuddly, ancre VBL f02e + chemin CPU
// identique) montre que l'origine d'horloge trame NeoST = Hatari, et que les
// DEUX datations devaient revenir ENSEMBLE (+8 chacune) aux valeurs fidèles
// de la table §8 (read −6, write +2). Historique : le +16 initial puis le −6
// étaient des rustines calibrées autour de biais CPU corrigés depuis (IACK
// sur-compté, alignement bus, double comptage STOP). Validé ENSEMBLE :
// Cuddly menu 250/250 verrouillées, EL loader + top-trick 40/40, LX titre
// propre, SHO byte-identique, étalons 19/19 + TOUS OK.
// NEOST_SYNC_OFF ajuste pour l'A/B (−8 → ancien −6).
static constexpr int kSyncWriteOffsetCyc = +2;

void Shifter::recordSyncWrite(bool isRes, uint8_t val) {
    if (!liveFrameClock_) return;
    int64_t fc = liveFrameClock_();
    if (fc < 0) return;
    fc += kSyncWriteOffsetCyc;
    // DEBUG (NEOST_SYNC_OFF) : offset ADDITIONNEL de datation des écritures freq/res —
    // sert à mesurer un écart systématique de datation CPU↔glue contre l'oracle.
    static const int syncOff = [] { const char* s = std::getenv("NEOST_SYNC_OFF"); return s ? std::atoi(s) : 0; }();
    fc += syncOff;
    if (fc < 0) fc = 0;
    // DEBUG (NEOST_SYNC_TRACE) : trace TOUTES les écritures freq/res (ligne/cycle) pour
    // diagnostiquer la détection des retraits de bordure (≠ NEOST_BORDER_TRACE qui n'émet
    // QUE si un trick est déjà armé). Cf. menu Cuddly : retrait bordure basse non détecté.
    if (std::getenv("NEOST_SYNC_TRACE")) {
        const int cpl = geometry().cyclesPerLine;
        std::fprintf(stderr, "[SYNC] %s val=%02x line=%lld cyc=%lld pc=%06x\n",
            isRes ? "res " : "freq", val, static_cast<long long>(fc / cpl),
            static_cast<long long>(fc % cpl), bus_.cpu ? bus_.cpu->pc() : 0);
    }
    // DIAG (NEOST_WRITE_DIAG) : tranche le modèle de datation. fcRaw = liveFrameClock
    // (avant offset) ; into = cycles BUS écoulés DANS l'instruction au write8 (≈ P+2 si
    // liveFrameClock est LIVE, ≈0 s'il est épinglé au début d'instr). Permet de comparer
    // au modèle Hatari CE (fin d'accès = fcRaw + 2).
    // CONCLU (2026-06-18) : into=2..16 selon l'instr → liveFrameClock est LIVE, capte DÉJÀ
    // la position sous-instr → la datation faithful Hatari-CE = fcRaw+2 CONSTANT (pas
    // par-instruction). Cf. docs/MOIRA_WINUAE_CONVERGENCE.md §7 : datation par-instruction
    // RÉFUTÉE. Le résidu EL = un stabilisateur nop-slide qui ne verrouille pas (videoCounter
    // read daté +10 vs Hatari) + hacks write(+16 ≥+14 loader)/read(+4) co-calibrés → refonte
    // coordonnée, pas la datation d'écriture seule.
    if (std::getenv("NEOST_WRITE_DIAG") && bus_.cpu) {
        const int cpl = geometry().cyclesPerLine;
        const int64_t fcRaw = liveFrameClock_();
        const int64_t into = bus_.cpu->cyclesIntoInstr();
        std::fprintf(stderr, "[WDIAG] %s val=%02x line=%lld cyc=%lld into=%lld pc=%06x\n",
            isRes ? "res " : "freq", val, static_cast<long long>(fcRaw / cpl),
            static_cast<long long>(fcRaw % cpl), static_cast<long long>(into),
            bus_.cpu->pc());
    }
    // Filtre Hatari « écriture de la même valeur ignorée » : Video_Sync_WriteByte
    // compare Freq = $FF820A & 2 à ShifterFrame.Freq (video.c:3055-3057) et
    // Video_WriteToGlueRes compare Res = $FF8260 & 3 à ShifterFrame.Res
    // (video.c:1629-1631) AVANT Video_Update_Glue_State — une réécriture
    // redondante n'atteint jamais la machine Glue. Sans ce filtre, réécrire #0
    // dans $FF820A en 60 Hz déclenchait un RIGHT_MINUS_2 parasite (ligne 158
    // octets). Le dernier Freq/Res vu PERSISTE à travers les trames (remis à −1
    // au seul reset, comme ShifterFrame). Restent HORS filtre, comme Hatari : le
    // wait state bus et le stockage du registre (`sync = v` / `mode = ...` dans
    // write8, ≙ IoMem qui garde la valeur) et les traces ci-dessus (≙ LOG_TRACE,
    // émis avant le filtre).
    if (isRes) {
        const int res = val & 0x03;
        if (res == lastGlueRes_) return;                 // même Res → Glue inchangée
        lastGlueRes_ = res;
    } else {
        const int freq = val & 0x02;
        if (freq == lastGlueFreq_) return;               // même Freq → Glue inchangée
        lastGlueFreq_ = freq;
    }
    syncWrites_.push_back({ static_cast<int32_t>(fc), val, isRes });
    updateLiveStartHBL(static_cast<int32_t>(fc), isRes, val);   // VDE_On live (retrait haut)
    // Machine Glue LIVE : consomme l'écriture immédiatement (fenêtre DE de la ligne
    // courante à jour pour les lectures $FF8209 qui suivent).
    liveGlueCatchUp(static_cast<int>(fc / geometry().cyclesPerLine));
}

// Met à jour le VDE_On LIVE du compteur vidéo sur une écriture freq — détection du
// RETRAIT de bordure HAUTE (port du comportement de Hatari Video_Update_Glue_State /
// nStartHBL, video.c ~2895). Sur le vrai matériel, une bascule 60 Hz pendant la
// bordure haute (avant VDE_On 50 Hz = ligne 63) ouvre le haut de l'écran : la 1ʳᵉ
// ligne affichée passe à 34 (VDE_On 60 Hz) et le compteur d'adresse vidéo commence
// donc à monter dès la ligne 34 au lieu de 63. C'est exactement ce dont dépendent les
// boucles d'auto-synchro fullscreen (Cuddly Demo) qui sondent $FF8209 pour se caler.
//
// Modèle (approximation fidèle au RÉSULTAT) : toute bascule 60 Hz dans la bordure haute
// VERROUILLE VDE_On=34 pour la trame — on ne fait que BAISSER (jamais remonter). En
// effet la boucle d'auto-synchro toggle 60→50 Hz à chaque itération ; sur le matériel
// la décision est latchée au passage de la ligne et le 50 Hz qui suit ne la ré-ferme
// pas. Un retrait gauche/droite (sur les lignes AFFICHÉES ≥ 63) ou bas (ligne 262)
// n'entre pas dans la fenêtre [0,63) → non concerné. Un écran 50 Hz ordinaire ne fait
// AUCUNE bascule freq → liveStartHBL_ reste 63 (zéro régression). On NE touche QUE
// liveStartHBL_ (lu par videoCounter) ; la géométrie de rendu (replayGlue) est inchangée.
void Shifter::updateLiveStartHBL(int32_t frameCycle, bool isRes, uint8_t val) {
    if (frameMode_ == Mode::High) return;            // mono : pas concerné
    if (isRes) return;                               // seules les bascules freq bougent VDE_On ici
    const Geometry g = geometry();
    const int line = frameCycle / g.cyclesPerLine;
    constexpr int VDE_On_50 = 63, VDE_On_60 = 34;
    const bool freq60 = !(val & 0x02);               // bit1=0 → 60 Hz
    if (freq60 && line < VDE_On_50 && VDE_On_60 < liveStartHBL_)
        liveStartHBL_ = VDE_On_60;                   // retrait haut verrouillé (sticky)
}

// Scanline affichée d'après la machine Glue LIVE (cf. déclaration).
bool Shifter::liveLineDisplayed(int line) {
    if (frameMode_ != Mode::High && !syncWrites_.empty()
        && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
        liveGlueCatchUp(line);
        // Fenêtre verticale [nStartHBL, nEndHBL + BlankLines) comme Hatari
        // (video.c:3649) : les lignes blanches no-sync repoussent la fin d'autant.
        if (line < glueStartHBL_ || line >= glueEndHBL_ + glueBlankLines_) return false;
        return !(glueLines_[static_cast<std::size_t>(line)].borderMask & glue::NO_DE);
    }
    const Geometry g = geometry();                       // fenêtre nominale (zéro trick)
    return line >= g.dispStartLine && line < g.dispStartLine + g.displayLines;
}

// Valeurs par défaut d'une scanline selon res/freq COURANTS au début de la ligne
// (port de Hatari Video_StartHBL). DisplayStartCycle n'est posé que s'il vaut -1
// (une écriture de la ligne précédente a pu le pré-positionner : right-off full).
void Shifter::startHBL(int line, int curRes, int freqHz) {
    GlueLine& L = glueLines_[line];
    if (curRes == 2) {                                   // haute résolution (71 Hz)
        if (L.displayStartCycle == -1) L.displayStartCycle = glue::HDE_On_Hi;   // 4
        L.displayEndCycle = glue::HDE_Off_Hi;            // 164
        if (nScreenRefreshRate_ != 71) {                 // ligne hi dans écran non-71 → retrait gauche par défaut
            L.borderMask |= glue::LEFT_OFF;
            L.displayPixelShift = -4;
        }
    } else if (freqHz == 50) {
        if (L.displayStartCycle == -1) L.displayStartCycle = glue::HDE_On_Low_50;  // 56
        L.displayEndCycle = glue::HDE_Off_Low_50;        // 376
    } else {                                             // 60 Hz
        if (L.displayStartCycle == -1) L.displayStartCycle = glue::HDE_On_Low_60;  // 52
        L.displayEndCycle = glue::HDE_Off_Low_60;        // 372
        if (nScreenRefreshRate_ == 50)                   // ligne 60 Hz dans écran 50 Hz → left+2/right-2
            L.borderMask |= (glue::LEFT_PLUS_2 | glue::RIGHT_MINUS_2);
    }
}

// Port FIDÈLE de Video_Update_Glue_State (chemin STF) : applique une écriture
// freq/res au cycle `lineCycles` de la scanline `line`. Met à jour la GlueLine
// (DE start/end, BorderMask, PixelShift), les lignes voisines (right-off full), et
// les bordures haut/bas (glueStartHBL_/glueEndHBL_ + glueVOverscan_).
void Shifter::updateGlueState(int line, int lineCycles, bool writeToRes, int freqHz) {
    using namespace glue;
    if (writeToRes) lineCycles--;                        // GLUE latche la res 1 cyc avant la freq (STF)

    GlueLine& GL = glueLines_[line];
    int DE_start = GL.displayStartCycle;
    int DE_end   = GL.displayEndCycle;
    uint32_t BorderMask = GL.borderMask;
    bool Freq_match_found = false;
    const int cpl = geometry().cyclesPerLine;
    // Canal HBL_Pos/nCyclesPerLine (video.c 2231-2232 + application 2849-2877) :
    // posé par les branches « Freq_match » de phase 1 ci-dessous, lu par l'appelant
    // (liveGlueCatchUp → Machine, replayGlue → attribution) après chaque écriture.
    glueHblPos_     = -1;
    glueCyclesLine_ = -1;

    // ===== Phase 1 : valeur de Freq AVANT DE_start (STF, video.c 2244-2438) =====
    if (freqHz == 71 && lineCycles <= HDE_On_Hi) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) {
            DE_start = HDE_On_Hi; DE_end = HDE_Off_Hi;
            BorderMask |= LEFT_OFF; GL.displayPixelShift = -4;
            BorderMask &= ~LEFT_PLUS_2;
        }
    } else if (freqHz == 71 && lineCycles <= HBlank_Off_Low_50) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= (BLANK | NO_DE); }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz == 71 && lineCycles <= HDE_On_Low_50) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= NO_DE; }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz != 71) {
        if (lineCycles <= HDE_On_Hi && (BorderMask & LEFT_OFF)) {
            if (freqHz == 50) DE_start = HDE_On_Low_50;
            else { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
            BorderMask &= ~LEFT_OFF; GL.displayPixelShift = 0;
        }
        if (lineCycles <= HBlank_Off_Low_50 && (BorderMask & (BLANK | NO_DE)) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~(BLANK | NO_DE);
        } else if (lineCycles <= HDE_On_Low_50 && (BorderMask & NO_DE) && !(BorderMask & BLANK) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~NO_DE;
        }
    }

    // Ligne 50 Hz qui continue en 60 Hz (et réciproque) — video.c 2342-2421
    if (freqHz == 60 && lineCycles < Line_Set_Pal) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_60; glueCyclesLine_ = CyclesLine_60;
        if (!(BorderMask & NO_DE)) {
            if (DE_start > 0) { DE_end = HDE_Off_Low_60; BorderMask |= RIGHT_MINUS_2; }
            if (lineCycles > HBlank_Off_Low_60 && lineCycles <= HBlank_Off_Low_50) BorderMask |= BLANK;
            if (DE_start == HDE_On_Low_50) { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
        }
    } else if (freqHz == 50 && lineCycles <= HDE_On_Low_60) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_50; glueCyclesLine_ = CyclesLine_50;
        if (!(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Low_50;
            BorderMask &= ~RIGHT_MINUS_2;
            if (DE_start == HDE_On_Low_60) { DE_start = HDE_On_Low_50; BorderMask &= ~LEFT_PLUS_2; }
        }
    } else if (freqHz == 50 && lineCycles <= Line_Set_Pal) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_50; glueCyclesLine_ = CyclesLine_50;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Low_50; BorderMask &= ~RIGHT_MINUS_2; }
    }

    if (freqHz == 60 && lineCycles > HDE_On_Low_60 && lineCycles <= HDE_On_Low_50 && !(BorderMask & NO_DE)) {
        Freq_match_found = true;
        if (DE_start == HDE_On_Low_50) { DE_start = 0; DE_end = 0; BorderMask |= NO_DE; }
    }

    // ===== Phase 2 : valeur de Freq ENTRE DE_start et DE_end (video.c 2667-2841) =====
    if (!Freq_match_found) {
        GlueLine& NX = glueLines_[line + 1];             // ligne suivante (right-off full, no-sync)
        if (freqHz == 71 && lineCycles <= DE_end && lineCycles <= HDE_Off_Hi && !(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Hi; BorderMask |= STOP_MIDDLE; BorderMask &= ~RIGHT_MINUS_2;
        } else if (freqHz == 71 && lineCycles <= DE_end && !(BorderMask & NO_DE)) {
            DE_end = LINE_END_FULL;
            BorderMask |= (RIGHT_OFF | RIGHT_OFF_FULL);
            NX.borderMask |= LEFT_OFF; NX.displayStartCycle = HDE_On_Hi;
        } else if (freqHz == 71 && lineCycles <= cpl + HSync_On_Off_Low) {
            BorderMask |= NO_SYNC;
            NX.borderMask |= (BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = 0; NX.displayEndCycle = 0;
            glueBlankLines_++;
        } else if (freqHz == 71 && lineCycles <= cpl + HSync_Off_Off_Low) {
            BorderMask |= SYNC_HIGH;
            NX.borderMask |= (BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = 0; NX.displayEndCycle = 0;
            glueBlankLines_++;
        } else if (freqHz == 71) {
            NX.borderMask |= LEFT_OFF;
            NX.displayStartCycle = HDE_On_Hi; NX.displayEndCycle = HDE_Off_Hi; NX.displayPixelShift = -4;
        }

        if (freqHz == 60 && lineCycles <= DE_end && lineCycles <= HDE_Off_Low_60 && !(BorderMask & NO_DE)) {
            if (DE_end == HDE_Off_Low_50) BorderMask |= RIGHT_MINUS_2;
            DE_end = HDE_Off_Low_60;
            if (BorderMask & STOP_MIDDLE) BorderMask &= ~STOP_MIDDLE;
            else if (BorderMask & (RIGHT_OFF | RIGHT_OFF_FULL)) {
                BorderMask &= ~(RIGHT_OFF | RIGHT_OFF_FULL);
                NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
            }
        } else if (freqHz == 50 && lineCycles <= DE_end && lineCycles <= HDE_Off_Low_50 && !(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Low_50;
            if (BorderMask & RIGHT_MINUS_2) BorderMask &= ~RIGHT_MINUS_2;
            else if (BorderMask & STOP_MIDDLE) BorderMask &= ~STOP_MIDDLE;
            else if (BorderMask & (RIGHT_OFF | RIGHT_OFF_FULL)) {
                BorderMask &= ~(RIGHT_OFF | RIGHT_OFF_FULL);
                NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
            }
        } else if (freqHz == 60 && lineCycles <= DE_end && lineCycles > HDE_Off_Low_60
                   && lineCycles <= HDE_Off_Low_50 && !(BorderMask & NO_DE)) {
            if (DE_end == HDE_Off_Low_50) {              // retrait bordure DROITE
                DE_end = cpl + HSync_On_Off_Low;        // 462 (50 Hz) — l'affichage va jusqu'au HSYNC
                BorderMask |= RIGHT_OFF;
                BorderMask &= ~RIGHT_MINUS_2;
            }
        } else if (freqHz != 71 && lineCycles <= cpl + HSync_On_Off_Low) {
            if (lineCycles <= DE_end) {
                DE_end = cpl + HSync_On_Off_Low;
                if (BorderMask & RIGHT_OFF_FULL) {
                    BorderMask &= ~RIGHT_OFF_FULL;
                    NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
                }
            } else if (BorderMask & NO_SYNC) {
                BorderMask &= ~NO_SYNC;
                NX.borderMask &= ~(BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = -1; glueBlankLines_--;
            }
        } else if (freqHz != 71 && lineCycles <= cpl + HSync_Off_Off_Low) {
            if (BorderMask & SYNC_HIGH) {
                BorderMask &= ~SYNC_HIGH;
                NX.borderMask &= ~(BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = -1; glueBlankLines_--;
            }
        }
    }

    // ===== Bordures HAUT/BAS (video.c 2896-2991) =====
    // Top : tant que la 1ʳᵉ ligne affichée n'est pas atteinte, on peut la déplacer.
    if (line < glueStartHBL_ - 1
        || (line == glueStartHBL_ - 1 && lineCycles <= RemoveTopBorder_Pos)) {
        int Top_Pos = (freqHz == 71) ? VDE_On_Hi : (freqHz == 60 ? VDE_On_60 : VDE_On_50);
        if (Top_Pos != glueStartHBL_
            && (line < Top_Pos - 1 || (line == Top_Pos - 1 && lineCycles <= RemoveTopBorder_Pos))) {
            glueStartHBL_ = Top_Pos;
            if (nScreenRefreshRate_ == 50 && glueStartHBL_ < VDE_On_50) glueVOverscan_ |= VO_NO_TOP;
            else glueVOverscan_ &= ~VO_NO_TOP;
            glueVOverscan_ &= ~VO_NO_DE;
        } else {
            if (nScreenRefreshRate_ == 50 && freqHz != 50) glueVOverscan_ |= VO_NO_DE;
            else glueVOverscan_ &= ~VO_NO_DE;
        }
    }
    // Bottom : tant que la dernière ligne affichée n'est pas atteinte.
    if (line < glueEndHBL_ - 1
        || (line == glueEndHBL_ - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
        int Bottom_Pos = (freqHz == 71) ? VDE_Off_Hi : (freqHz == 60 ? VDE_Off_60 : VDE_Off_50);
        if (line < VDE_Off_60 - 1
            || (line == VDE_Off_60 - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (nScreenRefreshRate_ == 60 && freqHz != 60) { glueEndHBL_ = VDE_Off_NoBottom_60; glueVOverscan_ |= VO_NO_BOTTOM_60; }
            else if (nScreenRefreshRate_ == 50 && freqHz == 60) { glueEndHBL_ = VDE_Off_60; glueVOverscan_ |= VO_BOTTOM_SHORT_50; }
            else { glueEndHBL_ = Bottom_Pos; glueVOverscan_ &= ~(VO_NO_BOTTOM_60 | VO_BOTTOM_SHORT_50); }
        } else if (line < VDE_Off_50 - 1
                   || (line == VDE_Off_50 - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (glueVOverscan_ & VO_NO_BOTTOM_60) { /* déjà retiré, inchangeable */ }
            else if (nScreenRefreshRate_ == 50 && freqHz != 50) { glueEndHBL_ = VDE_Off_NoBottom_50; glueVOverscan_ |= VO_NO_BOTTOM_50; }
            else { glueEndHBL_ = Bottom_Pos; glueVOverscan_ &= ~VO_NO_BOTTOM_50; }
        } else if (line < VDE_Off_Hi - 1
                   || (line == VDE_Off_Hi - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (glueVOverscan_ & VO_NO_BOTTOM_50) { /* déjà retiré */ }
            else { glueEndHBL_ = Bottom_Pos; }
        }
    }

    GL.displayStartCycle = static_cast<int16_t>(DE_start);
    GL.displayEndCycle   = static_cast<int16_t>(DE_end);
    GL.borderMask        = BorderMask;
}

// Rejoue la machine Glue sur les écritures freq/res datées de la trame, ligne par
// ligne (StartHBL defaults → écritures via updateGlueState). Remplit glueLines_,
// glueStartHBL_/glueEndHBL_ et arme bordersTrick_ si une bordure est retirée.
void Shifter::replayGlue() {
    bordersTrick_ = false;
    if (frameMode_ == Mode::High) return;                // mono : pas de bordures modélisées

    const Geometry g = geometry();
    const int lpf = g.linesPerFrame;
    const int cpl = g.cyclesPerLine;
    nScreenRefreshRate_ = (frameSync_ & 0x02) ? 50 : 60;
    const int baseStart = g.dispStartLine;               // VDE_On de l'écran (63/34)
    const int baseEnd   = baseStart + g.displayLines;    // VDE_Off (263/234)

    glueLines_.assign(static_cast<std::size_t>(lpf) + 2, GlueLine{ -1, 0, 0, 0 });
    glueStartHBL_   = baseStart;
    glueEndHBL_     = baseEnd;
    glueVOverscan_  = 0;
    glueBlankLines_ = 0;

    std::stable_sort(syncWrites_.begin(), syncWrites_.end(),
                     [](const SyncWrite& a, const SyncWrite& b){ return a.frameCycle < b.frameCycle; });

    // État res/freq COURANT (début de trame = valeurs verrouillées). res : 0=low,
    // 1=med, 2=hi ; freq : bit1 de $FF820A (1=50 Hz).
    int curRes  = (frameMode_ == Mode::Medium) ? 1 : (frameMode_ == Mode::High ? 2 : 0);
    int curFreq50 = (frameSync_ & 0x02) ? 1 : 0;

    // V2 res-switch (opt-in NEOST_V2) : attribution à LONGUEUR DE LIGNE VARIABLE —
    // une ligne où tombe une impulsion hi-res PRÉCOCE (≤56) ne fait que 224 cyc
    // (port HBL_Pos/nCyclesPerLine), COHÉRENT avec le raccourcissement live
    // (Machine setHblShorten) : sans ça l'état glue ne refléterait pas les lignes
    // raccourcies. Hors V2 : attribution fixe `frameCycle/cpl` historique (inchangée).
    static const bool v2 = std::getenv("NEOST_V2") != nullptr;
    // Longueurs de ligne PAR-LIGNE (NEOST_LINELEN) : l'attribution suit la grille
    // RÉELLE — la longueur d'une ligne évolue au fil de ses PROPRES écritures
    // (dernier nCyclesPerLine posé par un « Freq_match », défaut cpl), comme la
    // chaîne StartCycle/nCyclesPerLine de Hatari. Remplace l'heuristique V2
    // « impulsion hi ≤57 → 224 ».
    static const bool lineLen = std::getenv("NEOST_LINELEN") != nullptr;
    std::size_t wi = 0;
    const std::size_t nw = syncWrites_.size();
    int64_t lineCyc = 0;                                  // cycle-trame du début de la ligne (V2/LINELEN)
    for (int line = 0; line < lpf; ++line) {
        int freqHz = (curRes == 2) ? 71 : (curFreq50 ? 50 : 60);
        startHBL(line, curRes, freqHz);
        int len = cpl;
        if (v2 && !lineLen) {                            // ligne raccourcie ? (heuristique V2)
            for (std::size_t k = wi; k < nw && syncWrites_[k].frameCycle < lineCyc + 57; ++k)
                if (syncWrites_[k].isRes && (syncWrites_[k].val & 3) == 2) { len = 224; break; }
        }
        // Applique les écritures de CETTE ligne (cycle croissant). En mode LINELEN
        // la borne de fin de ligne (lineCyc+len) est DYNAMIQUE : chaque écriture
        // peut la déplacer via glueCyclesLine_.
        while (wi < nw) {
            const SyncWrite& w = syncWrites_[wi];
            if (lineLen || v2) { if (w.frameCycle >= lineCyc + len) break; }
            else               { if (w.frameCycle / cpl != line) break; }
            ++wi;
            const int lc = (lineLen || v2) ? static_cast<int>(w.frameCycle - lineCyc)
                                           : static_cast<int>(w.frameCycle % cpl);
            if (w.isRes) curRes    = w.val & 0x03;
            else         curFreq50 = (w.val & 0x02) ? 1 : 0;
            freqHz = (curRes == 2) ? 71 : (curFreq50 ? 50 : 60);
            updateGlueState(line, lc, w.isRes, freqHz);
            if (lineLen && glueCyclesLine_ > 0) len = glueCyclesLine_;
        }
        lineCyc += len;
    }

    // Détection : une bordure est-elle retirée ? (haut/bas déplacés, ou une ligne
    // affichée a un DE élargi gauche/droite).
    if (glueStartHBL_ != baseStart || glueEndHBL_ != baseEnd) bordersTrick_ = true;
    if (!bordersTrick_) {
        for (int sl = glueStartHBL_; sl < glueEndHBL_; ++sl) {
            const GlueLine& L = glueLines_[sl];
            if (L.displayStartCycle >= 0
                && (L.displayStartCycle < g.lineStartCycle || L.displayEndCycle > g.lineEndCycle)) {
                bordersTrick_ = true; break;
            }
        }
    }

    // Stat Glue (gated NEOST_GLUE_STAT) : liste les écritures freq/res datées de la
    // trame (ligne, cycle, registre, valeur) — diagnostic « pourquoi pas de trick ».
    if (!syncWrites_.empty() && std::getenv("NEOST_GLUE_STAT")) {
        std::fprintf(stderr, "[gluestat] %zu écritures :", syncWrites_.size());
        for (std::size_t i = 0; i < syncWrites_.size() && i < 24; ++i) {
            const SyncWrite& w = syncWrites_[i];
            std::fprintf(stderr, " %s=%02X@%lld+%d", w.isRes ? "res" : "frq", w.val,
                         (long long)(w.frameCycle / cpl), (int)(w.frameCycle % cpl));
        }
        std::fprintf(stderr, " | trick=%d start=%d end=%d\n", bordersTrick_ ? 1 : 0,
                     glueStartHBL_, glueEndHBL_);
    }

    // Trace bordure (gated NEOST_BORDER_TRACE) : pour le diff oracle Hatari
    // (video_border_h/v). Émet les retraits détectés cette trame, format comparable.
    if (bordersTrick_ && std::getenv("NEOST_BORDER_TRACE")) {
        if (glueStartHBL_ < baseStart)
            std::fprintf(stderr, "detect remove top (nStartHBL=%d)\n", glueStartHBL_);
        if (glueEndHBL_ > baseEnd)
            std::fprintf(stderr, "detect remove bottom (nEndHBL=%d)\n", glueEndHBL_);
        int nLeft = 0, nRight = 0;
        for (int sl = glueStartHBL_; sl < glueEndHBL_ && sl < (int)glueLines_.size(); ++sl) {
            const GlueLine& L = glueLines_[sl];
            if (L.displayStartCycle >= 0 && L.displayStartCycle < g.lineStartCycle) ++nLeft;
            if (L.displayEndCycle > g.lineEndCycle) ++nRight;
        }
        if (nLeft)  std::fprintf(stderr, "detect remove left x%d\n", nLeft);
        if (nRight) std::fprintf(stderr, "detect remove right x%d\n", nRight);
    }

    // INSTRUMENTATION (gated NEOST_VARLINE_TRACE) — chantier « longueur de ligne variable » :
    // calcule l'attribution ligne/lineCyc à LONGUEUR VARIABLE (508 pour une ligne 60 Hz,
    // 512 pour 50 Hz, 224 hi — façon Hatari ShifterLines[].StartCycle / Video_ConvertPosition)
    // et la compare à l'attribution FIXE (fc/cpl) utilisée actuellement. Ne change RIEN au
    // comportement ; sert à mesurer la divergence avant de porter le modèle. La longueur d'une
    // ligne est fixée par la freq au comparateur HBL (~cyc 502).
    if (!syncWrites_.empty() && std::getenv("NEOST_VARLINE_TRACE")) {
        int res = (frameMode_ == Mode::Medium) ? 1 : (frameMode_ == Mode::High ? 2 : 0);
        int f50 = (frameSync_ & 0x02) ? 1 : 0;
        int64_t cyc = 0; int vline = 0; std::size_t i = 0; int ndiff = 0, nshown = 0;
        while (vline < lpf + 2 && i < nw) {
            int r2 = res, f2 = f50; std::size_t j = i;        // freq au comparateur (~502)
            while (j < nw && syncWrites_[j].frameCycle < cyc + 502) {
                if (syncWrites_[j].isRes) r2 = syncWrites_[j].val & 3;
                else                      f2 = (syncWrites_[j].val & 2) ? 1 : 0;
                ++j;
            }
            const int freqHz = (r2 == 2) ? 71 : (f2 ? 50 : 60);
            const int len = (freqHz == 71) ? 224 : (freqHz == 60 ? 508 : 512);
            while (i < nw && syncWrites_[i].frameCycle < cyc + len) {
                const SyncWrite& w = syncWrites_[i];
                const int fixedLine = static_cast<int>(w.frameCycle / cpl);
                if (fixedLine != vline) {
                    ++ndiff;
                    if (++nshown <= 24)
                        std::fprintf(stderr, "[varline] %s=%02x fc=%d : fixe=L%d/c%d  var=L%d/c%d\n",
                            w.isRes ? "res" : "frq", w.val, w.frameCycle, fixedLine,
                            static_cast<int>(w.frameCycle % cpl), vline,
                            static_cast<int>(w.frameCycle - cyc));
                }
                if (w.isRes) res = w.val & 3; else f50 = (w.val & 2) ? 1 : 0;
                ++i;
            }
            cyc += len; ++vline;
        }
        std::fprintf(stderr, "[varline] %d/%zu writes mésattribués (fixe≠variable) | dérive finale=%lld cyc\n",
                     ndiff, nw, static_cast<long long>(cyc - static_cast<int64_t>(vline) * cpl));
    }
}

// Décode `nPix` index planaires à partir de l'adresse vidéo `base` (rendu fenêtré
// des bordures : largeur explicite, base fournie). Applique le SCROLL FIN STE avec
// le même modèle matériel que decodeLineIndices (port Video_CopyScreenLineColor) :
//  • PREFETCH ($FF8265) : 1 groupe de 16 px lu EN PLUS, la ligne est décalée à
//    gauche de `scroll` px (l'appelant consomme idx[s + scroll]) ;
//  • SANS prefetch ($FF8264) : aucun octet en plus, l'affichage démarre 16 px plus
//    tard (bord gauche couleur 0) — données décodées à partir de idx[16] puis
//    idx[0..16+scroll) mis à 0.
// scroll = 0 (ST/STF) → décodage historique strictement inchangé. Renvoie le
// décalage scroll. `idx` doit tenir nPix arrondi au groupe + 16 + 16 octets.
int Shifter::decodeWindowIndices(uint32_t base, int nPix, uint8_t* idx) const {
    const int planes = (frameMode_ == Mode::Medium) ? 2 : 4;   // low=4, med=2
    const int groupB = 2 * planes;                             // octets pour 16 px
    const int groups = (nPix + 15) / 16;
    const int  scroll   = hwScrollCount;                       // 0 hors STE scrollé
    const bool prefetch = hwScrollPrefetch;
    const int  decodeGroups = (scroll && prefetch) ? groups + 1 : groups;
    int px = (scroll && !prefetch) ? 16 : 0;
    for (int gI = 0; gI < decodeGroups; ++gI) {
        const uint32_t a  = base + static_cast<uint32_t>(gI) * groupB;
        const uint16_t p0 = bus_.read16(a);
        const uint16_t p1 = planes > 1 ? bus_.read16(a + 2) : 0;
        const uint16_t p2 = planes > 2 ? bus_.read16(a + 4) : 0;
        const uint16_t p3 = planes > 3 ? bus_.read16(a + 6) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    if (scroll && !prefetch)
        std::memset(idx, 0, static_cast<std::size_t>(16 + scroll));   // bord gauche couleur 0
    return scroll;
}

int Shifter::decodeWindowIndicesFromBytes(const uint8_t* src, int srcLen, int nPix, uint8_t* idx) const {
    // Même décodage planaire (et même modèle de scroll fin STE) que
    // decodeWindowIndices, mais depuis la CAPTURE de la ligne (octets
    // échantillonnés au faisceau) au lieu du bus. Au-delà de srcLen (marge de
    // capture épuisée) : octets à 0, comme une RAM vierge.
    const int planes = (frameMode_ == Mode::Medium) ? 2 : 4;
    const int groupB = 2 * planes;
    const int groups = (nPix + 15) / 16;
    const int  scroll   = hwScrollCount;
    const bool prefetch = hwScrollPrefetch;
    const int  decodeGroups = (scroll && prefetch) ? groups + 1 : groups;
    auto rd16 = [&](int off) -> uint16_t {
        const uint8_t hiB = (off     < srcLen) ? src[off]     : 0;
        const uint8_t loB = (off + 1 < srcLen) ? src[off + 1] : 0;
        return static_cast<uint16_t>((hiB << 8) | loB);
    };
    int px = (scroll && !prefetch) ? 16 : 0;
    for (int gI = 0; gI < decodeGroups; ++gI) {
        const int off     = gI * groupB;
        const uint16_t p0 = rd16(off);
        const uint16_t p1 = planes > 1 ? rd16(off + 2) : 0;
        const uint16_t p2 = planes > 2 ? rd16(off + 4) : 0;
        const uint16_t p3 = planes > 3 ? rd16(off + 6) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    if (scroll && !prefetch)
        std::memset(idx, 0, static_cast<std::size_t>(16 + scroll));
    return scroll;
}

// Re-rendu de la trame AVEC retrait de bordures : pour chaque LIGNE du buffer
// overscan, on calcule la scanline correspondante (sl = baseStart + row - activeY_),
// et si elle est affichée [glueStartHBL_, glueEndHBL_) on décode sa fenêtre
// [DisplayStartCycle, DisplayEndCycle) depuis l'ADRESSE VIDÉO ACCUMULÉE (port de
// Video_CalculateAddress : une ligne plus large lit plus d'octets → décale les
// suivantes). Palette ROULANTE (gère raster par ligne ET spec512 intra-ligne).
// Hors fenêtre (et lignes de bordure) = couleur registre 0 au cycle courant.
void Shifter::renderGlueFrame() {
    const Geometry g    = geometry();
    const int W         = curW_;                           // largeur buffer (overscan)
    const int cpl       = g.cyclesPerLine;
    const int baseStart = g.dispStartLine;                 // scanline du haut de l'actif (buffer row activeY_)
    // Moyenne résolution : le shifter sort 2 PIXELS PAR CYCLE (640 px sur la même
    // fenêtre DE de 320 cycles, 4 octets par groupe de 16 px au lieu de 8 — port du
    // LineRes par-ligne de Video_CopyScreenLineColor ; le chemin spec512 sans
    // bordure de finishFrame fait déjà ce mapping via c*span/W). Basse rés :
    // 1 px/cycle (chemin historique inchangé).
    const int ppc       = (frameMode_ == Mode::Medium) ? 2 : 1;
    // Cycle du pixel balayé à la colonne x=0 du buffer : bordure gauche incluse en
    // basse rés bordée (56−48=8) ; en moyenne rés le buffer n'a PAS de bordure
    // (bordered() = low seulement) → colonne 0 = début DE nominal.
    const int visFirst  = g.lineStartCycle - (bordered() ? kBorderLeftPx : 0);
    // Scroll fin STE : appliqué par decodeWindowIndices* (même modèle que
    // decodeLineIndices) ; l'émetteur lit idx[s + scroll]. 0 sur ST/STF.
    const int scroll    = hwScrollCount;

    std::stable_sort(colorWrites_.begin(), colorWrites_.end(),
                     [](const ColorWrite& a, const ColorWrite& b){ return a.frameCycle < b.frameCycle; });
    // Recalage spec512 (wait states bus + offset pixel↔couleur) UNIQUEMENT pour une
    // vraie image spec512 (palette réécrite intra-ligne). Pour un écran glue ordinaire
    // (desktop 60 Hz, barres raster), la palette est posée une fois hors-affichage :
    // on garde l'ancien chemin (offset 0, pas de wait states) → rendu byte-identique,
    // zéro régression. Les démos overscan spec512 (palette intra-ligne + bordures)
    // bénéficient du même recalage que le chemin sans bordure.
    const int glueAlignCyc = spec512Active_ ? kSpec512AlignCyc : 0;
    if (spec512Active_) applyShifterBusAlignment();
    std::array<uint16_t, 16> pal = frameStartPalette_;
    const std::size_t n = colorWrites_.size();
    std::size_t cur = 0;
    uint32_t addr = vcFrameBase_ & 0xFFFFFFu;              // compteur vidéo latché au VBL (≙ Video_ClearOnVBL)
    const int nLines = static_cast<int>(glueLines_.size());

    // DIAG (gated NEOST_RENDER_TRACE=<numéro de trame>) : état per-ligne au rendu glue.
    static int s_renderFrame = -1; ++s_renderFrame;
    static const int traceFrame = []{ const char* s = std::getenv("NEOST_RENDER_TRACE"); return s ? std::atoi(s) : -1; }();
    static const bool renderAll = std::getenv("NEOST_RENDER_ALL") != nullptr;   // toutes les lignes (≠ 12)
    const bool rtr = (traceFrame >= 0 && s_renderFrame >= traceFrame);
    if (rtr) std::fprintf(stderr, "[render f%d] base=%06x start=%d end=%d vover=%d\n",
                          s_renderFrame, vcFrameBase_ & 0xFFFFFF, glueStartHBL_, glueEndHBL_, glueVOverscan_);

    uint8_t idx[960];                                      // max DE med (462-0)*2 px + groupe scroll
    for (int row = 0; row < curH_; ++row) {
        const int sl = baseStart + (row - activeY_);       // scanline de cette ligne buffer
        // Fenêtre verticale = [nStartHBL, nEndHBL + BlankLines) comme Hatari
        // (video.c:3985, :1568) : les lignes blanches no-sync insérées (glueBlank-
        // Lines_) repoussent la fin d'affichage d'autant. 0 sans trick no-sync.
        const bool displayed = (sl >= glueStartHBL_ && sl < glueEndHBL_ + glueBlankLines_
                                && sl >= 0 && sl < nLines);
        int ds = 0, de = 0, shift = 0; uint32_t bm = 0;
        if (displayed) { const GlueLine& L = glueLines_[sl]; ds = L.displayStartCycle; de = L.displayEndCycle; bm = L.borderMask; shift = L.displayPixelShift; }
        const bool lineHasDE = displayed && !(bm & glue::NO_DE) && de > ds;
        // Ligne BLANK (bascule 60/50 près du blanking) : le shifter LIT ses octets
        // (le compteur avance, cf. glueLineBytes) mais le signal vidéo est coupé —
        // Hatari efface la ligne à l'index couleur 0 APRÈS l'avoir copiée
        // (video.c:4081 memset → couleur 0 ; le « vrai noir » est un TODO Hatari,
        // video.c:3983). On rend donc pal[0] sur toute la ligne, sans décoder.
        const bool lineBlank = lineHasDE && (bm & glue::BLANK) != 0;
        const int  nPix = lineHasDE ? (de - ds) * ppc : 0;
        if (rtr && displayed && (renderAll || sl < baseStart + 12))
            std::fprintf(stderr, "  sl%d ds=%d de=%d bm=%03x nPix=%d addr=%06x\n", sl, ds, de, bm, nPix, addr & 0xFFFFFF);
        // decodeWindowIndices décode des GROUPES de 16 px (+1 groupe si scroll avec
        // prefetch, ou offset 16 sans prefetch) : la plage valide de idx est
        // [0, nDec) — marge pour le DisplayPixelShift et le scroll.
        const int  nDec = lineHasDE ? ((nPix + 15) / 16) * 16 + (scroll ? 16 : 0) : 0;
        // Source des pixels : la CAPTURE datée au faisceau de cette scanline si elle
        // existe (cf. lineSnap_ — seul l'échantillon voit un sprite dessiné puis
        // effacé EN COURSE avec le faisceau, ex. robot du menu Cuddly), sinon repli
        // relecture RAM en fin de trame (lignes non committées : bordure haute
        // ouverte, bas de trame au-delà des lignes actives).
        if (nPix > 0 && !lineBlank) {
            const bool haveSnap = sl >= 0 && sl < static_cast<int>(lineSnapLen_.size())
                                  && lineSnapLen_[sl] > 0;
            if (haveSnap)
                decodeWindowIndicesFromBytes(lineSnap_.data() + static_cast<std::size_t>(sl) * kLineSnapBytes,
                                             lineSnapLen_[sl], nPix, idx);
            else
                decodeWindowIndices(addr, nPix, idx);
        }

        uint32_t* dst = frame_.data() + static_cast<std::size_t>(row) * W;
        for (int x = 0; x < W; ++x) {
            const int cyc = visFirst + x / ppc;            // cycle du pixel balayé à cette colonne
            const int64_t limit = static_cast<int64_t>(sl) * cpl + cyc - glueAlignCyc;
            while (cur < n && colorWrites_[cur].frameCycle <= limit) {
                pal[colorWrites_[cur].index] = colorWrites_[cur].colour;
                ++cur;
            }
            if (lineHasDE && !lineBlank && cyc >= ds && cyc < de) {
                // Décalage pixel de la ligne (Hatari DisplayPixelShift : <0 = vers la
                // gauche, p.ex. -4 sur le retrait gauche hi/lo). 0 pour les lignes
                // normales → aucun effet (top/bottom/écran standard inchangés).
                // En moyenne rés, 2 pixels par cycle (x/ppc + sous-pixel x%ppc).
                // Le scroll fin STE décale la source (idx[s + scroll], cf.
                // decodeWindowIndices — modèle decodeLineIndices).
                int s = (cyc - ds) * ppc + (x % ppc) - shift + scroll;
                if (s < 0) s = 0; else if (s >= nDec) s = nDec - 1;
                dst[x] = stColorToArgb(pal[idx[s]]);       // dans la fenêtre → contenu
            } else {
                dst[x] = stColorToArgb(pal[0]);            // hors fenêtre / bordure / blank → registre 0
            }
        }
        // Adresse vidéo ACCUMULÉE : avance par NOMBRES D'OCTETS FIXES selon les
        // drapeaux de bordure (port de Video_CopyScreenLineColor / BORDERBYTES_*,
        // video.h:111-115) et NON par (DE_end-DE_start)/2 : la fenêtre RIGHT_OFF
        // s'arrête à cpl-50=462 mais le shifter lit 160+44=204 octets (bord réel
        // 464) — l'ancien calcul perdait 1 octet PAR LIGNE et le décor dérivait
        // cumulativement vers le bas (loader TDA de Rick Dangerous : bandes de
        // garbage qui empirent ligne à ligne).
        // + line-offset STE ($FF820F, en mots) et avance de scroll fin (prefetch =
        //   1 mot PAR PLAN), comme l'accumulation par ligne de videoCounter — les
        //   `pVideoRaster += LineWidth*2` / `+= n*2` de Video_CopyScreenLineColor.
        //   0 sur ST/STF (chemin historique inchangé).
        if (lineHasDE) addr += static_cast<uint32_t>(glueLineBytes(sl)
                             + static_cast<int>(lineWidth) * 2 + scrollCounterAdvance());
    }
}

// Auto-test déterministe de la machine Glue (cf. déclaration). Chaque scénario
// injecte des écritures freq/res à des cycles exacts puis vérifie l'état calculé
// contre les valeurs Hatari. Affiche un récap sur stderr ; renvoie true si tout OK.
bool Shifter::glueSelfTest() {
    frameMode_ = Mode::Low;                       // STF 50 Hz basse rés
    frameSync_ = 0x02;
    resizeFor(frameMode_);
    const int cpl = geometry().cyclesPerLine;     // 512
    int pass = 0, fail = 0;

    // Rejoue une liste d'écritures {line, cycle, isRes(0=freq/1=res), val}.
    auto run = [&](std::vector<std::array<int,4>> writes) {
        syncWrites_.clear();
        for (const auto& v : writes)
            syncWrites_.push_back({ v[0] * cpl + v[1],
                                    static_cast<uint8_t>(v[3]), v[2] != 0 });
        replayGlue();
    };
    auto chk = [&](const char* name, long got, long want) {
        if (got == want) { ++pass; }
        else { ++fail; std::fprintf(stderr, "  FAIL %-26s got=%ld want=%ld\n", name, got, want); }
    };

    // 1. Bordure DROITE : 60 Hz @ cyc 374 puis 50 Hz @ 380, ligne 100.
    run({ {100,374,0,0x00}, {100,380,0,0x02} });
    chk("right DE_start", glueLines_[100].displayStartCycle, 56);
    chk("right DE_end",   glueLines_[100].displayEndCycle,   462);   // cpl-50 (RIGHT_OFF)
    chk("right mask",     (glueLines_[100].borderMask & glue::RIGHT_OFF) ? 1 : 0, 1);

    // 2. Bordure GAUCHE : hi-rés @ 2 puis lo-rés @ 6, ligne 100.
    run({ {100,2,1,0x02}, {100,6,1,0x00} });
    chk("left DE_start",  glueLines_[100].displayStartCycle, 4);     // HDE_On_Hi
    chk("left DE_end",    glueLines_[100].displayEndCycle,   376);
    chk("left mask",      (glueLines_[100].borderMask & glue::LEFT_OFF) ? 1 : 0, 1);

    // 3. RIGHT-2 (ligne 60 Hz) : 60 Hz @ 100 puis 50 Hz @ 400, ligne 100.
    run({ {100,100,0,0x00}, {100,400,0,0x02} });
    chk("right-2 DE_end", glueLines_[100].displayEndCycle,   372);   // HDE_Off_Low_60
    chk("right-2 mask",   (glueLines_[100].borderMask & glue::RIGHT_MINUS_2) ? 1 : 0, 1);

    // 4. Retrait HAUT : 60 Hz @ ligne 10, 50 Hz @ ligne 40.
    run({ {10,100,0,0x00}, {40,100,0,0x02} });
    chk("top nStartHBL",  glueStartHBL_, 34);                        // VDE_On_60
    chk("top NO_TOP",     (glueVOverscan_ & glue::VO_NO_TOP) ? 1 : 0, 1);
    chk("top nEndHBL",    glueEndHBL_, 263);                         // bas inchangé

    // 5. Retrait BAS : 60 Hz @ ligne 261.
    run({ {261,100,0,0x00} });
    chk("bottom nEndHBL", glueEndHBL_, 310);                         // VDE_Off_NoBottom_50

    // 6. Écran NORMAL (aucune écriture) : aucune bordure retirée.
    run({});
    chk("normal nStartHBL", glueStartHBL_, 63);
    chk("normal nEndHBL",   glueEndHBL_, 263);
    chk("normal DE_start",  glueLines_[100].displayStartCycle, 56);
    chk("normal DE_end",    glueLines_[100].displayEndCycle, 376);
    chk("normal trick",     bordersTrick_ ? 1 : 0, 0);

    // 7. STOP_MIDDLE : hi-rés @ cyc 100 (entre DE, ≤160), ligne 100.
    run({ {100,100,1,0x02}, {100,500,1,0x00} });
    chk("stopmid DE_end", glueLines_[100].displayEndCycle, 164);     // HDE_Off_Hi
    chk("stopmid mask",   (glueLines_[100].borderMask & glue::STOP_MIDDLE) ? 1 : 0, 1);

    std::fprintf(stderr, "[glue-selftest] %d OK, %d FAIL\n", pass, fail);
    return fail == 0;
}

// Décode toute la trame d'un coup (repli / appel direct hors ordonnanceur).
void Shifter::renderFrame() {
    beginFrame();
    for (int y = 0; y < curAH_; ++y) renderLine(y);   // lignes ACTIVES (bordures déjà posées)
}

uint8_t Shifter::read8(uint32_t addr) {
    // Palette $FF8240-$FF825F : 16 mots, big-endian.
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        syncCpuBus();          // wait state bus 4 cycles (lecture registre couleur)
        const int i = (addr - 0xFF8240) / 2;
        return (addr & 1) ? static_cast<uint8_t>(palette[i])
                          : static_cast<uint8_t>(palette[i] >> 8);
    }
    // Compteur d'adresse vidéo (lecture seule) : position courante du balayage.
    // $FF8205 = bits 16-23, $FF8207 = 8-15, $FF8209 = 0-7 (cf. Hatari
    // Video_ScreenCounter_ReadByte). Certains diagnostics (Test Kit) attendent
    // que ce compteur reflète la base vidéo + l'avance du faisceau.
    // Adresse de base vidéo ($FF8201/03, + octet bas STE $FF820D) : RELISIBLE —
    // le ST renvoie la dernière valeur écrite (Hatari : IoMem_ReadWithoutInterception).
    // Indispensable : les diagnostics RÉCUPÈRENT la base écran en relisant ces
    // registres pour calculer leur framebuffer (sans ça → base 0 → ils dessinent
    // sur la table des vecteurs et plantent).
    if (addr == 0xFF8201) return static_cast<uint8_t>(videoBase >> 16);
    if (addr == 0xFF8203) return static_cast<uint8_t>(videoBase >> 8);
    // Octet bas de la base vidéo $FF820D : STE seulement. Sur ST/MegaST le registre
    // n'existe pas : zone VOID → 0xFF (ioMemTabST.c:53, IoMem_VoidRead — l'ancien 0
    // suivait Video_BaseLow_ReadByte, qui n'est PAS mappé sur la table ST).
    if (addr == 0xFF820D) return machineIsSte(bus_.machine) ? static_cast<uint8_t>(videoBase) : 0xFF;
    // Une écriture du compteur pendant le DE est en attente (vcDelayedOffset_) : la
    // relecture doit déjà la refléter (port Video_ScreenCounter_ReadByte qui ajoute
    // VideoCounterDelayedOffset & ~1 à l'adresse calculée).
    if (addr == 0xFF8205 || addr == 0xFF8207 || addr == 0xFF8209) {
        // Wait-state de la lecture du compteur vidéo $FF8205/07/09. La VALEUR est
        // échantillonnée au cycle d'ACCÈS (avant tout wait, façon Hatari
        // Cycles_GetCounterOnReadAccess), PUIS le CPU pourrait être retardé.
        // ⚠ DÉFAUT = 0 (2026-06-18). Mesuré à l'oracle Hatari sur EL EN JEU (poll fullscreen
        // $ee78 lecture simple + $3700 double lecture) AVEC `NEOST_RAM_SLOT` défaut-ON :
        //   $ee78 = 20 cyc/itér (NeoST VC_WAIT=0) = 20 (Hatari) ; VC_WAIT=2 → 24 (+4 FAUX).
        //   $3700 = 36 (NeoST VC_WAIT=0) = 36 (Hatari) ; VC_WAIT=2 → 40 (+4 FAUX).
        // RAM_SLOT (alignement créneau bus) fournit DÉJÀ le +2 que ce wait ajoutait jadis
        // → VC_WAIT=2 DOUBLE-COMPTE (+4) et fait DÉRIVER le poll → SCRAMBLE EL en jeu.
        // VC_WAIT=0 : EL en jeu propre (garbage→paysage net), étalons BYTE-IDENTIQUES
        // (spec512/overscan_top/scroll/glue 19-0). L'ancien commentaire « +2 requis sinon
        // LX/EL noir » datait d'AVANT RAM_SLOT défaut-ON → périmé. Override : NEOST_VC_WAIT.
        const uint32_t vc = videoCounter() + (vcDelayedOffset_ & ~1);
        static const int vcWait = [] { const char* s = std::getenv("NEOST_VC_WAIT"); return s ? std::atoi(s) : 0; }();
        if (vcWait && bus_.cpu) bus_.cpu->addBusWaitCycles(vcWait);
        // DEBUG (oracle Hatari `--trace video_addr`) : trace chaque lecture du compteur
        // vidéo avec assez d'état pour diff'er au cycle. Gated NEOST_VC_TRACE.
        static const char* vctr = std::getenv("NEOST_VC_TRACE");
        if (vctr) {
            int64_t fc = beamClock_ ? beamClock_() : 0;
            fc += kVideoCounterReadOffsetCyc;
            const Geometry g = geometry();
            const int ln = g.cyclesPerLine ? static_cast<int>(fc / g.cyclesPerLine) : 0;
            const int X  = g.cyclesPerLine ? static_cast<int>(fc % g.cyclesPerLine) : 0;
            const uint32_t pc = bus_.cpu ? bus_.cpu->pc() : 0;
            const int into = bus_.cpu ? static_cast<int>(bus_.cpu->cyclesIntoInstr()) : -1;
            std::fprintf(stderr,
                "VC reg=%05x base=%06x addr=%06x fc=%lld line=%d X=%d start=%d cpl=%d "
                "liveStart=%d sync=%zu pc=%06x into=%d\n",
                addr, videoBase & 0xFFFFFFu, vc, static_cast<long long>(fc), ln, X,
                g.lineStartCycle, g.cyclesPerLine, liveStartHBL_,
                syncWrites_.size(), pc, into);
        }
        if (addr == 0xFF8205) return static_cast<uint8_t>(vc >> 16);
        if (addr == 0xFF8207) return static_cast<uint8_t>(vc >> 8);
        return static_cast<uint8_t>(vc);
    }
    // Synchro $FF820A : bits inutilisés 2-7 forcés à 1 (ST et STE), cf. Hatari
    // Video_Sync_ReadByte (IoMem[0xff820a] |= 0xfc). On NE masque PAS le champ
    // stocké `sync` : videoCounter() s'en sert toujours via `sync & 2`.
    if (addr == 0xFF820A) return static_cast<uint8_t>((sync & 0x03) | 0xFC);
    // Largeur de ligne STE $FF820F : sur ST/MegaST le registre n'existe pas —
    // zone void → 0xFF (absent de ioMemTable_ST ; Video_LineWidth_ReadByte n'y
    // est mappé que sur STE).
    if (addr == 0xFF820F) return machineIsSte(bus_.machine) ? lineWidth : 0xFF;
    if (addr == 0xFF8260) { syncCpuBus(); return static_cast<uint8_t>(mode); }
    // Scroll fin (STE seulement — sur ST/MegaST $FF8262-7F est une zone void → 0xFF,
    // ioMemTabST.c:72). $FF8264 : Hatari n'intercepte PAS la lecture (video.c:5813,
    // Video_HorScroll_Read_8264 = wait state seul) → on renvoie la dernière valeur
    // BRUTE écrite, comme IoMem. $FF8265 : valeur de scroll courante.
    if (addr == 0xFF8264 && machineIsSte(bus_.machine)) { syncCpuBus(); return hwScrollReg8264_; }
    if (addr == 0xFF8265) {
        if (!machineIsSte(bus_.machine)) return 0xFF;
        syncCpuBus();
        return hwScrollCount;
    }
    // Tout autre registre routé mais non géré : zones « void » du shifter.
    // Port fidèle Hatari (ioMem.c IoMem_VoidRead/IoMem_VoidRead_00) :
    //  - STE/MegaSTE : $FF820B, $FF8262-63 et $FF8266-7F lisent 0x00
    //    (ioMemTabSTE.c IoMem_VoidRead_00) ; le reste (dont $FF820C/$FF820E)
    //    lit 0xFF (IoMem_VoidRead).
    //  - ST/MegaST : TOUTES les zones void lisent 0xFF (ioMemTabST.c).
    if (machineIsSte(bus_.machine) &&
        (addr == 0xFF820B || addr == 0xFF8262 || addr == 0xFF8263 ||
         (addr >= 0xFF8266 && addr <= 0xFF827F)))
        return 0x00;
    return 0xFF;
}

// Reconstruit l'adresse vidéo courante — port fidèle de Hatari Video_CalculateAddress :
//   addr = videoBase + ligne*bpl + NbBytes, avec NbBytes = ((X - LineStartCycle) >> 1) & ~1
// où X = cycles DANS la ligne et le shifter lit 2 cycles/octet entre LineStartCycle
// (56 en 50 Hz, 52 en 60 Hz ; 0 en haute rés) et LineEndCycle (376). Après la dernière
// ligne affichée, le compteur reste figé jusqu'au rechargement VBL. (L'ancienne version
// supposait 1 octet/cycle depuis le cycle 216 — faux en milieu de ligne, d'où l'échec
// du test « T0 » des diagnostics qui relisent $FF8205/07/09 au cycle près.)
uint32_t Shifter::videoCounter() const {
    if (!beamClock_) return videoBase & 0xFFFFFF;       // pas d'horloge → base brute
    int64_t fc = beamClock_();                          // cycles dans la trame
    fc += kVideoCounterReadOffsetCyc;                   // datation lecture fidèle Hatari (fin d'accès − 8)
    // (NEOST_VC_OFF est déjà intégré à kVideoCounterReadOffsetCyc — ne pas l'ajouter
    // une 2ᵉ fois ici ; l'ancien double-ajout faussait les balayages de calibration.)
    // Géométrie VERROUILLÉE de la trame (cycles/ligne et début DE dépendent de la
    // fréquence 50/60/71 Hz : avant, 512 et 56/52 étaient figés → compteur faux en
    // 60 Hz). frameSync_ est posé par beginFrame, comme frameMode_.
    const Geometry g = geometry();
    const int  kCyclesPerLine = g.cyclesPerLine;
    const bool hi   = (frameMode_ == Mode::High);
    const int  bpl  = hi ? 80 : 160;                    // octets/ligne affichée
    const int  disp = g.displayLines;                   // lignes affichées
    const int  lineStart = g.lineStartCycle;            // début Display-Enable (50/60/71 Hz)
    // Stride réel d'une ligne = octets affichés + line-offset STE ($FF820F, en mots)
    // + 1 mot PAR PLAN si scroll fin avec prefetch (cf. scrollCounterAdvance).
    // lineWidth=0 et scroll=0 sur ST/STF → bpl.
    const int  stride = bpl + static_cast<int>(lineWidth) * 2 + scrollCounterAdvance();
    // Compteur MATÉRIALISÉ (≙ pVideoRaster) : vcLineBase_ = début de la ligne active
    // vcLineY_ (les lignes déjà rendues ont déjà accumulé leur stride réel — lineWidth
    // variable, écritures du compteur, scroll). L'affichage couvre les lignes
    // [dispStart, dispStart+disp) (VDE_On..VDE_Off) ; avant (bordure HAUTE) le
    // compteur reste à la base latchée ; pendant, il vaut base de ligne + offset
    // intra-ligne ; après (bordure BASSE), il reste figé sur l'écran entièrement lu.
    // VDE_On/Off LIVE : sur une trame à écritures freq/res, on consulte la machine
    // Glue LIVE (glueStartHBL_/glueEndHBL_, mêmes règles que Hatari nStartHBL/nEndHBL,
    // re-fermeture comprise) — l'ancien modèle « sticky » (liveStartHBL_, retrait
    // verrouillé à la 1ʳᵉ bascule) MENTAIT aux calibrations qui testent une impulsion
    // puis vérifient si elle a réellement ouvert le haut (Enchanted Land en jeu :
    // le moteur croyait sa visée bonne alors que le GLUE la refusait). Un écran sans
    // écriture freq/res garde le chemin historique (liveStartHBL_ = 63, zéro régression).
    int line = static_cast<int>(fc / kCyclesPerLine);
    int X    = static_cast<int>(fc % kCyclesPerLine);
    // NEOST_LINELEN : la LECTURE du compteur se mappe sur la GRILLE RÉELLE des
    // débuts de ligne (glueLineStart_), pas sur la grille fixe 512 — port du
    // Video_ConvertPosition sur nCyclesPerLine réel qu'utilise
    // Video_CalculateAddress (video.c). Décisif pour The Cuddly Demos (menu
    // robot) : le synchroniseur de la démo (pc=f264) émet une paire 60/50 Hz
    // PAR LIGNE → ces lignes font 508 cycles au comparateur HBL → chez Hatari le
    // compteur de chaque ligne suivante démarre 4 cyc plus tôt (+2 octets lus
    // par paire émise). En grille fixe, NeoST lisait 4-6 octets de MOINS que
    // Hatari au même instant → la sortie du poll (octet bas > $40, comparaison
    // SIGNÉE) glissait de la ligne 34 à la ligne 36 ~1 trame sur 10 → le
    // clignotement vertical bistable (fenêtre 34..310 ↔ 63..263). Mesuré à
    // l'oracle (traces video_addr + cpu_disasm, 2026-07-03) : chemin CPU et
    // ancre VBL identiques, seule la fonction valeur(t) différait.
    static const bool lineLenRead = std::getenv("NEOST_LINELEN") != nullptr;
    if (lineLenRead && frameMode_ != Mode::High && !syncWrites_.empty()
        && static_cast<std::size_t>(line) + 2 < glueLineStart_.size()) {
        const_cast<Shifter*>(this)->liveGlueCatchUp(line + 1);
        int wl = liveGlueLine_ < line + 1 ? liveGlueLine_ : line + 1;
        if (wl >= 0) {
            while (wl > 0 && fc < glueLineStart_[wl]) --wl;
            while (wl < liveGlueLine_ && static_cast<std::size_t>(wl) + 1 < glueLineStart_.size()
                   && fc >= glueLineStart_[wl + 1]) ++wl;
            line = wl;
            X = static_cast<int>(fc - glueLineStart_[wl]);
        }
    }
    // Compteur REDÉMARRÉ en fin de trame (port Video_RestartVideoCounter, ligne
    // 310/260 cycle 56) : les lectures au-delà renvoient la base rechargée, FIGÉE
    // jusqu'au DE de la trame suivante (chez Hatari, pVideoRaster vaut alors
    // &STRam[VideoBase] et plus rien ne l'avance avant la prochaine ligne affichée).
    if (vcRestartBase_ >= 0 && line >= vcRestartLine_)
        return static_cast<uint32_t>(vcRestartBase_) & 0xFFFFFF;
    int dispStart = liveStartHBL_;
    int disp2     = disp;
    if (frameMode_ != Mode::High && !syncWrites_.empty()
        && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
        const_cast<Shifter*>(this)->liveGlueCatchUp(line);
        dispStart = glueStartHBL_;
        // Bordure basse retirée → plus long ; + BlankLines (lignes blanches no-sync
        // insérées) comme le `nEndHBL + BlankLines` de Video_CalculateAddress
        // (video.c:1568) — ces lignes sont NO_DE (glueLineBytes = 0), elles ne font
        // pas avancer le compteur mais restent dans la fenêtre active.
        disp2     = glueEndHBL_ + glueBlankLines_ - glueStartHBL_;
    }
    const int  la   = line - dispStart;                 // index de ligne active du faisceau
    uint32_t addr = vcLineBase_;
    if (la >= 0) {
        // Ligne au-delà de celle du compteur matérialisé (rendu pas encore passé) :
        // extrapole au stride courant. Bordure basse : figé à l'écran entièrement lu.
        const int laEff = la < disp2 ? la : disp2;
        if (laEff > vcLineY_) {
            if (!syncWrites_.empty() && frameMode_ != Mode::High) {
                // Trame à tricks : somme des octets RÉELS par ligne (machine Glue
                // live) — une ligne élargie (left/right off) ou raccourcie (-2)
                // décale toutes les suivantes (accumulation inter-lignes, port de
                // Video_CalculateAddress qui parcourt ShifterLines[]).
                const_cast<Shifter*>(this)->liveGlueCatchUp(line);
                const int extra = static_cast<int>(lineWidth) * 2 + scrollCounterAdvance();
                for (int y = vcLineY_; y < laEff; ++y)
                    addr += static_cast<uint32_t>(glueLineBytes(dispStart + y) + extra);
            } else {
                addr += static_cast<uint32_t>(laEff - vcLineY_) * static_cast<uint32_t>(stride);
            }
        }
        // Offset intra-ligne UNIQUEMENT si la ligne courante n'a pas déjà été rendue
        // (la < vcLineY_ = bordure droite : le stride de la ligne est déjà accumulé).
        if (la < disp2 && laEff >= vcLineY_) {
            // Port FIDÈLE de Video_CalculateAddress (video.c:1508-1565, 2026-07-02) :
            // pour l'intra-ligne, Hatari NE lit PAS DisplayStart/EndCycle de la Glue —
            // il RECONSTRUIT le départ (ds) et la TAILLE (CurSize) depuis le
            // borderMask :  LEFT_OFF → ds = LINE_START_CYCLE_71 = **0** (pas le
            // HDE_On_Hi=4 de la Glue !), LEFT_PLUS_2 → 52 ; fin = ds + CurSize×2
            // avec CurSize par la table de bordures (fullscreen = 230). L'ancienne
            // version NeoST (ds = displayStartCycle, taille = (End−Start)/2 = 229)
            // était fausse de 2-4 octets sur les lignes à tricks → le stabilisateur
            // beam-sync d'Enchanted Land en jeu (poll $8209 sur lignes fullscreen)
            // se calait −16 cyc vs Hatari (freq 360/368 au lieu de 376/384) → ses
            // impulsions rataient la fenêtre bordure-droite (372,376] → scroll qui
            // saute. Trame SANS écriture freq/res → chemin historique inchangé.
            int ds = lineStart, lineBytes = bpl;
            bool leftOff = false;
            uint32_t bm = 0;
            bool haveGlue = false;
            if (frameMode_ != Mode::High && !syncWrites_.empty()
                && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
                const_cast<Shifter*>(this)->liveGlueCatchUp(line);
                const GlueLine& L = glueLines_[static_cast<std::size_t>(line)];
                if (L.displayStartCycle >= 0) {
                    haveGlue = true;
                    bm = L.borderMask;
                    leftOff = (bm & glue::LEFT_OFF) != 0;
                    int curSize = 160;                                // BORDERBYTES_NORMAL
                    if (leftOff)                       curSize += 26; // BORDERBYTES_LEFT
                    else if (bm & glue::LEFT_PLUS_2)   curSize += 2;
                    else if (hwScrollCount && hwScrollPrefetch) curSize += 8;
                    if (bm & glue::STOP_MIDDLE)        curSize -= 106;
                    else if (bm & glue::RIGHT_MINUS_2) curSize -= 2;
                    else if (bm & glue::RIGHT_OFF)     curSize += 44; // BORDERBYTES_RIGHT
                    if (bm & glue::RIGHT_OFF_FULL)     curSize += 22; // BORDERBYTES_RIGHT_FULL
                    if (bm & glue::LEFT_PLUS_2)        ds = 52;       // LINE_START_CYCLE_60
                    else if (leftOff)                  ds = 0;        // LINE_START_CYCLE_71
                    else if (hwScrollCount && hwScrollPrefetch) ds = lineStart - 16;
                    lineBytes = curSize;
                }
            }
            // Scroll fin STE avec PREFETCH hors trame à tricks : le MMU démarre 16
            // cycles avant le HDE_On et lit 8 octets de plus (l'ANCRE que mesure la
            // calibration sync-scroll d'EL — sans ce −16 ses impulsions visaient
            // ~16 cyc trop tôt et aucun comparateur n'était enjambé).
            if (!haveGlue && hwScrollCount && hwScrollPrefetch) { ds -= 16; lineBytes += 8; }
            int Xc = X;
            if (Xc < ds) Xc = ds;
            else if (Xc > ds + lineBytes * 2) {
                Xc = ds + lineBytes * 2;               // affichage désactivé (bordure droite)
                addr += static_cast<uint32_t>(lineWidth) * 2u;   // STE : mots sautés dès DE off
            }
            int nb = (Xc - ds) >> 1;                    // 2 cycles par octet
            nb &= ~1;                                   // le shifter lit par MOTS
            // Bordure gauche ouverte : 2 octets de moins que la valeur théorique
            // (26 octets non multiples de 4 cycles) — SANS garde nb>0, comme Hatari.
            if (leftOff) nb -= 2;
            if (bm & glue::NO_DE) nb = 0;
            addr += static_cast<uint32_t>(nb);
        }
    }
    // RESTART du compteur en fin de trame (Video_RestartVideoCounter, HBL 310/260
    // cycle 56, ULM DSOTS) : PORTÉ (2026-07-02) — cf. le early-return vcRestartBase_
    // en tête. Indispensable aux moteurs double-buffer beam-syncés (Enchanted Land
    // en jeu) : la base du handler VBL est posée APRÈS la ligne 0 → le latch
    // begin-frame seul renvoyait l'ANCIEN buffer toute la trame et le stabilisateur
    // ($8209) tournait à vide. ⚠ L'ancienne crainte (« un poll compteur==base sort à
    // la ligne 310 et bascule 60 Hz à la frontière → géométrie de trame flip ») est
    // re-testée aux étalons : overscan_top et make_overscan_test restent verts.
    return addr & 0xFFFFFF;
}

// Écriture du compteur vidéo $FF8205/07/09 (STE/TT) — port fidèle de
// Video_ScreenCounter_WriteByte (video.c:5145-5250). On reconstruit la nouvelle
// adresse en remplaçant UN octet de la valeur courante (corrigée d'une éventuelle
// modification déjà différée), puis :
//   • affichage pas commencé sur la ligne (cycle ≤ MMUStart = HDE_On − 16 si scroll),
//     ligne déjà rendue (bordure droite — notre rendu à DE_end est déjà passé, ce qui
//     équivaut au pVideoRasterDelayed d'Hatari appliqué en fin de ligne), ou faisceau
//     hors zone affichée → application IMMÉDIATE au compteur matérialisé ;
//   • écriture PENDANT le DE → on mémorise l'ÉCART (vcDelayedOffset_), appliqué à la
//     fin de la ligne (endVideoLine) — sur un vrai STE cela produit des artefacts,
//     l'adresse de fin de ligne est en revanche exacte. Étalons : Stardust Tunnel STE,
//     Braindamage End Part.
void Shifter::writeVideoCounterByte(uint32_t addr, uint8_t v) {
    // Octet haut limité comme la base/DMA : adresses vidéo ≤ $3FFFFF sur les machines
    // ≤ 4 Mo (port DMA_MaskAddressHigh — NeoST plafonne la ST-RAM à 4 Mo).
    if (addr == 0xFF8205) v &= 0x3F;
    const uint32_t cur = videoCounter();                       // adresse courante (brute)
    uint32_t an = (cur + static_cast<uint32_t>(vcDelayedOffset_)) & 0xFFFFFFu;
    if (addr == 0xFF8205)      an = (an & 0x00FFFFu) | (uint32_t(v) << 16);
    else if (addr == 0xFF8207) an = (an & 0xFF00FFu) | (uint32_t(v) << 8);
    else                       an = (an & 0xFFFF00u) | uint32_t(v);
    an &= ~1u;      // bit 0 forcé à 0 (compteur aligné mot — Hatari video.c:5179 addr_new &= ~1)

    int line = 0, cyc = 0;
    const Geometry g  = geometry();
    const bool havePos = beamPos(line, cyc);
    const int  la      = havePos ? line - liveStartHBL_ : -1;
    const bool active  = havePos && la >= 0 && la < g.displayLines;
    // Le MMU commence à lire 16 cycles AVANT le HDE_On quand le scroll fin est armé
    // AVEC PREFETCH ($FF8265 ; rien via $FF8264) — port Video_GetMMUStartCycle.
    const int mmuStart = g.lineStartCycle - ((hwScrollCount && hwScrollPrefetch) ? 16 : 0);
    if (!havePos || !active || cyc <= mmuStart || la < vcLineY_) {
        vcLineBase_ = an;                                      // application immédiate
        vcDelayedOffset_ = 0;
    } else {
        vcDelayedOffset_ = static_cast<int>(an) - static_cast<int>(cur);  // pendant le DE → fin de ligne
    }
}

void Shifter::write8(uint32_t addr, uint8_t v) {
    // Adresse de base vidéo : octets haut ($FF8201) et milieu ($FF8203). Le bit
    // bas est fixé à 0 (le ST aligne le framebuffer sur 256 octets).
    const bool ste = machineIsSte(bus_.machine);
    switch (addr) {
        case 0xFF8201:
            videoBase = (videoBase & 0x00FF00) | (uint32_t(v) << 16);
            // STE/TT : écrire l'octet haut/milieu remet à 0 l'octet bas $FF820D
            // (cf. Hatari Video_ScreenBase_WriteByte).
            if (ste) videoBase &= 0xFFFF00;
            return;
        case 0xFF8203:
            videoBase = (videoBase & 0xFF0000) | (uint32_t(v) << 8);
            if (ste) videoBase &= 0xFFFF00;
            // DIAG beam-sync (gated NEOST_BASE_TRACE) : ligne/cycle/valeur des écritures de
            // base vidéo. Une base jittery trame à trame (joueur immobile) = la boucle de
            // calibration fullscreen du jeu ne converge pas (résidu phase CPU↔faisceau),
            // PAS un trick glue res-switch — cf. EL en jeu (scramble).
            static const bool baseTrace_ = std::getenv("NEOST_BASE_TRACE") != nullptr;
            if (baseTrace_ && liveFrameClock_) {
                const int64_t fc = liveFrameClock_(); const int cpl = geometry().cyclesPerLine;
                std::fprintf(stderr, "[base] $8203=%02x line=%lld cyc=%lld\n", v,
                             (long long)(fc>=0?fc/cpl:-1), (long long)(fc>=0?fc%cpl:-1));
            }
            return;
        case 0xFF820A: syncCpuBus(); recordSyncWrite(false, v); sync = v; return;   // synchro 50/60 Hz (+ bordures + wait-state bus, FIX2 : aligne l'accès sur 4 cyc comme $FF8260/palette, port wait_cpu_cycle_write)
        // Compteur vidéo $FF8205/07/09 : INSCRIPTIBLE sur STE/TT seulement (port
        // Video_ScreenCounter_WriteByte) — immédiat hors affichage, différé sinon.
        case 0xFF8205: case 0xFF8207: case 0xFF8209:
            if (ste) writeVideoCounterByte(addr, v);
            return;
        // Octet bas de la base vidéo $FF820D (STE) : bit0 ignoré (aligné pair).
        case 0xFF820D:
            if (ste) videoBase = (videoBase & 0xFFFF00) | (uint32_t(v) & ~1u);
            return;
        // Largeur de ligne STE $FF820F — port Video_LineWidth_WriteByte : applicable
        // IMMÉDIATEMENT si le Display-Enable de la ligne courante n'est pas terminé
        // (ou faisceau hors zone affichée) ; sinon DIFFÉRÉ à la fin de la ligne
        // (NewLineWidth, cf. endVideoLine). Étalon : Pacemaker (bump mapping).
        case 0xFF820F:
            if (ste) {
                int line = 0, cyc = 0;
                const Geometry g = geometry();
                const int la = beamPos(line, cyc) ? line - liveStartHBL_ : -1;
                const bool active = la >= 0 && la < g.displayLines;
                if (!active || cyc <= g.lineEndCycle) { lineWidth = v; newLineWidth_ = -1; }
                else                                  { newLineWidth_ = v; }
            }
            return;
        case 0xFF8260: syncCpuBus(); recordSyncWrite(true, v); mode = static_cast<Mode>(v & 0x3);
            // V2 res-switch : une écriture hi-res ($8260 bit1) peut raccourcir la ligne
            // courante (la Machine vérifie qu'elle est précoce). Cf. setHblShorten.
            if ((v & 0x3) == 2 && hblShorten_) hblShorten_();
            return;  // résolution (+ bordures + wait state bus)
        // Scroll fin horizontal STE — port Video_HorScroll_Write : $FF8264 sans
        // prefetch, $FF8265 avec. Applicable IMMÉDIATEMENT si l'affichage de la ligne
        // courante n'a pas commencé (cycle ≤ HDE_On, ou faisceau hors zone affichée) ;
        // sinon la nouvelle valeur est DIFFÉRÉE à la fin de la ligne (NewHWScrollCount,
        // cf. endVideoLine). Étalons : Mindrewind, Digiworld 2, cool_ste.
        case 0xFF8264: case 0xFF8265:
            syncCpuBus();
            if (ste) {
                const uint8_t sc       = v & 0x0F;
                const bool    prefetch = (addr == 0xFF8265);
                // $FF8264 : mémorise l'octet BRUT pour la relecture (Hatari laisse
                // IoMem tel quel — la lecture n'est pas interceptée, video.c:5813).
                if (!prefetch) hwScrollReg8264_ = v;
                int line = 0, cyc = 0;
                const Geometry g = geometry();
                const int la = beamPos(line, cyc) ? line - liveStartHBL_ : -1;
                const bool active = la >= 0 && la < g.displayLines;
                if (!active || cyc <= g.lineStartCycle) {
                    hwScrollCount = sc; hwScrollPrefetch = prefetch; newHwScrollCount_ = -1;
                } else {
                    newHwScrollCount_ = sc; newHwScrollPrefetch_ = prefetch;
                }
            }
            return;
        default: break;
    }
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        syncCpuBus();          // wait state bus 4 cycles AVANT de dater l'écriture
        const int i = (addr - 0xFF8240) / 2;
        uint16_t col;
        if (bus_.ioAccessWidth() == 1) {
            // Quirk matériel (port Video_ColorReg_WriteWord) : sur une écriture
            // OCTET, le 68000 pose l'octet sur les DEUX moitiés du bus de données
            // et le Shifter latche le MOT entier → l'octet est dupliqué, que
            // l'adresse soit paire ou impaire (move.b #$07,$FF8240 → couleur $707).
            col = uint16_t((uint16_t(v) << 8) | v);
        } else {
            // Écriture mot/long : le bus la découpe en deux write8 (big-endian).
            col = (addr & 1) ? uint16_t((palette[i] & 0xFF00) | v)
                             : uint16_t((palette[i] & 0x00FF) | (uint16_t(v) << 8));
        }
        // La couleur est STOCKÉE masquée — palette ST 512 couleurs ($777) ou STE
        // 4096 ($FFF) : des jeux écrivent $FFFF et RELISENT pour détecter le STE.
        palette[i] = col & (ste ? 0x0FFF : 0x0777);
        recordColorWrite(i);   // spec512 : date l'écriture au cycle ALIGNÉ (palette intra-ligne)
    }
    // Tout autre registre nouvellement routé mais non géré ($FF8266-$FF827F) :
    // écriture sans effet (no-op), comme les zones « void » du shifter.
}
