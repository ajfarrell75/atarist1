// =============================================================================
//  ConfigPath.hpp — OÙ vit neost.cfg ? (chantier A36, 2026-08-28)
//
//  Avant : `exeDir + "/../neost.cfg"`, point. Correct pour `build/neost` (arbre de
//  dev), pour l'AppImage et pour le .zip Windows — tous « portables », le binaire
//  et sa config voyagent ensemble. Faux dès qu'on installe pour de bon : un
//  `/usr/bin/neost` cherche sa config dans `/usr/`, où l'utilisateur n'écrit pas.
//  L'écriture échouait alors en silence, ou pire, réussissait pour root seulement.
//
//  La règle, dans l'ordre — et elle ne SURPREND jamais l'installation portable :
//    1. si `<exeDir>/../neost.cfg` EXISTE, c'est lui. Rien ne change pour l'arbre
//       de dev, l'AppImage, le zip Windows, la borne — pas de migration, pas de
//       config qui « disparaît » après une mise à jour ;
//    2. sinon, si une config UTILISATEUR existe, c'est elle
//       (`$XDG_CONFIG_HOME/neost/neost.cfg`, défaut `~/.config/neost/` ;
//        `%APPDATA%\neost\neost.cfg` sous Windows) ;
//    3. sinon, on choisit où ÉCRIRE : à côté du binaire si ce dossier est
//       inscriptible (installation portable neuve), sinon dans la config
//       utilisateur (installation système).
//
//  POURQUOI UNE FONCTION PURE. La règle a quatre cas et deux plateformes ; la
//  tester « pour de vrai » demanderait de simuler des installations. Elle prend
//  donc ses sondes (existe ? inscriptible ? environnement ?) en paramètre — le
//  test les fournit en mémoire, la production les branche sur le disque. C'est
//  exactement ce que fait déjà `hostpath::Style` pour la sémantique Windows, et
//  c'est ce qui a permis d'attraper l'issue #37 depuis un Mac.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "util/HostPath.hpp"

#include <functional>
#include <string>

namespace neost::cfgpath {

// Sondes injectables (cf. le bandeau) : en production elles regardent le disque et
// l'environnement, en test elles répondent depuis une table.
struct Probe {
    std::function<bool(const std::string&)> exists;       // ce fichier existe-t-il ?
    std::function<bool(const std::string&)> dirWritable;  // ce dossier est-il inscriptible ?
    std::function<std::string(const char*)> env;          // variable d'environnement (vide si absente)
};

// Sondes réelles (système de fichiers + getenv).
Probe systemProbe();

// Dossier de configuration UTILISATEUR, sans le nom de fichier. Vide si l'on ne
// sait pas le construire (ni XDG_CONFIG_HOME ni HOME, ni APPDATA sous Windows) —
// l'appelant retombe alors sur le dossier du binaire.
std::string userConfigDir(const Probe& p, hostpath::Style style = hostpath::kNative);

// Chemin COMPLET du neost.cfg à utiliser, selon la règle en quatre cas du bandeau.
std::string resolve(const std::string& exeDir, const Probe& p,
                    hostpath::Style style = hostpath::kNative);

// Dossier des profils nommés : TOUJOURS à côté du neost.cfg retenu — les deux se
// déplacent ensemble, sinon on installe des profils que la config ne retrouve pas.
std::string profilesDirFor(const std::string& cfgFile, hostpath::Style style = hostpath::kNative);

}  // namespace neost::cfgpath
