// =============================================================================
//  Framing.cpp — cf. Framing.hpp. Déplacé depuis main.cpp (stContentRegion)
//  SANS changement de comportement : mêmes latches, mêmes seuils.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Framing.hpp"

#include <algorithm>

#include "core/Shifter.hpp"

namespace neost {

void stContentRegion(const Shifter& sh, int& cTop, int& cH, int& cW) {
    static int overscanLatch     = 0;   // bordure ouverte (haut ou bas)
    static int fullOverscanLatch = 0;   // bordure BASSE retirée (démos full-overscan)
    cW = std::max(1, sh.activeWidth());   // défaut : la zone active
    // snapBordersOpen/snapLiveTop/snapLiveHeight : snapshot capturé à finishFrame(),
    // stable au rendu (les champs live glueStartHBL_/glueEndHBL_ sont remis à zéro
    // par beginFrame_() du cycle suivant AVANT que le rendu ne s'exécute).
    if (sh.snapBordersOpen()) overscanLatch = 30;   // ~0,6 s de maintien
    else if (overscanLatch > 0) --overscanLatch;
    if (overscanLatch == 0) {
        fullOverscanLatch = 0;              // plus de trick : reset du second latch
        cTop = sh.activeTop();
        cH   = sh.activeHeight();
        return;
    }
    // Second latch : détecte une bordure BASSE retirée (LX, Cuddly…) et latche ce
    // constat pour éviter les basculements frame-à-frame.
    if (sh.snapLiveHeight() + sh.snapLiveTop() > sh.activeHeight() + sh.activeTop())
        fullOverscanLatch = 30;
    else if (fullOverscanLatch > 0)
        --fullOverscanLatch;

    // Bordure ouverte : l'image occupe aussi les bordures latérales → tout le buffer
    // devient du contenu, y compris en largeur.
    cW = std::max(1, sh.width());
    if (fullOverscanLatch > 0) {
        // Bordure BASSE retirée (démos full-overscan : Cuddly, LX…) : buffer entier.
        cTop = 0;
        cH   = sh.height();
    } else {
        // Bordure HAUTE seule (ex. Enchanted Land en jeu) : même zoom que la zone
        // active, légèrement remontée pour montrer 2 lignes overscan en haut.
        cTop = std::max(0, sh.activeTop() - 2);
        cH   = sh.activeHeight();
    }
}

}  // namespace neost
