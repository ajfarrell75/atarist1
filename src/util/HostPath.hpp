// =============================================================================
//  HostPath.hpp — LA définition des chemins de l'HÔTE, une seule fois.
//
//  Pourquoi ce module existe : NeoST avait QUATRE notions concurrentes de
//  « chemin absolu » — GemdosHd (port de File_MakeAbsoluteName), les deux
//  résolveurs de main.cpp, et le lambda `resolve` de --from-cfg côté headless.
//  Toutes appliquaient la règle Unix « ça commence par '/' ». Sous Windows un
//  chemin absolu commence par une LETTRE DE LECTEUR (« C:\Temp ») ou une racine
//  UNC (« \\serveur\partage ») : le dossier glissé sur la fenêtre était donc pris
//  pour relatif et préfixé du répertoire courant, ce qui a rendu le lecteur C:
//  inutilisable sur TOUS les paquets Windows (issue #37) — et le même défaut
//  dormait encore dans --from-cfg après le correctif.
//
//  Deux règles de conception :
//
//   · SÉPARATEUR INTERNE = '/'. Win32 accepte '/' dans toutes ses API fichier ;
//     normaliser à l'entrée évite d'avoir à comparer deux séparateurs partout
//     (c'est déjà ce que fait GemdosHd, dont tout le corps compare avec '/').
//
//   · LE STYLE EST UN PARAMÈTRE D'EXÉCUTION, pas un #ifdef. C'est LA leçon de
//     l'issue #37 : personne ne pouvait exercer la sémantique Windows depuis un
//     Mac ou une CI Linux, donc personne ne l'a exercée. Avec `Style`, l'auto-test
//     valide les deux mondes sur n'importe quelle machine — cf. tests/selftest_logic.cpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <string>

namespace neost::hostpath {

// Style de chemin de l'hôte. `Native` n'est PAS une troisième valeur : c'est
// celle des deux qui correspond à la plateforme de compilation.
enum class Style { Posix, Windows };

constexpr Style kNative =
#if defined(_WIN32)
    Style::Windows;
#else
    Style::Posix;
#endif

constexpr char SEP = '/';        // séparateur interne (accepté par Win32 aussi)

// « C: », « c:/… » — préfixe de lecteur Windows.
bool hasDriveLetter(const std::string& p);

// Chemin ABSOLU ? Unix : commence par '/'. Windows : lettre de lecteur ou racine
// UNC ; un simple '/' de tête y est un chemin RELATIF au lecteur courant, donc
// ambigu — on le traite comme absolu (c'est ce que fait le CRT) mais il ne doit
// pas servir de forme canonique.
bool isAbsolute(const std::string& p, Style style = kNative);

// '\' → '/' (sans effet en style Posix, où '\' est un caractère de nom LÉGAL —
// convertir y casserait les noms de fichiers, d'où le paramètre).
std::string normalizeSeparators(std::string p, Style style = kNative);

// Retire les séparateurs de fin (File_CleanFileName). Préserve « C:/ » sous
// Windows : rabattu en « C: », il désignerait le dossier COURANT du lecteur.
std::string stripTrailingSep(std::string p, Style style = kNative);

// Concatène en garantissant un séparateur unique. Un `tail` absolu écrase `base`
// (sémantique de std::filesystem::path::operator/ et du bon sens : joindre une
// base à un chemin déjà absolu était exactement le bug #37).
std::string join(const std::string& base, const std::string& tail, Style style = kNative);

// Résolution LEXICALE : normalise les séparateurs, préfixe `cwd` si le chemin est
// relatif, puis résout « . » et « .. » sans toucher au disque (donc sans résoudre
// les liens symboliques — c'est le travail de physicalCanon côté GemdosHd).
// Port de File_MakeAbsoluteName d'Hatari, rendu conscient de Windows.
std::string lexicalAbsolute(const std::string& p, const std::string& cwd,
                            Style style = kNative);

// Idem, avec le répertoire courant du processus.
std::string makeAbsolute(const std::string& p);

// Répertoire courant du processus, séparateurs normalisés ('\' sous Windows).
std::string currentDir();

}  // namespace neost::hostpath
