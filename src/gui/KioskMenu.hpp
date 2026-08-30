// =============================================================================
//  KioskMenu.hpp — le menu plein écran de la borne (START manette ou F9).
//
//  Modèle « comme une vraie machine » : insérer une disquette n'est PAS un reboot,
//  REDÉMARRER est un bouton explicite. Le menu ne monte ni ne démonte rien lui-même
//  — il pose des requêtes dans App, la boucle les consomme (cf. App.hpp).
//
//  La table de touches est exportée parce que la NAVIGATION (flèches/manette) vit
//  dans la boucle, pas dans le dessin : c'est elle qui déplace la sélection d'une
//  ligne à l'autre du clavier virtuel.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <string>

struct App;

struct KioskKey { const char* label; uint8_t scancode; int kind; };
extern const KioskKey KIOSK_KEYS[];
extern const int KIOSK_KEY_ROWS[][2];
extern const int KIOSK_KEY_ROWN;

// Scrutations : la liste des jeux, le navigateur de dossiers, ses raccourcis, et
// la purge des dossiers ROM disparus (true = la liste a changé → à persister).
void kioskScanDisks(App& A, const std::string& disksDir, const std::string& mounted);
void kioskScanBrowse(App& A, const std::string& dir);
bool kioskPruneRomDirs(App& A);
void kioskComputeShortcuts(App& A);

void drawKioskDiskMenu(App& A, const std::string& disksDir, const std::string& mounted);
