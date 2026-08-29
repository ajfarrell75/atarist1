// =============================================================================
//  MidiEndpoint.hpp — Désigner un appareil MIDI hôte SANS ambiguïté.
//
//  Le nom d'affichage ne suffit pas. Deux machines du MÊME MODÈLE branchées
//  ensemble — deux claviers identiques, le cas d'un studio — portent EXACTEMENT le
//  même nom. Les désigner par leur nom, c'est alors ouvrir deux fois le même
//  appareil et laisser l'autre muet, sans que rien ne le dise.
//
//  L'index, lui, ne peut pas servir de solution : il se renumérote dès qu'on
//  débranche un voisin, et une config mémorisée en index se mettrait à piloter le
//  mauvais appareil au branchement suivant. C'est le piège inverse.
//
//  D'où le couple (nom, identifiant unique) :
//    · CoreMIDI fournit kMIDIPropertyUniqueID, stable d'un branchement à l'autre ;
//    · ALSA n'a pas d'équivalent (client:port change à chaque fois) → uid vide.
//
//  L'appariement ci-dessous marche dans les deux cas, et c'est pour ça qu'il est
//  écrit ICI, en fonction PURE : il se teste sans le moindre appareil branché.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace neost::midi {

// Un point de terminaison tel que l'hôte le présente.
struct Endpoint {
    std::string name;
    std::string uid;      // vide = l'hôte n'en fournit pas (ALSA)
};

// Un appareil VOULU par la configuration.
struct Wanted {
    std::string name;
    std::string uid;      // vide = config d'avant les identifiants, ou hôte sans uid
};

// Pour chaque `want`, l'INDEX du point de terminaison retenu, ou -1 s'il est absent.
// Un même point n'est jamais attribué deux fois : c'est ce qui permet à deux appareils
// homonymes d'être ouverts tous les deux, chacun sur le sien, même sans identifiant.
//
// Deux passes, et l'ordre compte : les correspondances par IDENTIFIANT sont toutes
// résolues AVANT les correspondances par nom. Sinon un appareil désigné par son nom
// pourrait rafler le point de terminaison qu'un autre réclamait par son identifiant —
// le critère fiable doit servir en premier.
inline std::vector<int> matchEndpoints(const std::vector<Wanted>& want,
                                       const std::vector<Endpoint>& have) {
    std::vector<int> out(want.size(), -1);
    std::vector<bool> taken(have.size(), false);

    for (std::size_t w = 0; w < want.size(); ++w) {
        if (want[w].uid.empty()) continue;
        for (std::size_t e = 0; e < have.size(); ++e)
            if (!taken[e] && have[e].uid == want[w].uid) { out[w] = int(e); taken[e] = true; break; }
    }
    for (std::size_t w = 0; w < want.size(); ++w) {
        if (out[w] >= 0) continue;
        for (std::size_t e = 0; e < have.size(); ++e)
            if (!taken[e] && have[e].name == want[w].name) { out[w] = int(e); taken[e] = true; break; }
    }
    return out;
}

// Étiquette d'affichage : le nom seul quand il est unique, suffixé « #n » quand
// plusieurs points le partagent. Sans ce suffixe, l'interface montrerait deux lignes
// rigoureusement identiques et l'utilisateur n'aurait aucun moyen de savoir laquelle
// est laquelle.
inline std::string displayLabel(const std::vector<Endpoint>& have, std::size_t idx) {
    if (idx >= have.size()) return {};
    std::size_t total = 0, rank = 0;
    for (std::size_t i = 0; i < have.size(); ++i)
        if (have[i].name == have[idx].name) { ++total; if (i <= idx) ++rank; }
    if (total <= 1) return have[idx].name;
    return have[idx].name + " #" + std::to_string(rank);
}

} // namespace neost::midi
