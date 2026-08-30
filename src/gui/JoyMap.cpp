// =============================================================================
//  JoyMap.cpp — cf. JoyMap.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/JoyMap.hpp"

#include "gui/App.hpp"
#include "io/JoystickInput.hpp"

// GUID d'une manette présente ("" sinon) — clé de persistance de son rôle.
std::string joyGuid(int jid) {
    const char* g = glfwJoystickPresent(jid) ? glfwGetJoystickGUID(jid) : nullptr;
    return g ? g : "";
}
// Rôles EFFECTIFS par jid pour cette trame (consommés par stjoy::compose/composeAux
// et le menu kiosk) : table GUID→rôle appliquée aux manettes présentes.
void joyResolveRoles(const App& A, int8_t roles[GLFW_JOYSTICK_LAST + 1]) {
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        roles[jid] = stjoy::ROLE_AUTO;
        if (!glfwJoystickPresent(jid)) continue;
        const auto it = A.joyRoleByGuid.find(joyGuid(jid));
        if (it != A.joyRoleByGuid.end()) roles[jid] = it->second;
    }
}
// joymap= "guid:rôle,guid:rôle" avec rôle ∈ {0, 1, x} — cf. Config::joymap.
void joymapParse(App& A, const std::string& s) {
    A.joyRoleByGuid.clear();
    std::size_t p = 0;
    while (p < s.size()) {
        std::size_t e = s.find(',', p);
        if (e == std::string::npos) e = s.size();
        const std::string item = s.substr(p, e - p);
        const std::size_t c = item.rfind(':');
        if (c != std::string::npos && c > 0 && c + 1 < item.size()) {
            const char r = item[c + 1];
            if      (r == '0') A.joyRoleByGuid[item.substr(0, c)] = stjoy::ROLE_PORT0;
            else if (r == '1') A.joyRoleByGuid[item.substr(0, c)] = stjoy::ROLE_PORT1;
            else if (r == 'x') A.joyRoleByGuid[item.substr(0, c)] = stjoy::ROLE_OFF;
        }
        p = e + 1;
    }
}
std::string joymapSerialize(const App& A) {
    std::string s;
    for (const auto& [guid, role] : A.joyRoleByGuid) {
        if (role == stjoy::ROLE_AUTO) continue;   // AUTO = absent de la liste
        if (!s.empty()) s += ',';
        s += guid;
        s += ':';
        s += (role == stjoy::ROLE_PORT0) ? '0' : (role == stjoy::ROLE_PORT1) ? '1' : 'x';
    }
    return s;
}
