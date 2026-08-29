// =============================================================================
//  MidiDeviceProfiles.hpp — Plans de canaux d'appareils CONNUS, pour poser un
//  masque d'aiguillage d'un clic au lieu de cocher seize cases.
//
//  ⚠ Ce sont des DÉFAUTS D'USINE relevés dans la documentation constructeur, pas
//  des constantes universelles : sur la plupart des machines, l'utilisateur peut
//  réassigner ces canaux. Le profil est donc un RACCOURCI, jamais une vérité — il
//  pose un masque que l'on reste libre de modifier case par case ensuite, et
//  l'interface le présente comme tel.
//
//  Règle d'entrée dans cette table : une source constructeur VÉRIFIÉE, citée. Un
//  plan de canaux deviné enverrait l'utilisateur chercher une panne qui n'existe
//  pas — « pourquoi ma boîte à rythmes reste muette » — et c'est exactement le
//  genre d'erreur qu'une table comme celle-ci rend invisible.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>

namespace neost::midi {

struct DeviceProfile {
    const char* match;      // fragment cherché dans le nom d'affichage de l'appareil
    const char* label;      // texte du bouton
    uint16_t    channels;   // masque : bit n = canal n+1
    const char* detail;     // infobulle : le plan complet, notes de percussion comprises
};

// Masque : bits 0-3 = canaux 1-4, bit 9 = canal 10.
inline constexpr uint16_t kCircuitTracks = 0x000F | 0x0200;

inline constexpr DeviceProfile kDeviceProfiles[] = {
    // Novation Circuit Tracks — « Circuit Tracks Programmer's Reference Guide » v3
    // (Focusrite/Novation) : Synth 1 canal 1, Synth 2 canal 2, pistes MIDI 1 et 2
    // canaux 3 et 4, Drums 1-4 TOUS sur le canal 10 (ils se distinguent par la NOTE,
    // pas par le canal), canal 16 réservé au Project Control. Réassignables en
    // Setup View.
    {"Circuit Tracks", "Circuit Tracks: synths + MIDI + drums", kCircuitTracks,
     "Synth 1 = ch 1, Synth 2 = ch 2, MIDI 1/2 = ch 3/4,\n"
     "Drums 1-4 = ch 10, told apart by NOTE:\n"
     "  60 = Drum 1   62 = Drum 2   64 = Drum 3   65 = Drum 4\n"
     "Ch 16 is reserved for Project Control.\n"
     "Factory defaults - reassignable in the device's Setup View."},
};

// Profil correspondant à un nom d'appareil, ou nullptr. Comparaison par FRAGMENT :
// le nom affiché par le système porte des suffixes variables (« Circuit Tracks
// MIDI », « Circuit Tracks CTRL »).
inline const DeviceProfile* profileFor(const std::string& deviceName) {
    for (const auto& p : kDeviceProfiles)
        if (deviceName.find(p.match) != std::string::npos) return &p;
    return nullptr;
}

} // namespace neost::midi
