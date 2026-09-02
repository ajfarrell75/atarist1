// =============================================================================
//  MouseScale.hpp — Mise à l'échelle du delta de la souris HÔTE vers l'IKBD.
//
//  Pourquoi ce réglage existe : la souris du ST est mécanique, ~200 points par
//  pouce, et le TOS n'offre aucune accélération réglable digne de ce nom. Une
//  souris moderne à 1600, 3200 ou 8000 dpi envoie jusqu'à 40 fois plus de pas
//  pour le même geste — le pointeur GEM traverse alors l'écran au moindre
//  mouvement. Le curseur « Emulated mouse speed » (page Input) divise le delta
//  hôte AVANT qu'il n'atteigne l'IKBD.
//
//  Pourquoi une FONCTION, et pas trois lignes dans la boucle : le point délicat
//  n'est pas la multiplication, c'est le RESTE. Sous 1,0 la plupart des deltas
//  mis à l'échelle valent moins d'un pas entier ; les tronquer sans les reporter
//  ferait perdre TOUS les petits mouvements — la souris ST ne bougerait plus du
//  tout sur un déplacement lent, et le réglage serait inutilisable au moment
//  précis où il sert. Isolée ici, la règle est exercée par tests/selftest_logic.cpp
//  depuis n'importe quelle plateforme, sans fenêtre ni souris.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cmath>

namespace neost::mousescale {

// Plage du curseur, et bornes de relecture du neost.cfg (cf. AppConfig.cpp).
// Le plancher n'est PAS 0 : à 0 la souris serait figée sans que rien ne le dise.
inline constexpr float kMin = 0.05f, kMax = 4.0f, kDefault = 1.0f;

// Ajoute `raw` (delta hôte, en pixels écran) mis à l'échelle par `speed` au
// reste `acc`, puis en retire le pas ENTIER rendu. `acc` conserve la fraction
// pour l'appel suivant. Rend 0 tant que le cumul n'atteint pas un pas entier.
//
// `speed` non fini ou hors plage est ramené dans les bornes : un `mousespeed=nan`
// venu d'un fichier corrompu rendrait `acc` NaN, et `int(NaN)` est un comportement
// INDÉFINI — la souris ST resterait figée pour le reste de la session.
inline int step(double raw, float speed, double& acc) {
    if (!std::isfinite(speed)) speed = kDefault;
    if (speed < kMin) speed = kMin;
    if (speed > kMax) speed = kMax;
    if (!std::isfinite(acc)) acc = 0.0;          // idem, par prudence sur l'état
    acc += raw * static_cast<double>(speed);
    const int whole = static_cast<int>(acc);      // troncature VERS ZÉRO (garde le signe)
    acc -= static_cast<double>(whole);
    return whole;
}

}  // namespace neost::mousescale
