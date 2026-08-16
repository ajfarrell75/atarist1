// =============================================================================
//  UiCommon.cpp — cf. UiCommon.hpp.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/UiCommon.hpp"

#include "imgui.h"

// Bouton à ICÔNE SEULE (le texte est superflu quand le pictogramme est explicite) :
// l'infobulle au survol rappelle l'action. Renvoie true au clic.
bool IconButton(const char* icon, const char* tooltip) {
    const bool clicked = ImGui::Button(icon);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}
