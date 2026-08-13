// =============================================================================
//  Framing.hpp — Région de CONTENU de la trame courante : le cœur du ZOOM
//  ADAPTATIF, extrait de main.cpp (stContentRegion) pour être partagé.
//
//  Trois consommateurs : le kiosk (viewport GL), la fenêtre « Atari ST Screen »
//  du bureau (UV d'image), et le plein écran du frontend WASM (UV + taille
//  intrinsèque du canvas). La règle est la même partout ; seule la façon de
//  l'appliquer diffère — c'est exactement la frontière modèle/rendu.
//
//  Deux cadrages francs, jamais au pixel (→ zéro saccade) :
//   · Défaut (99 % des jeux) : cadre FIXE sur la ZONE ACTIVE (rectangle net donné
//     par le matériel — activeTop/activeHeight), qui ne bouge JAMAIS. Un champ
//     d'étoiles, un fond noir : rien ne fait « respirer » le zoom.
//   · Overscan (démos, ouvertures de bordures — Enchanted Land, Lethal Xcess) :
//     quand la Glue signale une bordure retirée, on montre le BUFFER ENTIER.
//     Hystérésis (latch ~0,6 s) pour ne pas basculer sur un retrait d'une trame.
//
//  À appeler UNE fois par trame RENDUE : l'hystérésis se compte en trames.
//  Les latches sont des statiques de fonction — un seul jeu par processus, donc
//  un aller-retour bureau ⇄ kiosk ne réinitialise pas l'hystérésis en cours.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

class Shifter;

namespace neost {

// [cTop, cTop+cH) = lignes de contenu du buffer ST. cW = largeur (en px du
// buffer) qui doit RESTER visible : la zone active seule en régime normal (les
// bordures latérales sont du décor, rognables), le buffer entier dès qu'une
// bordure est ouverte (l'image déborde alors DANS les bordures). Les frontends
// s'en servent comme plancher : ils n'amputent jamais l'image elle-même.
void stContentRegion(const Shifter& sh, int& cTop, int& cH, int& cW);

}  // namespace neost
