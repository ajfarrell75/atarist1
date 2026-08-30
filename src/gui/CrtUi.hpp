// =============================================================================
//  CrtUi.hpp — presets et panneau de réglage des effets CRT.
//
//  La passe elle-même vit dans gui/CrtEffectStack ; ici, seulement de quoi la
//  nommer (presets) et la régler (fenêtre flottante + fragment de page).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <string>

#include "gui/CrtEffectStack.h"

struct App;

// Presets CRT nommés (kiosk / --crt-preset / neost.cfg). Renseigne `p` et `on`.
// Renvoie false si le nom est inconnu (params laissés intacts).
bool applyCrtPreset(const std::string& name, neost::CrtParams& p, bool& on);
// Fragment (pas de Begin/End) : les curseurs. `changed` passe à true si l'on a touché.
void drawCrtControls(App& A, bool& changed);
// Fenêtre flottante autour du fragment ci-dessus.
void drawCrtSettings(App& A, bool& changed);
