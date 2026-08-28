// =============================================================================
//  ShifterInternal.hpp — outillage PARTAGÉ par les trois unités du Shifter
//  (Shifter.cpp, VideoGlue.cpp, VideoCounter.cpp). Chantier A32, 2026-08-28.
//
//  Ces helpers étaient `static` dans l'unique Shifter.cpp. Le découpage par rôle
//  les rend communs à trois unités : ils passent en `inline` (UNE définition, pas
//  une par unité — un verrou d'environnement doit être lu une seule fois).
//
//  ⚠ Ce n'est PAS une API : seuls les trois .cpp du Shifter l'incluent.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Lecture d'un verrou d'environnement booléen, avec la MÊME règle partout :
// variable absente → défaut, sinon « 0 » (ou toute valeur nulle) = OFF. Les sites
// NEOST_LINELEN testaient jadis la seule PRÉSENCE de la variable, alors que
// Machine.cpp lit sa valeur : NEOST_LINELEN=0, censé désactiver le mécanisme pour
// l'A/B, désactivait la moitié Machine et ACTIVAIT la moitié Shifter — l'A/B
// mesurait donc un hybride jamais validé.
inline bool envFlag(const char* name, bool dflt) {
    const char* s = std::getenv(name);
    return s ? (std::atoi(s) != 0) : dflt;
}

// Cf. le bandeau A16/A16b de Shifter.cpp pour l'histoire de ces deux verrous.
inline bool lineLenAttrEnv() {
    static const bool on = envFlag("NEOST_LINELEN_ATTR", false);
    return on;
}

// Décalage de la lecture du compteur vidéo ($FF8205/07/09) — le récit de sa
// calibration est dans VideoCounter.cpp. NEOST_VC_OFF ajuste pour l'A/B.
inline const int kVideoCounterReadOffsetCyc =
    -6 + [] { const char* s = std::getenv("NEOST_VC_OFF"); return s ? std::atoi(s) : 0; }();

// Décalage de datation des écritures freq/res — récit dans VideoGlue.cpp.
inline constexpr int kSyncWriteOffsetCyc = +2;

// Table d'éclatement d'un octet de bitplane en 8 octets « 0 ou 1 », dans l'ordre
// d'affichage (bit 7 = pixel le plus à gauche = premier octet en mémoire).
// Construite par memcpy depuis un tableau d'octets : la table a donc, quel que soit
// le boutisme de l'hôte, exactement la disposition mémoire que decodeLineIndices
// recopie ensuite dans `idx`. 2 Ko, résidents en L1 (une ligne de cache par 8 octets).
namespace {
    struct SpreadTable {
        uint64_t v[256];
        SpreadTable() {
            for (int b = 0; b < 256; ++b) {
                uint8_t bytes[8];
                for (int k = 0; k < 8; ++k) bytes[k] = uint8_t((b >> (7 - k)) & 1);
                std::memcpy(&v[b], bytes, 8);
            }
        }
    };
    const SpreadTable g_spread;
    // Raccourci de lecture. Initialisé APRÈS g_spread (ordre d'initialisation garanti
    // au sein d'une même unité de compilation).
    const uint64_t* const kSpread = g_spread.v;
}
