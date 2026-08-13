// =============================================================================
//  MiniJson.hpp — Extracteur JSON minimal pour le déport « JSON » du FujiNet.
//
//  Le vrai FujiNet parse le JSON SUR le périphérique et la machine 8/16 bits ne
//  reçoit que la valeur demandée. On reproduit exactement ça : pas de DOM
//  exposé, une seule opération — extraire la valeur pointée par un chemin
//  « /clé/0/sous-clé » (séparateur '/', indices décimaux pour les tableaux),
//  rendue en texte (chaînes sans guillemets, nombres/booléens tels quels,
//  objets/tableaux re-sérialisés tels qu'écrits dans le document).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <string>

namespace minijson {

// Extrait la valeur au chemin `path` du document `doc`. Renvoie true et remplit
// `out` si le chemin existe et le document est bien formé jusqu'à lui.
bool query(const std::string& doc, const std::string& path, std::string& out);

// Validation légère (utilisée par la commande 'P') : le document commence-t-il
// par une valeur JSON complète ? (Pas un validateur strict — suffisant pour
// rendre FN_ERR sur un corps HTML par exemple.)
bool looksLikeJson(const std::string& doc);

} // namespace minijson
