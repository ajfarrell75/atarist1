// =============================================================================
//  DockLayout.hpp — ancrage des fenêtres + taille de la fenêtre hôte.
//
//  Les fenêtres de debug deviennent des ONGLETS d'une disposition persistante au
//  lieu d'une pile qui se recouvre. Exige la branche `docking` de Dear ImGui
//  (IMGUI_HAS_DOCK) ; le code compile tel quel avec la branche master.
//
//  La taille de la fenêtre HÔTE (qui n'est pas une fenêtre ImGui) voyage dans le
//  même imgui.ini, via un gestionnaire de réglages personnalisé.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

struct App;

// À poser AVANT le premier NewFrame (qui charge imgui.ini et applique la taille relue).
void registerWindowSettings(App& A);
// Sème la disposition d'ancrage (une fois, ou sur demande de réinitialisation).
void applyDockLayout(App& A);
// Pose le dockspace sur la zone de travail du viewport. visible=false → nœud vide.
void renderDockSpace(App& A, bool visible);
