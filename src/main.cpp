// =============================================================================
//  main.cpp — le point d'entrée, et rien d'autre.
//
//  NeoST, frontend fenêtré (GLFW3 + OpenGL + Dear ImGui). Le matériel et la
//  boucle d'horloge vivent dans Machine (cœur sans GUI) ; l'état du frontend vit
//  dans App (gui/App.hpp), son démarrage dans gui/AppInit.cpp, sa boucle dans
//  gui/AppLoop.cpp, ses fenêtres dans gui/*.cpp.
//
//  Modèle temporel (cf. Machine) : 68000 ~8 MHz, 512 cycles/ligne, 313 lignes
//  PAL ≈ 50 Hz. Le Timer C du MFP (200 Hz) débloque l'accueil EmuTOS.
//
//  Ce fichier a compté 5 100 lignes (chantier A9, 2026-08-30). Ce qu'il en reste
//  est la seule chose qu'un point d'entrée doive dire : dans quel ordre.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/App.hpp"

int main(int argc, char** argv) {
    App& A = app();
    // appInit renvoie un code de sortie (--help, --version, option inconnue, échec
    // d'ouverture de fenêtre) ou < 0 pour « continuer ». Sortir AVANT d'ouvrir quoi
    // que ce soit est ce qui rend --help et --version testables sans écran.
    if (const int rc = appInit(A, argc, argv); rc >= 0) return rc;
    appLoop(A);
    appShutdown(A);
    return 0;
}
