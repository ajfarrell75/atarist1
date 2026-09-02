// =============================================================================
//  Framing.cpp — cf. Framing.hpp. Déplacé depuis main.cpp (stContentRegion).
//
//  RÉVISÉ le 2026-09-02 (chantier « autozoom Enchanted Land »), cf. le bandeau
//  de Framing.hpp pour la règle et le CHANGELOG pour les mesures.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Framing.hpp"

#include <algorithm>

#include "core/Shifter.hpp"

namespace neost {

namespace {
// Part des lignes affichées qui doivent déborder latéralement pour qu'on élargisse
// le cadre aux bordures gauche/droite. MESURÉ, pas choisi au goût (2026-09-02) :
// Closure élargit 272-275 lignes sur 276 (99 %), Enchanted Land 4 sur 229 et
// l'étalon overscan_top 5 sur 229 (~2 %). Un quart sépare les deux régimes avec
// deux ordres de grandeur de marge de chaque côté.
constexpr int kSideNum = 1, kSideDen = 4;
}  // namespace

void stContentRegion(const Shifter& sh, int& cTop, int& cH, int& cW) {
    static int overscanLatch = 0;   // bordure ouverte (haut ou bas)
    static int openStreak = 0;      // trames CONSÉCUTIVES à bordure ouverte (cf. kAttack)
    static int latchTop = 0, latchBot = 0;   // étendue verticale RETENUE (cf. plus bas)
    static bool latchWide = false;           // largeur RETENUE : buffer entier ?

    // snapBordersOpen/snapLive* : snapshot capturé à finishFrame(), stable au rendu
    // (les champs live glueStartHBL_/glueEndHBL_ sont remis à zéro par beginFrame_()
    // du cycle suivant AVANT que le rendu ne s'exécute).
    // ⚠ IL FAUT QUE LA BORDURE RESTE OUVERTE POUR QUE LE CADRE CHANGE.
    // L'hystérésis d'origine ne protégeait QUE le retour : elle empêchait de rebasculer
    // trop vite, mais UNE SEULE trame à bordure ouverte suffisait à élargir le cadre —
    // et le verrou le maintenait alors élargi 30 trames, soit 0,6 s. Mesuré sur Super
    // Hang-On (rapport utilisateur du 2026-09-02, « des bandes pleine largeur qui
    // apparaissent de temps en temps, souvent noires ») : un transitoire d'UNE trame
    // (f=2, `live=58+197` contre une zone active `29+200`) faisait passer le cadre de
    // `top=29 h=200 w=320` à `top=29 h=226 w=416` pendant 29 trames — l'image saute,
    // se redimensionne, et les BORDURES NOIRES entrent dans le cadre. C'est la bande.
    // Une vraie démo overscan, elle, ouvre ses bordures à CHAQUE trame : exiger kAttack
    // trames consécutives ne lui coûte que 60 ms, imperceptibles, et immunise le cadre
    // contre les transitoires isolés.
    constexpr int kAttack = 3;
    if (sh.snapBordersOpen()) { if (openStreak < kAttack) ++openStreak; }
    else openStreak = 0;
    if (openStreak >= kAttack) overscanLatch = 30;   // ~0,6 s de maintien au relâchement
    else if (overscanLatch > 0) --overscanLatch;

    if (overscanLatch == 0) {                       // régime normal : zone active
        latchTop = latchBot = 0; latchWide = false;
        // (openStreak n'est PAS remis ici : il suit l'état de la Glue, pas le verrou.)
        cTop = sh.activeTop();
        cH   = sh.activeHeight();
        cW   = std::max(1, sh.activeWidth());
        return;
    }

    // --- VERTICAL : ce que la Glue AFFICHE réellement ---------------------------
    // On prend l'étendue LIVE (haut et bas), et non plus « zone active remontée de
    // 2 lignes ». Ce -2 était un ajustement cosmétique qui ROGNAIT du contenu :
    // sur Enchanted Land il coupait les 2 dernières lignes de l'image, et sur
    // l'étalon overscan_top les 29 lignes du haut, que ce programme dessine
    // vraiment (mesuré : sa boîte de contenu commence à y=0).
    const int liveTop = std::max(0, sh.snapLiveTop());
    const int liveBot = std::min(sh.height(), liveTop + std::max(1, sh.snapLiveHeight()));

    // --- LARGEUR : la moitié LATÉRALE du trick, en PROPORTION -------------------
    // `snapBordersOpen()` est vrai dès qu'une bordure — HAUTE, BASSE ou LATÉRALE —
    // bouge, et l'ancien code en déduisait « l'image occupe aussi les bordures
    // latérales → tout le buffer ». C'était faux pour tout titre qui n'ouvre que le
    // haut : Enchanted Land voyait son cadre élargi à 416 px pour une image qui n'en
    // occupe que 320, donc un zoom 1,3× trop petit et deux bandes noires. On tranche
    // désormais sur la PROPORTION de lignes réellement élargies (cf. kSideNum/Den).
    const int shown = std::max(1, liveBot - liveTop);
    const bool wide = sh.snapSideTrickLines() * kSideDen >= shown * kSideNum;

    // --- HYSTÉRÉSIS : le cadre ne RÉTRÉCIT jamais tant que le latch tient ---------
    // Même intention que les deux latches d'origine — « cadrages francs, jamais au
    // pixel, zéro saccade » — mais appliquée à l'étendue elle-même : on retient
    // l'UNION de ce qui a été affiché pendant la fenêtre de maintien. Une trame
    // isolée qui rouvre une bordure ne fait donc pas « respirer » le zoom, et une
    // démo qui ouvre progressivement ses bordures ne le fait pas sauter en arrière.
    if (latchBot == 0) { latchTop = liveTop; latchBot = liveBot; }   // 1ʳᵉ trame du latch
    else { latchTop = std::min(latchTop, liveTop); latchBot = std::max(latchBot, liveBot); }
    latchWide = latchWide || wide;

    cTop = latchTop;
    cH   = std::max(1, latchBot - latchTop);
    cW   = std::max(1, latchWide ? sh.width() : sh.activeWidth());
}

}  // namespace neost
