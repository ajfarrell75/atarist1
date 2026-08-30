// =============================================================================
//  JoyMap.hpp — affectation des manettes hôte aux ports ST, par GUID.
//
//  Le jid GLFW change au rebranchement ; le GUID, non. La table est éditée dans
//  le menu borne « Joysticks » et persistée dans neost.cfg (joymap=).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <GLFW/glfw3.h>
#include <cstdint>
#include <string>

struct App;

// GUID d'une manette présente ("" sinon) — clé de persistance de son rôle.
std::string joyGuid(int jid);
// Rôles EFFECTIFS par jid pour cette trame (consommés par stjoy::compose/composeAux
// et le menu borne) : table GUID→rôle appliquée aux manettes présentes.
void joyResolveRoles(const App& A, int8_t roles[GLFW_JOYSTICK_LAST + 1]);
// joymap= "guid:rôle,guid:rôle" avec rôle ∈ {0, 1, x} — cf. Config::joymap.
void joymapParse(App& A, const std::string& s);
std::string joymapSerialize(const App& A);
