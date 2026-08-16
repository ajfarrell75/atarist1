// =============================================================================
//  AppConfig.hpp — neost.cfg : structure, analyse, écriture, profils nommés.
//
//  Extrait de main.cpp, qui frôlait les 5000 lignes en mélangeant la persistance,
//  une douzaine de panneaux ImGui et la boucle principale. Ce découpage n'est pas
//  cosmétique : le FORMAT de configuration est de la logique pure (des parseurs,
//  une écriture atomique) et, tant qu'il vivait dans main.cpp, RIEN ne pouvait
//  l'exercer — main.cpp n'est lié à aucune cible de test. Ici, la TU ne dépend ni
//  de GLFW ni d'ImGui : elle entre dans neost_core, donc dans neost-selftest
//  (cf. tests/selftest_logic.cpp, aller-retour parse↔écriture).
//
//  Ce qui RESTE dans main.cpp : tout ce qui touche la Machine (loadRtcFromConfig,
//  snapshotRtc) et la politique de gel de la borne (saveConfig).
//
//  ⚠ writeConfigKeys et parseConfigLine se répondent clé pour clé : ne toucher
//  qu'ensemble, sinon un réglage s'écrit sans jamais se relire (ou l'inverse).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <ctime>
#include <iosfwd>
#include <string>
#include <vector>

#include "gui/CrtParams.h"
#include "io/Rtc.hpp"

namespace neost::appconfig {

// --- Persistance des préférences (dernier ROM, type de moniteur) -------------
// Fichier neost.cfg à la racine du projet (à côté de build/).
// cpu = cœur 68000 (toujours "moira", cycle-exact ; clé conservée pour rétro-compat —
// une ancienne valeur "musashi" est tolérée puis ramenée à "moira" avec un avertissement).
// Défaut machine : 512 Ko ST + cœur Moira.
// joyport = port ST visé par l'émulation joystick clavier (0 ou 1, défaut 1 =
// port « jeux »). L'activation de cette émulation N'EST PAS persistée : elle
// démarre toujours OFF (elle avale flèches + Ctrl droit, ce qui « casse » le
// clavier des jeux — Cuddly, Captain Blood…) ; F11/menu l'activent à la session.
struct Config { std::string rom; std::string disk; std::string diskb; std::string cart; bool mono = false;
                std::string gemdos;   // HD GEMDOS : dossier hôte monté en C: (vide = off)
                std::string acsi;     // image disque dur ACSI cible 0 (vide = off)
                bool fujinet = false; // FujiNet virtuel sur le bus ACSI (extension NeoST)
                int  fujinetTarget = 6;      // cible ACSI du FujiNet (0-7)
                std::string fujinetHosts;    // host slots, séparés par '|' (slot 0 en tête)
                bool modem = false;   // modem Hayes sur l'USART (pont AT → TCP)
                bool ethernec = false; // NE2000/EtherNEC sur le port cartouche
                std::string cpu = "moira"; std::string machine = "st";
                std::string mem = "512k"; bool fpu = false;   // MC68881 Mega STE (cf. Fpu.hpp)
                int joyport = 1;
                // Affectation des manettes HÔTE aux ports ST, persistée par GUID :
                // "guid:rôle,guid:rôle" avec rôle ∈ {0, 1, x} (port 0, port 1, off).
                // Une manette absente de la liste est AUTO (cf. stjoy::resolveAssign).
                std::string joymap;
                float joydeadzone = 0.30f; bool fastfdc = false;
                float volume = 1.0f;   // volume maître de la sortie audio (0..1, barre de menu)
                int audioLatencyMs = 85; // coussin audio visé (cf. Audio::setLatencyMs, --audio-latency)
                bool driveSound = true;  // bruits mécaniques du lecteur (roms/drivesound/, cf. DriveSound)
                bool showHex = true, showCpu = true;
                bool showJoy = false;
                bool showCfg = true;           // fenêtre des réglages matériels
                bool showFloppy = true;        // fenêtre indépendante des disquettes
                // Version de la DISPOSITION d'interface. Quand elle change, on resème la
                // disposition ancrée une fois : sinon un imgui.ini écrit par une version
                // précédente garde des nœuds pour des fenêtres disparues et la nouvelle
                // fenêtre Configuration flotte au-dessus de l'écran ST.
                int uiVersion = 0;
                bool dock = true;              // mode ancré (dockspace ImGui) — cf. renderDockSpace
                bool autoZoom = true;          // zoom adaptatif de l'écran ST — cf. g_autoZoom
                bool crt = false;              // effets CRT actifs (façade moniteur)
                neost::CrtParams crtParams;    // réglages CRT (cf. gui/CrtParams.h)
                std::vector<std::string> romDirs;   // kiosk : dossiers ROM/disques additionnels (en plus de disks/)
                std::string rtc; std::time_t rtcSaved = 0; };

// Chemin du neost.cfg (à côté de build/, cf. exeDir).
std::string cfgPath(const std::string& exeDir);

// « sec,min,hour,wday,day,month,year » → Rtc::DateTime (false si la ligne ment).
bool parseRtcConfig(const std::string& s, Rtc::DateTime& dt);

// Applique UNE ligne « clé=valeur » à `c` (partagé par neost.cfg et les profils).
void parseConfigLine(Config& c, std::string line);
Config loadConfig(const std::string& exeDir);

// Sérialisation. full=true : neost.cfg complet ; false : profil nommé.
void writeConfigKeys(std::ostream& f, const Config& w, bool full);
bool writeConfigAtomic(const std::string& finalPath, const Config& w, bool full,
                       bool cwdFallback, std::string& err);

// Profils nommés (profiles/*.cfg, même format que neost.cfg).
std::string profilesDir(const std::string& exeDir);
std::string profileFileName(const std::string& in);
std::vector<std::string> listProfiles(const std::string& dir);
bool saveProfile(const std::string& dir, const std::string& name,
                 const Config& c, std::string& err);
bool loadProfileInto(const std::string& dir, const std::string& name, Config& c);
bool deleteProfile(const std::string& dir, const std::string& name);

}  // namespace neost::appconfig
