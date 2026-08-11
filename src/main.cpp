// =============================================================================
//  main.cpp — Frontend fenêtré de NeoST (GLFW3 + OpenGL + Dear ImGui).
//
//  Le matériel et la boucle d'horloge vivent dans Machine (cœur sans GUI) ; ce
//  fichier ne fait que : créer la fenêtre, téléverser le framebuffer décodé du
//  Shifter dans une texture, router le clavier vers l'IKBD, et afficher l'UI de
//  debug. Le même cœur tourne en headless (neost-headless) pour les traces.
//
//  Modèle temporel (cf. Machine) : 68000 ~8 MHz, 512 cycles/ligne, 313 lignes
//  PAL ≈ 50 Hz. Le Timer C du MFP (200 Hz) débloque l'accueil EmuTOS.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
// glext.h est REQUIS hors macOS : le <GL/gl.h> livré par Windows est figé en
// OpenGL 1.1 et ignore GL_BGRA / GL_UNSIGNED_INT_8_8_8_8_REV (GL 1.2), dont le
// téléversement du framebuffer ARGB du Shifter a besoin. Seul l'EN-TÊTE est
// ancien : tout pilote réel expose ces formats depuis vingt ans. Même schéma
// que gui/CrtEffectStack.cpp et gui/OpenGLShader.cpp.
#include <GL/glext.h>
#endif
#include "gui/CrtEffectStack.h"   // passe d'effets CRT (opt-in, façade moniteur)
#include "core/Symbols.hpp"       // table de symboles du débogueur (noms ↔ adresses)
#include <cfloat>                 // FLT_MAX (contrainte de ratio fenêtre écran)
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>
#include <string>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>       // _NSGetExecutablePath (résolution du chemin exécutable)
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX               // sinon windows.h définit min()/max() en MACROS et
#endif                         // casse tous les std::min / std::max du fichier
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN    // pas de winsock/ole : rien d'utile ici, des conflits sûrs
#endif
#include <windows.h>           // GetModuleFileNameW (résolution du chemin exécutable)
#endif

#include "core/Machine.hpp"
#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "io/JoystickInput.hpp"

namespace fs = std::filesystem;

// Résout un chemin de données indépendamment du répertoire courant : tel quel,
// puis relatif au répertoire de l'exécutable (utile quand on lance depuis build/).
// std::filesystem et non stat() : <sys/stat.h> n'existe pas partout, et la
// surcharge à error_code ne LANCE jamais (un chemin illisible = « absent »).
static bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec;
}
static std::string resolveData(const std::string& given, const std::string& exeDir) {
    const std::string cands[] = { given, exeDir + "/" + given, exeDir + "/../" + given, "../" + given };
    for (const auto& c : cands) if (fileExists(c)) return c;
    return given;
}

// Choisit un TOS compatible avec la MACHINE sélectionnée si le ROM courant ne
// convient pas — pour que « choisir Mega STE » donne un VRAI Mega STE. Seul cas
// géré : le Mega STE exige TOS ≥ 2.0x (lui seul programme cache 16 Ko / SCU / 16 MHz).
// Cherche dans roms/ un tos206<pays> (pays du ROM courant), sinon tos206 / tos206us,
// sinon etos256<pays> / etos256us. Renvoie le chemin LOGIQUE (« roms/tos206fr.img »),
// ou "" si le ROM courant convient déjà (ou aucun candidat trouvé).
static std::string pickTosForMachine(const std::string& machine,
                                     const std::string& curRomLogical,
                                     const std::string& exeDir,
                                     const std::string& romsDir) {
    if (machine != "megaste") return "";
    { std::ifstream f(resolveData(curRomLogical, exeDir), std::ios::binary);
      if (f) { uint8_t b[2] = {0, 0}; f.seekg(2); f.read(reinterpret_cast<char*>(b), 2);
               if (((b[0] << 8) | b[1]) >= 0x0200) return ""; } }   // déjà compatible
    std::string cc = fs::path(curRomLogical).stem().string();       // ex. "tos162fr"
    cc = (cc.size() >= 2) ? cc.substr(cc.size() - 2) : std::string();
    if (cc.size() != 2 || !std::isalpha((unsigned char)cc[0]) || !std::isalpha((unsigned char)cc[1]))
        cc.clear();
    auto has = [&](const std::string& n) { std::error_code ec; return fs::exists(fs::path(romsDir) / n, ec); };
    for (const std::string& cand : { "tos206" + cc + ".img", std::string("tos206.img"),
                                     std::string("tos206us.img"), "etos256" + cc + ".img",
                                     std::string("etos256us.img") })
        if (has(cand)) return "roms/" + cand;
    return "";
}

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
                bool showCfg = true;           // fenêtre « Configuration » (tout se règle là)
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
static std::string cfgPath(const std::string& exeDir) { return exeDir + "/../neost.cfg"; }

static bool parseRtcConfig(const std::string& s, Rtc::DateTime& dt) {
    return std::sscanf(s.c_str(), "%d,%d,%d,%d,%d,%d,%d",
                       &dt.sec, &dt.min, &dt.hour, &dt.wday,
                       &dt.day, &dt.month, &dt.year) == 7;
}
static void loadRtcFromConfig(Machine& m, const Config& c) {
    Rtc::DateTime dt;
    if (!c.rtc.empty() && parseRtcConfig(c.rtc, dt)) {
        m.rtc.setDateTime(dt);
        if (c.rtcSaved > 0) {
            const std::time_t now = std::time(nullptr);
            if (now > c.rtcSaved) m.rtc.advanceSeconds(now - c.rtcSaved);
        }
    }
}
static void snapshotRtc(Machine& m, Config& c) {
    const Rtc::DateTime dt = m.rtc.getDateTime();
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d,%d",
                  dt.sec, dt.min, dt.hour, dt.wday, dt.day, dt.month, dt.year);
    c.rtc = buf;
    c.rtcSaved = std::time(nullptr);
}
// Applique UNE ligne « clé=valeur » à `c`. Extrait de loadConfig pour être partagé
// avec les PROFILS nommés (profiles/*.cfg, même format) : un profil n'écrit qu'un
// sous-ensemble des clés, et tout ce qu'il omet garde donc la valeur déjà présente
// dans `c`. C'est ce qui permet de charger un profil PAR-DESSUS la configuration
// courante sans tenir à jour une liste de recopie champ par champ.
static void parseConfigLine(Config& c, std::string line) {
    // Fin de ligne CRLF (fichier passé par Windows, un éditeur, un partage réseau) :
    // getline ne retire que le \n, et TOUTES les valeurs sont comparées EXACTEMENT
    // (parseMachine, parseRamBytes, == "1"). Un \r collé faisait donc tomber chaque
    // clé sur son défaut SILENCIEUX — machine ST demandée, STE démarrée ; 4 Mo
    // demandés, 512 Ko alloués — et rendait tout chemin introuvable. Pire : saveConfig
    // réécrivait ensuite le fichier avec les \r intacts, donc la panne était définitive.
    // Même rognage que SymbolTable (Symbols.cpp). On retire aussi les espaces de fin.
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
    if      (line.rfind("rom=", 0)  == 0) c.rom  = line.substr(4);
    else if (line.rfind("disk=", 0) == 0) c.disk = line.substr(5);
    else if (line.rfind("cart=", 0) == 0) c.cart = line.substr(5);
    else if (line.rfind("gemdos=", 0) == 0) c.gemdos = line.substr(7);
    else if (line.rfind("acsi=", 0) == 0) c.acsi = line.substr(5);
    else if (line.rfind("mono=", 0) == 0) c.mono = (line.substr(5) == "1");
    else if (line.rfind("cpu=", 0)  == 0) c.cpu  = line.substr(4);
    else if (line.rfind("machine=", 0) == 0) c.machine = line.substr(8);
    else if (line.rfind("mem=", 0)  == 0) c.mem  = line.substr(4);
    else if (line.rfind("fpu=", 0)  == 0) c.fpu  = (line.substr(4) == "1");
    else if (line.rfind("joyport=", 0) == 0) c.joyport = (line.substr(8) == "0") ? 0 : 1;
    else if (line.rfind("joymap=", 0) == 0) c.joymap = line.substr(7);
    else if (line.rfind("joydeadzone=", 0) == 0) c.joydeadzone = std::strtof(line.substr(12).c_str(), nullptr);
    else if (line.rfind("fastfdc=", 0) == 0) c.fastfdc = (line.substr(8) == "1");
    else if (line.rfind("volume=", 0) == 0) {
        c.volume = std::strtof(line.substr(7).c_str(), nullptr);
        if (c.volume < 0.0f) c.volume = 0.0f;
        if (c.volume > 1.0f) c.volume = 1.0f;
    }
    else if (line.rfind("audio_latency_ms=", 0) == 0) c.audioLatencyMs = std::atoi(line.substr(17).c_str());
    else if (line.rfind("drivesound=", 0) == 0) c.driveSound = (line.substr(11) == "1");
    // showDisk=/showCart=/showHd= : clés d'anciennes fenêtres (bibliothèques),
    // devenues des pages de la Configuration. Ignorées silencieusement.
    else if (line.rfind("showHex=", 0) == 0) c.showHex = (line.substr(8) == "1");
    else if (line.rfind("showCpu=", 0) == 0) c.showCpu = (line.substr(8) == "1");
    else if (line.rfind("showJoy=", 0) == 0) c.showJoy = (line.substr(8) == "1");
    else if (line.rfind("showCfg=", 0) == 0) c.showCfg = (line.substr(8) == "1");
    else if (line.rfind("uiVersion=", 0) == 0) c.uiVersion = std::atoi(line.c_str() + 10);
    else if (line.rfind("diskb=", 0)  == 0) c.diskb   = line.substr(6);
    else if (line.rfind("dock=", 0) == 0) c.dock = (line.substr(5) == "1");
    else if (line.rfind("autozoom=", 0) == 0) c.autoZoom = (line.substr(9) == "1");
    else if (line.rfind("rtc_saved=", 0) == 0) c.rtcSaved = std::strtoll(line.substr(10).c_str(), nullptr, 10);
    else if (line.rfind("rtc=", 0) == 0) c.rtc = line.substr(4);
    else if (line.rfind("kiosk_romdir=", 0) == 0) { const std::string d = line.substr(13); if (!d.empty()) c.romDirs.push_back(d); }
    // Effets CRT (cf. gui/CrtParams.h). Un preset (--crt-preset / applyCrtPreset)
    // n'est qu'un raccourci qui écrit ces mêmes clés numériques.
    else if (line.rfind("crt=", 0) == 0) c.crt = (line.substr(4) == "1");
    else if (line.rfind("crt_bright=", 0)  == 0) c.crtParams.brightness  = std::strtof(line.substr(11).c_str(), nullptr);
    else if (line.rfind("crt_contrast=", 0) == 0) c.crtParams.contrast   = std::strtof(line.substr(13).c_str(), nullptr);
    else if (line.rfind("crt_sat=", 0)     == 0) c.crtParams.saturation  = std::strtof(line.substr(8).c_str(), nullptr);
    else if (line.rfind("crt_hue=", 0)     == 0) c.crtParams.hue         = std::strtof(line.substr(8).c_str(), nullptr);
    else if (line.rfind("crt_sharp=", 0)   == 0) c.crtParams.sharpness   = std::strtof(line.substr(10).c_str(), nullptr);
    else if (line.rfind("crt_persist=", 0) == 0) c.crtParams.persistence = std::strtof(line.substr(12).c_str(), nullptr);
    else if (line.rfind("crt_scanlines=", 0) == 0) c.crtParams.scanlines = std::strtof(line.substr(14).c_str(), nullptr);
    else if (line.rfind("crt_barrel=", 0)  == 0) c.crtParams.barrel      = std::strtof(line.substr(11).c_str(), nullptr);
    else if (line.rfind("crt_mask=", 0)    == 0) c.crtParams.shadowMask  = static_cast<neost::CrtParams::ShadowMask>(std::atoi(line.substr(9).c_str()));
    else if (line.rfind("crt_maskstr=", 0) == 0) c.crtParams.shadowMaskStrength = std::strtof(line.substr(12).c_str(), nullptr);
    else if (line.rfind("crt_lumgain=", 0) == 0) c.crtParams.luminanceGain = std::strtof(line.substr(12).c_str(), nullptr);
    else if (line.rfind("crt_center=", 0)  == 0) c.crtParams.centerLighting = std::strtof(line.substr(11).c_str(), nullptr);
    else if (line.rfind("crt_gamma=", 0)   == 0) c.crtParams.phosphorGamma  = std::strtof(line.substr(10).c_str(), nullptr);
}
static Config loadConfig(const std::string& exeDir) {
    Config c;
    std::ifstream f(cfgPath(exeDir));
    if (!f) f.open("neost.cfg");
    std::string line;
    while (std::getline(f, line)) parseConfigLine(c, line);
    return c;
}
// Mode kiosk (borne/expo) : plein écran sans chrome, config figée, sortie par chord.
// Activé par --kiosk. Déclaré ici car saveConfig doit le consulter (gel de la config).
static bool g_kiosk = false;
// Lancé en borne (--kiosk) — invariant de DÉPLOIEMENT, distinct de g_kiosk qui suit
// la bascule à chaud. Sans lui, un aller-retour F8 rendrait la main à tous les
// saveConfig du GUI pour le reste du processus : la borne repartirait sur le disque
// et les réglages du dernier visiteur au lieu de sa configuration d'exposition.
static bool g_kioskLaunched = false;
// Bascule GUI ⇄ kiosk À CHAUD (F8, menu Machine, action « DESKTOP MODE » du menu
// borne). La demande est POSÉE ici puis appliquée en tête de boucle, à une frontière
// de trame : la bascule prend un instantané de la machine et le restaure derrière
// elle, et Machine::loadState n'accepte que l'entre-deux-trames.
//   0 = rien à faire · 1 = passer en kiosk · 2 = revenir au GUI.
static int g_kioskSwitchReq = 0;
// Zoom ADAPTATIF (cale le contenu réel sur la hauteur disponible) : ON par défaut.
// S'applique aux DEUX modes — plein écran kiosk (viewport GL) et fenêtre « Atari ST
// Screen » du bureau (UV de l'image) — pour que le bureau présente le même cadrage
// que la borne. OFF = cadre complet fixe (pillarbox, rien ne déborde).
// Bascule : F10 en kiosk (où les touches ne vont pas au ST), menu Résolution au
// bureau (F10 y est une touche du ST, on ne la confisque pas).
static bool g_autoZoom = true;
// Menu kiosk plein écran (START manette ou F9). Le jeu est MIS EN PAUSE tant que le
// menu est ouvert (cf. boucle d'émulation). Modèle « comme une vraie machine » :
//   · INSÉRER une disquette (A) = on échange le contenu du lecteur — JAMAIS de reboot
//     (exactement comme glisser une disquette : le jeu en cours continue).
//   · REDÉMARRER la machine (X) = bouton reset explicite → la machine reboote sur la
//     disquette actuellement insérée. C'est le SEUL moyen de relancer.
//   · QUITTER NeoST (Y) = avec confirmation (page QUIT).
// Déclaré ici (hors garde ImGui) car onKey doit consulter g_kioskDiskMenu pour ne
// pas transmettre les touches de navigation au ST pendant le menu.
enum { KIOSK_PAGE_LIST = 0, KIOSK_PAGE_KEYS = 1, KIOSK_PAGE_QUIT = 2,
       KIOSK_PAGE_BROWSE = 3, KIOSK_PAGE_ROMDIRS = 4, KIOSK_PAGE_JOY = 5 };
static bool g_kioskDiskMenu = false;          // menu ouvert
static int  g_kioskPage     = KIOSK_PAGE_LIST;
static int  g_kioskDiskSel  = 0;              // index disquette sélectionnée (menu INTÉRIEUR)
// Page liste = DEUX menus qu'on bascule avec gauche/droite : INTÉRIEUR (liste des
// jeux) et EXTÉRIEUR (Redémarrer / Clavier / Quitter). g_kioskZone = quel menu a le
// focus ; le FEU valide l'item surligné du menu focalisé.
enum { KIOSK_ZONE_LIST = 0, KIOSK_ZONE_ACTIONS = 1 };
static int  g_kioskZone   = KIOSK_ZONE_LIST;
static int  g_kioskActSel = 0;                // index action (menu EXTÉRIEUR, 0..4)
static int  g_kioskKeySel   = 0;              // page clavier : touche/clic sélectionné
static int  g_kioskJoySel   = 0;              // page joysticks : manette sélectionnée
static std::vector<std::string> g_kioskDisks; // chemins listés à l'ouverture
// Page « Clavier & souris » : un appui (A) envoie la touche/clic au ST puis la
// relâche après quelques trames (frappe brève). Injection différée gérée dans la
// boucle. -1 / false = rien à relâcher. La page CLAVIER ne met PAS le jeu en pause
// (sinon la touche envoyée ne serait jamais traitée par le jeu).
static int  g_kioskKeyRelease   = -1;         // scancode ST à relâcher (sinon -1)
static bool g_kioskMouseRelL    = false;      // clic gauche à relâcher
static bool g_kioskMouseRelR    = false;      // clic droit à relâcher
static int  g_kioskInjectHold   = 0;          // trames restantes avant relâche
// Dossier ROM/disques ADDITIONNEL (choisi via l'action « ADD ROM FOLDER » du menu) :
// scanné en PLUS de disks/ pour la liste des jeux, affiché en bas du menu, persisté
// dans neost.cfg (kiosk_romdir=). Vide = disks/ seul.
static std::vector<std::string> g_kioskRomDirs;
// Page « ROM FOLDERS » (gestion : ajoute/retire les dossiers ci-dessus). Entrée 0 =
// « + ADD A FOLDER » (ouvre le navigateur), entrées 1..N = dossiers configurés, chacun
// avec une croix ❌ (FEU = retirer). g_romDirSel = ligne sélectionnée.
static int g_romDirSel = 0;
// Page « SELECT ROM FOLDER » (navigateur de répertoires piloté à la manette, plein
// écran) : g_browseDir = dossier courant (ABSOLU → « .. » remonte jusqu'à /) ;
// g_browseSubdirs = ses sous-dossiers triés. Raccourcis (racine /, home, volumes
// montés) calculés à l'ouverture. g_browseSel indexe, dans l'ordre :
//   [0] valider ce dossier · [1] .. parent · [2..2+S) raccourcis · [2+S..] sous-dossiers.
static std::string g_browseDir;
static std::vector<std::string> g_browseSubdirs;
static std::vector<std::string> g_browseShortcutPaths;   // cibles des raccourcis
static std::vector<std::string> g_browseShortcutLabels;  // libellés (icône FA + nom)
static int g_browseSel = 0;
// Image PRISTINE de la configuration, telle que lue au démarrage : c'est elle que le
// mode borne réécrit (cf. saveConfig), et non la structure de travail salie en séance.
static Config g_cfgPristine;

// Sérialise `w` en « clé=valeur ». `full` = le neost.cfg complet ; false = un PROFIL
// nommé, qui laisse dehors ce qui n'appartient pas à un jeu de réglages :
//   · rtc= / rtc_saved=  → état de la machine, pas un réglage ;
//   · kiosk_romdir=      → déploiement de la borne, propre à l'installation ;
//   · showXxx= / dock= / uiVersion= → disposition de l'interface (cousins d'imgui.ini) :
//     charger un profil ne doit pas déplacer les fenêtres de l'utilisateur.
// Tout ce qui n'est PAS écrit ici reste donc inchangé au chargement d'un profil
// (cf. parseConfigLine) — les deux fonctions se répondent, ne toucher qu'ensemble.
static void writeConfigKeys(std::ostream& f, const Config& w, bool full) {
    f << "rom=" << w.rom << "\ndisk=" << w.disk << "\ndiskb=" << w.diskb
      << "\ncart=" << w.cart
      << "\ngemdos=" << w.gemdos << "\nacsi=" << w.acsi
      << "\nmono=" << (w.mono ? 1 : 0)
      << "\ncpu=" << w.cpu << "\nmachine=" << w.machine << "\nmem=" << w.mem
      << "\nfpu=" << (w.fpu ? 1 : 0)
      << "\njoyport=" << w.joyport
      << "\njoymap=" << w.joymap
      << "\njoydeadzone=" << w.joydeadzone << "\nfastfdc=" << (w.fastfdc ? 1 : 0)
      << "\nvolume=" << w.volume
      << "\naudio_latency_ms=" << w.audioLatencyMs
      << "\ndrivesound=" << (w.driveSound ? 1 : 0) << "\n";
    if (full)
        f << "showHex=" << (w.showHex ? 1 : 0)
          << "\nshowCpu=" << (w.showCpu ? 1 : 0)
          << "\nshowJoy=" << (w.showJoy ? 1 : 0)
          << "\nshowCfg=" << (w.showCfg ? 1 : 0)
          << "\nuiVersion=" << w.uiVersion
          << "\ndock=" << (w.dock ? 1 : 0) << "\n";
    f << "autozoom=" << (w.autoZoom ? 1 : 0)
      << "\ncrt=" << (w.crt ? 1 : 0)
      << "\ncrt_bright=" << w.crtParams.brightness
      << "\ncrt_contrast=" << w.crtParams.contrast
      << "\ncrt_sat=" << w.crtParams.saturation
      << "\ncrt_hue=" << w.crtParams.hue
      << "\ncrt_sharp=" << w.crtParams.sharpness
      << "\ncrt_persist=" << w.crtParams.persistence
      << "\ncrt_scanlines=" << w.crtParams.scanlines
      << "\ncrt_barrel=" << w.crtParams.barrel
      << "\ncrt_mask=" << static_cast<int>(w.crtParams.shadowMask)
      << "\ncrt_maskstr=" << w.crtParams.shadowMaskStrength
      << "\ncrt_lumgain=" << w.crtParams.luminanceGain
      << "\ncrt_center=" << w.crtParams.centerLighting
      << "\ncrt_gamma=" << w.crtParams.phosphorGamma << "\n";
    if (full) {
        f << "rtc=" << w.rtc << "\nrtc_saved=" << w.rtcSaved << "\n";
        // Dossiers ROM additionnels (0..N) : une ligne kiosk_romdir= par dossier.
        for (const auto& d : w.romDirs) f << "kiosk_romdir=" << d << "\n";
    }
}

// Écriture ATOMIQUE : on rédige un fichier temporaire à côté, on vérifie que tout
// s'est bien écrit, PUIS on renomme par-dessus. Auparavant, ouvrir le flux tronquait
// (O_TRUNC) le fichier AVANT de savoir si on saurait le réécrire, et aucun retour
// n'était testé : disque plein, quota atteint ou coupure au mauvais moment laissaient
// un neost.cfg amputé — réglages CRT, horloge, joymap et TOUS les kiosk_romdir perdus,
// sans le moindre message. L'échec survenait dans le destructeur du flux, hors de
// portée de tout point de contrôle.
// `cwdFallback` autorise le repli historique du neost.cfg vers le répertoire courant
// quand le dossier du dépôt n'est pas inscriptible ; un profil, lui, échoue franchement
// (le message remonte dans l'interface). false ⇒ RIEN n'a été écrit, l'ancien fichier
// est intact, et `err` porte le motif.
static bool writeConfigAtomic(const std::string& finalPath, const Config& w, bool full,
                              bool cwdFallback, std::string& err) {
    std::string tmpPath = finalPath + ".tmp";
    std::ofstream f(tmpPath);
    if (!f && cwdFallback) { tmpPath = "neost.cfg.tmp"; f.open(tmpPath); }
    if (!f) {
        err = "cannot write (" + tmpPath + ")";
        f.close(); std::error_code rmec; fs::remove(tmpPath, rmec);
        return false;
    }
    writeConfigKeys(f, w, full);
    f.flush();
    const bool ok = f.good();
    f.close();
    if (!ok) {   // le flush a échoué : on garde l'ANCIEN fichier intact
        err = "incomplete write (" + tmpPath + ") — previous file kept";
        std::error_code rmec; fs::remove(tmpPath, rmec);
        return false;
    }
    // Le nom de destination est celui du .tmp amputé de son suffixe : si l'ouverture est
    // retombée sur le dossier courant, le rename doit y rester aussi.
    const std::string dest = tmpPath.substr(0, tmpPath.size() - 4);
    std::error_code mvec;
    fs::rename(tmpPath, dest, mvec);
    if (mvec) {
        err = "cannot replace (" + tmpPath + " → " + dest + "): " + mvec.message();
        std::error_code rmec; fs::remove(tmpPath, rmec);
        return false;
    }
    return true;
}
// force=true : écrit la config MÊME en kiosk (normalement figé). Utilisé pour le seul
// réglage que la borne a le droit de persister : le dossier ROM additionnel choisi via
// le menu in-game (le reste de la config kiosk reste identique à ce qui a été chargé).
static void saveConfig(const std::string& exeDir, Config& c, Machine* machine = nullptr, bool force = false) {
    if ((g_kiosk || g_kioskLaunched) && !force) return;   // kiosk : configuration figée — la borne repart toujours identique
    if (machine) snapshotRtc(*machine, c);
    // MODE BORNE : la configuration est FIGÉE, et `force` ne lève ce gel que pour deux
    // réglages d'exploitation — les dossiers ROM et l'affectation des manettes. Mais
    // `force` réécrivait TOUT le fichier depuis la structure en mémoire, or celle-ci a
    // été salie entre-temps (F10 pose cfg.autoZoom sans passer par ici, et un aller-retour
    // par le bureau rend tous les menus atteignables). La borne repartait donc avec les
    // réglages du dernier visiteur — exactement l'invariant que le gel doit garantir.
    // On repart donc de l'image PRISTINE lue au démarrage, en n'y reportant que les deux
    // champs autorisés. Le déclencheur n'a même pas besoin d'être volontaire : un dossier
    // ROM disparu suffit (auto-purge → saveConfig(force=true)).
    const Config* src = &c;
    Config kioskOut;
    if ((g_kiosk || g_kioskLaunched) && force) {
        kioskOut         = g_cfgPristine;
        kioskOut.romDirs = c.romDirs;
        kioskOut.joymap  = c.joymap;
        kioskOut.rtc     = c.rtc;          // horloge : état machine, pas un réglage d'expo
        kioskOut.rtcSaved = c.rtcSaved;
        src = &kioskOut;
    }
    const Config& w = *src;
    std::string err;
    if (!writeConfigAtomic(cfgPath(exeDir), w, /*full=*/true, /*cwdFallback=*/true, err))
        std::fprintf(stderr, "[cfg] %s — configuration NOT saved\n", err.c_str());
}

#if defined(NEOST_WITH_IMGUI)
// ─────────────────────────────────────────────────────────────────────────────
// PROFILS DE RÉGLAGES NOMMÉS — dossier profiles/, un fichier .cfg par profil, au
// MÊME format que neost.cfg (cf. writeConfigKeys/parseConfigLine). neost.cfg reste
// la configuration COURANTE, écrite automatiquement à chaque changement ; un profil
// est une photo nommée qu'on rappelle plus tard (« 520 ST + TOS 1.02 + ma démo »).
// Charger un profil = repartir de la config courante et lui appliquer les lignes du
// fichier : ce qu'un profil ne dit pas ne change pas. Réservé à l'interface (le
// headless n'a pas de notion de profil) → sous garde ImGui pour ne pas laisser de
// fonctions inutilisées dans une compilation sans GUI.
// ─────────────────────────────────────────────────────────────────────────────
// Dossier des profils : à côté de neost.cfg. Quand ce dossier-là n'est pas inscriptible
// (installation en lecture seule), writeConfigAtomic replie neost.cfg sur le répertoire
// COURANT — on suit le même repli, sinon les profils seraient la seule chose cassée là où
// la configuration, elle, fonctionne. Résolu UNE fois, après la première écriture de
// neost.cfg (c'est elle qui tranche l'emplacement) : cf. l'appelant. Ne CRÉE rien ; seul
// l'enregistrement d'un profil crée le dossier.
static std::string profilesDir(const std::string& exeDir) {
    const std::string primary = exeDir + "/../profiles";
    std::error_code ec;
    if (fs::is_directory(primary, ec)) return primary;      // déjà utilisé : on y reste
    if (fs::exists(cfgPath(exeDir), ec)) return primary;    // neost.cfg est là → les profils aussi
    return "profiles";
}

// Nom saisi → nom de FICHIER sûr. Le champ est libre : sans ce filtre, « ../neost.cfg »
// ou un nom contenant « / » écrirait HORS du dossier des profils. On ne garde donc pas
// une liste blanche de lettres (elle mangerait les accents, « Démos » → « Dmos ») : on
// retire les séparateurs de chemin, les caractères de contrôle et les réservés Windows,
// puis les points et espaces de bord (« .. », fichiers cachés, noms refusés par Windows).
// Renvoie "" si rien d'utilisable ne reste — l'appelant refuse alors d'écrire.
static std::string profileFileName(const std::string& in) {
    static const std::string kBanned = "/\\:*?\"<>|";
    std::string out;
    for (unsigned char ch : in) {
        if (ch < 0x20 || ch == 0x7f) continue;
        if (kBanned.find(char(ch)) != std::string::npos) continue;
        out += char(ch);
    }
    while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
    while (!out.empty() && (out.back()  == ' ' || out.back()  == '.')) out.pop_back();
    if (out.size() > 64) out.resize(64);
    return out;
}

// Profils présents, triés sans tenir compte de la casse. Itération MANUELLE comme les
// pages Disquettes/Cartouche : l'incrément d'un range-for LANCE filesystem_error si le
// dossier devient illisible en cours de parcours, et rien ne l'attrape ici.
static std::vector<std::string> listProfiles(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        std::error_code ec2;
        if (it->is_regular_file(ec2) && it->path().extension() == ".cfg")
            names.push_back(it->path().stem().string());
        it.increment(ec);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](unsigned char x, unsigned char y) {
                                                return std::tolower(x) < std::tolower(y);
                                            });
    });
    return names;
}

static bool saveProfile(const std::string& dir, const std::string& name,
                        const Config& c, std::string& err) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) { err = "cannot create " + dir; return false; }
    return writeConfigAtomic(dir + "/" + name + ".cfg", c, /*full=*/false, /*cwdFallback=*/false, err);
}

// Applique le profil PAR-DESSUS `c` (déjà rempli avec la config courante) : les clés
// qu'un profil n'écrit pas (horloge, disposition de l'interface) restent telles quelles.
static bool loadProfileInto(const std::string& dir, const std::string& name, Config& c) {
    std::ifstream f(dir + "/" + name + ".cfg");
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) parseConfigLine(c, line);
    return true;
}

static bool deleteProfile(const std::string& dir, const std::string& name) {
    std::error_code ec;
    return fs::remove(fs::path(dir) / (name + ".cfg"), ec) && !ec;
}

#include "imgui.h"
#include "imgui_internal.h"   // gestionnaire de réglages personnalisé (ImGuiSettingsHandler)
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
// --- Pictogrammes Font Awesome 5 Free Solid (fonts/fa-solid-900.ttf, fusionnés dans
// la police ImGui — cf. chargement dans main()). Chaînes UTF-8 des codepoints FA de la
// zone à usage privé. À préfixer à un libellé : ICON_FA_REDO " Reset".
#define ICON_FA_STAR          "\xef\x80\x85"
#define ICON_FA_POWER_OFF     "\xef\x80\x91"
#define ICON_FA_REDO          "\xef\x80\x9e"
#define ICON_FA_VOLUME_OFF    "\xef\x80\xa6"
#define ICON_FA_VOLUME_DOWN   "\xef\x80\xa7"
#define ICON_FA_VOLUME_UP     "\xef\x80\xa8"
#define ICON_FA_VOLUME_MUTE   "\xef\x9a\xa9"
#define ICON_FA_ADJUST        "\xef\x81\x82"
#define ICON_FA_EJECT         "\xef\x81\x92"
#define ICON_FA_HDD           "\xef\x82\xa0"
#define ICON_FA_FOLDER_OPEN   "\xef\x81\xbc"
#define ICON_FA_SAVE          "\xef\x83\x87"
#define ICON_FA_BOLT          "\xef\x83\xa7"
#define ICON_FA_DESKTOP       "\xef\x84\x88"
#define ICON_FA_GAMEPAD       "\xef\x84\x9b"
#define ICON_FA_KEYBOARD      "\xef\x84\x9c"
#define ICON_FA_SERVER        "\xef\x88\xb3"
#define ICON_FA_CLONE         "\xef\x89\x8d"
#define ICON_FA_MICROCHIP     "\xef\x8b\x9b"
#define ICON_FA_SIGN_OUT_ALT  "\xef\x8b\xb5"
#define ICON_FA_COMPACT_DISC  "\xef\x94\x9f"
#define ICON_FA_MEMORY        "\xef\x94\xb8"
#define ICON_FA_PALETTE       "\xef\x94\xbf"
#define ICON_FA_TIMES         "\xef\x80\x8d"
#define ICON_FA_PLUS          "\xef\x81\xa7"
#define ICON_FA_BUG           "\xef\x86\x88"
#define ICON_FA_PLAY          "\xef\x81\x8b"
#define ICON_FA_PAUSE         "\xef\x81\x8c"
#define ICON_FA_STEP_FORWARD  "\xef\x81\x91"
#define ICON_FA_EXPAND        "\xef\x81\xa5"
// Engrenage U+F013 : la plage 0xf000-0xf8ff chargée dans la police le couvre déjà.
#define ICON_FA_COG           "\xef\x80\x93"

// Bouton à ICÔNE SEULE (le texte est superflu quand le pictogramme est explicite) :
// l'infobulle au survol rappelle l'action. Renvoie true au clic.
static bool IconButton(const char* icon, const char* tooltip) {
    const bool clicked = ImGui::Button(icon);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

// --- Persistance de la taille de la fenêtre PRINCIPALE (fenêtre GLFW) dans imgui.ini ---
// La fenêtre hôte n'est pas une fenêtre ImGui ; on enregistre donc sa taille via un
// gestionnaire de réglages ImGui personnalisé, qui écrit/relit une section
// « [NeoST][Window] Size=L,H » dans imgui.ini (à côté des positions des sous-fenêtres).
static GLFWwindow* g_window = nullptr;        // fenêtre hôte (pour interroger/poser sa taille)
static int  g_iniWinW = 0, g_iniWinH = 0;     // taille relue depuis imgui.ini
static bool g_iniWinValid = false;
// Disposition ancrée déjà SEMÉE au moins une fois. Persistée à côté de la taille de
// fenêtre (même section imgui.ini, où vit aussi la disposition des nœuds d'ancrage) :
// c'est le seul moyen de distinguer « première exécution » de « l'utilisateur a
// tout désancré exprès » — le nœud, lui, existe toujours dès le 1er DockSpace().
static bool g_dockSeeded = false;
// Géométrie FENÊTRÉE mémorisée (position + taille), tenue à jour hors kiosk. Sert à
// deux choses : restaurer la fenêtre en sortant du plein écran kiosk, et écrire dans
// imgui.ini la taille fenêtrée — jamais celle du plein écran, sinon un aller-retour
// kiosk laisserait la fenêtre à la taille de l'écran au prochain lancement.
static int g_winX = 0, g_winY = 0, g_winW = 1280, g_winH = 860;
// La géométrie ci-dessus a-t-elle été OBSERVÉE au moins une fois en mode fenêtré ?
// Un test sur (0,0) ne suffirait pas : une fenêtre légitimement placée à l'origine
// se ferait recentrer à chaque sortie de borne.
static bool g_winGeomValid = false;

static void* WinSettings_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/) {
    return (void*)1;                           // une seule entrée → on accepte toujours
}
static void WinSettings_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    int w = 0, h = 0, v = 0;
    if (std::sscanf(line, "Size=%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
        g_iniWinW = w; g_iniWinH = h; g_iniWinValid = true;
    }
    else if (std::sscanf(line, "DockSeeded=%d", &v) == 1) g_dockSeeded = (v != 0);
}
static void WinSettings_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*) {
    if (!g_iniWinValid) return;
    g_winW = g_iniWinW; g_winH = g_iniWinH;    // taille à retrouver en quittant le kiosk
    // En kiosk la fenêtre APPARTIENT au moniteur (plein écran exclusif) : la
    // redimensionner changerait le mode vidéo. On garde la taille pour plus tard.
    if (!g_kiosk && g_window) glfwSetWindowSize(g_window, g_iniWinW, g_iniWinH);
}
static void WinSettings_WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
    if (!g_window) return;
    int w = g_winW, h = g_winH;                // en kiosk : la dernière taille FENÊTRÉE
    if (!g_kiosk) glfwGetWindowSize(g_window, &w, &h);
    buf->appendf("[%s][Window]\n", handler->TypeName);
    buf->appendf("Size=%d,%d\n", w, h);
    buf->appendf("DockSeeded=%d\n\n", g_dockSeeded ? 1 : 0);
}
#endif

namespace {
// Signe des deltas souris → IKBD. Vérifié en headless (injection contrôlée) :
// paquet +dx = curseur à droite, paquet +dy = curseur vers le bas. GLFW donne
// les mêmes signes (origine en haut-gauche), donc identité sur les deux axes.
constexpr int MOUSE_X_SIGN = +1;
constexpr int MOUSE_Y_SIGN = +1;

Ikbd* g_ikbd = nullptr;                // cible des callbacks clavier/souris GLFW
bool  g_mouseCaptured = false;         // souris capturée → entrées dirigées vers le ST
bool  g_dbgMouse = false;              // NEOST_DEBUG_MOUSE=1 → trace les paquets souris
bool  g_dbgJoy = false;                // NEOST_DEBUG_JOY=1 → trace l'état brut des manettes
bool  g_kbdJoy = false;                // émulation joystick au clavier (flèches + Ctrl droit)
int   g_kbdJoyPort = 1;                // port ST visé par l'émulation clavier (0/1)
float g_joyDeadzone = 0.30f;           // zone morte centrale des sticks analogiques [0,0.95]
uint8_t g_lastJoy0 = 0, g_lastJoy1 = 0; // dernier octet composé posé sur l'IKBD (fenêtre Joystick)
// Affectation des manettes hôte aux ports ST, par GUID (stable au rebranchement —
// le jid GLFW peut changer). Absente de la table = AUTO. Éditée dans le menu kiosk
// « Joysticks », persistée dans neost.cfg (joymap=, cf. joymapParse/Serialize).
static std::map<std::string, int8_t> g_joyRoleByGuid;

// GUID d'une manette présente ("" sinon) — clé de persistance de son rôle.
static std::string joyGuid(int jid) {
    const char* g = glfwJoystickPresent(jid) ? glfwGetJoystickGUID(jid) : nullptr;
    return g ? g : "";
}
// Rôles EFFECTIFS par jid pour cette trame (consommés par stjoy::compose/composeAux
// et le menu kiosk) : table GUID→rôle appliquée aux manettes présentes.
static void joyResolveRoles(int8_t roles[GLFW_JOYSTICK_LAST + 1]) {
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        roles[jid] = stjoy::ROLE_AUTO;
        if (!glfwJoystickPresent(jid)) continue;
        const auto it = g_joyRoleByGuid.find(joyGuid(jid));
        if (it != g_joyRoleByGuid.end()) roles[jid] = it->second;
    }
}
// joymap= "guid:rôle,guid:rôle" avec rôle ∈ {0, 1, x} — cf. Config::joymap.
static void joymapParse(const std::string& s) {
    g_joyRoleByGuid.clear();
    std::size_t p = 0;
    while (p < s.size()) {
        std::size_t e = s.find(',', p);
        if (e == std::string::npos) e = s.size();
        const std::string item = s.substr(p, e - p);
        const std::size_t c = item.rfind(':');
        if (c != std::string::npos && c > 0 && c + 1 < item.size()) {
            const char r = item[c + 1];
            if      (r == '0') g_joyRoleByGuid[item.substr(0, c)] = stjoy::ROLE_PORT0;
            else if (r == '1') g_joyRoleByGuid[item.substr(0, c)] = stjoy::ROLE_PORT1;
            else if (r == 'x') g_joyRoleByGuid[item.substr(0, c)] = stjoy::ROLE_OFF;
        }
        p = e + 1;
    }
}
static std::string joymapSerialize() {
    std::string s;
    for (const auto& [guid, role] : g_joyRoleByGuid) {
        if (role == stjoy::ROLE_AUTO) continue;   // AUTO = absent de la liste
        if (!s.empty()) s += ',';
        s += guid;
        s += ':';
        s += (role == stjoy::ROLE_PORT0) ? '0' : (role == stjoy::ROLE_PORT1) ? '1' : 'x';
    }
    return s;
}
bool  g_showHex = true, g_showCpu = true;   // fenêtres d'inspection masquables
bool  g_showJoy = false;               // fenêtre joystick (visualisation live)
bool  g_showCfg = true;                // fenêtre Configuration (tout se règle là)
std::vector<std::string> g_dropped;    // chemins glissés-déposés, consommés dans la boucle

// --- Effets CRT (façade moniteur) : passe FBO shader appliquée à l'écran ST.
// Opt-in, à échec gracieux (cf. gui/CrtEffectStack). En kiosk la config est
// figée → g_crtOn / g_crtParams viennent du neost.cfg (ou de --crt/--crt-preset).
static neost::CrtEffectStack g_crt;
static neost::CrtParams      g_crtParams;
static bool g_crtOn   = false;         // effets CRT activés
static bool g_crtInit = false;         // initialize() déjà tenté (une seule fois)
static bool g_showCrt = false;         // fenêtre de réglages CRT visible (fenêtré)
// --- Débogueur (fenêtré) : breakpoints PC + pause/continue/step-frame -------------
static bool g_showDbg     = false;     // fenêtre « Débogueur » visible
static bool g_dbgPaused   = false;     // émulation gelée (breakpoint atteint ou pause manuelle)
static bool g_dbgStepFrame = false;    // requête « avancer d'une trame » (traitée dans la boucle)
static bool g_dbgStepInstr = false;    // requête « avancer d'une instruction » (idem)
static SymbolTable g_symbols;          // table de symboles (noms ↔ adresses) du débogueur
// --- Ancrage (docking) : les fenêtres de debug deviennent des ONGLETS d'une
// disposition persistante au lieu d'une pile de fenêtres qui se recouvrent.
// Repris de POM2 (MainWindow::renderDockSpace / applyDockLayout). Exige la branche
// `docking` de Dear ImGui (IMGUI_HAS_DOCK) — cf. extern/imgui, sous-module épinglé.
// Le #ifdef laisse le code compiler tel quel avec un imgui de la branche master.
static bool    g_dockOn    = true;     // mode ancré actif (persisté : neost.cfg dock=)
static bool    g_dockReset = false;    // requête « réinitialiser la disposition »
#ifdef IMGUI_HAS_DOCK
static ImGuiID g_dockId    = 0;        // identifiant du nœud racine du dockspace
#endif
// Save-state rapide (F5 sauver / F7 charger, slot fichier unique neost.state).
static std::string g_stateMsg;         // message transitoire affiché en overlay
static int         g_stateMsgFrames = 0;
bool  g_joyCfgDirty = false;           // un réglage joystick a changé → resauver neost.cfg
// Champs de saisie du menu Machine → Disque dur (dossier HD GEMDOS / image ACSI).
// Globaux (et non statiques du menu) pour que le sous-menu Profils puisse les
// resynchroniser : g_*Init = false → relecture de cfg à la prochaine ouverture.
char g_gdBuf[512] = {0}, g_hdBuf[512] = {0};

void onGlfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// Callback bouton souris : ÉVÉNEMENTIEL (capte chaque transition, même un
// double-clic rapide qu'une scrutation par trame manquerait). Envoie un paquet
// IKBD sans mouvement portant l'état courant des boutons.
void onMouseButton(GLFWwindow* w, int /*button*/, int /*action*/, int /*mods*/) {
    if (!g_ikbd || !g_mouseCaptured) return;
    const bool l = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    const bool r = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (g_dbgMouse) std::fprintf(stderr, "[mouse] button  L=%d R=%d\n", l, r);
    g_ikbd->mouseEvent(0, 0, l, r);
}

// --- Écran ST : téléverse le framebuffer ARGB du Shifter dans une texture GL.
//  L'affichage se fait ensuite dans une fenêtre ImGui "Atari ST Screen" (via
//  ImGui::Image) ; en l'absence d'ImGui, on retombe sur un quad plein cadre.
struct GlScreen {
    GLuint tex = 0;
    int w = 0, h = 0;

    void init() {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    void update(const uint32_t* px, int pw, int ph) {
        glBindTexture(GL_TEXTURE_2D, tex);
        if (pw != w || ph != h) {           // la résolution ST a changé → réalloue
            w = pw; h = ph;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                         GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
        }
    }
    void drawFullscreen() { blitTexFullscreen(tex); }   // repli sans ImGui

    // Quad plein écran d'une texture arbitraire (V inversé : ligne 0 en haut).
    // Static pour être partagé par le blit brut et le blit post-CRT.
    static void blitTexFullscreen(GLuint t) {
        glBindTexture(GL_TEXTURE_2D, t);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
            glTexCoord2f(0.f, 1.f); glVertex2f(-1.f, -1.f);
            glTexCoord2f(1.f, 1.f); glVertex2f( 1.f, -1.f);
            glTexCoord2f(1.f, 0.f); glVertex2f( 1.f,  1.f);
            glTexCoord2f(0.f, 0.f); glVertex2f(-1.f,  1.f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
};

// Applique la passe d'effets CRT si activée. Renvoie la texture à afficher :
// l'écran ST brut (s.tex) si les effets sont off, indisponibles (échec shader /
// contexte 2.1 macOS) ou si process() échoue — passthrough sans surprise.
// dstW×dstH = taille écran cible (pilote l'anti-alias analytique scanline/masque).
static GLuint crtApply(const GlScreen& s, int dstW, int dstH) {
    if (!g_crtOn || s.tex == 0) return s.tex;
    if (!g_crt.available()) {
        if (g_crtInit) return s.tex;        // déjà tenté et échoué → brut
        g_crtInit = true;
        if (!g_crt.initialize()) return s.tex;
    }
    g_crt.setParams(g_crtParams);
    const GLuint out = g_crt.process(s.tex, s.w, s.h, dstW, dstH);
    return out ? out : s.tex;
}

// Presets CRT nommés (kiosk / --crt-preset / neost.cfg). Renseigne `p` et `on`.
// Un preset n'est qu'un point de départ : le panneau de réglage peut ensuite
// tout ajuster, et les valeurs numériques figées écrasent le nom au save.
// Renvoie false si le nom est inconnu (params laissés intacts).
static bool applyCrtPreset(const std::string& name, neost::CrtParams& p, bool& on) {
    using SM = neost::CrtParams::ShadowMask;
    if (name == "off") { on = false; return true; }
    neost::CrtParams q{};   // défauts neutres
    if (name == "leger" || name == "light") {
        q.scanlines = 0.18f; q.barrel = 0.03f; q.persistence = 0.20f;
        q.luminanceGain = 1.10f; q.centerLighting = 0.96f;
    } else if (name == "arcade") {
        q.scanlines = 0.45f; q.barrel = 0.12f; q.persistence = 0.35f;
        q.shadowMask = SM::Triad; q.shadowMaskStrength = 0.60f;
        q.luminanceGain = 1.50f; q.centerLighting = 0.82f; q.phosphorGamma = 1.30f;
    } else if (name == "phosphor" || name == "phosphore") {
        q.scanlines = 0.30f; q.barrel = 0.08f; q.persistence = 0.60f;
        q.shadowMask = SM::Aperture; q.shadowMaskStrength = 0.40f;
        q.luminanceGain = 1.35f; q.centerLighting = 0.88f; q.phosphorGamma = 1.50f;
    } else {
        return false;   // nom inconnu
    }
    p = q; on = true;
    return true;
}

// Région de CONTENU de la trame courante, en lignes du buffer ST — le cœur du zoom
// adaptatif, PARTAGÉ par le kiosk (qui la cale en viewport GL) et par la fenêtre
// « Atari ST Screen » du bureau (qui la cale en UV d'image). Deux cadrages francs,
// jamais au pixel (→ zéro saccade) :
//  · Défaut (99 % des jeux) : cadre FIXE sur la ZONE ACTIVE (rectangle net donné par
//    le matériel — activeTop/activeHeight), qui ne bouge JAMAIS. Un champ d'étoiles,
//    un fond noir : rien ne fait « respirer » le zoom.
//  · Overscan (démos, ouvertures de bordures — Enchanted Land, Lethal Xcess) : quand
//    la Glue signale une bordure retirée, on montre le BUFFER ENTIER. Hystérésis
//    (latch) pour ne pas basculer sur un retrait d'une seule trame.
// Les latches sont des statiques de fonction : un seul jeu pour toute l'application,
// donc un aller-retour bureau ⇄ kiosk ne réinitialise pas l'hystérésis en cours.
// cW = largeur (en px du buffer) qui doit RESTER visible : la zone active seule en
// régime normal (les bordures latérales sont du décor, rognables), le buffer entier
// dès qu'une bordure est ouverte (l'image déborde alors DANS les bordures). Le
// cadrage du bureau s'en sert comme plancher : il n'ampute jamais l'image elle-même.
static void stContentRegion(Machine& machine, int& cTop, int& cH, int& cW) {
    static int overscanLatch     = 0;   // bordure ouverte (haut ou bas)
    static int fullOverscanLatch = 0;   // bordure BASSE retirée (démos full-overscan)
    cW = std::max(1, machine.shifter.activeWidth());   // défaut : la zone active
    // snapBordersOpen/snapLiveTop/snapLiveHeight : snapshot capturé à finishFrame(),
    // stable au rendu (les champs live glueStartHBL_/glueEndHBL_ sont remis à zéro
    // par beginFrame_() du cycle suivant AVANT que le rendu GL ne s'exécute).
    if (machine.shifter.snapBordersOpen()) overscanLatch = 30;   // ~0,6 s de maintien
    else if (overscanLatch > 0)             --overscanLatch;
    if (overscanLatch == 0) {
        fullOverscanLatch = 0;              // plus de trick : reset du second latch
        cTop = machine.shifter.activeTop();
        cH   = machine.shifter.activeHeight();
        return;
    }
    // Second latch : détecte une bordure BASSE retirée (LX, Cuddly…) et latche ce
    // constat pour éviter les basculements frame-à-frame.
    if (machine.shifter.snapLiveHeight() + machine.shifter.snapLiveTop()
            > machine.shifter.activeHeight() + machine.shifter.activeTop())
        fullOverscanLatch = 30;
    else if (fullOverscanLatch > 0)
        --fullOverscanLatch;

    // Bordure ouverte : l'image occupe aussi les bordures latérales → tout le buffer
    // devient du contenu, y compris en largeur.
    cW = std::max(1, machine.shifter.width());
    if (fullOverscanLatch > 0) {
        // Bordure BASSE retirée (démos full-overscan : Cuddly, LX…) : buffer entier.
        cTop = 0;
        cH   = machine.shifter.height();
    } else {
        // Bordure HAUTE seule (ex. Enchanted Land en jeu) : même zoom que la zone
        // active, légèrement remontée pour montrer 2 lignes overscan en haut.
        cTop = std::max(0, machine.shifter.activeTop() - 2);
        cH   = machine.shifter.activeHeight();
    }
}

// Rendu kiosk ADAPTATIF : on cale la région de contenu [cTop, cTop+cH) sur la
// HAUTEUR de l'écran (ratio pixel gardé). Contenu court → gros zoom, les bordures
// inutilisées débordent hors écran (rognées) → l'image remplit l'écran, peu de
// bandes noires. Contenu plein-cadre/overscan → tient entier (pillarbox latéral).
void drawStKiosk(GlScreen& s, int fbw, int fbh, int cTop, int cH) {
    if (s.w <= 0 || s.h <= 0 || fbw <= 0 || fbh <= 0 || cH <= 0) return;
    // Effets CRT « cadre complet » (v1) : la passe traite tout le buffer ST à la
    // résolution écran (fbw×fbh, bornée), puis le zoom kiosk (viewport ci-dessous)
    // cadre/rogne le résultat comme pour la texture brute. Le cadrage du quad
    // reposant sur les UV (0..1 = cadre entier), la taille FBO ne change PAS le
    // cadrage — juste la finesse d'anti-alias. Baril/vignette encadrent donc tout
    // le cadre ST (bords courbés rognés hors écran en zoom fort — assumé v1).
    // Aspect pixel : basse rés (≤480 px de large) et 200 lignes = pixels doublés.
    const float sx = (s.w <= 480) ? 2.f : 1.f;
    const float sy = (s.h <= 300) ? 2.f : 1.f;
    const float scale = (float)fbh / (cH * sy);        // px écran par px ST logique (vertical)
    const float vw = s.w * sx * scale, vh = s.h * sy * scale;   // cadre COMPLET à cette échelle
    // Passe CRT demandée à la taille du CADRE ENTIER À CE ZOOM, pas à celle de l'écran :
    // le viewport ci-dessous étire ensuite le résultat d'un facteur s.h/cH, et un FBO
    // calé sur l'écran voyait donc son masque triade et ses scanlines — calculés
    // analytiquement pour un pas de 1 px écran — magnifiés d'autant, d'où moiré et
    // perte d'alignement sur la grille du moniteur. C'est exactement la correction
    // déjà appliquée au cadrage du bureau (cf. drawStScreen) ; les deux moitiés du
    // zoom adaptatif sont maintenant cohérentes.
    const GLuint t = crtApply(s, std::max(1, (int)std::lround(vw)),
                                 std::max(1, (int)std::lround(vh)));
    const float cc = cTop + cH / 2.0f;                 // ligne ST au centre du contenu
    const float vy = fbh / 2.0f - vh * (1.0f - cc / s.h);       // centre le contenu à l'écran
    const float vx = (fbw - vw) / 2.0f;
    glViewport((int)std::lround(vx), (int)std::lround(vy),
               (int)std::lround(vw), (int)std::lround(vh));
    GlScreen::blitTexFullscreen(t);                    // le buffer déborde → GL rogne les bordures
}

// Traduit une touche GLFW en scancode "make" du clavier Atari ST (0 = ignorée).
// Les scancodes ST suivent la matrice de l'IKBD, pas l'ASCII (cf. doc Atari).
uint8_t glfwToStScancode(int key) {
    switch (key) {
        case GLFW_KEY_ESCAPE: return 0x01;
        case GLFW_KEY_1: return 0x02; case GLFW_KEY_2: return 0x03;
        case GLFW_KEY_3: return 0x04; case GLFW_KEY_4: return 0x05;
        case GLFW_KEY_5: return 0x06; case GLFW_KEY_6: return 0x07;
        case GLFW_KEY_7: return 0x08; case GLFW_KEY_8: return 0x09;
        case GLFW_KEY_9: return 0x0A; case GLFW_KEY_0: return 0x0B;
        case GLFW_KEY_MINUS: return 0x0C; case GLFW_KEY_EQUAL: return 0x0D;
        case GLFW_KEY_BACKSPACE: return 0x0E; case GLFW_KEY_TAB: return 0x0F;
        case GLFW_KEY_Q: return 0x10; case GLFW_KEY_W: return 0x11;
        case GLFW_KEY_E: return 0x12; case GLFW_KEY_R: return 0x13;
        case GLFW_KEY_T: return 0x14; case GLFW_KEY_Y: return 0x15;
        case GLFW_KEY_U: return 0x16; case GLFW_KEY_I: return 0x17;
        case GLFW_KEY_O: return 0x18; case GLFW_KEY_P: return 0x19;
        case GLFW_KEY_LEFT_BRACKET: return 0x1A; case GLFW_KEY_RIGHT_BRACKET: return 0x1B;
        case GLFW_KEY_ENTER: return 0x1C; case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return 0x1D;
        case GLFW_KEY_A: return 0x1E; case GLFW_KEY_S: return 0x1F;
        case GLFW_KEY_D: return 0x20; case GLFW_KEY_F: return 0x21;
        case GLFW_KEY_G: return 0x22; case GLFW_KEY_H: return 0x23;
        case GLFW_KEY_J: return 0x24; case GLFW_KEY_K: return 0x25;
        case GLFW_KEY_L: return 0x26; case GLFW_KEY_SEMICOLON: return 0x27;
        case GLFW_KEY_APOSTROPHE: return 0x28; case GLFW_KEY_GRAVE_ACCENT: return 0x29;
        case GLFW_KEY_LEFT_SHIFT: return 0x2A; case GLFW_KEY_BACKSLASH: return 0x2B;
        case GLFW_KEY_Z: return 0x2C; case GLFW_KEY_X: return 0x2D;
        case GLFW_KEY_C: return 0x2E; case GLFW_KEY_V: return 0x2F;
        case GLFW_KEY_B: return 0x30; case GLFW_KEY_N: return 0x31;
        case GLFW_KEY_M: return 0x32; case GLFW_KEY_COMMA: return 0x33;
        case GLFW_KEY_PERIOD: return 0x34; case GLFW_KEY_SLASH: return 0x35;
        case GLFW_KEY_RIGHT_SHIFT: return 0x36; case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT: return 0x38; case GLFW_KEY_SPACE: return 0x39;
        case GLFW_KEY_CAPS_LOCK: return 0x3A;
        case GLFW_KEY_F1: return 0x3B; case GLFW_KEY_F2: return 0x3C;
        case GLFW_KEY_F3: return 0x3D; case GLFW_KEY_F4: return 0x3E;
        case GLFW_KEY_F5: return 0x3F; case GLFW_KEY_F6: return 0x40;
        case GLFW_KEY_F7: return 0x41; case GLFW_KEY_F8: return 0x42;
        case GLFW_KEY_F9: return 0x43; case GLFW_KEY_F10: return 0x44;
        case GLFW_KEY_HOME: return 0x47;
        case GLFW_KEY_UP: return 0x48; case GLFW_KEY_LEFT: return 0x4B;
        case GLFW_KEY_RIGHT: return 0x4D; case GLFW_KEY_DOWN: return 0x50;
        case GLFW_KEY_INSERT: return 0x52; case GLFW_KEY_DELETE: return 0x53;
        // Touches spécifiques ST sans équivalent direct (mapping Hatari sdl/keymap.c) :
        // Help/Undo + parenthèses du pavé numérique ST.
        case GLFW_KEY_PRINT_SCREEN: return 0x62;            // Help
        case GLFW_KEY_END: return 0x61;                     // Undo
        case GLFW_KEY_PAGE_UP: return 0x63;                 // ( pavé num. ST
        case GLFW_KEY_PAGE_DOWN: return 0x64;               // ) pavé num. ST
        // Pavé numérique (scancodes ST 0x65-0x72 + 0x4A/0x4E, cf. Hatari).
        case GLFW_KEY_KP_0: return 0x70; case GLFW_KEY_KP_1: return 0x6D;
        case GLFW_KEY_KP_2: return 0x6E; case GLFW_KEY_KP_3: return 0x6F;
        case GLFW_KEY_KP_4: return 0x6A; case GLFW_KEY_KP_5: return 0x6B;
        case GLFW_KEY_KP_6: return 0x6C; case GLFW_KEY_KP_7: return 0x67;
        case GLFW_KEY_KP_8: return 0x68; case GLFW_KEY_KP_9: return 0x69;
        case GLFW_KEY_KP_DECIMAL: return 0x71;
        case GLFW_KEY_KP_DIVIDE: return 0x65;
        case GLFW_KEY_KP_MULTIPLY: return 0x66;
        case GLFW_KEY_KP_SUBTRACT: return 0x4A;
        case GLFW_KEY_KP_ADD: return 0x4E;
        case GLFW_KEY_KP_ENTER: return 0x72;
        case GLFW_KEY_KP_EQUAL: return 0x61;                // Undo (comme Hatari)
        default: return 0x00;
    }
}

// -----------------------------------------------------------------------------
//  Keymap international — port de Hatari sdl/keymap.c (mapping SYMBOLIQUE).
//
//  Le mapping positionnel ci-dessus (glfwToStScancode) suppose un hôte ET un TOS
//  QWERTY US. Hatari mappe d'abord par le CARACTÈRE que la touche produit sur la
//  disposition HÔTE (SDL keysym ↔ ici glfwGetKeyName), à travers une table par
//  défaut + des surcharges selon le PAYS du TOS chargé (Keymap_SetCountry, pays
//  lu dans l'en-tête ROM, mot os_conf à $1C >> 1). Ainsi un hôte AZERTY tape « a »
//  → scancode ST 0x10 (position du A sur un clavier ST français) quand un TOS FR
//  est chargé, et l'hôte QWERTY sous TOS FR obtient aussi les bons caractères.
//  Les touches NON imprimables (Entrée, flèches, F1-F10, pavé…) restent sur le
//  mapping positionnel. L'AUTOREPEAT, lui, est déjà conforme : les GLFW_REPEAT
//  sont ignorés (cf. onKey) — sur le vrai ST, c'est le TOS qui répète (l'IKBD
//  n'émet qu'un make par appui), comme Hatari.
// -----------------------------------------------------------------------------
// Pays du TOS chargé (codes Hatari tos.h : 0=US, 1=DE, 2=FR, 3=UK… 127=EmuTOS
// multilangue → table par défaut). -1 tant qu'aucune ROM n'est chargée.
int g_kbdCountry = -1;

// Premier point de code Unicode d'une chaîne UTF-8 (les noms de touches GLFW
// sont en UTF-8 : « é », « ù », « § »… sur les claviers nationaux).
uint32_t utf8First(const char* s) {
    const auto* u = reinterpret_cast<const unsigned char*>(s);
    if (u[0] < 0x80) return u[0];
    if ((u[0] & 0xE0) == 0xC0 && u[1]) return uint32_t(u[0] & 0x1F) << 6 | (u[1] & 0x3F);
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2])
        return uint32_t(u[0] & 0x0F) << 12 | uint32_t(u[1] & 0x3F) << 6 | (u[2] & 0x3F);
    return 0;
}

// Table SYMBOLIQUE par défaut (port de Keymap_SymbolicToStScanCode_default,
// partie imprimable — le reste passe par le mapping positionnel). 0xFF = pas de
// correspondance symbolique → repli positionnel.
uint8_t symbolicDefault(uint32_t cp) {
    switch (cp) {
        case '!':  return 0x09;   // hôte azerty
        case '"':  return 0x04;
        case '#':  return 0x2B;   // hôte DE/UK, pour TOS FR/UK/DK/NL
        case '$':  return 0x1B;
        case '&':  return 0x02;
        case '\'': return 0x28;
        case '(':  return 0x63;   // ( pavé num. ST
        case ')':  return 0x64;
        case '*':  return 0x66;
        case '+':  return 0x4E;
        case ',':  return 0x33;
        case '-':  return 0x35;   // défaut DE/IT/SE/CH/FI/NO/DK/CZ
        case '.':  return 0x34;
        case '/':  return 0x35;
        case '0':  return 0x0B;
        case '1':  return 0x02; case '2': return 0x03; case '3': return 0x04;
        case '4':  return 0x05; case '5': return 0x06; case '6': return 0x07;
        case '7':  return 0x08; case '8': return 0x09; case '9': return 0x0A;
        case ':':  return 0x34;
        case ';':  return 0x27;
        case '<':  return 0x60;
        case '=':  return 0x0D;
        case '>':  return 0x34;
        case '?':  return 0x35;
        case '@':  return 0x28;
        case '[':  return 0x1A;
        case '\\': return 0x2B;
        case ']':  return 0x1B;
        case '^':  return 0x2B;
        case '_':  return 0x0C;
        case '`':  return 0x29;
        case 'a':  return 0x1E; case 'b': return 0x30; case 'c': return 0x2E;
        case 'd':  return 0x20; case 'e': return 0x12; case 'f': return 0x21;
        case 'g':  return 0x22; case 'h': return 0x23; case 'i': return 0x17;
        case 'j':  return 0x24; case 'k': return 0x25; case 'l': return 0x26;
        case 'm':  return 0x32; case 'n': return 0x31; case 'o': return 0x18;
        case 'p':  return 0x19; case 'q': return 0x10; case 'r': return 0x13;
        case 's':  return 0x1F; case 't': return 0x14; case 'u': return 0x16;
        case 'v':  return 0x2F; case 'w': return 0x11; case 'x': return 0x2D;
        case 'y':  return 0x15; case 'z': return 0x2C;
        // Lettres nationales (latin-1+, mêmes valeurs que Hatari) :
        case 167:  return 0x29;   // § suisse
        case 168:  return 0x1B;   // ¨ suisse
        case 176:  return 0x35;   // ° espagnol
        case 178:  return 0x29;   // ² français
        case 180:  return 0x0D;   // ´ allemand
        case 223:  return 0x0C;   // ß allemand
        case 224:  return 0x0B;   // à français
        case 225:  return 0x09;   // á tchèque
        case 228:  return 0x28;   // ä allemand
        case 229:  return 0x1A;   // å suédois
        case 231:  return 0x0A;   // ç français
        case 232:  return 0x08;   // è français
        case 233:  return 0x03;   // é français
        case 236:  return 0x0D;   // ì italien
        case 237:  return 0x0A;   // í tchèque
        case 241:  return 0x27;   // ñ espagnol
        case 242:  return 0x27;   // ò italien
        case 243:  return 0x02;   // ó tchèque
        case 246:  return 0x27;   // ö allemand
        case 249:  return 0x28;   // ù français
        case 250:  return 0x1A;   // ú tchèque
        case 252:  return 0x1A;   // ü allemand
        case 253:  return 0x08;   // ý tchèque
        default:   return 0xFF;
    }
}

// Surcharges par pays du TOS (ports de Keymap_SymbolicToStScanCode_US/DE/FR/UK).
// Les autres pays Hatari (ES/IT/SE/CH/NO/DK/NL/CZ) retombent sur la table par
// défaut — leurs lettres nationales y sont déjà.
uint8_t symbolicForCountry(uint32_t cp) {
    switch (g_kbdCountry) {
        case 0:   // TOS US
            if (cp == '-') return 0x0C;
            break;
        case 1:   // TOS allemand (QWERTZ : y/z croisés, # + / déplacés)
            switch (cp) {
                case '#': return 0x29; case '+': return 0x1B; case '/': return 0x65;
                case 'y': return 0x2C; case 'z': return 0x15;
            }
            break;
        case 2:   // TOS français (AZERTY : a/q, z/w, m, ponctuation déplacée)
            switch (cp) {
                case '\'': return 0x05; case '(': return 0x06; case ')': return 0x0C;
                case ',':  return 0x32; case '-': return 0x0D; case ';': return 0x33;
                case '=':  return 0x35; case '^': return 0x1A;
                case 'a':  return 0x10; case 'm': return 0x27; case 'q': return 0x1E;
                case 'w':  return 0x2C; case 'z': return 0x11;
                case 167:  return 0x07;   // §
            }
            break;
        case 3:   // TOS UK
            if (cp == '-')  return 0x0C;
            if (cp == '\\') return 0x60;
            break;
    }
    return symbolicDefault(cp);
}

// Scancode ST d'une touche GLFW : d'abord le SYMBOLIQUE (touches imprimables,
// caractère donné par la disposition hôte via glfwGetKeyName), sinon repli
// POSITIONNEL (fonctions, flèches, pavé, modificateurs — et touches sans nom).
uint8_t stScancodeFor(int key, int scancode) {
    if (key >= GLFW_KEY_SPACE && key < GLFW_KEY_ESCAPE) {        // plage « imprimable »
        const char* name = glfwGetKeyName(key, scancode);
        if (name && name[0]) {
            const uint8_t sc = symbolicForCountry(utf8First(name));
            if (sc != 0xFF) return sc;
        }
    }
    return glfwToStScancode(key);
}

// Lit le pays du TOS chargé dans son en-tête ROM (mot os_conf à $1C, pays =
// os_conf >> 1 — cf. Hatari tos.c) et arme les surcharges symboliques.
void updateKbdCountry(const std::vector<uint8_t>& rom) {
    if (rom.size() < 0x1E) { g_kbdCountry = -1; return; }
    g_kbdCountry = ((rom[0x1C] << 8) | rom[0x1D]) >> 1;
    static const char* names[] = {"US", "DE", "FR", "UK"};
    std::fprintf(stderr, "[kbd] TOS layout: %s (symbolic mapping)\n",
                 g_kbdCountry >= 0 && g_kbdCountry <= 3 ? names[g_kbdCountry]
                 : g_kbdCountry == 127 ? "multilingual (default)" : "other (default)");
}

// Callback clavier GLFW → IKBD. La touche Suppr (DEL) est réservée à l'hôte (elle
// libère la souris capturée), donc jamais transmise au ST. Échap, lui, est bien
// envoyé au ST (beaucoup de jeux/applications s'en servent).
void onKey(GLFWwindow*, int key, int scancode, int action, int /*mods*/) {
    if (!g_ikbd || action == GLFW_REPEAT) return;   // TOS gère sa propre répétition (pas l'IKBD)
    if (key == GLFW_KEY_DELETE) return;             // touche hôte (libération souris)
    // Touches réservées HÔTE : F5/F7 (save-state), F8 (bascule GUI ⇄ kiosk),
    // F11 (bascule joystick clavier), + F9/F10 en kiosk (menu disques, zoom). Sans
    // cette exclusion, le ST recevait la touche F5/F7 EN MÊME TEMPS que l'état était
    // écrasé/rechargé sous ses pieds (beaucoup de jeux/GEM mappent les touches de fonction).
    // F8 = bascule borne ⇄ bureau, traitée ICI (dans le callback) et non par scrutation :
    // un appui bref peut être posé ET relâché entre deux tours de boucle — glfwGetKey ne
    // verrait alors jamais l'état PRESS. La demande est appliquée en tête de boucle.
    if (key == GLFW_KEY_F8) {
        if (action == GLFW_PRESS) g_kioskSwitchReq = g_kiosk ? 2 : 1;
        return;
    }
    if (key == GLFW_KEY_F5 || key == GLFW_KEY_F7 || key == GLFW_KEY_F11) return;
    // F9/F10/F12 sont des raccourcis HÔTE du kiosk : ils ne partent pas au ST. K a été
    // ABANDONNÉ comme raccourci — c'est une lettre, donc du jeu (taper ses initiales dans
    // une table des scores ouvrait le bandeau clavier) ; l'intercepter privait en plus le
    // ST du K sans même supprimer l'ouverture parasite, qui venait de la scrutation.
    if (g_kiosk && (key == GLFW_KEY_F9 || key == GLFW_KEY_F10 || key == GLFW_KEY_F12)) return;
    const uint8_t sc = stScancodeFor(key, scancode);   // symbolique (layout hôte + pays TOS) → positionnel
    if (!sc) return;
    // Suivi des touches dont le MAKE a été transmis au ST : leur BREAK doit
    // TOUJOURS partir, même si entre-temps un widget ImGui a pris le focus ou
    // que l'émulation joystick a été (dés)activée. Sinon la touche reste
    // « collée » côté ST (make sans break) et le clavier semble en panne.
    static bool stHeld[128] = {};
    if (action != GLFW_PRESS) {
        if (stHeld[sc & 0x7F]) {
            stHeld[sc & 0x7F] = false;
            g_ikbd->keyEvent(sc, false);
        }
        return;
    }
#if defined(NEOST_WITH_IMGUI)
    // On ne cède le clavier à ImGui (saisie d'un champ) QUE hors capture souris :
    // souris capturée = l'utilisateur « est dans » le ST, les touches (espace
    // inclus) doivent toujours l'atteindre, jamais être avalées par un widget
    // resté focalisé (sinon le clavier ST « se déconnecte »).
    if (!g_mouseCaptured && ImGui::GetIO().WantCaptureKeyboard) return;
#endif
    // Émulation joystick clavier active : les touches du joystick (flèches + Ctrl
    // droit) pilotent la manette et NE sont PAS transmises au clavier ST (sinon
    // double effet) ; elles sont scrutées par trame dans la boucle (cf. stjoy::compose).
    if (g_kbdJoy && stjoy::kbdBit(key)) return;
    // Overlay kiosk de choix de disquette ouvert : le clavier pilote l'overlay
    // (flèches/Entrée/Échap), on ne transmet PAS le MAKE au ST. Les BREAK des
    // touches déjà tenues sont gérés plus haut → pas de touche « collée ».
    if (g_kioskDiskMenu) return;
    stHeld[sc & 0x7F] = true;
    g_ikbd->keyEvent(sc, true);
}

#if defined(NEOST_WITH_IMGUI)
void drawHexViewer(Bus& bus) {
    static int base = 0;
    ImGui::Begin("Memory (hex)");
    ImGui::InputInt("Base address", &base, 16, 256,
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    // Le champ ne doit pas confisquer durablement le clavier du ST : dès qu'il
    // perd l'édition (Entrée/Échap/clic ailleurs), on relâche le focus fenêtre
    // pour que WantCaptureKeyboard retombe et que les touches (espace inclus)
    // repartent vers le ST. Sinon le clavier ST « se déconnecte » tant que ce
    // champ garde le focus.
    if (ImGui::IsItemDeactivated())
        ImGui::SetWindowFocus(nullptr);
    if (base < 0) base = 0;
    const auto& mem = bus.ram;
    // Clamp HAUT aussi : saisir $7FFFFFFF ferait déborder base + row*16 (UB signé,
    // adresse négative qui repasse la garde `< mem.size()` → lecture hors bornes).
    if (base > (int)mem.size()) base = (int)mem.size();
    for (int row = 0; row < 16; ++row) {
        const int addr = base + row * 16;
        char line[128];
        int n = std::snprintf(line, sizeof line, "%06X:", addr);
        for (int col = 0; col < 16 && (addr + col) < (int)mem.size(); ++col)
            n += std::snprintf(line + n, sizeof line - n, " %02X", mem[addr + col]);
        ImGui::TextUnformatted(line);
    }
    ImGui::End();
}

// reqReset passe à true si le bouton RESET est cliqué.
void drawCpuState(Cpu68k& cpu, bool& reqReset) {
    ImGui::Begin("CPU 68000");
    if (IconButton(ICON_FA_POWER_OFF, "Reset (hardware RESET)")) reqReset = true;
    ImGui::Separator();
    ImGui::Text("PC = %08X    SR = %04X", cpu.pc(), cpu.sr());
    ImGui::Separator();
    for (int i = 0; i < 8; ++i)
        ImGui::Text("D%d=%08X   A%d=%08X", i, cpu.reg(i), i, cpu.reg(i + 8));
    ImGui::End();
}

// Fenêtre Joystick : visualisation LIVE de ce que voit l'hôte et de ce qui est
// réellement envoyé au ST. Affiche, pour chaque manette présente, le nom, si elle
// est reconnue « gamepad » (mapping SDL), ses axes (bruts + gamepad) sous forme de
// barres avec la zone morte, ses boutons et son hat ; puis l'octet ST composé pour
// chaque port avec les 5 bits décodés. Inclut les réglages (émulation clavier,
// port, zone morte) modifiables ici. lastJoy0/1 = ce qui a été posé sur l'IKBD.
void drawJoystickAxisBar(const char* label, float v, float dz) {
    // v ∈ [-1,1] → barre [0,1] ; coloration si |v| dépasse la zone morte.
    const float frac = (v + 1.0f) * 0.5f;
    const bool active = (v < -dz) || (v > dz);
    if (active) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.80f, 0.30f, 1.0f));
    char buf[32]; std::snprintf(buf, sizeof buf, "%+.2f", v);
    ImGui::ProgressBar(frac, ImVec2(140.0f, 0.0f), buf);
    if (active) ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextUnformatted(label);
}

void drawJoyDirLed(const char* label, bool on) {
    const ImVec4 col = on ? ImVec4(0.20f, 0.85f, 0.30f, 1.0f) : ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    ImGui::TextColored(col, "%s", label);
    ImGui::SameLine();
}

void drawJoystickWindow(GLFWwindow* win, uint8_t lastJoy0, uint8_t lastJoy1) {
    ImGui::Begin("Joystick", &g_showJoy);

    // --- Réglages (modifient les globals ; resauve via g_joyCfgDirty) -----------
    if (ImGui::Checkbox("Keyboard emulation (arrows + right Ctrl)", &g_kbdJoy)) g_joyCfgDirty = true;
    ImGui::SameLine(); ImGui::TextDisabled("(F11)");
    ImGui::Text("Emulated port:"); ImGui::SameLine();
    if (ImGui::RadioButton("1 (games)", g_kbdJoyPort == 1)) { g_kbdJoyPort = 1; g_joyCfgDirty = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("0 (mouse)", g_kbdJoyPort == 0)) { g_kbdJoyPort = 0; g_joyCfgDirty = true; }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Dead zone", &g_joyDeadzone, 0.0f, 0.95f, "%.2f")) {
        if (g_joyDeadzone < 0.0f) g_joyDeadzone = 0.0f;
        if (g_joyDeadzone > 0.95f) g_joyDeadzone = 0.95f;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) g_joyCfgDirty = true;

    ImGui::Separator();

    // --- Sortie réellement envoyée au ST (le plus important) --------------------
    auto decodeRow = [](const char* who, uint8_t v) {
        ImGui::Text("%s  $%02X :", who, v); ImGui::SameLine();
        drawJoyDirLed("UP",    v & stjoy::UP);
        drawJoyDirLed("DOWN",  v & stjoy::DOWN);
        drawJoyDirLed("LEFT",  v & stjoy::LEFT);
        drawJoyDirLed("RIGHT", v & stjoy::RIGHT);
        drawJoyDirLed("FIRE",  v & stjoy::FIRE);
        ImGui::NewLine();
    };
    ImGui::TextDisabled("→ Sent to the IKBD (ST):");
    decodeRow("Port 0", lastJoy0);
    decodeRow("Port 1", lastJoy1);

    ImGui::Separator();

    // --- État brut de chaque manette présente -----------------------------------
    int nPresent = 0;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickPresent(jid)) continue;
        ++nPresent;
        const char* nm = glfwGetJoystickName(jid);
        const int stPort = (nPresent == 1) ? 1 : (nPresent == 2 ? 0 : -1);
        ImGui::Text("Pad %d: %s", jid, nm ? nm : "?");
        if (stPort >= 0) { ImGui::SameLine(); ImGui::TextDisabled("→ ST port %d", stPort); }

        GLFWgamepadstate gs;
        if (glfwGetGamepadState(jid, &gs)) {
            ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f), "  recognized as a gamepad (SDL mapping)");
            ImGui::Indent(8.0f);
            drawJoystickAxisBar("LX", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X],  g_joyDeadzone);
            drawJoystickAxisBar("LY", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y],  g_joyDeadzone);
            drawJoystickAxisBar("RX", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], g_joyDeadzone);
            drawJoystickAxisBar("RY", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], g_joyDeadzone);
            ImGui::Unindent(8.0f);
        } else {
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f), "  NOT recognized as a gamepad → raw read");
        }

        // Axes bruts (toujours affichés : révèlent un axe non centré au repos).
        int axN = 0, btN = 0, hatN = 0;
        const float*         ax  = glfwGetJoystickAxes(jid, &axN);
        const unsigned char* bt  = glfwGetJoystickButtons(jid, &btN);
        const unsigned char* hat = glfwGetJoystickHats(jid, &hatN);
        ImGui::Text("  Raw axes (%d):", axN);
        for (int i = 0; i < axN && ax; ++i) {
            char lbl[24]; std::snprintf(lbl, sizeof lbl, "a%d%s", i,
                                        (i == 0 ? " (X?)" : i == 1 ? " (Y?)" : ""));
            ImGui::Indent(8.0f); drawJoystickAxisBar(lbl, ax[i], g_joyDeadzone); ImGui::Unindent(8.0f);
        }
        ImGui::Text("  Buttons (%d):", btN); ImGui::SameLine();
        for (int i = 0; i < btN && bt; ++i)
            if (bt[i]) { ImGui::SameLine(); ImGui::Text("%d", i); }
        if (hat && hatN >= 1)
            ImGui::Text("  Hat0 : %s%s%s%s", (hat[0]&GLFW_HAT_UP)?"U":"", (hat[0]&GLFW_HAT_DOWN)?"D":"",
                        (hat[0]&GLFW_HAT_LEFT)?"L":"", (hat[0]&GLFW_HAT_RIGHT)?"R":"");
        // Décomposition analogique / numérique + effet du filtre anti-bloqué.
        const float thr = (g_joyDeadzone < 0.0f) ? 0.0f : (g_joyDeadzone > 0.95f ? 0.95f : g_joyDeadzone);
        uint8_t an = 0, dg = 0; stjoy::readStickRaw(jid, thr, an, dg);
        const uint8_t fin = stjoy::readStick(jid, g_joyDeadzone);
        ImGui::Text("  analog $%02X | raw digital $%02X", an, dg);
        if ((dg & ~fin) & ~an)
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f),
                               "  stuck-input filter: jammed digital bits ignored ($%02X)",
                               uint8_t((dg & ~fin) & ~an));
        ImGui::Text("  → ST byte sent: $%02X", fin);
        ImGui::Separator();
    }
    if (nPresent == 0) ImGui::TextDisabled("No pad detected. (Keyboard: enable the emulation above.)");
    (void)win;
    ImGui::End();
}

// Fenêtre de réglages des effets CRT (façade moniteur). Modifie g_crtOn /
// g_crtParams ; pose `changed`=true si l'utilisateur a touché quelque chose
// (l'appelant recopie alors dans neost.cfg et resauve). Les presets écrivent
// les mêmes champs numériques → une fois figés ils survivent au save.
// Contrôles CRT SANS fenêtre : partagés par la fenêtre flottante « Effets CRT »
// et par la page Écran de la fenêtre Configuration (proposition B) — une seule
// définition des réglages, deux endroits où les afficher.
void drawCrtControls(bool& changed) {
    if (ImGui::Checkbox("Enable CRT effects", &g_crtOn)) {
        changed = true;
        if (g_crtOn && !g_crt.available() && !g_crtInit) { g_crtInit = true; g_crt.initialize(); }
    }
    // Diagnostic : shader indisponible (ex. contexte GL 2.1 sur macOS legacy).
    if (g_crtOn && g_crtInit && !g_crt.available()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Shader unavailable:");
        ImGui::TextWrapped("%s", g_crt.lastError().c_str());
        ImGui::TextDisabled("→ ST screen shown raw (passthrough).");
    }

    ImGui::TextDisabled("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Light"))    { applyCrtPreset("light",    g_crtParams, g_crtOn); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Arcade"))   { applyCrtPreset("arcade",   g_crtParams, g_crtOn); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Phosphor")) { applyCrtPreset("phosphor", g_crtParams, g_crtOn); changed = true; }

    ImGui::Separator();
    ImGui::BeginDisabled(!g_crtOn);
    neost::CrtParams& p = g_crtParams;
    bool ch = false;
    ch |= ImGui::SliderFloat("Brightness",  &p.brightness, -0.5f, 0.5f);
    ch |= ImGui::SliderFloat("Contrast",    &p.contrast,    0.5f, 1.5f);
    ch |= ImGui::SliderFloat("Saturation",  &p.saturation,  0.0f, 2.0f);
    ch |= ImGui::SliderFloat("Hue",         &p.hue,        -0.5f, 0.5f);
    ImGui::Separator();
    ch |= ImGui::SliderFloat("Sharpness",   &p.sharpness,   0.0f, 1.0f);
    ch |= ImGui::SliderFloat("Persistence", &p.persistence, 0.0f, 0.98f);
    ImGui::Separator();
    ch |= ImGui::SliderFloat("Scanlines",   &p.scanlines,   0.0f, 1.0f);
    ch |= ImGui::SliderFloat("Barrel",      &p.barrel,      0.0f, 0.30f);

    ImGui::Separator();
    static const char* kMaskNames[] = {
        "Off", "Triad (3 stripes)", "Aperture grille (Trinitron)", "Dots (offset triads)"
    };
    int maskIdx = static_cast<int>(p.shadowMask);
    if (ImGui::Combo("Shadow mask", &maskIdx, kMaskNames, IM_ARRAYSIZE(kMaskNames))) {
        p.shadowMask = static_cast<neost::CrtParams::ShadowMask>(maskIdx);
        ch = true;
    }
    ImGui::BeginDisabled(p.shadowMask == neost::CrtParams::ShadowMask::Off);
    ch |= ImGui::SliderFloat("Mask strength", &p.shadowMaskStrength, 0.0f, 1.0f);
    ImGui::EndDisabled();
    ch |= ImGui::SliderFloat("Luminance gain",    &p.luminanceGain, 1.0f, 2.0f);
    ch |= ImGui::SliderFloat("Vignette",          &p.centerLighting, 0.5f, 1.0f);
    ch |= ImGui::SliderFloat("Phosphor gamma",    &p.phosphorGamma, 0.6f, 2.6f);
    ImGui::EndDisabled();

    if (ch) changed = true;
}

// Fenêtre flottante des effets CRT (conservée : on règle un moniteur en le REGARDANT,
// donc à côté de l'écran ST, pas dans une page de configuration).
void drawCrtSettings(bool& changed) {
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("CRT Effects", &g_showCrt);
    drawCrtControls(changed);
    ImGui::End();
}

// Fenêtre « Débogueur » (fenêtré) : breakpoints PC + pause/continue/step-frame +
// désassemblage autour du PC. Le moteur de breakpoints vit dans Cpu68k (conteneur
// Guards de Moira) ; ici on ne fait qu'AFFICHER/piloter. Le gel effectif de
// l'émulation (g_dbgPaused) et le pas-à-pas trame (g_dbgStepFrame) sont traités
// dans la boucle principale. Les registres/mémoire ont déjà leurs propres fenêtres.
void drawDebugger(Machine& machine) {
    Cpu68k& cpu = machine.cpu;
    // Étiquette symbolique « <nom+off> » d'une adresse (vide si aucun symbole).
    auto symLabel = [](uint32_t a) -> std::string {
        uint32_t off = 0;
        const std::string n = g_symbols.nameFor(a, &off);
        if (n.empty()) return {};
        char b[160]; std::snprintf(b, sizeof b, " <%s+%u>", n.c_str(), off);
        return b;
    };
    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_BUG " Debugger", &g_showDbg);

    // --- État + transport -----------------------------------------------------
    if (g_dbgPaused) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           ICON_FA_PAUSE " PAUSED  \xe2\x80\x94  PC=$%06X%s",
                           cpu.pc(), symLabel(cpu.pc()).c_str());
        if (cpu.breakpointHit() && cpu.breakpointHitIsWatch())
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "  watchpoint: access $%06X%s",
                               cpu.breakpointHitAddr(), symLabel(cpu.breakpointHitAddr()).c_str());
        if (ImGui::Button(ICON_FA_PLAY " Continue")) {
            cpu.clearBreakpointHit();   // arme le skip-once de l'adresse courante
            g_dbgPaused = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STEP_FORWARD " Step (1 instr)")) g_dbgStepInstr = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STEP_FORWARD " Step (1 frame)")) g_dbgStepFrame = true;
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), ICON_FA_PLAY " Running");
        if (ImGui::Button(ICON_FA_PAUSE " Pause")) g_dbgPaused = true;
    }
    ImGui::Separator();

    // --- Symboles : chargement (.sym nm-style ou exécutable TOS) + bp par nom --
    ImGui::Text("Symbols (%zu)", g_symbols.count());
    static char symPath[512] = "";
    static char symBaseBuf[16] = "";
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##sympath", ".sym or .TOS path", symPath, sizeof symPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputTextWithHint("##symbase", "hex base", symBaseBuf, sizeof symBaseBuf,
                             ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Load") && symPath[0]) {
        const uint32_t base = (uint32_t)std::strtoul(symBaseBuf, nullptr, 16);
        g_symbols.load(symPath, base);   // auto-détecte nm-style vs exécutable TOS
    }
    // Breakpoint par symbole (nom → adresse via la table).
    static char symBp[64] = "";
    ImGui::SetNextItemWidth(220.0f);
    const bool symEnter = ImGui::InputTextWithHint("##symbp", "symbol name", symBp, sizeof symBp,
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Symbol BP") || symEnter) && symBp[0]) {
        uint32_t a = 0;
        if (g_symbols.lookup(symBp, a)) { cpu.setBreakpoint(a); symBp[0] = '\0'; }
    }
    ImGui::Separator();

    // --- Breakpoints : ajout + liste ------------------------------------------
    ImGui::Text("Breakpoints (%d)", cpu.breakpointCount());
    static char bpBuf[16] = "";
    ImGui::SetNextItemWidth(120.0f);
    const bool entered = ImGui::InputText("##bpaddr", bpBuf, sizeof bpBuf,
                                          ImGuiInputTextFlags_CharsHexadecimal |
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add") || entered) && bpBuf[0]) {
        cpu.setBreakpoint((uint32_t)std::strtoul(bpBuf, nullptr, 16));
        bpBuf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) cpu.clearAllBreakpoints();

    if (ImGui::BeginChild("##bplist", ImVec2(0, 120), true)) {
        for (int i = 0; i < cpu.breakpointCount(); ++i) {
            uint32_t a = 0;
            if (!cpu.breakpointByIndex(i, a)) continue;
            ImGui::PushID(i);
            if (ImGui::SmallButton(ICON_FA_TIMES)) {   // retirer (les indices bougent → on sort)
                cpu.clearBreakpoint(a);
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            char dis[256]; cpu.disassemble(dis, a);
            ImGui::Text("$%06X%s  %s", a, symLabel(a).c_str(), dis);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();

    // --- Watchpoints mémoire : arrêt à l'accès (lecture OU écriture) d'une adresse
    ImGui::Text("Watchpoints (%d)", cpu.watchpointCount());
    static char wpBuf[16] = "";
    ImGui::SetNextItemWidth(120.0f);
    const bool wpEnter = ImGui::InputTextWithHint("##wpaddr", "hex address", wpBuf, sizeof wpBuf,
                                                  ImGuiInputTextFlags_CharsHexadecimal |
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add##wp") || wpEnter) && wpBuf[0]) {
        cpu.setWatchpoint((uint32_t)std::strtoul(wpBuf, nullptr, 16));
        wpBuf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all##wp")) cpu.clearAllWatchpoints();
    if (ImGui::BeginChild("##wplist", ImVec2(0, 80), true)) {
        for (int i = 0; i < cpu.watchpointCount(); ++i) {
            uint32_t a = 0;
            if (!cpu.watchpointByIndex(i, a)) continue;
            ImGui::PushID(1000 + i);
            if (ImGui::SmallButton(ICON_FA_TIMES)) { cpu.clearWatchpoint(a); ImGui::PopID(); break; }
            ImGui::SameLine();
            ImGui::Text("$%06X%s", a, symLabel(a).c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();

    // --- Désassemblage autour du PC (clic sur une ligne = toggle breakpoint) ---
    ImGui::TextDisabled("Disassembly (click = toggle a breakpoint)");
    if (ImGui::BeginChild("##disasm", ImVec2(0, 0), true)) {
        const uint32_t pc = cpu.pc();
        uint32_t addr = pc;
        for (int i = 0; i < 24; ++i) {
            char dis[256];
            int len = cpu.disassemble(dis, addr);
            if (len <= 0) len = 2;
            const bool isPc  = (addr == pc);
            const bool hasBp = cpu.hasBreakpoint(addr);
            char line[300];
            std::snprintf(line, sizeof line, "%s %s $%06X%s  %s",
                          hasBp ? ICON_FA_TIMES : "  ", isPc ? ">" : " ", addr,
                          symLabel(addr).c_str(), dis);
            if (hasBp) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.4f, 1.0f));
            else if (isPc) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.5f, 1.0f));
            if (ImGui::Selectable(line, isPc)) {
                if (hasBp) cpu.clearBreakpoint(addr); else cpu.setBreakpoint(addr);
            }
            if (hasBp || isPc) ImGui::PopStyleColor();
            addr += (uint32_t)len;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// Longueur du préfixe commun (insensible à la casse) entre deux noms de fichier.
// Sert à mesurer la « proximité » : les disquettes d'un même jeu (« Jeu (Disk A) »,
// « Jeu (Disk B) »…) partagent un long préfixe et ne diffèrent qu'au marqueur B/C/D.
static size_t commonPrefixLenCI(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < n && std::tolower((unsigned char)a[i]) == std::tolower((unsigned char)b[i])) ++i;
    return i;
}

// Deux noms de fichier sont-ils des SUITES du même jeu (face A/B, partie 1/2/3,
// Disk 1 of 2 / Disk 2 of 2…) ? Vrai si les noms sont identiques SAUF un court
// jeton central : long préfixe commun + long suffixe commun, écart au milieu
// borné. Rejette « Space Harrier » / « Space Crusade » (préfixe commun mais
// suffixes/milieux tout différents). `a`, `b` supposés déjà en minuscules.
static bool kioskAreSiblings(const std::string& a, const std::string& b) {
    if (a == b) return true;
    const size_t p = commonPrefixLenCI(a, b);
    if (p < 3) return false;
    size_t s = 0;
    const size_t maxS = std::min(a.size(), b.size()) - p;   // ne pas empiéter sur le préfixe
    while (s < maxS && a[a.size() - 1 - s] == b[b.size() - 1 - s]) ++s;
    const size_t gapA = a.size() - p - s, gapB = b.size() - p - s;
    if (gapA > 12 || gapB > 12) return false;               // le morceau qui diffère doit être court
    return (p + s) * 2 >= std::min(a.size(), b.size());      // préfixe+suffixe couvrent l'essentiel
}

// Recense les images montables (.st/.msa/.dim/.stx) sous disks/ (récursif) →
// g_kioskDisks, TRIÉES par proximité au disque courant `mounted` (préfixe commun
// décroissant, puis alphabétique). Les « suites » du jeu en cours (phases B/C/D)
// remontent ainsi en tête. Appelé à l'ouverture de l'overlay kiosk.
static void kioskScanDisks(const std::string& disksDir, const std::string& mounted) {
    g_kioskDisks.clear();
    // Scanne récursivement un dossier → images montables, avec dédup (le dossier ROM
    // additionnel peut recouvrir disks/). Appelé pour disks/ PUIS pour g_kioskRomDir.
    auto scanInto = [&](const std::string& dir) {
        std::error_code e2;
        if (dir.empty() || !fs::is_directory(dir, e2)) return;
        // ⚠ Le range-for incrémente via operator++() qui LANCE filesystem_error sur
        // un dossier illisible (EACCES — le raccourci « / » du kiosk traverse /root,
        // /proc…) : itération manuelle avec increment(ec) + skip_permission_denied.
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, e2), end;
        // BORNES DURES. Ce scan tourne dans le THREAD GUI, à chaque ouverture du menu
        // borne : sans limite, un dossier vaste fige l'interface (mesuré : /home =
        // 2,7 M d'entrées, 4,6 s cache chaud, et c'est à une touche du raccourci
        // « Home »). Refuser la racine et $HOME ne suffit pas — il faut borner le
        // parcours lui-même : profondeur, nombre d'entrées, et budget de temps.
        constexpr int  kMaxDepth   = 6;
        constexpr long kMaxEntries = 40000;
        const auto     tStart      = std::chrono::steady_clock::now();
        long seen = 0;
        while (!e2 && it != end) {
            if (++seen > kMaxEntries) break;
            if ((seen & 0x3FF) == 0 &&
                std::chrono::steady_clock::now() - tStart > std::chrono::milliseconds(800)) break;
            if (it.depth() >= kMaxDepth) it.disable_recursion_pending();
            const fs::directory_entry& e = *it;
            std::error_code e3;
            if (e.is_regular_file(e3)) {
                std::string ext = e.path().extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx") {
                    const std::string p = e.path().string();
                    if (std::find(g_kioskDisks.begin(), g_kioskDisks.end(), p) == g_kioskDisks.end())
                        g_kioskDisks.push_back(p);
                }
            }
            it.increment(e2);
        }
    };
    scanInto(disksDir);
    for (const auto& d : g_kioskRomDirs) scanInto(d);
    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string mref = lower(fs::path(mounted).filename().string());
    std::sort(g_kioskDisks.begin(), g_kioskDisks.end(),
              [&](const std::string& a, const std::string& b) {
                  const std::string an = lower(fs::path(a).filename().string());
                  const std::string bn = lower(fs::path(b).filename().string());
                  // 1) les VRAIES suites du jeu monté (face A/B, partie 1/2/3) en tête
                  if (!mref.empty()) {
                      const bool sa = kioskAreSiblings(an, mref), sb = kioskAreSiblings(bn, mref);
                      if (sa != sb) return sa;
                  }
                  // 2) puis par proximité de nom (préfixe commun), 3) alphabétique
                  const size_t pa = commonPrefixLenCI(an, mref);
                  const size_t pb = commonPrefixLenCI(bn, mref);
                  if (pa != pb) return pa > pb;
                  return an < bn;
              });
}

// Recense les SOUS-DOSSIERS immédiats de `dir` (triés, insensible à la casse) →
// g_browseSubdirs, et remet la sélection en tête. Pour le navigateur « SELECT ROM
// FOLDER » piloté à la manette.
static void kioskScanBrowse(const std::string& dir) {
    g_browseSubdirs.clear();
    g_browseSel = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::error_code e2;
        if (e.is_directory(e2)) g_browseSubdirs.push_back(e.path().string());
    }
    std::sort(g_browseSubdirs.begin(), g_browseSubdirs.end(),
              [](const std::string& a, const std::string& b) {
                  auto low = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
                  return low(fs::path(a).filename().string()) < low(fs::path(b).filename().string());
              });
}

// Retire de g_kioskRomDirs les dossiers qui n'existent plus (mal détectés / débranchés).
// Renvoie true si la liste a changé → l'appelant re-sauve la config.
static bool kioskPruneRomDirs() {
    bool changed = false;
    for (size_t i = 0; i < g_kioskRomDirs.size(); ) {
        std::error_code ec;
        if (!fs::is_directory(g_kioskRomDirs[i], ec)) {
            g_kioskRomDirs.erase(g_kioskRomDirs.begin() + (long)i);
            changed = true;
        } else ++i;
    }
    return changed;
}

// Calcule les raccourcis du navigateur : racine /, home, puis chaque VOLUME MONTÉ
// (par son nom) sous /run/media/$USER, /media/$USER, /media, /mnt. Chaque libellé
// embarque une icône FA. Appelé à l'ouverture du navigateur.
static void kioskComputeShortcuts() {
    g_browseShortcutPaths.clear();
    g_browseShortcutLabels.clear();
    auto add = [&](const std::string& path, const std::string& label) {
        std::error_code ec;
        if (!fs::is_directory(path, ec)) return;
        if (std::find(g_browseShortcutPaths.begin(), g_browseShortcutPaths.end(), path)
                != g_browseShortcutPaths.end()) return;   // dédup
        g_browseShortcutPaths.push_back(path);
        g_browseShortcutLabels.push_back(label);
    };
    add("/", std::string(ICON_FA_SERVER) + " / (filesystem root)");
    // USERPROFILE en repli : Windows ne définit pas HOME, et sans lui le
    // raccourci « Home » du navigateur borne disparaissait purement et simplement.
    const char* home = std::getenv("HOME");
    if (!home || !*home) home = std::getenv("USERPROFILE");
    if (home && *home)
        add(home, std::string(ICON_FA_FOLDER_OPEN) + " Home  (" + home + ")");
    // Emplacements de montage (portable : le garde is_directory ci-dessous fait que
    // chaque OS n'expose que ceux qui existent, pas besoin de #ifdef) :
    //   · macOS  : /Volumes (tous les volumes montés, par nom)
    //   · Linux  : /run/media/$USER (udisks2), /media/$USER (classique), /mnt (manuel)
    // On évite le /media NU (il listerait des noms d'utilisateurs) — « / » y donne accès.
    const char* user = std::getenv("USER");
    std::vector<std::string> roots;
    roots.push_back("/Volumes");                                  // macOS
    if (user) { roots.push_back(std::string("/run/media/") + user);
                roots.push_back(std::string("/media/") + user); }
    roots.push_back("/mnt");
    for (const auto& r : roots) {
        std::error_code ec;
        if (!fs::is_directory(r, ec)) continue;
        for (const auto& e : fs::directory_iterator(r, ec)) {
            std::error_code e2;
            if (e.is_directory(e2))
                add(e.path().string(),
                    std::string(ICON_FA_HDD) + " " + e.path().filename().string());
        }
    }
}

// Table de la page « Clavier & souris » du menu kiosk. kind : 0 = touche ST
// (scancode), 1 = clic gauche souris, 2 = clic droit. Disposée en 3 rangées
// (F1-F8, chiffres 1-0, Espace + clics) — cf. KIOSK_KEY_ROWS pour la navigation.
struct KioskKey { const char* label; uint8_t scancode; int kind; };
static const KioskKey KIOSK_KEYS[] = {
    {"F1",0x3B,0},{"F2",0x3C,0},{"F3",0x3D,0},{"F4",0x3E,0},
    {"F5",0x3F,0},{"F6",0x40,0},{"F7",0x41,0},{"F8",0x42,0},          // rangée 0 (8)
    {"1",0x02,0},{"2",0x03,0},{"3",0x04,0},{"4",0x05,0},{"5",0x06,0},
    {"6",0x07,0},{"7",0x08,0},{"8",0x09,0},{"9",0x0A,0},{"0",0x0B,0}, // rangée 1 (10)
    {"SPACE",0x39,0},{"RETURN",0x1C,0},{"ESCAPE",0x01,0},
    {"T",0x14,0},{"Y",0x15,0},{"N",0x31,0},
    {"CLICK L",0,1},{"CLICK R",0,2},                                  // rangée 2 (8)
};
[[maybe_unused]] static const int KIOSK_KEY_COUNT = (int)(sizeof(KIOSK_KEYS) / sizeof(KIOSK_KEYS[0]));
// Bornes de rangées (indices de début) : [0,8) [8,18) [18,26).
static const int KIOSK_KEY_ROWS[][2] = { {0, 8}, {8, 18}, {18, 26} };
static const int KIOSK_KEY_ROWN = 3;

// Menu kiosk PLEIN ÉCRAN (rendu par-dessus l'écran ST). Pages selon g_kioskPage.
// La navigation (clavier/manette) est gérée dans la boucle ; ici on AFFICHE.
// `mounted` = chemin monté (marqué « ● »).
void drawKioskDiskMenu(const std::string& disksDir, const std::string& mounted) {
    const ImGuiIO& io = ImGui::GetIO();
    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string mname = mounted.empty() ? std::string("(none)")
                                              : fs::path(mounted).filename().string();
    const ImVec4 kGreen (0.30f, 1.0f, 0.40f, 1.0f);
    const ImVec4 kYellow(1.0f, 0.9f, 0.3f, 1.0f);
    const ImVec4 kOrange(1.0f, 0.60f, 0.15f, 1.0f);
    const bool keysPage = (g_kioskPage == KIOSK_PAGE_KEYS);

    // Voile sombre plein écran — UNIQUEMENT pour les pages plein écran (liste,
    // quitter). La page Clavier est un petit bandeau translucide : on VOIT le jeu
    // tourner dessous (et donc qu'on a avancé) quand on lui envoie une touche.
    if (!keysPage) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##kioskveil", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::End();
    }

    // Géométrie selon la page : plein cadre centré pour la liste/quitter, petit
    // bandeau ancré EN BAS (translucide) pour le clavier.
    const ImVec2 fullSz(io.DisplaySize.x * 0.72f, io.DisplaySize.y * 0.82f);
    if (keysPage) {
        const ImVec2 ksz(io.DisplaySize.x * 0.60f, io.DisplaySize.y * 0.30f);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.98f),
                                ImGuiCond_Always, ImVec2(0.5f, 1.0f));   // ancré en bas
        ImGui::SetNextWindowSize(ksz, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
    } else {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(fullSz, ImGuiCond_Always);
    }
    ImGui::Begin("##kioskmenu", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoSavedSettings);

    // ================= PAGE 1 : DEUX menus (intérieur / extérieur) ============
    // Menu INTÉRIEUR = liste des jeux ; menu EXTÉRIEUR = Redémarrer / Clavier /
    // Quitter. On BASCULE de l'un à l'autre avec GAUCHE/DROITE ; haut/bas navigue
    // dans le menu focalisé ; le FEU valide son item surligné. Le menu qui a le focus
    // est vif (cursor vert ▶), l'autre est estompé.
    if (g_kioskPage == KIOSK_PAGE_LIST) {
        const int nd = (int)g_kioskDisks.size();
        const bool zList = (g_kioskZone == KIOSK_ZONE_LIST);
        const bool zAct  = (g_kioskZone == KIOSK_ZONE_ACTIONS);
        const ImVec4 kDim(0.5f, 0.5f, 0.5f, 1.0f);   // item sélectionné du menu NON focalisé
        ImGui::SetWindowFontScale(3.0f);
        ImGui::TextColored(kYellow, ICON_FA_GAMEPAD " MENU");
        ImGui::SameLine(); ImGui::SetWindowFontScale(1.5f);
        ImGui::TextDisabled("  \xe2\x97\x80\xe2\x96\xb6 switch menu   \xc2\xb7   up/down select"
                            "   \xc2\xb7   L1/R1 fast   \xc2\xb7   FIRE confirm   \xc2\xb7   (B) resume");
        ImGui::Separator();

        // --- Menu INTÉRIEUR : liste des jeux (valider = INSÉRER à chaud) -------
        const std::string mrefL = lower(mname);
        // Réserve pour le 2ᵉ menu (4 actions @2.3 + « Roms found » @1.3) calée sur son
        // CONTENU réel → aucun espace vide en bas : la liste des jeux prend tout le reste,
        // le 2ᵉ menu vient flush contre le bas de la fenêtre.
        ImGui::SetWindowFontScale(1.0f);
        const float sp = ImGui::GetStyle().ItemSpacing.y;
        const float bf = ImGui::GetFontSize();               // taille de police de base
        const float footer = 6.0f * (bf * 2.3f + sp)         // 6 rangées d'action @2.3
                           + (bf * 1.3f + sp)                // ligne « Roms found » @1.3
                           + (sp + 6.0f);                    // séparateur + petite marge
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(zList ? kYellow : kDim,
                           zList ? "\xe2\x96\xb6 " ICON_FA_COMPACT_DISC " GAMES"
                                 : "  " ICON_FA_COMPACT_DISC " GAMES");
        ImGui::BeginChild("##kdlist", ImVec2(0, ImGui::GetContentRegionAvail().y - footer), true);
        ImGui::SetWindowFontScale(2.7f);   // noms de fichiers TRÈS gros
        if (g_kioskDisks.empty())
            ImGui::TextDisabled("(no image in %s/)", disksDir.c_str());
        for (int i = 0; i < nd; ++i) {
            const bool sel = (i == g_kioskDiskSel);
            const bool cur = (g_kioskDisks[i] == mounted);
            const std::string fn = fs::path(g_kioskDisks[i]).filename().string();
            const bool sibling = !mrefL.empty() && !cur && kioskAreSiblings(lower(fn), mrefL);
            // Cursor vert seulement si le menu JEUX a le focus ; sinon item courant estompé.
            if (sel)          ImGui::PushStyleColor(ImGuiCol_Text, zList ? kGreen : kDim);
            else if (sibling) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.6f, 1.0f));
            ImGui::Text("%s %s%s", (sel && zList) ? "\xe2\x96\xb6" : "   ", cur ? ICON_FA_COMPACT_DISC " " : "", fn.c_str());
            if (sel || sibling) ImGui::PopStyleColor();
            if (sel && zList) ImGui::SetScrollHereY(0.5f);
        }
        ImGui::EndChild();

        // --- Menu EXTÉRIEUR : boutons d'action --------------------------------
        ImGui::SetWindowFontScale(2.3f);
        auto actionRow = [&](int idx, const ImVec4& col, const char* label) {
            const bool sel = (g_kioskActSel == idx);
            // Vif + cursor vert si le menu ACTIONS a le focus et cet item est choisi.
            ImGui::PushStyleColor(ImGuiCol_Text, (sel && zAct) ? kGreen : (zAct ? col : kDim));
            ImGui::Text("%s %s", (sel && zAct) ? "\xe2\x96\xb6" : "  ", label);
            ImGui::PopStyleColor();
        };
        actionRow(0, kOrange,                          ICON_FA_REDO " RESTART MACHINE");
        actionRow(1, ImVec4(0.55f, 0.8f, 1.0f, 1.0f),  ICON_FA_KEYBOARD " KEYBOARD & MOUSE");
        actionRow(2, ImVec4(0.8f, 0.7f, 1.0f, 1.0f),   ICON_FA_GAMEPAD " JOYSTICKS");
        actionRow(3, ImVec4(0.6f, 0.95f, 0.6f, 1.0f),  ICON_FA_FOLDER_OPEN " ROM FOLDERS");
        actionRow(4, ImVec4(0.75f, 0.85f, 1.0f, 1.0f), ICON_FA_CLONE " DESKTOP MODE");
        actionRow(5, ImVec4(1.0f, 0.5f, 0.4f, 1.0f),   ICON_FA_SIGN_OUT_ALT " QUIT NEOST");
        ImGui::SetWindowFontScale(1.0f);

        // Bas de fenêtre : nombre de ROMs trouvées (les DOSSIERS se voient dans « ROM FOLDERS »).
        ImGui::Separator();
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextColored(kYellow, ICON_FA_COMPACT_DISC " Roms found: %d", (int)g_kioskDisks.size());
        ImGui::SetWindowFontScale(1.0f);
    }

    // ================= PAGE 2 : Clavier & souris (petit bandeau) ==============
    // Jeu NON en pause : la touche/clic est envoyée au jeu qui tourne dessous.
    else if (g_kioskPage == KIOSK_PAGE_KEYS) {
        ImGui::SetWindowFontScale(1.5f);
        ImGui::TextColored(kYellow, ICON_FA_KEYBOARD " KEYBOARD & MOUSE");
        ImGui::SameLine();
        ImGui::TextDisabled(" (A) press  \xc2\xb7  (B) close");
        ImGui::Separator();

        for (int r = 0; r < KIOSK_KEY_ROWN; ++r) {
            ImGui::SetWindowFontScale(2.4f);
            for (int i = KIOSK_KEY_ROWS[r][0]; i < KIOSK_KEY_ROWS[r][1]; ++i) {
                const bool sel = (i == g_kioskKeySel);
                if (i > KIOSK_KEY_ROWS[r][0]) ImGui::SameLine();
                if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                char cell[24];
                std::snprintf(cell, sizeof cell, sel ? "[%s]" : " %s ", KIOSK_KEYS[i].label);
                ImGui::TextUnformatted(cell);
                if (sel) ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0, 4));
        }
        ImGui::SetWindowFontScale(1.0f);
    }

    // ================= PAGE 3 : confirmation de sortie =======================
    else if (g_kioskPage == KIOSK_PAGE_QUIT) {
        ImGui::SetWindowFontScale(3.1f);
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), ICON_FA_SIGN_OUT_ALT " QUIT NEOST?");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::SetWindowFontScale(2.4f);
        ImGui::TextDisabled("The kiosk will close.");
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::Separator();
        ImGui::SetWindowFontScale(2.0f);
        ImGui::TextColored(kGreen, ICON_FA_POWER_OFF " (A) Yes, quit");
        ImGui::SetWindowFontScale(1.8f);
        ImGui::TextDisabled("(B) No, back to menu");
        ImGui::SetWindowFontScale(1.0f);
    }

    // ============ PAGE 4 : navigateur « SELECT ROM FOLDER » (plein écran) ======
    // Liste : [0] valider CE dossier, [1] .. (parent), [2..] sous-dossiers. La
    // navigation/validation est gérée dans la boucle ; ici on AFFICHE.
    else if (g_kioskPage == KIOSK_PAGE_BROWSE) {
        ImGui::SetWindowFontScale(2.6f);
        ImGui::TextColored(kYellow, ICON_FA_FOLDER_OPEN " SELECT ROM FOLDER");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextDisabled("up/down move   \xc2\xb7   (A) enter / select   \xc2\xb7   (B) cancel");
        ImGui::Separator();
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", g_browseDir.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##kbrowse", ImVec2(0, 0), true);
        ImGui::SetWindowFontScale(2.2f);
        // Ordre des lignes : [0] valider · [1] .. · [2..2+S) raccourcis · [reste] sous-dossiers.
        const int nShort = (int)g_browseShortcutPaths.size();
        const int total  = 2 + nShort + (int)g_browseSubdirs.size();
        for (int i = 0; i < total; ++i) {
            const bool sel = (i == g_browseSel);
            const char* cur = sel ? "\xe2\x96\xb6" : "   ";
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
            if (i == 0)
                ImGui::Text("%s " ICON_FA_STAR " [ USE THIS FOLDER ]", cur);
            else if (i == 1)
                ImGui::Text("%s " ICON_FA_FOLDER_OPEN " ..", cur);
            else if (i < 2 + nShort)
                ImGui::Text("%s %s", cur, g_browseShortcutLabels[i - 2].c_str());
            else
                ImGui::Text("%s " ICON_FA_FOLDER_OPEN " %s", cur,
                            fs::path(g_browseSubdirs[i - 2 - nShort]).filename().string().c_str());
            if (sel) { ImGui::PopStyleColor(); ImGui::SetScrollHereY(0.5f); }
        }
        ImGui::EndChild();
        ImGui::SetWindowFontScale(1.0f);
    }

    // ============ PAGE 5 : gestion des dossiers ROM (ajouter / retirer) ========
    // [0] « + ADD A FOLDER » (→ navigateur) ; [1..N] dossiers configurés, chacun avec
    // une croix rouge (FEU = retirer). Auto-prune des dossiers disparus fait à l'ouverture.
    else if (g_kioskPage == KIOSK_PAGE_JOY) {
        // Affectation des manettes hôte aux ports joystick ST. Une ligne par
        // manette PRÉSENTE : nom + rôle (AUTO avec le port effectif résolu, PORT 1,
        // PORT 0, OFF) + pastille verte si la manette émet (pour identifier
        // physiquement laquelle est laquelle : bouger le stick l'allume).
        ImGui::SetWindowFontScale(2.6f);
        ImGui::TextColored(kYellow, ICON_FA_GAMEPAD " JOYSTICKS");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextDisabled("up/down move   \xc2\xb7   (A) cycle port   \xc2\xb7   (B) back"
                            "   \xc2\xb7   move a stick to spot it \xe2\x97\x8f");
        ImGui::Separator();
        ImGui::BeginChild("##kjoy", ImVec2(0, 0), true);
        ImGui::SetWindowFontScale(2.2f);
        int8_t roles[GLFW_JOYSTICK_LAST + 1];
        joyResolveRoles(roles);
        int8_t assign[GLFW_JOYSTICK_LAST + 1];
        stjoy::resolveAssign(roles, assign);
        int row = 0;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (!glfwJoystickPresent(jid)) continue;
            const bool sel = (row == g_kioskJoySel);
            const char* nm = glfwGetJoystickName(jid);
            char role[32];
            if      (roles[jid] == stjoy::ROLE_PORT1) std::snprintf(role, sizeof role, "PORT 1");
            else if (roles[jid] == stjoy::ROLE_PORT0) std::snprintf(role, sizeof role, "PORT 0");
            else if (roles[jid] == stjoy::ROLE_OFF)   std::snprintf(role, sizeof role, "OFF");
            else if (assign[jid] >= 0) std::snprintf(role, sizeof role, "AUTO (PORT %d)", assign[jid]);
            else                       std::snprintf(role, sizeof role, "AUTO (unused)");
            const bool active = stjoy::readStick(jid, g_joyDeadzone) != 0;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
            ImGui::Text("%s %s \xe2\x80\x94 %s", sel ? "\xe2\x96\xb6" : "  ",
                        nm ? nm : "(unnamed)", role);
            if (sel) ImGui::PopStyleColor();
            if (active) {
                ImGui::SameLine();
                ImGui::TextColored(kGreen, "\xe2\x97\x8f");
            }
            if (sel) ImGui::SetScrollHereY(0.5f);
            ++row;
        }
        if (row == 0)
            ImGui::TextDisabled("   (no joystick detected \xe2\x80\x94 plug one in, "
                                "the list is live)");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Separator();
        ImGui::TextDisabled("PORT 1 = games port. Buttons: A/B = fire, X = SPACE, Y = RETURN.");
        ImGui::EndChild();
        ImGui::SetWindowFontScale(1.0f);
    }

    else if (g_kioskPage == KIOSK_PAGE_ROMDIRS) {
        ImGui::SetWindowFontScale(2.6f);
        ImGui::TextColored(kYellow, ICON_FA_FOLDER_OPEN " ROM FOLDERS");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextDisabled("up/down move   \xc2\xb7   (A) add / remove   \xc2\xb7   (B) back");
        ImGui::Separator();
        ImGui::BeginChild("##kromdirs", ImVec2(0, 0), true);
        ImGui::SetWindowFontScale(2.2f);
        const int total = 1 + (int)g_kioskRomDirs.size();
        for (int i = 0; i < total; ++i) {
            const bool sel = (i == g_romDirSel);
            const char* cur = sel ? "\xe2\x96\xb6" : "   ";
            if (i == 0) {
                if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                ImGui::Text("%s " ICON_FA_PLUS " [ ADD A FOLDER ]", cur);
                if (sel) ImGui::PopStyleColor();
            } else {
                ImGui::Text("%s", cur); ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), " " ICON_FA_TIMES " ");
                ImGui::SameLine(0.0f, 0.0f);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                ImGui::Text("%s", g_kioskRomDirs[i - 1].c_str());
                if (sel) ImGui::PopStyleColor();
            }
            if (sel) ImGui::SetScrollHereY(0.5f);
        }
        if (g_kioskRomDirs.empty()) {
            ImGui::SetWindowFontScale(1.3f);
            ImGui::TextDisabled("   (no extra folder \xe2\x80\x94 only disks/ is scanned)");
        }
        ImGui::EndChild();
        ImGui::SetWindowFontScale(1.0f);
    }

    ImGui::End();
}

// ─── Ancrage (docking) ──────────────────────────────────────────────────────
// Porté de POM2 (MainWindow::renderDockSpace / applyDockLayout).
//
// Sème la disposition par DÉFAUT : l'écran ST au centre, les bibliothèques à
// droite, les inspecteurs en onglets sous elles, le débogueur en bas. Reconstruit
// tout à partir de zéro (RemoveNode désancre d'abord : une fenêtre non citée ici
// finit flottante, jamais coincée dans un vieux nœud).
static void applyDockLayout() {
#ifdef IMGUI_HAS_DOCK
    if (g_dockId == 0) return;
    ImGui::DockBuilderRemoveNode(g_dockId);
    ImGui::DockBuilderAddNode(g_dockId, ImGuiDockNodeFlags_DockSpace);
    // SetNodeSize compte : les ratios de découpe sont calculés sur la taille du
    // nœud — sans lui, la première découpe donne des tailles fantaisistes.
    ImGui::DockBuilderSetNodeSize(g_dockId, ImGui::GetMainViewport()->WorkSize);

    // `centre` est RELIÉ au reste après chaque découpe : on rogne les côtés
    // successivement et l'écran ST garde le milieu.
    ImGuiID centre = g_dockId;
    ImGuiID right  = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.30f, nullptr, &centre);
    ImGuiID rlow   = ImGui::DockBuilderSplitNode(right,  ImGuiDir_Down,  0.50f, nullptr, &right);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down,  0.32f, nullptr, &centre);

    // L'écran ST est TOUJOURS le centre.
    ImGui::DockBuilderDockWindow("Atari ST Screen", centre);
    // Le reste s'ancre par titre LITTÉRAL. Les fenêtres actuellement masquées sont
    // affectées quand même : c'est cette affectation qui les fera réapparaître en
    // onglet du bon groupe plus tard, au lieu de flotter par-dessus l'écran.
    ImGui::DockBuilderDockWindow(ICON_FA_COG " Configuration", right);
    ImGui::DockBuilderDockWindow("CPU 68000",     rlow);
    ImGui::DockBuilderDockWindow("Memory (hex)", rlow);
    ImGui::DockBuilderDockWindow("Joystick",      rlow);
    ImGui::DockBuilderDockWindow("CRT Effects",    rlow);
    ImGui::DockBuilderDockWindow(ICON_FA_BUG " Debugger",      bottom);
    ImGui::DockBuilderFinish(g_dockId);
#endif
}

// Pose le dockspace sur la ZONE DE TRAVAIL du viewport — le menu et la barre de
// boutons en ont déjà réservé leur part (ce sont des `BeginViewportSideBar`), donc
// le chrome n'est jamais recouvert et aucun décalage n'est codé en dur.
//   · PassthruCentralNode : un centre vide ne peint pas de fond (sinon dalle grise).
//   · KeepAliveOnly : en kiosk (ou mode ancré coupé) on ne DESSINE pas le dockspace
//     mais on garde le nœud VIVANT — sans ça, l'aller-retour kiosk↔GUI perdrait
//     l'ancrage des fenêtres. Rien n'est soumis dans ce mode (cf. imgui.cpp).
// À appeler APRÈS le menu/la barre et AVANT toute fenêtre ancrable : le nœud doit
// exister quand les Begin() suivants s'exécutent, sinon leur 1re trame est flottante.
static void renderDockSpace(bool visible) {
#ifdef IMGUI_HAS_DOCK
    // Mode ancré coupé (menu « Fenêtres ») : on ne soumet RIEN. Le viewport hôte de
    // DockSpaceOverViewport couvre toute la zone de travail — le laisser vivre sans
    // ancrage avalerait la souris au-dessus de l'écran ST.
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)) return;
    ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_PassthruCentralNode;
    if (!visible) flags |= ImGuiDockNodeFlags_KeepAliveOnly;
    g_dockId = ImGui::DockSpaceOverViewport(ImGui::GetID("NeoST_DockSpace"),
                                            ImGui::GetMainViewport(), flags);
    if (!visible) return;
    // 1re exécution avec l'ancrage : on sème. Conditionné à un drapeau PERSISTÉ et
    // non à « le nœud est-il vide » — DockSpaceOverViewport vient de le créer, donc
    // sa vacuité ne distingue pas « installation neuve » de « tout désancré exprès ».
    if (!g_dockSeeded) { g_dockSeeded = true; g_dockReset = true; ImGui::MarkIniSettingsDirty(); }
    if (g_dockReset)   { g_dockReset = false; applyDockLayout(); }
#else
    (void)visible;
#endif
}

// Fenêtre de l'écran ST : fenêtre de BASE (toujours là, jamais au premier plan).
// Placée sous les barres au 1er lancement, puis DÉPLAÇABLE par glissé de sa barre de
// titre (ImGui mémorise sa position). La taille d'affichage suit la résolution
// COURANTE du buffer en respectant l'aspect pixel ST : basse rés ×2/×2, moyenne
// ×1/×2, mono ×1/×1 — l'écran actif occupe donc toujours ~640×400.
// Clic dans l'image = capture souris.
//
// [cTop, cTop+cH) = région de CONTENU (cf. stContentRegion) : le bureau applique le
// MÊME zoom adaptatif que le kiosk, à ceci près qu'il le cadre en UV de l'image et
// non en viewport GL — les bordures inutilisées sortent du cadre au lieu d'ajouter
// des bandes noires. Zoom auto OFF → cTop=0, cH=hauteur du buffer (cadre entier).
void drawStScreen(const GlScreen& s, bool captured, bool& reqCapture, float topOffset,
                  int cTop, int cH, int cW) {
    // ANCRÉE : c'est le nœud qui donne position ET taille. On ne pose donc ni pos, ni
    // taille, ni contrainte de ratio (elles se battraient avec le nœud — la fenêtre
    // « pomperait » à chaque trame). L'image, elle, garde son ratio en letterbox.
    // L'état d'ancrage n'est connu qu'APRÈS Begin() → on relit celui de la trame
    // précédente (stable : un (dés)ancrage ne coûte qu'une trame de transition).
    static bool s_docked = false;
    // Aspect pixel ST : la basse rés a des pixels 2× plus larges/hauts que la mono
    // (320×200 et 640×400 couvrent la même surface écran). On dérive l'échelle des
    // dimensions du buffer (overscan inclus) : largeur ×2 si ≤ 480 px (classe basse
    // rés), hauteur ×2 si ≤ 300 lignes (classe 200 lignes).
    const float sx = (s.w <= 480) ? 2.0f : 1.0f;
    const float sy = (s.h <= 300) ? 2.0f : 1.0f;
    // Bornage défensif : la région vient du Glue LIVE, une trame de transition peut la
    // donner hors du buffer courant (changement de résolution).
    const int visTop = std::max(0, std::min(cTop, std::max(0, s.h - 1)));
    const int visH   = std::max(1, std::min(cH, s.h - visTop));
    // La taille « moniteur » se calcule sur la partie VISIBLE : c'est elle qui donne
    // l'aspect à respecter et la contrainte de ratio de la fenêtre.
    const float nativeW = s.w * sx, nativeH = visH * sy;
    const float aspect  = (nativeH > 0.f) ? nativeW / nativeH : 4.f / 3.f;
    static float s_aspect = aspect;   // capté pour le callback (mono/couleur → maj)
    s_aspect = aspect;
    if (!s_docked) {
        // FirstUseEver (et non Always) : on ne fixe la position qu'au tout 1er affichage,
        // sinon la fenêtre serait re-ancrée à chaque trame et impossible à déplacer.
        ImGui::SetNextWindowPos(ImVec2(0.0f, topOffset), ImGuiCond_FirstUseEver);
        // Taille par défaut = native (au 1er affichage) ; ensuite LIBREMENT redimensionnable.
        ImGui::SetNextWindowSize(ImVec2(nativeW, nativeH + 34.f), ImGuiCond_FirstUseEver);
        // Contrainte de ratio : la FENÊTRE garde l'aspect ST (l'image remplit alors sans
        // bandes). ImGui appelle ce callback pendant le redimensionnement.
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(160.f, 120.f), ImVec2(FLT_MAX, FLT_MAX),
            [](ImGuiSizeCallbackData* d) {
                const float extra = 34.f;   // barre de titre + ligne d'aide (approx.)
                const float a = *static_cast<float*>(d->UserData);
                d->DesiredSize.y = (d->DesiredSize.x / a) + extra;
            }, &s_aspect);
    }
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus;
    // Souris capturée → tout le mouvement va au ST (curseur verrouillé) : on FIGE la
    // fenêtre (pas de glissé). Une fois libérée (DEL), elle redevient déplaçable.
    if (captured) flags |= ImGuiWindowFlags_NoMove;
    ImGui::Begin("Atari ST Screen", nullptr, flags);
#ifdef IMGUI_HAS_DOCK
    s_docked = ImGui::IsWindowDocked();   // pour la trame SUIVANTE (cf. plus haut)
#endif
    ImGui::TextDisabled(captured ? "Mouse captured — press DEL to release it"
                                 : "Click inside the screen to capture the mouse (GEM cursor)");
    // Cadrage de l'image dans la zone dispo. Deux régimes :
    //  · Zoom auto (défaut) — RÈGLE DU KIOSK : l'échelle est pilotée par la HAUTEUR,
    //    la région de contenu cale dessus, et la largeur en excès (bordures latérales)
    //    est ROGNÉE aux UV au lieu d'ajouter des bandes. Si le cadre entier tient en
    //    largeur à cette échelle, on retombe sur un pillarbox latéral — exactement les
    //    deux cas de drawStKiosk, transposés du viewport GL aux UV.
    //    PLANCHER DE LARGEUR (bureau uniquement) : la hauteur seule pilotant le zoom,
    //    un panneau plus étroit que haut (docking : l'écran ST partage la fenêtre avec
    //    la Configuration) rognait jusque DANS l'image — bureau GEM amputé de ses menus
    //    « Bureau »/« Options », jeu coupé aux deux bords. L'échelle est donc bornée
    //    pour que `cW` (zone active, ou buffer entier si une bordure est ouverte) tienne
    //    toujours en largeur : on préfère une bande haut/bas à une image amputée. Le
    //    kiosk garde la règle pure — son écran est plus large que haut, le cas ne s'y
    //    présente pas.
    //  · Zoom auto OFF : ancien comportement, cadre entier en letterbox, jamais rogné.
    // Dans les deux cas le ratio pixel ST est respecté : l'image ne se déforme jamais.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float availW = std::max(1.f, avail.x), availH = std::max(1.f, avail.y);
    float dw, dh;
    float u0 = 0.f, u1 = 1.f;
    if (g_autoZoom) {
        // Bornage défensif : cW vient du Glue LIVE comme cTop/cH (une trame de
        // transition peut le donner hors du buffer courant).
        const float keepW = (float)std::max(1, std::min(cW, s.w));
        float scale = availH / (visH * sy);                // px écran par px ST (vertical)
        const float maxScale = availW / (keepW * sx);      // au-delà, on rognerait l'image
        if (scale > maxScale) scale = maxScale;
        dh = visH * sy * scale;                            // ≤ availH (bandes si borné)
        const float fullW = s.w * sx * scale;              // cadre ENTIER à cette échelle
        if (fullW > availW) {                              // déborde → on rogne les côtés
            const float visW = availW / (sx * scale);      // largeur ST réellement montrée
            u0 = 0.5f - visW / (2.f * s.w);
            u1 = 0.5f + visW / (2.f * s.w);
            dw = availW;
        } else {
            dw = fullW;                                    // tient → pillarbox latéral
        }
    } else {
        dw = availW; dh = dw / aspect;
        if (dh > availH) { dh = availH; dw = dh * aspect; }   // limité par la hauteur
    }
    dw = std::max(1.f, dw); dh = std::max(1.f, dh);
    // Centre l'image dans la zone dispo (bandes égales si la fenêtre n'a pas le ratio).
    const ImVec2 cur = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cur.x + (avail.x - dw) * 0.5f,
                               cur.y + (avail.y - dh) * 0.5f));
    const float v0 = (float)visTop / (float)s.h;
    const float v1 = (float)(visTop + visH) / (float)s.h;
    // Passe CRT (ou texture brute si off/indispo). La passe traite TOUJOURS le cadre
    // complet (comme en kiosk) : on lui demande donc la taille qu'aurait le cadre
    // ENTIER à ce zoom, pour que la portion visible garde la densité demandée au lieu
    // d'être sous-échantillonnée. Le cadrage, lui, se fait aux UV.
    const int dstW = (int)std::lround(dw), dstH = (int)std::lround(dh);
    const int fboW = (int)std::lround(dw / std::max(0.001f, u1 - u0));
    const int fboH = (int)std::lround(dh / std::max(0.001f, v1 - v0));
    const ImTextureID id = (ImTextureID)(intptr_t)crtApply(s, std::max(1, fboW), std::max(1, fboH));
    ImGui::Image(id, ImVec2((float)dstW, (float)dstH), ImVec2(u0, v0), ImVec2(u1, v1));
    if (!captured && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        reqCapture = true;
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Pages de la fenêtre « Configuration ». Ce sont des FRAGMENTS (pas de Begin/End) :
// la fenêtre unique les compose. Aucune ne monte ni ne démonte quoi que ce soit —
// elles remplissent des requêtes consommées en fin de trame, seul endroit qui sait
// enchaîner un reset et persister neost.cfg.
// ─────────────────────────────────────────────────────────────────────────────

// Page « Disquettes » : lecteurs A et B + la ludothèque de disks/.
// mountedA/mountedB = chemins montés (vides si lecteur vide).
void drawFloppyPage(const std::string& disksDir,
                    const std::string& mountedA, const std::string& mountedB,
                    std::string& reqMountA, std::string& reqMountB,
                    bool& reqEjectA, bool& reqEjectB) {
    auto driveRow = [](const char* letter, const std::string& mounted, bool& reqEject) {
        if (!mounted.empty()) {
            ImGui::PushID(letter);
            if (IconButton(ICON_FA_EJECT, "Eject")) reqEject = true;
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text("%s: %s", letter, fs::path(mounted).filename().string().c_str());
        } else {
            ImGui::Text("%s: ", letter); ImGui::SameLine();
            ImGui::TextDisabled("(empty)");
        }
    };
    driveRow("A", mountedA, reqEjectA);
    driveRow("B", mountedB, reqEjectB);
    ImGui::Separator();
    ImGui::TextDisabled("Images in %s/", disksDir.c_str());

    std::error_code ec;
    if (!fs::is_directory(disksDir, ec)) {
        ImGui::TextDisabled("(disks/ folder not found)");
        return;
    }
    const fs::path base(disksDir);
    // Récolte RÉCURSIVE des images .st/.msa/.dim/.stx, triées par ordre alphabétique de
    // DOSSIER puis de FICHIER (insensible à la casse) sur le chemin relatif à disks/.
    //
    // ⚠ MISE EN CACHE OBLIGATOIRE. Ce scan tournait à CHAQUE trame, et sa clé de tri
    // appelait fs::relative() — un weakly_canonical(), donc un readlink par composant —
    // DEPUIS le comparateur de std::sort, soit O(n log n) fois. Mesuré : 21 ms par trame
    // (le budget PAL entier) sur les 77 images du dépôt, et l'émulateur tombait de 50 à
    // 0,9 trame/s sur une ludothèque de 3000 images, la fenêtre étant ouverte par défaut.
    // Le chemin relatif est ici purement lexical : lexically_relative() ne touche pas le
    // disque. On calcule la clé UNE fois par image (décorer-trier-dévorer).
    struct Entry { std::string path, rel, key; };
    static std::vector<Entry> cache;
    static std::string  cacheDir;
    static double       cacheTime = -1.0;
    const double now = ImGui::GetTime();
    bool refresh = (cacheDir != disksDir) || cacheTime < 0.0 || (now - cacheTime) > 2.0;
    if (ImGui::SmallButton("Refresh")) refresh = true;
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu images)", cache.size());
    if (refresh) {
        cache.clear();
        cacheDir  = disksDir;
        cacheTime = now;
        // Itération manuelle : le range-for lancerait filesystem_error sur un
        // sous-dossier/symlink illisible.
        fs::recursive_directory_iterator dit(base, fs::directory_options::skip_permission_denied, ec), dend;
        while (!ec && dit != dend) {
            const fs::directory_entry& e = *dit;
            std::error_code ec2;
            if (e.is_regular_file(ec2)) {
                std::string ext = e.path().extension().string();
                for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx") {
                    Entry en;
                    en.path = e.path().string();
                    en.rel  = e.path().lexically_relative(base).generic_string();
                    en.key  = en.rel;
                    for (auto& ch : en.key) ch = (char)std::tolower((unsigned char)ch);
                    cache.push_back(std::move(en));
                }
            }
            dit.increment(ec);
        }
        ec.clear();
        std::sort(cache.begin(), cache.end(),
                  [](const Entry& a, const Entry& b) { return a.key < b.key; });
    }

    for (const auto& en : cache) {
        ImGui::PushID(en.path.c_str());
        // Chemin COMPLET et non nom de fichier : le scan est récursif, et deux
        // dumps homonymes dans deux sous-dossiers (« Xenon/DISK1.ST », « Gods/
        // DISK1.ST » — nommage très courant) étaient tous deux marqués « montée »,
        // aucun n'offrant plus le bouton Monter : le second devenait inaccessible.
        const bool inA = !mountedA.empty() && en.path == mountedA;
        const bool inB = !mountedB.empty() && en.path == mountedB;
        if (inA)      ImGui::TextDisabled("●A");
        else if (ImGui::SmallButton("A")) reqMountA = en.path;
        ImGui::SameLine(0.0f, 4.0f);
        if (inB)      ImGui::TextDisabled("●B");
        else if (ImGui::SmallButton("B")) reqMountB = en.path;
        ImGui::SameLine();
        ImGui::TextUnformatted(en.rel.c_str());        // affiché (montre le dossier)
        ImGui::PopID();
    }
}

// Page « Cartouche » : images de carts/ branchées sur le port $FA0000. Un reset
// reste nécessaire pour que le TOS relise le magic de boot.
void drawCartPage(const std::string& cartsDir, const std::string& mounted,
                  std::string& reqMount, bool& reqEject) {
    if (!mounted.empty()) {
        if (IconButton(ICON_FA_EJECT, "Eject")) reqEject = true;
        ImGui::SameLine();
        ImGui::Text("plugged: %s", fs::path(mounted).filename().string().c_str());
    } else {
        ImGui::TextDisabled("(cartridge port empty)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Images in %s/", cartsDir.c_str());

    std::error_code ec;
    if (fs::is_directory(cartsDir, ec)) {
        const std::string mountedName = mounted.empty() ? "" : fs::path(mounted).filename().string();
        // Itération MANUELLE, comme la page Disquettes : l'error_code passé au constructeur
        // ne couvre QUE la construction — l'incrément du range-for, lui, lève
        // filesystem_error si le dossier devient illisible en cours de parcours (carts/
        // sur une clé USB retirée), et personne ne l'attrape ici → std::terminate.
        fs::directory_iterator it(cartsDir, fs::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end) {
            const fs::directory_entry& e = *it;
            // is_regular_file() SANS error_code LANCE sur un symlink dont la cible est devenue
            // illisible (clé USB débranchée) : rien ne l'attrape ici → std::terminate.
            std::error_code ec2;
            if (e.is_regular_file(ec2)) {
                std::string ext = e.path().extension().string();
                for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == ".bin" || ext == ".img" || ext == ".rom") {
                    const std::string name = e.path().filename().string();
                    ImGui::PushID(name.c_str());
                    if (name == mountedName) {
                        ImGui::TextDisabled("●");          // branchée
                    } else if (ImGui::SmallButton("Plug in")) {
                        reqMount = e.path().string();
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::PopID();
                }
            }
            it.increment(ec);
        }
    } else {
        ImGui::TextDisabled("(carts/ folder not found)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Plugging in / ejecting restarts the machine to re-detect the cart.");
    ImGui::TextDisabled("Exclusive with the GEMDOS drive: both occupy $FA0000.");
}

// Page « Disques durs » : les deux chemins d'accès (HD GEMDOS = dossier hôte monté en
// C:, image ACSI = disque dur émulé sur la cible 0). Ils demandaient jusqu'ici de TAPER
// un chemin absolu dans un menu ; on liste ici les candidats trouvés sur disque — un
// clic monte, comme pour une disquette — le champ libre restant pour ce qui vit
// ailleurs (le glisser-déposer sur la fenêtre couvre le même besoin).
//
// Convention de rangement, volontairement simple et documentée dans hd/README.md :
//   · un DOSSIER dans hd/ (et le dossier gemdos/ du dépôt) = un lecteur GEMDOS ;
//   · un FICHIER image à plat dans hd/ = une image ACSI.
// Le scan des images ACSI n'est donc PAS récursif : sans ça, les .img rangés DANS un
// lecteur GEMDOS (hd/JEUX/DEMOS/truc.img) seraient proposés comme disques durs.
void drawHardDiskPage(const std::string& hdDir, const std::string& gemdosDefault,
                      const std::string& curGemdos, bool gemdosActive,
                      const std::string& curAcsi, bool acsiActive, int acsiParts,
                      std::string& reqMountGemdos, bool& reqEjectGemdos,
                      std::string& reqMountAcsi,  bool& reqEjectAcsi) {
    // Scan mis en cache 2 s, comme la page Disquettes : la fenêtre peut rester ouverte
    // et un parcours de dossier par trame coûte le budget PAL entier sur une grosse
    // ludothèque (cf. la note de drawFloppyPage).
    struct Cand { std::string path, label; };
    static std::vector<Cand> gemCands, acsiCands;
    static std::string cacheDir;
    static double      cacheTime = -1.0;
    const double now = ImGui::GetTime();
    bool refresh = (cacheDir != hdDir) || cacheTime < 0.0 || (now - cacheTime) > 2.0;
    if (refresh) {
        gemCands.clear(); acsiCands.clear();
        cacheDir = hdDir; cacheTime = now;
        std::error_code ec;
        // Le gemdos/ du dépôt est un lecteur à part entière : le proposer en tête.
        if (fs::is_directory(gemdosDefault, ec))
            gemCands.push_back({ gemdosDefault, gemdosDefault + "/" });
        ec.clear();
        if (fs::is_directory(hdDir, ec)) {
            fs::directory_iterator it(hdDir, fs::directory_options::skip_permission_denied, ec), end;
            while (!ec && it != end) {
                const fs::directory_entry& e = *it;
                std::error_code ec2;
                if (e.is_directory(ec2)) {
                    gemCands.push_back({ e.path().string(),
                                         e.path().lexically_relative(fs::path(hdDir).parent_path())
                                             .generic_string() + "/" });
                } else if (e.is_regular_file(ec2)) {
                    std::string ext = e.path().extension().string();
                    for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                    if (ext == ".img" || ext == ".hd" || ext == ".acsi" ||
                        ext == ".vhd" || ext == ".raw") {
                        acsiCands.push_back({ e.path().string(),
                                              e.path().filename().string() });
                    }
                }
                it.increment(ec);
            }
        }
        auto byLabel = [](const Cand& a, const Cand& b) { return a.label < b.label; };
        std::sort(gemCands.begin(),  gemCands.end(),  byLabel);
        std::sort(acsiCands.begin(), acsiCands.end(), byLabel);
    }

    // Le chemin monté vient de la config (« gemdos ») et le candidat du scan
    // (« build/../gemdos ») : la comparaison textuelle rate. fs::equivalent tranche
    // sur l'inode — le coût est négligeable, ces listes tiennent en quelques entrées.
    auto samePath = [](const std::string& a, const std::string& b) {
        if (a.empty() || b.empty()) return false;
        if (a == b) return true;
        std::error_code ec;
        return fs::equivalent(a, b, ec) && !ec;
    };

    // ── GEMDOS ────────────────────────────────────────────────────────────
    ImGui::TextDisabled(ICON_FA_FOLDER_OPEN " GEMDOS — host folder mounted as C:");
    if (gemdosActive) {
        if (IconButton(ICON_FA_EJECT, "Eject the GEMDOS drive")) reqEjectGemdos = true;
        ImGui::SameLine();
        ImGui::Text("mounted: %s", curGemdos.c_str());
        ImGui::TextDisabled("(occupies cartridge port $FA0000 — exclusive with a cartridge)");
    } else {
        ImGui::TextDisabled("(no GEMDOS drive mounted)");
    }
    for (const auto& c : gemCands) {
        ImGui::PushID(c.path.c_str());
        if (gemdosActive && samePath(c.path, curGemdos)) ImGui::TextDisabled("●");
        else if (ImGui::SmallButton("Mount")) reqMountGemdos = c.path;
        ImGui::SameLine();
        ImGui::TextUnformatted(c.label.c_str());
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(-70.0f);
    ImGui::InputTextWithHint("##gdPath", "path to a host folder…", g_gdBuf, sizeof g_gdBuf);
    ImGui::SameLine();
    if (ImGui::Button("Mount##gdFree") && g_gdBuf[0]) reqMountGemdos = g_gdBuf;

    ImGui::Separator();

    // ── ACSI ──────────────────────────────────────────────────────────────
    ImGui::TextDisabled(ICON_FA_HDD " ACSI — hard disk image (target 0)");
    if (acsiActive) {
        if (IconButton(ICON_FA_EJECT, "Eject the ACSI image")) reqEjectAcsi = true;
        ImGui::SameLine();
        ImGui::Text("mounted: %s — %d partition(s)",
                    fs::path(curAcsi).filename().string().c_str(), acsiParts);
    } else {
        ImGui::TextDisabled("(no ACSI image mounted)");
    }
    for (const auto& c : acsiCands) {
        ImGui::PushID(c.path.c_str());
        if (acsiActive && samePath(c.path, curAcsi)) ImGui::TextDisabled("●");
        else if (ImGui::SmallButton("Mount")) reqMountAcsi = c.path;
        ImGui::SameLine();
        ImGui::TextUnformatted(c.label.c_str());
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(-70.0f);
    ImGui::InputTextWithHint("##hdPath", "path to a hard disk image…", g_hdBuf, sizeof g_hdBuf);
    ImGui::SameLine();
    if (ImGui::Button("Mount##hdFree") && g_hdBuf[0]) reqMountAcsi = g_hdBuf;

    if (gemCands.empty() && acsiCands.empty())
        ImGui::TextDisabled("(nothing in %s/ — drop a folder or an image there)", hdDir.c_str());

    // Les deux montés : NeoST ne décale pas le lecteur GEMDOS derrière les partitions
    // ACSI (contrairement à Hatari) → les deux revendiquent C:.
    if (gemdosActive && acsiActive)
        ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f),
                           "GEMDOS and ACSI both claim C:!");
    ImGui::Separator();
    ImGui::TextDisabled("Folder = GEMDOS drive, file = ACSI image. Mounting restarts");
    ImGui::TextDisabled("the machine (TOS only probes disks at boot).");
}

// ─────────────────────────────────────────────────────────────────────────────
// Fenêtre « Configuration » — UNE adresse pour tout régler.
//
// Elle remplace six sous-menus (Modèle, Mémoire, ROM, Cartouche, Disque dur,
// Résolution/Joystick/Son) ET les trois anciennes fenêtres-bibliothèques : il n'y a
// désormais qu'UNE façon de monter un support, quelle qu'en soit la nature. La barre
// de menus ne garde que ce qu'on fait EN JOUANT.
//
// Deux principes qui expliquent la forme :
//  · Elle ne fait RIEN elle-même. Tout sort en requêtes (`ConfigUi::req*`) consommées
//    en fin de trame, seul endroit qui sait enchaîner reset + persistance. C'est la
//    discipline des anciennes bibliothèques, conservée.
//  · Les quatre réglages qui EXIGENT de reconstruire la machine (modèle, RAM, FPU,
//    ROM) sont mis EN ATTENTE au lieu de relancer à chaque clic : on les empile, le
//    pied de page les compte, « Appliquer et redémarrer » relance une seule fois.
//    Les montages, eux, restent immédiats — monter est une action, pas un réglage.
// ─────────────────────────────────────────────────────────────────────────────
struct ConfigUi {
    // Dossiers scannés (résolus une fois au démarrage).
    std::string disksDir, cartsDir, romsDir, hdDir, gemdosDir;
    std::string profDir;          // dossier des profils nommés (profiles/)
    // Lecture de l'état courant.
    const Config* cfg = nullptr;
    Machine* machine  = nullptr;
    float volume = 1.0f;          // volume maître courant (0..1)
    bool  color  = true;          // moniteur courant : couleur (vs mono)
    bool  driveSound = false;     // son du lecteur de disquettes
    bool  driveSoundAvail = false;    // échantillons roms/drivesound/ chargés (sinon : réglage sans effet)
    std::string curGemdos, curAcsi;   // chemins RÉSOLUS des disques durs montés

    // Réglages matériels EN ATTENTE (cf. « Appliquer et redémarrer »).
    std::string pendMachine, pendMem, pendRom;
    bool pendFpu  = false;
    bool pendInit = false;        // faux tant que les champs ci-dessus n'ont pas été semés

    // Requêtes sortantes (toutes consommées puis remises à zéro par l'appelant).
    std::string reqMountA, reqMountB, reqMountCart, reqMountGemdos, reqMountAcsi;
    bool reqEjectA = false, reqEjectB = false, reqEjectCart = false;
    bool reqEjectGemdos = false, reqEjectAcsi = false;
    bool reqApply  = false;       // appliquer les réglages matériels en attente
    bool reqKiosk  = false;
    int  reqMonitor = -1;         // 1 = couleur, 0 = mono
    float reqVolume = -1.0f;      // >= 0 → nouveau volume maître
    bool  volumeDone = false;     // fin de glissé → persister
    int   reqFastFdc = -1;        // 0/1 → nouvelle valeur du FDC rapide
    bool  cfgDirty = false;       // un réglage à effet immédiat a changé → saveConfig
    bool  reqSaveState = false, reqLoadState = false;
    // Profils nommés (page « Profiles ») : le nom demandé, consommé par la boucle.
    std::string reqSaveProfile, reqLoadProfile, reqDeleteProfile;
};

// Page ouverte. Statique : on revient là où on était en rouvrant la fenêtre.
enum ConfigPage {
    kCfgMachine = 0, kCfgMem, kCfgRom, kCfgFloppy, kCfgHd, kCfgCart,
    kCfgScreen, kCfgSound, kCfgInput, kCfgEmul, kCfgProfiles, kCfgKiosk, kCfgCount
};
int g_cfgPage = kCfgFloppy;   // au premier lancement : ce qu'on cherche le plus souvent
bool g_profilesDirty = false; // un profil vient d'être écrit/supprimé → relire le dossier

// Suffixe pays d'une ROM → fréquence de balayage. C'est LA cause d'écran « déchiré »
// la plus fréquente sur les démos européennes (images Spectrum 512 calculées pour le
// 50 Hz, jouées en 60 Hz) et rien ne la montrait dans l'interface. Cf. CLAUDE.md.
bool romIsNtsc(const std::string& filename) {
    const std::string stem = fs::path(filename).stem().string();
    return stem.size() >= 2 && stem.compare(stem.size() - 2, 2, "us") == 0;
}

void drawConfigWindow(ConfigUi& ui) {
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_COG " Configuration", &g_showCfg)) { ImGui::End(); return; }

    const Config& cfg = *ui.cfg;
    // Semis des champs « en attente » : à la première ouverture, et après chaque
    // application (l'appelant remet pendInit à false).
    if (!ui.pendInit) {
        ui.pendMachine = cfg.machine; ui.pendMem = cfg.mem;
        ui.pendRom = cfg.rom;         ui.pendFpu = cfg.fpu;
        ui.pendInit = true;
    }

    // ── Préréglages : la config matérielle complète en un clic. Codés en dur et
    // limités au MATÉRIEL (ils ne font que garnir les champs « en attente »). Les
    // configurations de l'utilisateur, elles, vivent dans la page « Profiles ».
    struct Profil { const char* label; const char* machine; const char* mem;
                    const char* rom; };
    static const Profil kProfils[] = {
        { "520 ST",   "st",      "512k", "roms/tos102uk.img" },
        { "1040 STE", "ste",     "1m",   "roms/tos162uk.img" },
        { "Mega STE", "megaste", "4m",   "roms/tos206fr.img" },
    };
    ImGui::TextDisabled("Presets:");
    for (const auto& p : kProfils) {
        ImGui::SameLine();
        const bool cur = ui.pendMachine == p.machine && ui.pendMem == p.mem
                      && fs::path(ui.pendRom).filename() == fs::path(p.rom).filename();
        if (cur) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(p.label)) {
            ui.pendMachine = p.machine; ui.pendMem = p.mem; ui.pendRom = p.rom;
            ui.pendFpu = false;
        }
        if (cur) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // ── Colonne de navigation + page ──────────────────────────────────────
    static const char* kPageNames[kCfgCount] = {
        ICON_FA_MICROCHIP " Machine",  ICON_FA_MEMORY " Memory",
        ICON_FA_SAVE " ROM / TOS",     ICON_FA_SAVE " Floppies",
        ICON_FA_HDD " Hard disks",     ICON_FA_COMPACT_DISC " Cartridge",
        ICON_FA_DESKTOP " Screen",     ICON_FA_VOLUME_UP " Sound",
        ICON_FA_GAMEPAD " Input",      ICON_FA_BOLT " Emulation",
        ICON_FA_STAR " Profiles",      ICON_FA_DESKTOP " Kiosk",
    };
    // Hauteur réservée au pied de page : le message « à jour » tient sur deux lignes
    // dans une fenêtre étroite — sans la deuxième, il déborde sous le bord.
    const float footH = ImGui::GetFrameHeightWithSpacing() + 2.0f * ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##cfgNav", ImVec2(165, -footH), true);
    for (int i = 0; i < kCfgCount; ++i)
        if (ImGui::Selectable(kPageNames[i], g_cfgPage == i)) g_cfgPage = i;
    ImGui::EndChild();
    ImGui::SameLine();
    // Barre de défilement HORIZONTALE : les noms de dumps sont longs par nature
    // (« st/Blood Money (1989)(Psygnosis)[cr Delight][m Superior][t].st ») et la
    // fenêtre est étroite quand elle est ancrée sur le côté — sans elle, la moitié
    // du nom est simplement invisible, sans aucun moyen d'aller la voir.
    ImGui::BeginChild("##cfgPage", ImVec2(0, -footH), true, ImGuiWindowFlags_HorizontalScrollbar);

    switch (g_cfgPage) {
    case kCfgMachine: {
        ImGui::TextDisabled("Machine model");
        static const char* labels[] = { "ST", "Mega ST", "STE", "Mega STE" };
        static const char* ids[]    = { "st", "megast", "ste", "megaste" };
        for (int i = 0; i < 4; ++i)
            if (ImGui::RadioButton(labels[i], ui.pendMachine == ids[i])) ui.pendMachine = ids[i];
        ImGui::Separator();
        // Le socket MC68881 n'existe QUE sur Mega STE (cf. Fpu.hpp) ; ailleurs il n'y a
        // rien à peupler et « not found » est le comportement fidèle.
        ImGui::BeginDisabled(ui.pendMachine != "megaste");
        ImGui::Checkbox("Populate the MC68881 FPU socket", &ui.pendFpu);
        ImGui::EndDisabled();
        if (ui.pendMachine != "megaste")
            ImGui::TextDisabled("(FPU socket: Mega STE only)");
        ImGui::Separator();
        ImGui::TextWrapped("A TOS ≤ 1.04 booted on an STE/Mega STE switches NeoST to ST "
                           "mode (like Hatari): those TOS versions know nothing of the "
                           "extra hardware.");
        break;
    }
    case kCfgMem: {
        ImGui::TextDisabled("ST-RAM");
        static const char* mlabels[] = { "256 KB", "512 KB", "1 MB", "2 MB", "4 MB" };
        static const char* mids[]    = { "256k", "512k", "1m", "2m", "4m" };
        for (int i = 0; i < 5; ++i)
            if (ImGui::RadioButton(mlabels[i], ui.pendMem == mids[i])) ui.pendMem = mids[i];
        ImGui::Separator();
        ImGui::TextWrapped("512 KB = the 1985 machine. Many games from 1989 on, and most "
                           "cracks/depackers, require 1 MB: with 512 KB they give no "
                           "warning, they just go haywire (black screen frozen after "
                           "the intro).");
        break;
    }
    case kCfgRom: {
        ImGui::TextDisabled("TOS images in %s/", ui.romsDir.c_str());
        std::error_code ec;
        if (fs::is_directory(ui.romsDir, ec)) {
            const std::string curName = fs::path(ui.pendRom).filename().string();
            std::vector<fs::path> roms;
            fs::directory_iterator it(ui.romsDir, fs::directory_options::skip_permission_denied, ec), end;
            while (!ec && it != end) {
                std::error_code ec2;
                if (it->is_regular_file(ec2)) {
                    std::string ext = it->path().extension().string();
                    for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                    if (ext == ".img" || ext == ".rom") roms.push_back(it->path());
                }
                it.increment(ec);
            }
            auto key = [](const fs::path& p) {
                std::string n = p.filename().string();
                for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
                return n;
            };
            std::sort(roms.begin(), roms.end(),
                      [&](const fs::path& a, const fs::path& b) { return key(a) < key(b); });
            for (const auto& p : roms) {
                const std::string name = p.filename().string();
                if (ImGui::RadioButton(name.c_str(), name == curName)) ui.pendRom = p.string();
                ImGui::SameLine();
                if (romIsNtsc(name)) ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f), "60 Hz NTSC");
                else                 ImGui::TextDisabled("50 Hz PAL");
            }
        } else {
            ImGui::TextDisabled("(roms/ folder not found)");
        }
        ImGui::Separator();
        ImGui::TextWrapped("The ROM sets the scan rate: an \"us\" suffix = 60 Hz NTSC, "
                           "\"uk/fr/de/es\" = 50 Hz PAL. European demos come out TORN "
                           "at 60 Hz — faithfully so, that also happens on real "
                           "hardware.");
        break;
    }
    case kCfgFloppy:
        drawFloppyPage(ui.disksDir,
                       ui.machine->fdc.mountedPath(0), ui.machine->fdc.mountedPath(1),
                       ui.reqMountA, ui.reqMountB, ui.reqEjectA, ui.reqEjectB);
        break;
    case kCfgHd:
        drawHardDiskPage(ui.hdDir, ui.gemdosDir,
                         ui.curGemdos, ui.machine->gemdos.active(),
                         ui.curAcsi, ui.machine->fdc.acsiActive(),
                         ui.machine->fdc.acsiPartitionCount(),
                         ui.reqMountGemdos, ui.reqEjectGemdos,
                         ui.reqMountAcsi,   ui.reqEjectAcsi);
        break;
    case kCfgCart:
        drawCartPage(ui.cartsDir, ui.machine->bus.mountedCartPath(),
                     ui.reqMountCart, ui.reqEjectCart);
        break;
    case kCfgScreen: {
        ImGui::TextDisabled("Atari monitor");
        if (ImGui::RadioButton("Color (low/medium res)", ui.color))  ui.reqMonitor = 1;
        if (ImGui::RadioButton("Mono (high res)",        !ui.color))  ui.reqMonitor = 0;
        ImGui::Separator();
        // Même cadrage adaptatif que la borne : l'écran cale sa zone de contenu sur la
        // hauteur disponible, les bordures inutilisées sortent du cadre, et une
        // ouverture de bordure (démo overscan) rend le cadre entier.
        if (ImGui::Checkbox("Auto zoom (adaptive framing)", &g_autoZoom)) ui.cfgDirty = true;
        ImGui::Separator();
        ImGui::TextDisabled("CRT look");
        bool crtChanged = false;
        drawCrtControls(crtChanged);
        if (crtChanged) ui.cfgDirty = true;
        break;
    }
    case kCfgSound: {
        ImGui::TextDisabled("Master volume (host output, independent of the emulated LMC1992)");
        int pct = int(ui.volume * 100.0f + 0.5f);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderInt("##vol", &pct, 0, 100, "%d %%")) ui.reqVolume = float(pct) / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit()) ui.volumeDone = true;
        ImGui::SameLine();
        if (ImGui::SmallButton(ui.volume <= 0.0f ? "Unmute" : "Mute")) {
            ui.reqVolume = (ui.volume <= 0.0f) ? 1.0f : 0.0f;
            ui.volumeDone = true;
        }
        ImGui::Separator();
        // Réglage MÉMORISÉ (drivesound=) — il ne l'était pas : la case se cochait, se
        // décochait, et repartait à « on » au lancement suivant.
        ImGui::BeginDisabled(!ui.driveSoundAvail);
        if (ImGui::Checkbox("Floppy drive sound", &ui.driveSound)) ui.cfgDirty = true;
        ImGui::EndDisabled();
        if (!ui.driveSoundAvail)
            ImGui::TextDisabled("(samples not found in roms/drivesound/)");
        ImGui::Separator();
        ImGui::TextDisabled("Audio latency is set at launch (--audio-latency MS, stored");
        ImGui::TextDisabled("in neost.cfg): changing it live would rebuild the audio");
        ImGui::TextDisabled("ring mid-playback.");
        ImGui::Text("Target cushion: %d ms", cfg.audioLatencyMs);
        break;
    }
    case kCfgInput: {
        // Émulation au clavier : réglage de SESSION, jamais persisté (elle avale les
        // flèches, ce qui « casse » le clavier des jeux qui s'en servent).
        ImGui::Checkbox("Keyboard joystick emulation (arrows + right Ctrl)", &g_kbdJoy);
        ImGui::SameLine(); ImGui::TextDisabled("(F11 — not remembered)");
        ImGui::TextDisabled("ST port driven by the keyboard:");
        if (ImGui::RadioButton("Port 1 (games)", g_kbdJoyPort == 1))  { g_kbdJoyPort = 1; g_joyCfgDirty = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Port 0 (mouse)", g_kbdJoyPort == 0)) { g_kbdJoyPort = 0; g_joyCfgDirty = true; }
        ImGui::Separator();
        // Zone morte centrale des sticks analogiques (anti-drift). Le D-pad numérique
        // n'est pas concerné. Mémorisée à la validation du slider.
        ImGui::TextDisabled("Analog stick dead zone");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("##deadzone", &g_joyDeadzone, 0.0f, 0.95f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            g_joyDeadzone = g_joyDeadzone < 0.0f ? 0.0f : (g_joyDeadzone > 0.95f ? 0.95f : g_joyDeadzone);
            g_joyCfgDirty = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("USB pads detected (1st → port 1, 2nd → port 0):");
        int nPad = 0;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (!glfwJoystickPresent(jid)) continue;
            const char* nm = glfwGetGamepadName(jid);
            if (!nm) nm = glfwGetJoystickName(jid);
            ImGui::BulletText("Port %d: %s", (nPad == 0) ? 1 : 0, nm ? nm : "?");
            ++nPad;
        }
        if (nPad == 0) ImGui::BulletText("(none)");
        break;
    }
    case kCfgEmul: {
        ImGui::TextDisabled("Floppy access speed");
        bool fast = cfg.fastfdc;
        if (ImGui::Checkbox("Fast FDC (delays ÷10)", &fast)) ui.reqFastFdc = fast ? 1 : 0;
        ImGui::TextDisabled("Same as --fastfdc: loading runs at accelerated speed.");
        ImGui::TextDisabled("TURN IT OFF to compare a trace with the Hatari oracle: the");
        ImGui::TextDisabled("frame numbers no longer match between the two.");
        ImGui::Separator();
        ImGui::TextDisabled("Machine state (save-state)");
        if (ImGui::Button(ICON_FA_SAVE " Save state (F5)"))   ui.reqSaveState = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load (F7)"))  ui.reqLoadState = true;
        ImGui::TextDisabled("A state carries a fingerprint of the config: one taken on");
        ImGui::TextDisabled("another machine/ROM is refused rather than applied.");
        break;
    }
    case kCfgProfiles: {
        // Un profil = une PHOTO NOMMÉE des réglages en vigueur. neost.cfg reste la
        // configuration courante (écrite toute seule à chaque changement) ; les profils
        // servent à revenir en un clic sur un attelage machine + ROM + support connu.
        // Lignes COURTES pré-découpées et boutons sur leur propre ligne, comme les
        // autres pages : ancrée sur le côté, la fenêtre est étroite — un TextWrapped y
        // débordait (le défilement horizontal de la page repousse le point de coupure)
        // et un bouton mis en SameLine sortait tout simplement du cadre.
        ImGui::TextDisabled("A named snapshot of the settings in effect:");
        ImGui::TextDisabled("machine, RAM, FPU, ROM, media, monitor, CRT,");
        ImGui::TextDisabled("sound, input. Loading one restarts the machine");
        ImGui::TextDisabled("(same single restart as \"Apply and restart\").");
        ImGui::Separator();
        // Le mode borne n'écrit RIEN sur le disque (« la borne repart identique ») :
        // les profils y sont consultables mais ni créés ni supprimés.
        const bool frozen = g_kiosk || g_kioskLaunched;
        static char nameBuf[80] = "";
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##profname", "profile name", nameBuf, sizeof nameBuf);
        const std::string clean = profileFileName(nameBuf);
        ImGui::BeginDisabled(clean.empty() || frozen);
        if (ImGui::Button(ICON_FA_SAVE " Save current settings")) {
            ui.reqSaveProfile = clean;
            nameBuf[0] = '\0';
        }
        ImGui::EndDisabled();
        if (frozen) ImGui::TextDisabled("(kiosk: frozen configuration, nothing is written)");
        ImGui::Separator();

        // Scan du dossier MIS EN CACHE : la fenêtre est redessinée à chaque trame, et
        // la page Disquettes a déjà payé le prix d'un parcours de dossier par trame
        // (21 ms, le budget PAL entier). Rafraîchi toutes les 2 s, et immédiatement
        // après une écriture ou une suppression (g_profilesDirty).
        struct ProfCache { std::vector<std::string> names; std::string dir; double t = -1.0; };
        static ProfCache pc;
        const double nowP = ImGui::GetTime();
        if (g_profilesDirty || pc.dir != ui.profDir || pc.t < 0.0 || (nowP - pc.t) > 2.0) {
            pc.names = listProfiles(ui.profDir);
            pc.dir   = ui.profDir;
            pc.t     = nowP;
            g_profilesDirty = false;
        }
        if (pc.names.empty()) ImGui::TextDisabled("(no profile saved yet)");
        // Suppression en DEUX temps : un profil est le fruit d'un réglage patient, et
        // les boutons sont côte à côte dans une fenêtre étroite.
        static std::string confirmDel;
        for (const std::string& n : pc.names) {
            ImGui::PushID(n.c_str());
            if (ImGui::SmallButton("Load")) ui.reqLoadProfile = n;
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::BeginDisabled(frozen);
            if (confirmDel == n) {
                if (ImGui::SmallButton("Delete?")) { ui.reqDeleteProfile = n; confirmDel.clear(); }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::SmallButton("Cancel")) confirmDel.clear();
            } else {
                if (ImGui::SmallButton("Overwrite")) ui.reqSaveProfile = n;
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::SmallButton("Delete")) confirmDel = n;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(n.c_str());
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Files: profiles/*.cfg, next to neost.cfg.");
        ImGui::TextDisabled("Debug windows / docking layout are NOT saved.");
        ImGui::TextDisabled("Audio latency only applies at the next launch.");
        break;
    }
    case kCfgKiosk: {
        ImGui::TextWrapped("Kiosk mode goes full screen with no chrome, freezes the "
                           "configuration (nothing more is written to neost.cfg) and "
                           "enables keyboard joystick emulation. Leave it with F8.");
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_DESKTOP " Switch to kiosk mode (F8)")) ui.reqKiosk = true;
        break;
    }
    default: break;
    }
    ImGui::EndChild();

    // ── Pied de page : réglages matériels en attente ──────────────────────
    int pending = 0;
    if (ui.pendMachine != cfg.machine) ++pending;
    if (ui.pendMem     != cfg.mem)     ++pending;
    if (ui.pendFpu     != cfg.fpu)     ++pending;
    if (fs::path(ui.pendRom).filename() != fs::path(cfg.rom).filename()) ++pending;
    if (pending > 0) {
        // Texte PUIS boutons sur leur propre ligne : ancrée sur le côté, la fenêtre
        // est étroite et « Appliquer et redémarrer » sortait du cadre.
        ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f), ICON_FA_REDO " %d pending hardware setting%s",
                           pending, pending > 1 ? "s" : "");
        if (ImGui::Button("Cancel")) ui.pendInit = false;       // resème depuis cfg
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_POWER_OFF " Apply and restart")) ui.reqApply = true;
    } else {
        ImGui::TextDisabled("Machine up to date. Model, RAM, FPU and ROM are applied");
        ImGui::TextDisabled("together, by a single restart.");
    }
    ImGui::End();
}
#endif // NEOST_WITH_IMGUI
} // namespace

int main(int argc, char** argv) {
    // Répertoire de l'exécutable (pour retrouver roms/ et disk/ depuis build/).
    // Lancé via le PATH, argv[0] est NU (« neost ») : l'ancien repli « . » faisait
    // écrire neost.cfg/neost.state dans ../ du cwd courant (config « split-brain »).
    // → on résout le vrai chemin : /proc/self/exe (Linux), _NSGetExecutablePath (macOS).
    const std::string exeDir = [&] {
        std::error_code ec;
#if defined(__linux__)
        const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec && self.has_parent_path()) return self.parent_path().string();
#elif defined(__APPLE__)
        char buf[4096]; uint32_t sz = sizeof buf;
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            const auto self = std::filesystem::canonical(buf, ec);
            if (!ec && self.has_parent_path()) return self.parent_path().string();
        }
#elif defined(_WIN32)
        // GetModuleFileNameW et non argv[0] : lancé depuis le menu Démarrer ou par
        // association de fichier, argv[0] peut être nu, et le cwd est alors celui de
        // l'explorateur — roms/ et neost.cfg seraient cherchés n'importe où.
        // La version W (et non A) : un chemin contenant des accents — « C:\\Users\\Frédéric »
        // — ressort en mojibake avec la version ANSI et aucun fichier n'est trouvé.
        {
            std::wstring buf(MAX_PATH, L'\0');
            for (;;) {
                const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
                if (n == 0) break;                       // échec : on tombera sur argv[0]
                if (n < buf.size()) { buf.resize(n); break; }
                if (buf.size() >= 32768) break;          // borne NT : on abandonne
                buf.resize(buf.size() * 2);              // tronqué : on réessaie plus grand
            }
            if (!buf.empty()) {
                const auto self = std::filesystem::path(buf).lexically_normal();
                if (self.has_parent_path()) return self.parent_path().string();
            }
        }
#endif
        const std::string a0 = argv[0] ? argv[0] : "";
        const auto i = a0.find_last_of('/');
        return (i == std::string::npos) ? std::string(".") : a0.substr(0, i);
    }();
    // Préférences mémorisées (dernier ROM + type de moniteur).
    Config cfg = loadConfig(exeDir);
    g_cfgPristine = cfg;      // référence figée pour le mode borne (cf. saveConfig)
    g_showHex = cfg.showHex; g_showCpu = cfg.showCpu;
    g_showJoy = cfg.showJoy; g_showCfg = cfg.showCfg;
    // Disposition ancrée : un imgui.ini écrit par une version antérieure garde des
    // nœuds pour des fenêtres qui n'existent plus (Disk/Cart Library) et ne connaît
    // pas la fenêtre Configuration — qui flotterait alors au-dessus de l'écran ST.
    // On resème la disposition UNE fois, puis on note la version dans neost.cfg.
    static constexpr int kUiVersion = 3;
    const bool uiLayoutOutdated = (cfg.uiVersion < kUiVersion);
    cfg.uiVersion = kUiVersion;
    g_dockOn   = cfg.dock;     // mode ancré mémorisé (cf. renderDockSpace)
    g_autoZoom = cfg.autoZoom; // zoom adaptatif de l'écran ST (bureau ET kiosk)
    g_crtOn    = cfg.crt;      g_crtParams = cfg.crtParams;   // effets CRT (figés en kiosk)
    g_kioskRomDirs = cfg.romDirs;   // dossiers ROM additionnels du menu kiosk (persistés)
    const std::string defRom = cfg.rom.empty() ? std::string("roms/etos192us.img") : cfg.rom;
    // Ligne de commande : arguments POSITIONNELS (ROM, disque) + DRAPEAUX.
    //   --kiosk            : borne en vrai plein écran EXCLUSIF (reste au-dessus de
    //                        tout, ne peut pas être recouvert par une autre fenêtre).
    //   --kiosk-monitor N  : moniteur cible (0 = principal ; défaut 0).
    int  kioskMonitor = 0;
    int  audioLatencyCli = 0;   // 0 = pas d'override CLI (on garde la valeur du neost.cfg)
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i] ? argv[i] : "";
        if (a == "--version") {           // identité de build (release-readiness)
#ifdef NEOST_VERSION
            std::printf("NeoST %s\n", NEOST_VERSION);
#else
            std::printf("NeoST (unknown version)\n");
#endif
            return 0;
        }
        if      (a == "--kiosk")           g_kiosk = g_kioskLaunched = true;
        else if (a == "--kiosk-monitor" && i + 1 < argc) kioskMonitor = std::atoi(argv[++i]);
        //   --audio-latency MS : coussin audio visé (défaut 85, borné [20,250] par Audio).
        //   Monter à 120-150 sur une machine juste (borne Raspberry Pi) : un underrun coûte
        //   un trou audible le temps de ré-amorcer, une latence un peu plus haute non.
        else if (a == "--audio-latency" && i + 1 < argc) audioLatencyCli = std::atoi(argv[++i]);
        //   --crt              : active les effets CRT (façade moniteur).
        //   --crt-preset NAME  : preset (off|leger|arcade|phosphor) ; implique --crt.
        else if (a == "--crt")             g_crtOn = true;
        else if (a == "--crt-preset" && i + 1 < argc) {
            const std::string name = argv[++i];
            if (!applyCrtPreset(name, g_crtParams, g_crtOn))
                std::fprintf(stderr, "[main] unknown CRT preset: '%s' "
                             "(off|light|arcade|phosphor)\n", name.c_str());
        }
        else if (!a.empty() && a[0] != '-') pos.push_back(a);
    }
    // Les overrides CLI priment sur le cfg (et resteront cohérents si le panneau
    // déclenche un save ultérieur en mode fenêtré).
    cfg.crt = g_crtOn; cfg.crtParams = g_crtParams;
    if (audioLatencyCli > 0) cfg.audioLatencyMs = audioLatencyCli;
    // Sans argument positionnel, ./neost recharge le dernier ROM (ou EmuTOS US).
    const std::string romLogical = !pos.empty() ? pos[0] : defRom;
    const std::string tosPath  = resolveData(romLogical, exeDir);
    const std::string defDisk  = cfg.disk.empty() ? std::string("disks/diskA.st") : cfg.disk;
    const std::string diskPath = resolveData(pos.size() > 1 ? pos[1] : defDisk, exeDir);
    const std::string cartPath = cfg.cart.empty() ? std::string() : resolveData(cfg.cart, exeDir);
    const std::string disksDir = resolveData("disks", exeDir);   // dossier pour la Disk Library
    const std::string cartsDir = resolveData("carts", exeDir);   // dossier pour la Cart Library
    const std::string hdDir    = resolveData("hd", exeDir);      // dossier pour la fenêtre Hard Disks
    const std::string gemdosDir = resolveData("gemdos", exeDir); // lecteur GEMDOS livré avec le dépôt
    const std::string romsDir  = resolveData("roms", exeDir);     // dossier pour le sélecteur de ROM

    g_dbgMouse = std::getenv("NEOST_DEBUG_MOUSE") != nullptr;
    g_dbgJoy   = std::getenv("NEOST_DEBUG_JOY")   != nullptr;

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;

    // Pas de hint de profil → contexte legacy compatible (GL 2.1, immediate mode).
    // Fenêtre hôte large : elle héberge la fenêtre ImGui "Atari ST Screen" + le debug.
    GLFWwindow* window = nullptr;
    if (g_kiosk) {
        int nmon = 0; GLFWmonitor** mons = glfwGetMonitors(&nmon);
        // Index borné DES DEUX côtés : --kiosk-monitor -1 passerait la borne haute
        // seule et lirait mons[-1] (pointeur poubelle → crash dans glfwGetVideoMode).
        GLFWmonitor* mon = (mons && kioskMonitor >= 0 && kioskMonitor < nmon)
                               ? mons[kioskMonitor] : glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        // Vrai plein écran EXCLUSIF : la fenêtre appartient au moniteur → reste
        // au-dessus de TOUT (panneaux/dock inclus), impossible à recouvrir. Hints
        // calés sur le mode courant → aucun changement de résolution.
        if (vm) {
            glfwWindowHint(GLFW_RED_BITS,     vm->redBits);
            glfwWindowHint(GLFW_GREEN_BITS,   vm->greenBits);
            glfwWindowHint(GLFW_BLUE_BITS,    vm->blueBits);
            glfwWindowHint(GLFW_REFRESH_RATE, vm->refreshRate);
        }
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        window = glfwCreateWindow(vm ? vm->width : 1280, vm ? vm->height : 860,
                                  "NeoST", mon, nullptr);
    } else {
#ifdef NEOST_VERSION
        window = glfwCreateWindow(1280, 860, "NeoST " NEOST_VERSION " — Atari ST", nullptr, nullptr);
#else
        window = glfwCreateWindow(1280, 860, "NeoST — Atari ST", nullptr, nullptr);
#endif
    }
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (g_kiosk) {
        // Curseur masqué + souris capturée d'emblée : les jeux GEM (souris) comme les
        // jeux joystick sont jouables à la borne, sans curseur hôte visible.
        g_mouseCaptured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    // VSync DÉSACTIVÉ : la boucle est cadencée par le bridage au temps émulé
    // (sleep_until, cf. plus bas), pas par l'écran. Avec vsync ON, swapBuffers
    // BLOQUE jusqu'au vblank suivant : sur un écran 60 Hz, le sleep à ~20 ms +
    // l'attente du vblank faisaient battre la boucle à ~30-37 fps au lieu de 50 →
    // temps émulé ralenti de 25-40 % (musique LENTE, tempo cadencé par les IRQ
    // émulées) et anneau audio produit sous le débit drainé (son HACHÉ, bruits
    // lecteur compris — même anneau). Le modèle « push » audio exige que la boucle
    // tienne EXACTEMENT la cadence des trames émulées.
    glfwSwapInterval(0);

    // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
    const MachineType machType0 = Machine::adjustMachineForTos(parseMachine(cfg.machine), tosPath);
    Machine machine(parseRamBytes(cfg.mem), Cpu68k::parseCore(cfg.cpu),
                    machType0);                   // RAM + cœur + modèle (cfg, ajusté au TOS)
    std::fprintf(stderr, "[main] CPU core: %s | machine: %s | RAM: %s\n",
                 Cpu68k::coreName(machine.cpu.core()),
                 machineName(machType0), cfg.mem.c_str());
    if (!machine.loadTos(tosPath))
        std::fprintf(stderr, "[main] Starting without a TOS (the CPU will run on nothing).\n");
    updateKbdCountry(machine.bus.rom);    // pays du TOS → surcharges keymap (FR/DE/UK…)
    if (!machine.loadDisk(diskPath))
        std::fprintf(stderr, "[main] No floppy mounted (%s).\n", diskPath.c_str());
    // Lecteur B mémorisé (diskb=) : remonté au démarrage comme le A. Silencieux si
    // l'image a disparu — un second lecteur vide est un état parfaitement normal.
    if (!cfg.diskb.empty() && !machine.fdc.loadImage(resolveData(cfg.diskb, exeDir), 1)) {
        std::fprintf(stderr, "[main] Drive B: image not found (%s).\n", cfg.diskb.c_str());
        cfg.diskb.clear();
    }
    if (!cartPath.empty() && !machine.loadCart(cartPath))
        std::fprintf(stderr, "[main] No cartridge mounted (%s).\n", cartPath.c_str());
    // Résolution de chemin façon resolveData mais tolérante aux DOSSIERS (le HD
    // GEMDOS monte un répertoire ; fs::exists accepte fichiers et dossiers).
    auto resolvePath = [&exeDir](const std::string& given) -> std::string {
        const std::string cands[] = { given, exeDir + "/" + given, exeDir + "/../" + given, "../" + given };
        std::error_code ec;
        for (const auto& c : cands) if (fs::exists(c, ec)) return c;
        return given;
    };
    // Disque dur GEMDOS : mappe un dossier hôte sur C: en redirigeant les appels
    // GEMDOS (façon Hatari). Installe une cartouche système à $FA0000 → exclusif
    // avec une cartouche externe. Mémorisé dans neost.cfg (gemdos=) et configurable
    // via le menu Machine → Disque dur ; NEOST_GEMDOS_DIR prime. Cf. io/GemdosHd.hpp
    // et l'option --gemdos du headless.
    if (const char* gd = std::getenv("NEOST_GEMDOS_DIR")) cfg.gemdos = gd;
    if (const char* hd = std::getenv("NEOST_ACSI_IMG"))   cfg.acsi  = hd;
    if (!cfg.gemdos.empty()) {
        if (!cartPath.empty())
            std::fprintf(stderr, "[main] cartridge ignored: incompatible with GEMDOS HD\n");
        if (!machine.gemdos.setDirectory(resolvePath(cfg.gemdos)))
            cfg.gemdos.clear();                // dossier invalide → on ne le mémorise pas
    }
    // Disque dur ACSI (image brute, cible 0) : le TOS lit la table de partitions et
    // monte C:/D:… Mémorisé (acsi=) et configurable via le menu ; NEOST_ACSI_IMG
    // prime. Cf. io/Acsi.hpp.
    if (!cfg.acsi.empty()) {
        if (machine.fdc.mountAcsi(resolvePath(cfg.acsi)))
            std::fprintf(stderr, "[main] ACSI : %d partition(s)\n", machine.fdc.acsiPartitionCount());
        else cfg.acsi.clear();                 // image invalide → idem
    }
    machine.mfp.setColorMonitor(!cfg.mono);   // moniteur mémorisé (avant le reset)
    machine.fdc.setFastFdc(cfg.fastfdc);      // FDC rapide mémorisé (accès disque ÷10)
    // Socket MC68881 (Mega STE uniquement, cf. Fpu.hpp) : sonde + trapping.
    machine.bus.setFpuPresent(cfg.fpu && machType0 == MachineType::MegaSte);
    machine.reset();
    loadRtcFromConfig(machine, cfg);                 // horloge Mega : reprise neost.cfg + pont hôte
    cfg.rom = romLogical; saveConfig(exeDir, cfg, &machine);

    // Son : un seul périphérique (Audio) mixe le YM2149 ET les bruits mécaniques
    // du lecteur. Le cœur émet des FdcSound, DriveSound joue les WAV de
    // roms/drivesound/ (jeu « epson_smd480l » = vrai lecteur) et Audio les
    // additionne au flux PSG (cf. Audio::render).
    DriveSound drive;
    // DISPONIBILITÉ (échantillons chargés) et ACTIVATION (réglage drivesound=) sont deux
    // choses : c'est la disponibilité qui décide du câblage — brancher DriveSound sur
    // l'Audio et armer le sink FdcSound — car ce câblage se fait UNE fois, ici. Le
    // réglage, lui, se rebascule à chaud (case de la page Son, chargement d'un profil) ;
    // s'il avait décidé du câblage, démarrer son coupé aurait rendu la case sans effet
    // pour toute la session.
    const bool driveSoundAvail = drive.init(resolveData("roms/drivesound/epson_smd480l", exeDir), 48000);
    bool driveSoundOn = driveSoundAvail && cfg.driveSound;
    drive.setEnabled(driveSoundOn);
    Audio audio(machine.psg, driveSoundAvail ? &drive : nullptr, &machine.dmasnd);
    audio.setLatencyMs(uint32_t(cfg.audioLatencyMs < 0 ? 0 : cfg.audioLatencyMs));  // AVANT start (borné dans Audio)
    audio.start();   // échec silencieux possible (CI / pas de carte son)
    // Sink FdcSound armé SEULEMENT si la sortie audio existe : sans elle, produceFrame ne
    // draine jamais DriveSound et chaque Step/Seek/Index allouait un son miniaudio jamais
    // recyclé (croissance mémoire non bornée sur une longue session).
    if (driveSoundAvail && audio.ok())
        machine.fdc.setSoundSink([&drive](FdcSound e) { drive.onEvent(e); });
    audio.setMasterVolume(cfg.volume);   // volume maître mémorisé (menu Son, neost.cfg)
    // La chaîne DMA/LMC1992 ne s'applique QUE si la machine courante a le son DMA :
    // sur ST/Mega ST le gain de rattrapage LMC (×2, compensation du ½-YM STE)
    // doublait le YM (clipping) et l'état microwire d'une session STE colorait le ST.
    // Prédicat DYNAMIQUE : suit les reconfigure à chaud (applyConfig).
    audio.setDmaGate([&machine] { return machineHasDmaSound(machine.machineType()); });
    // Modèle « push » (Phase C) : on ARME l'horodatage des écritures PSG (cycle CPU dans
    // la trame). Dès lors, write8 enregistre les écritures et la synthèse les rejoue au bon
    // instant (digidrums/sync-buzzer). produceFrame (après runFrame) génère et empile la trame.
    machine.psg.setCycleClock([&machine] { return machine.frameRelCycle(); });
    // Idem pour le son DMA STE : on horodate les transitions PLAY/STOP de la trame, pour
    // les rejouer au cycle exact (bruitages one-shot courts, queues de samples — cf.
    // DmaSound::mixStereo). Posé UNIQUEMENT côté frontend (jamais en headless → pas de fuite).
    machine.dmasnd.setCycleClock([&machine] { return machine.frameRelCycle(); });

    GlScreen screen;
    screen.init();

    // Applique la config courante (modèle / RAM / cœur / ROM) À CHAUD : reconfigure
    // la Machine en place (son adresse ne change pas → les références d'Audio vers
    // psg/dmasnd restent valides), recharge la ROM, repose le moniteur, puis reset.
    // C'est un hard reset avec les nouveaux paramètres — aucun redémarrage de l'appli.
    // Le disque monté est conservé.
    auto applyConfig = [&] {
        const std::string romP = resolveData(cfg.rom, exeDir);
        // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
        const MachineType machTypeR = Machine::adjustMachineForTos(parseMachine(cfg.machine), romP);
        machine.reconfigure(parseRamBytes(cfg.mem), Cpu68k::parseCore(cfg.cpu), machTypeR);
        if (!machine.loadTos(romP))
            // ROM absente/illisible (profil pointant un TOS non installé) : l'ANCIENNE
            // ROM reste chargée — on le dit au lieu de laisser croire au nouveau profil.
            std::fprintf(stderr, "[main] WARNING ROM not found: %s — the previous ROM stays active\n",
                         romP.c_str());
        updateKbdCountry(machine.bus.rom);   // la nouvelle ROM peut changer de pays clavier
        if (cfg.cart.empty()) machine.ejectCart();
        else                  machine.loadCart(resolveData(cfg.cart, exeDir));
        // L'eject/loadCart ci-dessus a écrasé la cartouche système du HD GEMDOS →
        // réaligne les montages disque dur sur la config : réinstalle le HD GEMDOS
        // s'il est configuré (exclusif avec cfg.cart), le démonte sinon — un profil
        // peut monter OU démonter, et reconfigure() ne touche pas à ces montages.
        // Idem ACSI (le montage survit à reconfigure, on ne remonte que si absent).
        if (cfg.gemdos.empty()) machine.gemdos.unmount();
        else                    machine.gemdos.setDirectory(resolvePath(cfg.gemdos));
        if (cfg.acsi.empty())   machine.fdc.unmountAcsi();
        else if (!machine.fdc.acsiActive()) machine.fdc.mountAcsi(resolvePath(cfg.acsi));
        machine.mfp.setColorMonitor(!cfg.mono);
        machine.fdc.setFastFdc(cfg.fastfdc);   // ré-applique le FDC rapide après reconfig
        machine.bus.setFpuPresent(cfg.fpu && machTypeR == MachineType::MegaSte);
        machine.reset();
        std::fprintf(stderr, "[main] live reconfigure: core %s | machine %s | RAM %s\n",
                     Cpu68k::coreName(machine.cpu.core()),
                     machineName(machTypeR), cfg.mem.c_str());
    };

    // Callbacks installés AVANT ImGui : ImGui chaîne les nôtres derrière les siens.
    g_ikbd = &machine.ikbd;
    // Émulation joystick clavier : off en mode normal (elle avale les flèches),
    // mais ON d'emblée en KIOSK — une borne se joue au joystick (flèches + Ctrl
    // droit = feu), sans menu pour l'activer. F11 la rebascule si besoin (jeu clavier).
    g_kbdJoy     = g_kiosk;
    g_kbdJoyPort = cfg.joyport;
    g_joyDeadzone = cfg.joydeadzone;    // zone morte des sticks (mémorisée)
    joymapParse(cfg.joymap);            // affectation manettes→ports (par GUID)
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    // Glisser-déposer : le seul chemin vers un support qui ne vit PAS dans les dossiers
    // du dépôt, faute de sélecteur de fichiers natif (aucune dépendance à ajouter pour
    // ça). Posé AVANT ImGui_ImplGlfw_InitForOpenGL(.., true) : le backend ImGui chaîne
    // vers le callback déjà installé, alors qu'installer après l'écraserait.
#if defined(NEOST_WITH_IMGUI)
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; ++i)
            if (paths[i]) g_dropped.emplace_back(paths[i]);
    });
#endif

#if defined(NEOST_WITH_IMGUI)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
#ifdef IMGUI_HAS_DOCK
    // Ancrage (branche `docking` de Dear ImGui) : les fenêtres de debug deviennent
    // des onglets d'une disposition persistante. Le MULTI-VIEWPORT (l'autre apport
    // de la branche) reste volontairement COUPÉ : il sort les fenêtres du contexte
    // GL de la fenêtre hôte, ce que le backend OpenGL 2 immediate-mode et la passe
    // CRT (FBO liés à ce contexte) ne suivent pas.
    if (g_dockOn) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    // Enregistre la taille de la fenêtre PRINCIPALE dans imgui.ini : gestionnaire de
    // réglages personnalisé, posé AVANT le 1er NewFrame (qui charge imgui.ini et applique
    // la taille relue). Un resize marque les réglages « sales » → ImGui resauvegarde.
    g_window = window;
    {
        ImGuiSettingsHandler h;
        h.TypeName   = "NeoST";
        h.TypeHash   = ImHashStr("NeoST");
        h.ReadOpenFn = WinSettings_ReadOpen;
        h.ReadLineFn = WinSettings_ReadLine;
        h.ApplyAllFn = WinSettings_ApplyAll;
        h.WriteAllFn = WinSettings_WriteAll;
        ImGui::AddSettingsHandler(&h);
    }
    glfwSetWindowSizeCallback(window, [](GLFWwindow*, int, int) {
        if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
    });
    // Police de l'interface : DejaVu Sans (dossier fonts/), nettement plus lisible que
    // la police bitmap intégrée d'ImGui. Doit être chargée AVANT le 1er rendu (l'atlas
    // est construit à la 1re trame). Repli silencieux sur la police par défaut si absente.
    {
        ImGuiIO& io = ImGui::GetIO();
        const std::string fontPath = resolveData("fonts/DejaVuSans.ttf", exeDir);
        if (fileExists(fontPath)) {
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.0f);
        } else {
            io.Fonts->AddFontDefault();   // base toujours présente (requis avant la fusion FA)
            std::fprintf(stderr, "[main] font %s not found — falling back to the default ImGui font.\n",
                         fontPath.c_str());
        }
        // Fusionne les pictogrammes Font Awesome dans la police courante (menus/boutons).
        const std::string faPath = resolveData("fonts/fa-solid-900.ttf", exeDir);
        if (fileExists(faPath)) {
            static const ImWchar fa_ranges[] = { 0xf000, 0xf8ff, 0 };
            ImFontConfig fcfg;
            fcfg.MergeMode = true;            // ajoute les glyphes FA à la police de base
            fcfg.PixelSnapH = true;
            fcfg.GlyphMinAdvanceX = 14.0f;    // chasse fixe des icônes (alignement)
            fcfg.GlyphOffset.y = 1.0f;        // léger recentrage vertical sur la ligne de texte
            io.Fonts->AddFontFromFileTTF(faPath.c_str(), 13.0f, &fcfg, fa_ranges);
        } else {
            std::fprintf(stderr, "[main] icon font %s not found — no pictograms.\n",
                         faPath.c_str());
        }
    }
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    // Disposition d'une version antérieure → on la resème une fois (cf. kUiVersion).
    if (uiLayoutOutdated) g_dockReset = true;
    ImGui_ImplOpenGL2_Init();
#endif

    std::printf("[main] Click inside the screen: capture mouse | DEL: release | "
                "Reset button in the CPU window | close the window: quit\n");
    std::printf("[main] Joystick: USB pad auto-detected (port 1) | F11 = keyboard "
                "emulation (arrows + right Ctrl) | \"Joystick\" menu\n");

    // Bridage à la durée ÉMULÉE de chaque trame : indispensable pour que le temps
    // émulé colle au temps réel — sinon le compteur 200 Hz d'EmuTOS s'emballe et les
    // écarts de double-clic (mesurés en tics) deviennent trop grands. Le vsync ne
    // suffit pas (60 Hz, voire non limitant). ⚠ La période N'EST PAS fixe : la
    // géométrie vidéo (50/60/71 Hz, cf. Shifter::Geometry) change la durée d'une
    // trame — 313×512 cyc ≈ 19,98 ms (50 Hz), 263×508 ≈ 16,66 ms (60 Hz, le défaut
    // d'EmuTOS US et de beaucoup de jeux NTSC), 501×224 ≈ 13,99 ms (mono 71 Hz).
    // L'ancien bridage FIXE à 20 ms ralentissait un écran 60 Hz de ~17 % : temps
    // émulé < temps réel → musique RALENTIE et anneau audio affamé (son HACHÉ, y
    // compris les bruits du lecteur, mixés dans le même anneau) — produceFrame
    // pousse `frameCycles × rate / 8 MHz` échantillons par trame, le thread audio
    // en draine `rate` par seconde : la cadence trame doit suivre la géométrie.
    using clock = std::chrono::steady_clock;
    static constexpr double kCpuHz = 8021248.0;   // horloge CPU/bus (PAL)
    auto emuNext = clock::now();   // échéance réelle de la prochaine trame émulée

    double lastMx = 0, lastMy = 0;
    if (g_kiosk) glfwGetCursorPos(window, &lastMx, &lastMy);   // évite le saut au 1er delta
    // Géométrie fenêtrée de départ (à retrouver en quittant le kiosk). Lancé en
    // --kiosk, on n'a JAMAIS été fenêtré : g_winX/g_winY resteraient à (0,0) et la
    // première sortie de borne collerait la fenêtre à l'origine de l'écran VIRTUEL
    // (donc sur le mauvais moniteur en bi-écran, barre de titre hors champ sous X11).
    // D'où le drapeau : tant qu'il est faux, la sortie de borne centre la fenêtre.
    if (!g_kiosk) {
        glfwGetWindowPos(window, &g_winX, &g_winY);
        glfwGetWindowSize(window, &g_winW, &g_winH);
        g_winGeomValid = true;
    }

    // ─── Bascule GUI ⇄ kiosk à chaud ────────────────────────────────────────
    // Le kiosk n'est plus seulement un drapeau de lancement : F8 (ou le menu
    // « Machine », ou l'action DESKTOP MODE du menu borne) fait l'aller-retour sans
    // relancer l'application. La MACHINE traverse la bascule intacte :
    //   1. instantané mémoire (Machine::saveState) AVANT de toucher à quoi que ce soit ;
    //   2. la fenêtre GLFW change de moniteur (plein écran exclusif ⇄ fenêtré) — le
    //      contexte GL, ses textures et la passe CRT survivent, on ne recrée rien ;
    //   3. l'instantané est rechargé derrière (Machine::loadState).
    // L'étape 3 est la ceinture de sécurité de l'étape 2 : le ST repart exactement
    // dans l'état quitté (jeu en cours compris) quoi que la bascule remue côté hôte
    // — reconfiguration d'entrée, recalage de cadence, anneau audio.
    // ⚠ À n'appeler qu'entre deux runFrame (loadState refuse le milieu de trame).
    // kbdJoyDesk : réglage « joystick au clavier » à rendre au bureau. Initialisé à
    // false et NON à g_kbdJoy — à ce point g_kbdJoy vaut g_kiosk, donc lancé en borne
    // on capturerait true et la sortie vers le bureau avalerait les flèches + Ctrl
    // droit du ST sans rien afficher. La branche GUI→KIOSK le rafraîchit ensuite.
    auto switchKioskMode = [&, kbdJoyDesk = (g_kiosk ? false : g_kbdJoy)](bool on) mutable {
        if (on == g_kiosk) return;
        std::vector<uint8_t> snap;
        machine.saveState(snap);                 // (1) instantané

        if (on) {
            // GUI → KIOSK. La config est GELÉE en kiosk : on persiste d'abord l'état
            // courant, sinon les préférences de la session seraient perdues.
            cfg.disk = machine.fdc.mountedPath();
            cfg.cart = machine.bus.mountedCartPath();
            cfg.mono = !machine.mfp.colorMonitor();
            cfg.showHex  = g_showHex;
            cfg.showCpu  = g_showCpu;  cfg.showJoy  = g_showJoy;  cfg.dock = g_dockOn;
            cfg.showCfg  = g_showCfg;
            saveConfig(exeDir, cfg, &machine);
            // ⚠ La référence PRISTINE doit devenir CE qui vient d'être persisté, pas la
            // config lue au démarrage. Sinon le gel kiosk se retourne contre la ligne
            // ci-dessus : plus tard, n'importe quel saveConfig(force=true) de la borne
            // (ajout/retrait d'un dossier ROM, réaffectation manette, ou simple
            // auto-purge d'un dossier ROM disparu) reconstruit le fichier depuis
            // g_cfgPristine et RÉÉCRIT par-dessus machine/mem/rom/disk/crt/dock/show*
            // avec les valeurs du lancement — les préférences de la séance, que ce
            // saveConfig venait d'enregistrer, sont perdues sans le moindre message.
            g_cfgPristine = cfg;
#if defined(NEOST_WITH_IMGUI)
            // Disposition des fenêtres écrite MAINTENANT : en kiosk plus aucune n'est
            // soumise, une sauvegarde automatique plus tard n'aurait rien à dire d'elles.
            if (ImGui::GetIO().IniFilename)
                ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
#endif
            glfwGetWindowPos(window, &g_winX, &g_winY);
            glfwGetWindowSize(window, &g_winW, &g_winH);
            g_winGeomValid = true;
            g_kiosk = true;                      // AVANT saveConfig suivant : config figée

            // (2) Plein écran EXCLUSIF sur le moniteur choisi (--kiosk-monitor).
            // Même borne que la création : un index hors plage lirait mons[-1].
            int nmon = 0; GLFWmonitor** mons = glfwGetMonitors(&nmon);
            GLFWmonitor* mon = (mons && kioskMonitor >= 0 && kioskMonitor < nmon)
                                   ? mons[kioskMonitor] : glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
            glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_FALSE);
            glfwSetWindowMonitor(window, mon, 0, 0,
                                 vm ? vm->width : g_winW, vm ? vm->height : g_winH,
                                 vm ? vm->refreshRate : GLFW_DONT_CARE);
            // Borne : curseur masqué, souris capturée, joystick clavier armé (une
            // borne se joue au joystick — flèches + Ctrl droit, sans menu pour l'activer).
            g_mouseCaptured = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            kbdJoyDesk = g_kbdJoy;               // à rendre en revenant au bureau
            g_kbdJoy   = true;
        } else {
            // KIOSK → GUI. Retour à la géométrie fenêtrée mémorisée + chrome rendu.
            g_kiosk = false;
            g_kioskDiskMenu = false;             // aucun overlay borne ne survit à la bascule
            g_kioskPage = KIOSK_PAGE_LIST; g_kioskZone = KIOSK_ZONE_LIST;
            // Jamais été fenêtré (session lancée en --kiosk) : centrer sur la zone de
            // travail du moniteur COURANT plutôt que d'atterrir en (0,0). Le moniteur
            // se lit AVANT glfwSetWindowMonitor(…, nullptr, …), qui le remet à null.
            if (!g_winGeomValid) {
                GLFWmonitor* m = glfwGetWindowMonitor(window);
                if (!m) m = glfwGetPrimaryMonitor();
                int mx = 0, my = 0, mw = 0, mh = 0;
                if (m) glfwGetMonitorWorkarea(m, &mx, &my, &mw, &mh);
                if (mw > 0 && mh > 0) {
                    g_winX = mx + (mw - g_winW) / 2;
                    g_winY = my + (mh - g_winH) / 2;
                }
                g_winGeomValid = true;
            }
            glfwSetWindowMonitor(window, nullptr, g_winX, g_winY, g_winW, g_winH,
                                 GLFW_DONT_CARE);
            glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_TRUE);
            g_mouseCaptured = false;             // le curseur hôte revient (clic écran = recapture)
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            // Émulation joystick clavier : on REND le réglage d'avant la borne (par
            // défaut OFF au bureau — sinon les flèches n'atteindraient plus le ST).
            g_kbdJoy = kbdJoyDesk;
        }

        // (3) Restaure l'instantané : le ST reprend EXACTEMENT où il en était.
        if (!snap.empty() && !machine.loadState(snap.data(), snap.size()))
            std::fprintf(stderr, "[kiosk] WARNING state restore failed — "
                                 "the machine carries on as is\n");
        // Recale l'horloge et le delta souris : la bascule a pris du temps réel et
        // déplacé le curseur (changement de mode). Sans ça : rafale de rattrapage
        // de trames + saut de souris d'un demi-écran à la reprise.
        emuNext = clock::now();
        glfwGetCursorPos(window, &lastMx, &lastMy);
        g_stateMsg = g_kiosk ? "\xef\x84\x88 Kiosk mode (F8 to go back)"
                             : "\xef\x84\x88 Desktop mode (F8 for the kiosk)";
        g_stateMsgFrames = 120;
        std::fprintf(stderr, "[kiosk] switched → %s\n", g_kiosk ? "KIOSK" : "DESKTOP");
    };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();                      // les transitions de boutons → onMouseButton

        // Bascule GUI ⇄ kiosk demandée (F8 / menus) : appliquée ICI, en tête de tour,
        // donc entre deux trames émulées — la seule fenêtre où l'instantané se recharge.
        if (g_kioskSwitchReq) {
            const bool on = (g_kioskSwitchReq == 1);
            g_kioskSwitchReq = 0;
            switchKioskMode(on);
        }


        // Suppr (DEL) libère la souris si elle est capturée (le curseur GEM est piloté
        // tant que la capture est active). Échap, lui, reste disponible pour le ST.
        // En kiosk, la souris reste TOUJOURS capturée (borne).
        if (!g_kiosk && g_mouseCaptured && glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS) {
            g_mouseCaptured = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        if (g_mouseCaptured) {                  // mouvement relatif → paquet IKBD (boutons inclus)
            double mx, my; glfwGetCursorPos(window, &mx, &my);
            const int dx = int(mx - lastMx), dy = int(my - lastMy);
            if (dx || dy) {
                lastMx += dx; lastMy += dy;     // on ne consomme QUE l'entier → le reste
                                                // fractionnaire s'accumule (drags lents)
                const bool l = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
                const bool r = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                if (g_dbgMouse) std::fprintf(stderr, "[mouse] move dx=%d dy=%d L=%d R=%d\n", dx, dy, l, r);
                machine.ikbd.mouseEvent(dx * MOUSE_X_SIGN, dy * MOUSE_Y_SIGN, l, r);
            }
        }

        // Sortie KIOSK. Deux moyens, toujours disponibles (sans menu ni bordure) :
        //  · Alt+F4 : le classique, sortie IMMÉDIATE. Géré explicitement ici car en
        //    plein écran EXCLUSIF le gestionnaire de fenêtres ne relaie pas toujours
        //    l'événement « close » à GLFW.
        //  · Ctrl+Shift+Q maintenu ~0,7 s : chord discret (évite les sorties accidentelles).
        // Le WM (Alt+F4 « normal », bouton fermer) reste actif aussi : on ne bloque
        // jamais glfwWindowShouldClose.
        if (g_kiosk) {
            const bool alt = glfwGetKey(window, GLFW_KEY_LEFT_ALT)  == GLFW_PRESS ||
                             glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            if (alt && glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, GLFW_TRUE);

            const bool ctrl  = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            const bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            const bool q     = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
            static int quitHold = 0;
            quitHold = (ctrl && shift && q) ? quitHold + 1 : 0;
            if (quitHold >= 35) glfwSetWindowShouldClose(window, GLFW_TRUE);   // ~0,7 s @ 50 Hz

            // F10 (front montant) : (dés)active le zoom adaptatif → cadre complet fixe.
            static bool f10Prev = false;
            const bool f10 = glfwGetKey(window, GLFW_KEY_F10) == GLFW_PRESS;
            if (f10 && !f10Prev) {
                g_autoZoom = !g_autoZoom;
                cfg.autoZoom = g_autoZoom;   // sinon un retour au bureau resauverait l'ANCIENNE valeur
                std::fprintf(stderr, "[kiosk] adaptive zoom %s\n", g_autoZoom ? "ON" : "OFF");
            }
            f10Prev = f10;
        }

        // F11 (front montant) : bascule l'émulation joystick au clavier. Pratique
        // surtout sans ImGui (pas de menu) ; mémorisé en config en fin de boucle.
        {
            static bool f11Prev = false;
            const bool f11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
            if (f11 && !f11Prev) {
                g_kbdJoy = !g_kbdJoy;   // bascule de session (jamais persistée)
                std::fprintf(stderr, "[joystick] keyboard emulation %s (port %d)\n",
                             g_kbdJoy ? "ON" : "OFF", g_kbdJoyPort);
            }
            f11Prev = f11;
        }

        // Joystick hôte → IKBD (manettes USB + émulation clavier). Scruté chaque
        // trame : l'IKBD répond avec cet état aux interrogations $16 / au report
        // auto $14. L'émulation clavier est inhibée si une saisie ImGui a le focus.
        {
            bool kbd = g_kbdJoy;
#if defined(NEOST_WITH_IMGUI)
            if (!g_mouseCaptured && ImGui::GetIO().WantCaptureKeyboard) kbd = false;
#endif
            uint8_t joy0 = 0, joy1 = 0;
            int8_t joyRoles[GLFW_JOYSTICK_LAST + 1];
            joyResolveRoles(joyRoles);   // affectation par GUID (menu kiosk « Joysticks »)
            stjoy::compose(window, kbd, g_kbdJoyPort, g_joyDeadzone, joy0, joy1, joyRoles);
            // Overlay kiosk ouvert : la manette pilote l'overlay → on n'envoie
            // rien au ST (sinon le jeu bougerait pendant la navigation).
            if (g_kioskDiskMenu) { joy0 = 0; joy1 = 0; }
            machine.ikbd.setJoystick(joy0, joy1);
            machine.bus.stePads.setJoystick(joy0, joy1);   // joypads STE ($FF9200/02) — même état
            // Boutons auxiliaires manette → touches ST : X = ESPACE, Y = RETURN
            // (cf. stjoy::readAux — jeux « PRESS SPACE » jouables sans clavier).
            // Make/break IKBD sur les FRONTS ; tout est relâché quand l'overlay
            // kiosk s'ouvre (la manette pilote alors le menu, pas le jeu).
            {
                static uint8_t prevAux = 0;
                uint8_t aux = g_kioskDiskMenu ? 0 : stjoy::composeAux(joyRoles);
                const uint8_t delta = aux ^ prevAux;
                if (delta & stjoy::AUX_SPACE)
                    machine.ikbd.keyEvent(0x39, aux & stjoy::AUX_SPACE);   // ESPACE
                if (delta & stjoy::AUX_RETURN)
                    machine.ikbd.keyEvent(0x1C, aux & stjoy::AUX_RETURN);  // RETURN
                prevAux = aux;
            }
            // Paddles / axes analogiques STE ($FF9211-17) : axes BRUTS de la
            // première manette hôte (stick gauche, sans zone morte — la plage
            // $04-$43 du STE est déjà grossière). Pad A = port « jeux ».
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                GLFWgamepadstate gs;
                if (glfwJoystickPresent(jid) && glfwGetGamepadState(jid, &gs)) {
                    machine.bus.stePads.setAnalog(0, gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X],
                                                     gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
                    break;
                }
            }
            g_lastJoy0 = joy0; g_lastJoy1 = joy1;   // pour la fenêtre Joystick
            // Diagnostic manette (NEOST_DEBUG_JOY=1) : ~3×/s, état brut des axes.
            if (g_dbgJoy) { static int t = 0; if (++t % 16 == 0) stjoy::debug(window, kbd, g_kbdJoyPort, g_joyDeadzone); }
        }

        machine.cpu.updateIpl();               // entrées reçues → réévalue l'IPL

        // Relâche différée de la touche/clic envoyé depuis la page Clavier du menu :
        // l'appui (MAKE) a été posé quand l'utilisateur a validé ; on relâche (BREAK)
        // quelques trames plus tard → frappe brève que le jeu voit passer.
        if (g_kioskInjectHold > 0 && --g_kioskInjectHold == 0) {
            if (g_kioskKeyRelease >= 0) {
                machine.ikbd.keyEvent((uint8_t)g_kioskKeyRelease, false);
                g_kioskKeyRelease = -1;
            }
            if (g_kioskMouseRelL || g_kioskMouseRelR) {
                machine.ikbd.mouseEvent(0, 0, false, false);
                g_kioskMouseRelL = g_kioskMouseRelR = false;
            }
        }

        // RATTRAPAGE : on émule autant de trames que le temps réel l'exige depuis la
        // dernière itération (pattern émulateur classique). Une itération GUI coûte
        // ce qu'elle coûte (ImGui + GL + granularité de sleep macOS ≈ 22-25 ms,
        // App Nap, fenêtre déplacée…) : si on n'exécutait qu'UNE trame par tour, la
        // boucle plafonnait à ~40 trames/s → temps émulé RALENTI de 20 % et anneau
        // audio affamé (son HACHÉ — le bug « musique lente + hachée »). Ici le temps
        // émulé suit le temps réel quel que soit le débit du GUI : tour lent → 2
        // trames émulées (l'affichage en saute une, inaperçu), tour rapide → 0 ou 1.
        // `emuNext` = échéance réelle de la PROCHAINE trame émulée ; chaque trame la
        // repousse de SA durée émulée (géométrie 50/60/71 Hz). Garde-fou : après une
        // longue pause (drag de fenêtre…), on abandonne le retard au-delà de 4 trames
        // au lieu de spiraler.
        // PAUSE KIOSK : menu ouvert (hors page Clavier, qui doit laisser tourner le
        // jeu pour qu'il reçoive les touches envoyées) → on gèle l'émulation. À la
        // reprise on recale l'échéance sur maintenant (aucun rattrapage en rafale).
        const bool kioskPaused = g_kiosk && g_kioskDiskMenu && g_kioskPage != KIOSK_PAGE_KEYS;
        if (kioskPaused || g_dbgPaused) {
            emuNext = clock::now();
            // Débogueur en pause : pas-à-pas TRAME (avance une trame puis reste pausé).
            // clearBreakpointHit arme le skip-once → l'instruction du breakpoint passe.
            if (g_dbgPaused && g_dbgStepFrame) {
                g_dbgStepFrame = false;
                machine.cpu.clearBreakpointHit();
                machine.runFrame();
                audio.produceFrame(machine.frameCycles());
            }
            // Pas-à-pas INSTRUCTION : une seule instruction, ordonnanceur en lockstep
            // (pas de produceFrame — trop court, le son reste muet en pas-à-pas).
            if (g_dbgPaused && g_dbgStepInstr) {
                g_dbgStepInstr = false;
                machine.stepInstruction();
            }
        } else {
            // 6 trames max ≈ 120 ms de retard résorbable d'un coup : un stall GUI
            // ponctuel (drag de fenêtre, rafale disque) plus court que ça se rattrape
            // SANS trou audible (le coussin de l'anneau fait ~85 ms).
            int ran = 0;
            while (clock::now() >= emuNext && ran < 6) {
                machine.runFrame();                          // une trame (timing + décodage)
                audio.produceFrame(machine.frameCycles());   // son de la trame → anneau (push)
                emuNext += std::chrono::nanoseconds(
                    static_cast<int64_t>(double(machine.frameCycles()) * 1e9 / kCpuHz));
                ++ran;
                if (machine.cpu.breakpointHit()) { g_dbgPaused = true; break; }   // débogueur : auto-pause
            }
            if (ran == 6 && clock::now() > emuNext) emuNext = clock::now();  // pause longue : resync
        }

        // Save-state rapide (F5 sauver / F7 charger) — slot fichier unique neost.state, à
        // la frontière de trame. En kiosk la config est figée mais l'état de jeu, lui, se
        // sauve/charge (ce n'est pas la config). Fronts montants.
        {
            static bool f5Prev = false, f7Prev = false;
            const std::string statePath = exeDir + "/../neost.state";
            const bool f5 = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
            const bool f7 = glfwGetKey(window, GLFW_KEY_F7) == GLFW_PRESS;
            if (f5 && !f5Prev) {
                const bool ok = machine.saveStateFile(statePath);
                g_stateMsg = ok ? "\xef\x83\x87 State saved (F5)" : "Save failed";
                g_stateMsgFrames = 120;
                std::fprintf(stderr, "[state] save %s → %s\n", ok ? "OK" : "FAILED", statePath.c_str());
            }
            if (f7 && !f7Prev) {
                const bool ok = machine.loadStateFile(statePath);
                g_stateMsg = ok ? "\xef\x80\x9e State restored (F7)" : "No state / failed";
                g_stateMsgFrames = 120;
                std::fprintf(stderr, "[state] load %s ← %s\n", ok ? "OK" : "FAILED", statePath.c_str());
            }
            f5Prev = f5; f7Prev = f7;
        }
        screen.update(machine.shifter.pixels(), machine.shifter.width(), machine.shifter.height());

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        if (g_kiosk) glClearColor(0.f, 0.f, 0.f, 1.f);   // kiosk : barres noires
        else         glClearColor(0.10f, 0.10f, 0.12f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Région de contenu du zoom adaptatif — commune au kiosk et au bureau.
        // Appelée à CHAQUE trame même zoom coupé : ses latches d'hystérésis sont des
        // statiques de fonction, et les sauter les GÈLERAIT à leur dernière valeur —
        // à la réactivation, un latch resté armé sur une démo overscan afficherait le
        // buffer entier pendant ~30 trames avant de retomber d'un coup.
        int cTop, cH, cW; stContentRegion(machine, cTop, cH, cW);
        int kTop = 0, kH = machine.shifter.height(), kW = machine.shifter.width();
        if (g_autoZoom) { kTop = cTop; kH = cH; kW = cW; }

        bool reqReset = false, reqHardReset = false, reqRebuild = false, reqCapture = false;
        int  reqMonitor = -1;
#if defined(NEOST_WITH_IMGUI)
        // Vit HORS de la trame : porte les réglages matériels en attente entre deux
        // ouvertures de la fenêtre.
        static ConfigUi cfgUi;
        cfgUi.disksDir = disksDir; cfgUi.cartsDir = cartsDir; cfgUi.romsDir = romsDir;
        cfgUi.hdDir    = hdDir;    cfgUi.gemdosDir = gemdosDir;
        // Résolu à la PREMIÈRE trame, donc après le saveConfig de démarrage : c'est lui
        // qui a tranché où vit neost.cfg, et les profils le suivent (cf. profilesDir).
        static const std::string profDirResolved = profilesDir(exeDir);
        cfgUi.profDir = profDirResolved;        // créé à la 1re sauvegarde de profil
#endif
#if defined(NEOST_WITH_IMGUI)
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        std::string reqMount, reqMountB; bool reqEject = false, reqEjectB = false;
        std::string reqMountCart; bool reqEjectCart = false;
        std::string reqMountGemdos, reqMountAcsi; bool reqEjectGemdos = false, reqEjectAcsi = false;
        if (!g_kiosk) {                          // KIOSK : aucun chrome ImGui (menu/toolbar/fenêtres)
        const bool color = machine.mfp.colorMonitor();

        // --- Menu (haut) -----------------------------------------------------
        // Court par construction : la barre de menus ne contient que ce qu'on fait EN
        // JOUANT. Tout ce qui se RÈGLE vit dans la fenêtre Configuration (F10).
        float menuH = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            menuH = ImGui::GetWindowSize().y;
            if (ImGui::BeginMenu(ICON_FA_MICROCHIP " Machine")) {
                if (ImGui::MenuItem(ICON_FA_REDO " Reset"))            reqReset = true;
                if (ImGui::MenuItem(ICON_FA_POWER_OFF " Hard Reset"))  reqHardReset = true;
                ImGui::Separator();
                ImGui::MenuItem(ICON_FA_COG " Configuration…", nullptr, &g_showCfg);
                // Raccourci vers la page des profils : sans lui, « enregistrer mes
                // réglages » n'était visible qu'en descendant la colonne de la fenêtre.
                if (ImGui::MenuItem(ICON_FA_STAR " Settings profiles…")) {
                    g_showCfg = true; g_cfgPage = kCfgProfiles;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_SAVE " Save state", "F5")) {
                    const bool ok = machine.saveStateFile(exeDir + "/../neost.state");
                    g_stateMsg = ok ? "\xef\x83\x87 State saved" : "Save failed";
                    g_stateMsgFrames = 120;
                }
                if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Load state", "F7")) {
                    const bool ok = machine.loadStateFile(exeDir + "/../neost.state");
                    g_stateMsg = ok ? "\xef\x80\x9e State restored" : "No state / failed";
                    g_stateMsgFrames = 120;
                }
                ImGui::Separator();
                // Bascule borne : plein écran sans chrome, config figée, navigation à
                // la manette. La machine traverse la bascule par instantané → le jeu
                // en cours continue. F8 revient au bureau.
                if (ImGui::MenuItem(ICON_FA_DESKTOP " Kiosk mode", "F8"))
                    g_kioskSwitchReq = 1;
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_SIGN_OUT_ALT " Quit")) glfwSetWindowShouldClose(window, 1);
                ImGui::EndMenu();
            }
            // Affichage : ce qui change la façon de REGARDER, pas la machine émulée.
            if (ImGui::BeginMenu(ICON_FA_DESKTOP " Display")) {
                if (ImGui::MenuItem(ICON_FA_PALETTE " Color (low res)", nullptr,  color)) reqMonitor = 1;
                if (ImGui::MenuItem(ICON_FA_ADJUST " Mono (high res)",     nullptr, !color)) reqMonitor = 0;
                ImGui::Separator();
                // Même cadrage adaptatif que la borne : l'écran cale sa zone de contenu
                // sur la hauteur disponible, les bordures inutilisées sortent du cadre,
                // et une ouverture de bordure (démo overscan) rend le cadre entier.
                // Décoché = cadre complet fixe. En kiosk, F10 bascule la même chose.
                if (ImGui::MenuItem(ICON_FA_EXPAND " Auto zoom (adaptive framing)",
                                    nullptr, &g_autoZoom)) {
                    cfg.autoZoom = g_autoZoom; saveConfig(exeDir, cfg, &machine);
                }
                ImGui::MenuItem(ICON_FA_DESKTOP " CRT effects (window)", nullptr, &g_showCrt);
#ifdef IMGUI_HAS_DOCK
                ImGui::Separator();
                // Mode ancré : les fenêtres deviennent des onglets d'une disposition
                // persistante (imgui.ini). Décoché → ImGui DÉTRUIT ses nœuds (elles
                // redeviennent flottantes) ; recoché → on resème la disposition par
                // défaut, la personnalisation précédente est perdue.
                if (ImGui::MenuItem(ICON_FA_CLONE " Docked mode", nullptr, &g_dockOn)) {
                    ImGuiIO& dio = ImGui::GetIO();
                    if (g_dockOn) { dio.ConfigFlags |=  ImGuiConfigFlags_DockingEnable; g_dockReset = true; }
                    else            dio.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
                    cfg.dock = g_dockOn; saveConfig(exeDir, cfg, &machine);
                }
                if (ImGui::MenuItem(ICON_FA_REDO " Default layout", nullptr, false, g_dockOn))
                    g_dockReset = true;
#endif
                ImGui::EndMenu();
            }
            // Fenêtres : rien que les outils d'INSPECTION. Les bibliothèques de supports
            // n'y sont plus — elles sont devenues des pages de la Configuration.
            if (ImGui::BeginMenu(ICON_FA_CLONE " Windows")) {
                ImGui::MenuItem(ICON_FA_MEMORY " Memory (hex)", nullptr, &g_showHex);
                ImGui::MenuItem(ICON_FA_MICROCHIP " CPU 68000",  nullptr, &g_showCpu);
                ImGui::MenuItem(ICON_FA_GAMEPAD " Joystick",     nullptr, &g_showJoy);
                ImGui::MenuItem(ICON_FA_BUG " Debugger",        nullptr, &g_showDbg);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                // Les raccourcis étaient jusqu'ici invisibles (F5/F7/F8/F11/F12 ne
                // figuraient nulle part sauf dans le code).
                ImGui::TextDisabled("Keyboard shortcuts");
                ImGui::Separator();
                struct Key { const char* k; const char* w; };
                static const Key keys[] = {
                    { "F5",  "save the machine state" },
                    { "F7",  "reload the state" },
                    { "F8",  "kiosk mode (toggle)" },
                    { "F11", "keyboard joystick emulation" },
                    { "F12", "mouse capture (or click inside the screen)" },
                    { "DEL", "release the captured mouse" },
                };
                for (const auto& k : keys) ImGui::BulletText("%-6s %s", k.k, k.w);
                ImGui::Separator();
                ImGui::TextDisabled("NeoST " NEOST_VERSION " — Atari ST emulator");
                ImGui::TextDisabled("Drag and drop: folder → C:, image → drive A,");
                ImGui::TextDisabled("hard disk image → ACSI, TOS → ROM.");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // --- Barre de boutons (sous le menu) ---------------------------------
        // Barre latérale de VIEWPORT (et non fenêtre positionnée à la main, depuis
        // l'arrivée de l'ancrage) : BeginViewportSideBar RÉSERVE sa hauteur dans la
        // zone de travail, donc le dockspace posé juste après commence dessous —
        // aucun décalage codé en dur, et la réservation suit la hauteur réelle.
        // Elle ne porte plus que des VERBES : les bascules de fenêtres faisaient
        // doublon avec le menu Fenêtres.
        const float toolPadY = ImGui::GetStyle().WindowPadding.y;
        const float toolH    = ImGui::GetFrameHeight() + toolPadY * 2.0f;
        ImGui::BeginViewportSideBar("##toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolH,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        if (IconButton(ICON_FA_COG, "Configuration")) g_showCfg = !g_showCfg;
        ImGui::SameLine();
        if (IconButton(ICON_FA_REDO, "Reset")) reqReset = true;
        ImGui::SameLine();
        // Reset à froid : efface la ST-RAM → EmuTOS/TOS refait un boot complet.
        if (IconButton(ICON_FA_POWER_OFF, "Hard Reset")) reqHardReset = true;
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        if (IconButton(color ? ICON_FA_ADJUST : ICON_FA_PALETTE, color ? "Switch to Mono" : "Switch to Color"))
            reqMonitor = color ? 0 : 1;
        // Volume : le seul réglage qu'on touche EN JOUANT, donc il reste ici (le
        // reste du son est dans la page Son).
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        {
            static float volBeforeMute = 1.0f;         // niveau restauré au dé-mute
            const float  vol   = audio.masterVolume();
            const bool   muted = vol <= 0.0f;
            const char*  vicon = muted      ? ICON_FA_VOLUME_MUTE
                               : vol < 0.5f ? ICON_FA_VOLUME_DOWN : ICON_FA_VOLUME_UP;
            if (IconButton(vicon, muted ? "Unmute" : "Mute")) {
                if (muted) audio.setMasterVolume(volBeforeMute > 0.0f ? volBeforeMute : 1.0f);
                else     { volBeforeMute = vol; audio.setMasterVolume(0.0f); }
                cfg.volume = audio.masterVolume(); saveConfig(exeDir, cfg, &machine);
            }
            ImGui::SameLine();
            int pct = int(vol * 100.0f + 0.5f);
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::SliderInt("##volume", &pct, 0, 100, "%d %%"))
                audio.setMasterVolume(float(pct) / 100.0f);
            if (ImGui::IsItemDeactivatedAfterEdit()) {   // fin de glissé → persiste
                cfg.volume = audio.masterVolume();
                saveConfig(exeDir, cfg, &machine);
            }
        }
        ImGui::End();

        // --- Barre d'état (bas) : l'identité de la machine, en permanence -----
        // C'est ce qui manquait le plus. Deux « bugs » sur trois venaient d'une
        // configuration INVISIBLE : une ROM « us » (60 Hz NTSC) qui déchire une démo
        // calculée pour le 50 Hz, ou 512 Ko là où le jeu en veut 1 Mo. Rien dans
        // l'interface ne le disait. Chaque segment est cliquable et ouvre SA page.
        const float statH = ImGui::GetFrameHeight() + toolPadY * 2.0f;
        ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, statH,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        {
            bool first = true;
            auto seg = [&](const std::string& text, int page, const char* tip, bool warn = false) {
                if (!first) { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); }
                first = false;
                if (warn) ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f), "%s", text.c_str());
                else      ImGui::TextUnformatted(text.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n(click: open the configuration)", tip);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        g_showCfg = true; g_cfgPage = page;
                    }
                }
            };
            auto shortName = [](const std::string& p) {
                return p.empty() ? std::string("—") : fs::path(p).filename().string();
            };
            // Modèle + RAM : les deux réglages qui décident si un jeu démarre.
            const std::string mdl = cfg.machine == "megaste" ? "Mega STE"
                                  : cfg.machine == "ste"     ? "STE"
                                  : cfg.machine == "megast"  ? "Mega ST" : "ST";
            const std::string ram = cfg.mem == "256k" ? "256 KB" : cfg.mem == "512k" ? "512 KB"
                                  : cfg.mem == "1m" ? "1 MB" : cfg.mem == "2m" ? "2 MB" : "4 MB";
            seg(mdl, kCfgMachine, "Emulated machine model");
            seg(ram, kCfgMem,     "Installed ST-RAM");
            seg(fs::path(cfg.rom).stem().string(), kCfgRom, "TOS image in ROM");
            // Le balayage est DÉDUIT de la ROM ; on le signale en orange en 60 Hz, la
            // configuration qui déchire les démos européennes.
            const int hz = machine.shifter.refreshHz();
            char hzbuf[32];
            std::snprintf(hzbuf, sizeof hzbuf, "%d Hz %s", hz, hz >= 55 ? "NTSC" : "PAL");
            seg(hzbuf, kCfgRom, "Scan rate (set by the ROM)", hz >= 55);
            seg("A: " + shortName(machine.fdc.mountedPath(0)), kCfgFloppy, "Floppy drive A");
            seg("B: " + shortName(machine.fdc.mountedPath(1)), kCfgFloppy, "Floppy drive B");
            const std::string c = machine.gemdos.active() ? shortName(cfg.gemdos) + "/"
                                : machine.fdc.acsiActive() ? shortName(cfg.acsi)
                                : std::string("—");
            seg("C: " + c, kCfgHd, "Hard disk (GEMDOS or ACSI)");
            char fps[32];
            std::snprintf(fps, sizeof fps, "%.1f fps", double(ImGui::GetIO().Framerate));
            seg(fps, kCfgEmul, "Host loop frame rate");
        }
        ImGui::End();

        // --- Dockspace (après les barres, AVANT toute fenêtre ancrable) ------
        renderDockSpace(g_dockOn);

        // --- Fenêtre écran (base) + fenêtres masquables ----------------------
        drawStScreen(screen, g_mouseCaptured, reqCapture, menuH + toolH, kTop, kH, kW);
        if (g_showCfg) {
            // La fenêtre ne monte/démonte/redémarre rien : elle remplit `cfgUi`, qu'on
            // déverse dans les requêtes de la boucle juste après. Les chemins de disque
            // dur sont lus dans `cfg` (tenu à jour par les montages) — ni GemdosHd ni
            // Acsi n'exposent le leur.
            cfgUi.cfg     = &cfg;
            cfgUi.machine = &machine;
            cfgUi.color   = color;
            cfgUi.volume  = audio.masterVolume();
            cfgUi.driveSound = driveSoundOn;
            cfgUi.driveSoundAvail = driveSoundAvail;
            cfgUi.curGemdos = cfg.gemdos.empty() ? std::string() : resolvePath(cfg.gemdos);
            cfgUi.curAcsi   = cfg.acsi.empty()   ? std::string() : resolvePath(cfg.acsi);
            drawConfigWindow(cfgUi);

            // Déversement des requêtes de la fenêtre dans celles de la boucle.
            if (!cfgUi.reqMountA.empty())      { reqMount       = cfgUi.reqMountA;      cfgUi.reqMountA.clear(); }
            if (!cfgUi.reqMountB.empty())      { reqMountB      = cfgUi.reqMountB;      cfgUi.reqMountB.clear(); }
            if (!cfgUi.reqMountCart.empty())   { reqMountCart   = cfgUi.reqMountCart;   cfgUi.reqMountCart.clear(); }
            if (!cfgUi.reqMountGemdos.empty()) { reqMountGemdos = cfgUi.reqMountGemdos; cfgUi.reqMountGemdos.clear(); }
            if (!cfgUi.reqMountAcsi.empty())   { reqMountAcsi   = cfgUi.reqMountAcsi;   cfgUi.reqMountAcsi.clear(); }
            if (cfgUi.reqEjectA)      { reqEject       = true; cfgUi.reqEjectA = false; }
            if (cfgUi.reqEjectB)      { reqEjectB      = true; cfgUi.reqEjectB = false; }
            if (cfgUi.reqEjectCart)   { reqEjectCart   = true; cfgUi.reqEjectCart = false; }
            if (cfgUi.reqEjectGemdos) { reqEjectGemdos = true; cfgUi.reqEjectGemdos = false; }
            if (cfgUi.reqEjectAcsi)   { reqEjectAcsi   = true; cfgUi.reqEjectAcsi = false; }
            if (cfgUi.reqMonitor >= 0) { reqMonitor = cfgUi.reqMonitor; cfgUi.reqMonitor = -1; }
            if (cfgUi.reqKiosk)       { g_kioskSwitchReq = 1; cfgUi.reqKiosk = false; }
            if (cfgUi.reqVolume >= 0.0f) {
                audio.setMasterVolume(cfgUi.reqVolume); cfgUi.reqVolume = -1.0f;
            }
            if (cfgUi.volumeDone) {
                cfg.volume = audio.masterVolume(); saveConfig(exeDir, cfg, &machine);
                cfgUi.volumeDone = false;
            }
            if (cfgUi.reqFastFdc >= 0) {
                cfg.fastfdc = (cfgUi.reqFastFdc != 0);
                machine.fdc.setFastFdc(cfg.fastfdc);
                saveConfig(exeDir, cfg, &machine);
                cfgUi.reqFastFdc = -1;
            }
            if (cfgUi.driveSound != driveSoundOn) {
                driveSoundOn = cfgUi.driveSound; drive.setEnabled(driveSoundOn);
                cfg.driveSound = driveSoundOn;   // persisté par cfgDirty (la case l'a levé)
            }
            if (cfgUi.reqSaveState) {
                const bool ok = machine.saveStateFile(exeDir + "/../neost.state");
                g_stateMsg = ok ? "\xef\x83\x87 State saved" : "Save failed";
                g_stateMsgFrames = 120; cfgUi.reqSaveState = false;
            }
            if (cfgUi.reqLoadState) {
                const bool ok = machine.loadStateFile(exeDir + "/../neost.state");
                g_stateMsg = ok ? "\xef\x80\x9e State restored" : "No state / failed";
                g_stateMsgFrames = 120; cfgUi.reqLoadState = false;
            }
            // « Appliquer et redémarrer » : les quatre réglages matériels d'un coup,
            // UN seul rebuild. Mega STE + TOS trop ancien → on remonte un TOS ≥ 2.06,
            // sinon « choisir Mega STE » redonnait un simple STE (cf. pickTosForMachine).
            if (cfgUi.reqApply) {
                cfg.machine = cfgUi.pendMachine;
                cfg.mem     = cfgUi.pendMem;
                cfg.fpu     = cfgUi.pendFpu;
                cfg.rom     = cfgUi.pendRom;
                const std::string autoRom = pickTosForMachine(cfg.machine, cfg.rom, exeDir, romsDir);
                if (!autoRom.empty()) cfg.rom = autoRom;
                saveConfig(exeDir, cfg, &machine);
                reqRebuild = true;
                cfgUi.reqApply = false;
                cfgUi.pendInit = false;      // resème depuis la config appliquée
            }
            if (cfgUi.cfgDirty) {
                cfg.autoZoom = g_autoZoom;
                cfg.crt = g_crtOn; cfg.crtParams = g_crtParams;
                saveConfig(exeDir, cfg, &machine);
                cfgUi.cfgDirty = false;
            }
            // ── Profils nommés (page « Profiles ») ─────────────────────────────
            // Borne : rien ne s'écrit sur le disque (invariant « la borne repart
            // identique »). La page grise déjà les boutons ; on double la garde ici,
            // seul endroit qui touche réellement au système de fichiers.
            if ((g_kiosk || g_kioskLaunched)
                && (!cfgUi.reqSaveProfile.empty() || !cfgUi.reqDeleteProfile.empty())) {
                cfgUi.reqSaveProfile.clear(); cfgUi.reqDeleteProfile.clear();
                g_stateMsg = "Kiosk mode: configuration frozen"; g_stateMsgFrames = 150;
            }
            if (!cfgUi.reqSaveProfile.empty()) {
                // On fige les réglages EN VIGUEUR, pas les champs « en attente » : un
                // profil décrit une machine qui tourne, pas un formulaire à moitié rempli.
                // Realignement préalable sur les globals d'interface — plusieurs chemins
                // les changent sans repasser par saveConfig (F10 en borne, panneau CRT,
                // fenêtre Joysticks), et le profil hériterait sinon de valeurs périmées.
                cfg.autoZoom = g_autoZoom;
                cfg.crt      = g_crtOn; cfg.crtParams = g_crtParams;
                cfg.volume   = audio.masterVolume();
                cfg.joyport  = g_kbdJoyPort; cfg.joydeadzone = g_joyDeadzone;
                cfg.joymap   = joymapSerialize();
                cfg.driveSound = driveSoundOn;
                std::string err;
                if (saveProfile(cfgUi.profDir, cfgUi.reqSaveProfile, cfg, err)) {
                    g_stateMsg = "\xef\x83\x87 Profile saved: " + cfgUi.reqSaveProfile;
                } else {
                    g_stateMsg = "Profile NOT saved";
                    std::fprintf(stderr, "[cfg] profile: %s\n", err.c_str());
                }
                g_stateMsgFrames = 150; g_profilesDirty = true;
                cfgUi.reqSaveProfile.clear();
            }
            if (!cfgUi.reqLoadProfile.empty()) {
                // On part de la config COURANTE : un profil n'écrit qu'un sous-ensemble
                // de clés, et tout ce qu'il tait (horloge, disposition, dossiers ROM de
                // la borne) doit rester en place.
                Config p = cfg;
                if (loadProfileInto(cfgUi.profDir, cfgUi.reqLoadProfile, p)) {
                    const std::string prevA = cfg.disk, prevB = cfg.diskb;
                    cfg = p;
                    // Réglages à effet immédiat (le matériel, lui, part au rebuild ci-dessous).
                    g_autoZoom = cfg.autoZoom;
                    g_crtOn    = cfg.crt; g_crtParams = cfg.crtParams;
                    g_kbdJoyPort  = cfg.joyport;
                    g_joyDeadzone = cfg.joydeadzone;
                    joymapParse(cfg.joymap);
                    audio.setMasterVolume(cfg.volume);
                    driveSoundOn = driveSoundAvail && cfg.driveSound;
                    drive.setEnabled(driveSoundOn);
                    // Disquettes : applyConfig() ne touche PAS aux lecteurs (« le disque
                    // monté est conservé ») → on monte/éjecte explicitement ce que dit le
                    // profil, par les requêtes normales de montage.
                    // Une image DISPARUE depuis l'enregistrement du profil ne doit pas
                    // s'inscrire dans neost.cfg : l'invariant des montages est qu'on ne
                    // persiste QU'un montage réussi, sinon le fantôme est retenté à chaque
                    // démarrage. Le lecteur garde alors ce qu'il avait, et on le dit.
                    std::string missing;
                    auto wantDisk = [&](std::string& want, const std::string& prev,
                                        std::string& reqMountSlot, bool& reqEjectSlot) {
                        if (want == prev) return;
                        if (want.empty()) { reqEjectSlot = true; return; }
                        const std::string img = resolveData(want, exeDir);
                        if (fileExists(img)) { reqMountSlot = img; return; }
                        if (missing.empty()) missing = want;
                        want = prev;
                    };
                    wantDisk(cfg.disk,  prevA, reqMount,  reqEject);
                    wantDisk(cfg.diskb, prevB, reqMountB, reqEjectB);
                    saveConfig(exeDir, cfg, &machine);
                    reqRebuild = true;        // modèle/RAM/FPU/ROM/cartouche/HD/moniteur/FDC
                    cfgUi.pendInit = false;   // resème les champs « en attente »
                    g_stateMsg = missing.empty()
                        ? "\xef\x80\x9e Profile loaded: " + cfgUi.reqLoadProfile
                        : "Profile loaded — missing floppy: "
                              + fs::path(missing).filename().string();
                } else {
                    g_stateMsg = "Profile not readable";
                    g_profilesDirty = true;   // disparu du dossier ? on relit la liste
                }
                g_stateMsgFrames = 150;
                cfgUi.reqLoadProfile.clear();
            }
            if (!cfgUi.reqDeleteProfile.empty()) {
                const bool okDel = deleteProfile(cfgUi.profDir, cfgUi.reqDeleteProfile);
                g_stateMsg = okDel ? "Profile deleted: " + cfgUi.reqDeleteProfile
                                   : "Delete failed";
                g_stateMsgFrames = 150; g_profilesDirty = true;
                cfgUi.reqDeleteProfile.clear();
            }
        }
        if (g_showHex)  drawHexViewer(machine.bus);
        if (g_showCpu)  drawCpuState(machine.cpu, reqReset);
        if (g_showJoy)  drawJoystickWindow(window, g_lastJoy0, g_lastJoy1);
        if (g_showDbg)  drawDebugger(machine);
        if (g_showCrt) {                     // fenêtre de réglages CRT
            bool crtChanged = false;
            drawCrtSettings(crtChanged);
            if (crtChanged) {                // recopie dans le cfg + resauve (no-op en kiosk)
                cfg.crt = g_crtOn; cfg.crtParams = g_crtParams;
                saveConfig(exeDir, cfg, &machine);
            }
        }
        // Un réglage joystick a changé dans la fenêtre → resauve neost.cfg.
        if (g_joyCfgDirty) {
            cfg.joyport = g_kbdJoyPort; cfg.joydeadzone = g_joyDeadzone;
            saveConfig(exeDir, cfg, &machine); g_joyCfgDirty = false;
        }
        }                                        // fin if(!g_kiosk) : chrome ImGui

        // KIOSK : aucun chrome dessiné, mais on garde le nœud d'ancrage VIVANT
        // (KeepAliveOnly ne soumet rien de visible). Sans ça, un aller-retour
        // GUI → kiosk → GUI rendrait toutes les fenêtres flottantes.
        if (g_kiosk) renderDockSpace(false);

        // --- Kiosk : menu in-game (START manette ou F9), jeu en PAUSE ------------
        // Modèle « vraie machine » : (A) INSÈRE la disquette choisie SANS jamais
        // rebooter (le jeu en cours continue) ; (X) REDÉMARRE la machine (bouton
        // reset) → reboot sur la disquette insérée ; (Y) quitte avec confirmation.
        if (g_kiosk) {
            // La navigation du menu ne consulte QUE les manettes réellement affectées à
            // un port ST (assign >= 0). Sans ce filtre, une manette explicitement mise
            // sur OFF pilotait quand même le menu : un encodeur arcade dont l'axe Y
            // repose de travers — le défaut même pour lequel la page JOYSTICKS existe —
            // faisait défiler la sélection en continu, rendant le menu (et donc la page
            // qui aurait permis de le corriger) inutilisable.
            int8_t navRoles[GLFW_JOYSTICK_LAST + 1];
            joyResolveRoles(navRoles);
            int8_t navAssign[GLFW_JOYSTICK_LAST + 1];
            stjoy::resolveAssign(navRoles, navAssign);
            auto navUsable = [&](int j) {
                return j >= 0 && j <= GLFW_JOYSTICK_LAST && navAssign[j] >= 0;
            };
            // Les BOUTONS de N'IMPORTE QUELLE manette naviguent, y compris une manette
            // mise sur OFF. Le filtre par rôle ne s'applique qu'aux AXES (c'est un stick
            // au repos décentré qui rend le menu fou, pas un bouton). Sans cette
            // asymétrie, un opérateur qui passe sa seule manette sur OFF depuis la page
            // JOYSTICKS — page dont c'est précisément la fonction — perdait TOUT contrôle,
            // et le réglage étant persisté, la borne redémarrait verrouillée.
            auto padBtn = [&](int b) {
                for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
                    GLFWgamepadstate gs;
                    if (glfwJoystickPresent(j) && glfwGetGamepadState(j, &gs) && gs.buttons[b])
                        return true;
                }
                return false;
            };
            // Zone morte de l'utilisateur (g_joyDeadzone) et non un seuil figé : un stick
            // au repos décentré ne doit pas compter comme une direction tenue.
            auto padAxis = [&](int axis) {
                // Sur la page JOYSTICKS elle-même, toute manette peut bouger la sélection :
                // sinon on ne pourrait pas RÉ-ACTIVER une manette qu'on vient de couper.
                const bool anyPad = (g_kioskPage == KIOSK_PAGE_JOY);
                for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
                    GLFWgamepadstate gs;
                    if ((anyPad || navUsable(j)) && glfwJoystickPresent(j) && glfwGetGamepadState(j, &gs)) {
                        const float v = gs.axes[axis];
                        if (std::fabs(v) > g_joyDeadzone) return v;
                    }
                }
                return 0.0f;
            };
            auto padAxisY = [&]() { return padAxis(GLFW_GAMEPAD_AXIS_LEFT_Y); };
            auto padAxisX = [&]() { return padAxis(GLFW_GAMEPAD_AXIS_LEFT_X); };
            // Ouvrir / fermer : START manette ou F9 clavier (front montant). La liste
            // est triée par PROXIMITÉ au disque courant → les phases B/C/D du jeu en
            // cours arrivent en tête, et la sélection démarre sur le disque monté.
            static bool pOpen = false;
            const bool openNow = padBtn(GLFW_GAMEPAD_BUTTON_START) ||
                                 glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS;
            if (openNow && !pOpen) {
                g_kioskDiskMenu = !g_kioskDiskMenu;
                g_kioskPage = KIOSK_PAGE_LIST;   // ré-ouverture : toujours sur la liste
                g_kioskZone = KIOSK_ZONE_LIST;   // focus par défaut : menu des jeux
                g_kioskActSel = 0;
                if (g_kioskDiskMenu) {
                    const std::string m = machine.fdc.mountedPath();
                    // Auto-prune : un dossier ROM disparu (débranché) est retiré + persisté.
                    if (kioskPruneRomDirs()) { cfg.romDirs = g_kioskRomDirs; saveConfig(exeDir, cfg, &machine, true); }
                    kioskScanDisks(disksDir, m);
                    g_kioskDiskSel = 0;
                    for (int i = 0; i < (int)g_kioskDisks.size(); ++i)
                        if (g_kioskDisks[i] == m) { g_kioskDiskSel = i; break; }
                }
            }
            pOpen = openNow;

            // SELECT (Back manette) ou K : ouvre/ferme DIRECTEMENT le bandeau Clavier &
            // souris, même en cours de jeu (sans passer par la liste). Le jeu tourne
            // dessous → la touche envoyée agit tout de suite.
            static bool pSelect = false;
            const bool selNow = padBtn(GLFW_GAMEPAD_BUTTON_BACK) ||
                                glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;   // F12, pas K (cf. onKey)
            if (selNow && !pSelect) {
                if (g_kioskDiskMenu && g_kioskPage == KIOSK_PAGE_KEYS) {
                    g_kioskDiskMenu = false;               // referme le clavier → reprise
                } else {
                    g_kioskDiskMenu = true;
                    g_kioskPage = KIOSK_PAGE_KEYS;         // ouvre direct le clavier
                    g_kioskKeySel = 0;
                }
            }
            pSelect = selNow;

            if (g_kioskDiskMenu) {
                // Fronts partagés (A / B) : lus une fois, réutilisés selon la page.
                static bool pOk = false, pCancel = false;
                const bool okNow = padBtn(GLFW_GAMEPAD_BUTTON_A) ||
                                   glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
                const bool caNow = padBtn(GLFW_GAMEPAD_BUTTON_B) ||
                                   glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;

                // Directions (D-pad / stick / flèches) avec répétition sur maintien,
                // partagées par toutes les pages (la liste n'utilise que haut/bas).
                const bool up    = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_UP)    ||
                                   glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS || padAxisY() < -0.5f;
                const bool down  = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_DOWN)  ||
                                   glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS || padAxisY() >  0.5f;
                const bool left  = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_LEFT)  ||
                                   glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS || padAxisX() < -0.5f;
                const bool right = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                                   glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || padAxisX() >  0.5f;
                // L1 / R1 (gâchettes hautes) ou Page↑/Page↓ : saut de PAGE dans la liste des
                // jeux (défilement rapide). Traités comme des directions (même répétition).
                const bool pgUp = padBtn(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)  ||
                                  glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS;
                const bool pgDn = padBtn(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER) ||
                                  glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;
                // Répétition TEMPORELLE (pas au nombre d'itérations) : la boucle tourne
                // sans vsync et à vide quand le jeu est en pause → un compteur de trames
                // défilerait à des centaines de pas/s (impossible de viser un item). On
                // se cale donc sur l'horloge : 1 pas au front, puis délai avant répétition,
                // puis un pas toutes les ~150 ms tant que la direction est maintenue.
                static bool navHeld = false;
                static clock::time_point navNext{};
                const bool nav = up || down || left || right || pgUp || pgDn;
                const auto navNow = clock::now();
                bool step = false;
                if (nav) {
                    if (!navHeld) { step = true; navHeld = true;
                                    navNext = navNow + std::chrono::milliseconds(400); }  // pause avant répét.
                    else if (navNow >= navNext) { step = true;
                                    navNext = navNow + std::chrono::milliseconds(150); }   // cadence de répét.
                } else {
                    navHeld = false;
                }

                if (g_kioskPage == KIOSK_PAGE_QUIT) {
                    // « Quitter ? » : A/Entrée confirme, B/Échap revient à la liste.
                    if (okNow && !pOk) glfwSetWindowShouldClose(window, GLFW_TRUE);
                    if (caNow && !pCancel) g_kioskPage = KIOSK_PAGE_LIST;

                } else if (g_kioskPage == KIOSK_PAGE_KEYS) {
                    // Grille de touches : navigation 2D. (A) envoie une frappe brève au
                    // ST (le jeu tourne derrière), (B) revient au menu.
                    if (step) {
                        int row = 0;
                        for (int r = 0; r < KIOSK_KEY_ROWN; ++r)
                            if (g_kioskKeySel >= KIOSK_KEY_ROWS[r][0] && g_kioskKeySel < KIOSK_KEY_ROWS[r][1]) row = r;
                        int col = g_kioskKeySel - KIOSK_KEY_ROWS[row][0];
                        if (up || down) {
                            row = (row + (down ? 1 : -1) + KIOSK_KEY_ROWN) % KIOSK_KEY_ROWN;
                            const int len = KIOSK_KEY_ROWS[row][1] - KIOSK_KEY_ROWS[row][0];
                            if (col >= len) col = len - 1;
                        } else {   // gauche/droite : dans la rangée, avec butée
                            const int len = KIOSK_KEY_ROWS[row][1] - KIOSK_KEY_ROWS[row][0];
                            col += right ? 1 : -1;
                            if (col < 0) col = 0;
                            if (col >= len) col = len - 1;
                        }
                        g_kioskKeySel = KIOSK_KEY_ROWS[row][0] + col;
                    }
                    // On ne pose une nouvelle frappe QUE si la précédente a fini son
                    // maintien (g_kioskInjectHold == 0) : sinon un 2ᵉ appui < 4 trames
                    // écraserait g_kioskKeyRelease → le MAKE précédent n'aurait jamais
                    // son BREAK (touche « collée » côté ST).
                    if (okNow && !pOk && g_kioskInjectHold == 0) {
                        const KioskKey& k = KIOSK_KEYS[g_kioskKeySel];
                        if (k.kind == 0) {                       // touche clavier ST
                            machine.ikbd.keyEvent(k.scancode, true);
                            g_kioskKeyRelease = k.scancode;
                        } else {                                 // clic souris (G/D)
                            const bool L = (k.kind == 1), R = (k.kind == 2);
                            machine.ikbd.mouseEvent(0, 0, L, R);
                            g_kioskMouseRelL = L; g_kioskMouseRelR = R;
                        }
                        g_kioskInjectHold = 4;                   // ~4 trames de maintien
                    }
                    if (caNow && !pCancel) g_kioskDiskMenu = false;   // (B) ferme → reprise du jeu

                } else if (g_kioskPage == KIOSK_PAGE_BROWSE) {
                    // Navigateur : [0] valider · [1] .. · [2..2+S) raccourcis · [reste] sous-dossiers.
                    const int nShort = (int)g_browseShortcutPaths.size();
                    const int total  = 2 + nShort + (int)g_browseSubdirs.size();
                    if (step && (up || down)) {
                        g_browseSel += down ? 1 : -1;
                        g_browseSel = (g_browseSel % total + total) % total;
                    }
                    if (okNow && !pOk) {
                        if (g_browseSel == 0) {                    // valider CE dossier → l'ajoute
                            // REFUS de la racine et du dossier personnel : kioskScanDisks
                            // les parcourrait RÉCURSIVEMENT, sans limite de profondeur ni
                            // de temps, DANS le thread GUI — la borne se figerait plusieurs
                            // minutes à chaque ouverture du menu, sans aucun retour.
                            // Comparaison CANONIQUE des deux côtés : l'égalité de chemins
                            // brute se contournait par une simple barre oblique finale
                            // (« /home/x/ » ≠ « /home/x »), et /home — le PARENT de tous
                            // les dossiers personnels, donc pire encore — passait tout droit.
                            // On refuse donc aussi tout ANCÊTRE du dossier personnel.
                            std::error_code cec;
                            fs::path bp = fs::weakly_canonical(fs::path(g_browseDir), cec);
                            if (cec) bp = fs::path(g_browseDir).lexically_normal();
                            const char* home = std::getenv("HOME");
                            if (!home || !*home) home = std::getenv("USERPROFILE");
                            bool tooBroad = (bp == bp.root_path());
                            if (!tooBroad && home && *home) {
                                std::error_code hec;
                                fs::path hp = fs::weakly_canonical(fs::path(home), hec);
                                if (hec) hp = fs::path(home).lexically_normal();
                                // bp == hp, ou bp est un ancêtre de hp (/home, /) → refus.
                                const std::string relToHome = hp.lexically_relative(bp).generic_string();
                                tooBroad = (bp == hp)
                                        || (!relToHome.empty() && relToHome.rfind("..", 0) != 0);
                            }
                            if (tooBroad) {
                                g_stateMsg = "Folder too broad (root / home) — refused";
                                g_stateMsgFrames = 180;
                            } else {
                            if (std::find(g_kioskRomDirs.begin(), g_kioskRomDirs.end(), g_browseDir)
                                    == g_kioskRomDirs.end())
                                g_kioskRomDirs.push_back(g_browseDir);
                            cfg.romDirs = g_kioskRomDirs;
                            saveConfig(exeDir, cfg, &machine, true);   // persiste MÊME en kiosk
                            kioskScanDisks(disksDir, machine.fdc.mountedPath());
                            g_kioskDiskSel = 0; g_romDirSel = 0;
                            g_kioskPage = KIOSK_PAGE_ROMDIRS;          // retour au gestionnaire
                            }
                        } else if (g_browseSel == 1) {             // .. parent
                            const fs::path p(g_browseDir);
                            if (p.has_parent_path() && p.parent_path() != p)
                                g_browseDir = p.parent_path().string();
                            kioskScanBrowse(g_browseDir);
                        } else if (g_browseSel < 2 + nShort) {     // raccourci (racine / volume monté)
                            g_browseDir = g_browseShortcutPaths[g_browseSel - 2];
                            kioskScanBrowse(g_browseDir);
                        } else {                                   // descendre dans un sous-dossier
                            g_browseDir = g_browseSubdirs[g_browseSel - 2 - nShort];
                            kioskScanBrowse(g_browseDir);
                        }
                    }
                    if (caNow && !pCancel) g_kioskPage = KIOSK_PAGE_ROMDIRS;   // (B) annuler

                } else if (g_kioskPage == KIOSK_PAGE_JOY) {
                    // Affectation des manettes : haut/bas sélectionne une manette
                    // PRÉSENTE, (A) fait tourner son rôle AUTO → PORT 1 → PORT 0 →
                    // OFF → AUTO (persisté par GUID via joymap=), (B) revient.
                    int jids[GLFW_JOYSTICK_LAST + 1]; int nj = 0;
                    for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j)
                        if (glfwJoystickPresent(j)) jids[nj++] = j;
                    if (nj > 0) {
                        if (g_kioskJoySel >= nj) g_kioskJoySel = nj - 1;
                        if (step && (up || down)) {
                            g_kioskJoySel += down ? 1 : -1;
                            g_kioskJoySel = (g_kioskJoySel % nj + nj) % nj;
                        }
                        if (okNow && !pOk) {
                            const std::string guid = joyGuid(jids[g_kioskJoySel]);
                            if (!guid.empty()) {
                                const auto it = g_joyRoleByGuid.find(guid);
                                const int8_t cur = (it != g_joyRoleByGuid.end())
                                                       ? it->second : int8_t(stjoy::ROLE_AUTO);
                                int8_t next;
                                switch (cur) {                       // AUTO→P1→P0→OFF→AUTO
                                    case stjoy::ROLE_AUTO:  next = stjoy::ROLE_PORT1; break;
                                    case stjoy::ROLE_PORT1: next = stjoy::ROLE_PORT0; break;
                                    case stjoy::ROLE_PORT0: next = stjoy::ROLE_OFF;   break;
                                    default:                next = stjoy::ROLE_AUTO;  break;
                                }
                                if (next == stjoy::ROLE_AUTO) g_joyRoleByGuid.erase(guid);
                                else                          g_joyRoleByGuid[guid] = next;
                                cfg.joymap = joymapSerialize();      // persiste (comme ROM FOLDERS)
                                saveConfig(exeDir, cfg, &machine, true);
                            }
                        }
                    }
                    if (caNow && !pCancel) { g_kioskPage = KIOSK_PAGE_LIST; g_kioskZone = KIOSK_ZONE_ACTIONS; }

                } else if (g_kioskPage == KIOSK_PAGE_ROMDIRS) {
                    // Gestion : [0] « + ADD » (→ navigateur), [1..N] dossiers (FEU = retirer).
                    const int total = 1 + (int)g_kioskRomDirs.size();
                    if (step && (up || down)) {
                        g_romDirSel += down ? 1 : -1;
                        g_romDirSel = (g_romDirSel % total + total) % total;
                    }
                    if (okNow && !pOk) {
                        if (g_romDirSel == 0) {                    // + ADD A FOLDER → navigateur
                            std::error_code ec2;
                            fs::path start = (!g_kioskRomDirs.empty() &&
                                              fs::is_directory(g_kioskRomDirs.back(), ec2))
                                                 ? fs::path(g_kioskRomDirs.back()) : fs::path(disksDir);
                            fs::path abs = fs::absolute(start, ec2);   // absolu → « .. » remonte jusqu'à /
                            g_browseDir = (ec2 ? start : abs).lexically_normal().string();
                            kioskComputeShortcuts();
                            kioskScanBrowse(g_browseDir);
                            g_kioskPage = KIOSK_PAGE_BROWSE;
                        } else {                                   // retirer ce dossier (croix ❌)
                            const int idx = g_romDirSel - 1;
                            if (idx >= 0 && idx < (int)g_kioskRomDirs.size())
                                g_kioskRomDirs.erase(g_kioskRomDirs.begin() + idx);
                            cfg.romDirs = g_kioskRomDirs;
                            saveConfig(exeDir, cfg, &machine, true);
                            kioskScanDisks(disksDir, machine.fdc.mountedPath());
                            g_kioskDiskSel = 0;
                            if (g_romDirSel > (int)g_kioskRomDirs.size())
                                g_romDirSel = (int)g_kioskRomDirs.size();
                        }
                    }
                    // (B) revient à la liste, focus sur les actions (d'où l'on venait).
                    if (caNow && !pCancel) { g_kioskPage = KIOSK_PAGE_LIST; g_kioskZone = KIOSK_ZONE_ACTIONS; }

                } else {   // KIOSK_PAGE_LIST — deux menus (intérieur / extérieur)
                    const int nd = (int)g_kioskDisks.size();
                    // GAUCHE/DROITE : bascule d'un menu à l'autre (front, non répété).
                    static bool pSwap = false;
                    const bool swapNow = left || right;
                    if (swapNow && !pSwap)
                        g_kioskZone = (g_kioskZone == KIOSK_ZONE_LIST) ? KIOSK_ZONE_ACTIONS
                                                                       : KIOSK_ZONE_LIST;
                    pSwap = swapNow;
                    // HAUT/BAS : navigue DANS le menu focalisé (chacun boucle sur lui-même).
                    // L1/R1 (pgUp/pgDn) : saut de PAGE dans la liste des jeux (défilement rapide).
                    if (step) {
                        if (g_kioskZone == KIOSK_ZONE_LIST) {
                            if (nd > 0) {
                                const int kPage = 10;   // taille du saut rapide L1/R1
                                int delta = 0;
                                if      (down) delta =  1;
                                else if (up)   delta = -1;
                                else if (pgDn) delta =  kPage;
                                else if (pgUp) delta = -kPage;
                                if (delta != 0) {
                                    g_kioskDiskSel += delta;
                                    if (delta == 1 || delta == -1)   // pas-à-pas : boucle
                                        g_kioskDiskSel = (g_kioskDiskSel % nd + nd) % nd;
                                    else                             // saut de page : butée
                                        g_kioskDiskSel = std::max(0, std::min(nd - 1, g_kioskDiskSel));
                                }
                            }
                        } else if (up || down) {
                            g_kioskActSel += down ? 1 : -1;
                            g_kioskActSel = (g_kioskActSel % 6 + 6) % 6;
                        }
                    }
                    // FEU (A/Entrée) : déclenche l'item surligné du menu focalisé.
                    if (okNow && !pOk) {
                        if (g_kioskZone == KIOSK_ZONE_LIST) {
                            // Borne défensive : la liste peut avoir rétréci (dossier ROM
                            // débranché) sans que le curseur ait bougé depuis.
                            if (nd > 0) reqMount = g_kioskDisks[std::min(std::max(0, g_kioskDiskSel), nd - 1)];  // INSÉRER à chaud
                        } else {
                            switch (g_kioskActSel) {
                                case 0: reqHardReset = true;              // Redémarrer
                                        g_kioskDiskMenu = false; break;
                                case 1: g_kioskPage = KIOSK_PAGE_KEYS;    // Clavier & souris
                                        g_kioskKeySel = 0; break;
                                case 2: g_kioskPage = KIOSK_PAGE_JOY;     // Joysticks (affectation)
                                        g_kioskJoySel = 0; break;
                                case 3:                                   // Dossiers ROM (gestion)
                                    if (kioskPruneRomDirs()) {           // retire les disparus + persiste
                                        cfg.romDirs = g_kioskRomDirs;
                                        saveConfig(exeDir, cfg, &machine, true);
                                        kioskScanDisks(disksDir, machine.fdc.mountedPath());
                                        g_kioskDiskSel = 0;   // la liste vient de rétrécir : l'ancien index pointerait hors du vecteur
                                    }
                                    g_romDirSel = 0;
                                    g_kioskPage = KIOSK_PAGE_ROMDIRS; break;
                                case 4:                                   // Mode bureau (GUI)
                                        g_kioskSwitchReq = 2;
                                        g_kioskDiskMenu = false; break;
                                case 5: g_kioskPage = KIOSK_PAGE_QUIT; break;  // Quitter
                            }
                        }
                    }
                    // (B) reprendre le jeu : ferme le menu.
                    if (caNow && !pCancel) g_kioskDiskMenu = false;
                }
                pOk = okNow; pCancel = caNow;

                drawKioskDiskMenu(disksDir, machine.fdc.mountedPath());
            }
        }

        // Overlay transitoire du save-state rapide (F5/F7) — coin bas-gauche, ~2,4 s.
        if (g_stateMsgFrames > 0) {
            --g_stateMsgFrames;
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(12, io.DisplaySize.y - 12), ImGuiCond_Always, ImVec2(0, 1));
            ImGui::SetNextWindowBgAlpha(0.75f);
            ImGui::Begin("##statemsg", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "%s", g_stateMsg.c_str());
            ImGui::End();
        }

        ImGui::Render();
        if (g_kiosk) drawStKiosk(screen, fbw, fbh, kTop, kH);   // rendu adaptatif (ImGui vide au-dessus)
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        // --- Glisser-déposer : le TYPE du support décide de la destination ----
        // Un DOSSIER = lecteur GEMDOS (C:), une image disquette = lecteur A, une image
        // de disque dur = ACSI, un TOS = ROM, une petite image = cartouche. Traité ICI,
        // juste avant la consommation des requêtes, pour que le montage prenne effet
        // dans la même trame. Ignoré en BORNE : sa config est figée (cf. saveConfig) et
        // un visiteur n'a de toute façon pas de bureau d'où glisser un fichier.
        if (!g_dropped.empty()) {
            if (!g_kiosk) for (const std::string& d : g_dropped) {
                std::error_code ec;
                if (fs::is_directory(d, ec)) { reqMountGemdos = d; continue; }
                std::string ext = fs::path(d).extension().string();
                for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx") {
                    reqMount = d;
                } else if (ext == ".hd" || ext == ".acsi" || ext == ".vhd" || ext == ".raw") {
                    reqMountAcsi = d;
                } else if (ext == ".img" || ext == ".rom" || ext == ".bin") {
                    // `.img` désigne aussi bien un TOS qu'une cartouche ou un disque dur
                    // (roms/, carts/ et hd/ en sont tous pleins) : trancher sur la TAILLE
                    // et l'en-tête, pas sur l'extension. Un TOS commence par BRA.S ($602E)
                    // et tient en 512 Ko ; une cartouche fait au plus 128 Ko ; au-delà,
                    // c'est un disque dur.
                    const std::uintmax_t sz = fs::file_size(d, ec);
                    uint8_t hdr[2] = { 0, 0 };
                    { std::ifstream f(d, std::ios::binary);
                      if (f) f.read(reinterpret_cast<char*>(hdr), 2); }
                    if (!ec && sz <= 512u * 1024u && hdr[0] == 0x60 && hdr[1] == 0x2E) {
                        cfg.rom = d; saveConfig(exeDir, cfg, &machine); reqRebuild = true;
                    } else if (!ec && sz <= 128u * 1024u) {
                        reqMountCart = d;
                    } else {
                        reqMountAcsi = d;
                    }
                } else {
                    g_stateMsg = "Dropped: unknown type (" +
                                 fs::path(d).filename().string() + ")";
                    g_stateMsgFrames = 150;
                }
            }
            g_dropped.clear();
        }
        // Disk Library : montage / éjection à chaud du lecteur A. La config n'est
        // persistée QUE si le montage a réussi — sinon une image corrompue serait
        // écrite dans neost.cfg et retentée à chaque boot.
        if (!reqMount.empty()) {
            if (machine.fdc.loadImage(reqMount)) {
                // En BORNE, ne pas mémoriser la disquette insérée par un visiteur : même
                // si saveConfig() refuse d'écrire ici, salir `cfg` suffirait à faire fuir
                // ce disk= lors d'un saveConfig(force=true) ultérieur (dossiers ROM,
                // joysticks), qui réécrit TOUT le fichier — et la borne ne repartirait
                // plus sur son jeu d'origine (invariant « config figée », cf. DEV.md).
                if (!g_kiosk && !g_kioskLaunched) {
                    cfg.disk = reqMount; saveConfig(exeDir, cfg, &machine);
                }
            } else {
                g_stateMsg = "Unreadable floppy image"; g_stateMsgFrames = 120;
            }
        }
        if (reqEject) {
            machine.fdc.eject();
            cfg.disk.clear(); saveConfig(exeDir, cfg, &machine);
        }
        // Lecteur B : même discipline que A. Le cœur le gère depuis toujours
        // (Fdc::loadImage(path, 1), option --diskb du headless) ; seule la GUI
        // l'ignorait — or plusieurs jeux ne DÉMARRENT qu'avec leur disque 2 monté
        // (Lethal Xcess) et les jeux 2 disquettes réclament d'en changer.
        if (!reqMountB.empty()) {
            if (machine.fdc.loadImage(reqMountB, 1)) {
                if (!g_kiosk && !g_kioskLaunched) {
                    cfg.diskb = reqMountB; saveConfig(exeDir, cfg, &machine);
                }
            } else {
                g_stateMsg = "Unreadable floppy image (B)"; g_stateMsgFrames = 120;
            }
        }
        if (reqEjectB) {
            machine.fdc.eject(1);
            cfg.diskb.clear(); saveConfig(exeDir, cfg, &machine);
        }
        // Cart Library : branchement / éjection à chaud du port cartouche.
        if (!reqMountCart.empty()) {
            // VALIDER AVANT de libérer : loadCart échoue sur un fichier illisible ou de
            // plus de 128 Ko (un .img volumineux dans carts/ est banal). Démonter le HD
            // GEMDOS d'abord laissait alors la machine SANS cartouche ET SANS C:, avec
            // cfg.gemdos vidé — donc le disque dur perdu au prochain saveConfig, sans
            // message ni reset pour l'expliquer.
            if (machine.loadCart(reqMountCart)) {
                if (machine.gemdos.active()) {  // $FA0000 occupé par la cartouche système GEMDOS
                    machine.gemdos.unmount();
                    cfg.gemdos.clear();
                }
                cfg.cart = reqMountCart; saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;       // le TOS sonde le port cartouche au boot
            } else {
                g_stateMsg = "Unreadable cartridge (max 128 KB)"; g_stateMsgFrames = 120;
            }
        }
        if (reqEjectCart) {
            machine.ejectCart();
            cfg.cart.clear(); saveConfig(exeDir, cfg, &machine);
            reqHardReset = true;           // relance sans la ROM $FA0000
        }
        // Disque dur (menu Machine → Disque dur) : GEMDOS HD et image ACSI. Chaque
        // opération force un hard reset — le TOS ne (re)sonde les disques qu'au boot.
        if (!reqMountGemdos.empty()) {
            // Même ordre que la cartouche : setDirectory échoue sur une simple faute de
            // frappe, et retirer la cartouche AVANT la sortait du bus SANS reset — un
            // programme qui tournait depuis $FA0000 partait alors dans le décor.
            const std::string gemHost = resolvePath(reqMountGemdos);
            const bool hadCart = !machine.bus.mountedCartPath().empty();
            if (machine.gemdos.setDirectory(gemHost)) {
                // setDirectory a DÉJÀ remplacé $FA0000 par la cartouche système (même
                // stockage) : appeler ejectCart ici viderait ce qu'on vient d'installer
                // et laisserait le vecteur trap #1 pointer sur un port dépeuplé. On se
                // contente donc d'oublier la cartouche utilisateur côté config.
                if (hadCart) cfg.cart.clear();
                cfg.gemdos = reqMountGemdos; saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;
            } else {
                g_stateMsg = "GEMDOS folder not found"; g_stateMsgFrames = 120;
            }
        }
        if (reqEjectGemdos) {
            machine.gemdos.unmount();
            cfg.gemdos.clear(); saveConfig(exeDir, cfg, &machine);
            reqHardReset = true;
        }
        if (!reqMountAcsi.empty()) {
            if (machine.fdc.mountAcsi(resolvePath(reqMountAcsi))) {
                std::fprintf(stderr, "[main] ACSI : %d partition(s)\n",
                             machine.fdc.acsiPartitionCount());
                cfg.acsi = reqMountAcsi; saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;
            } else {
                g_stateMsg = "Unreadable ACSI image"; g_stateMsgFrames = 120;
            }
        }
        if (reqEjectAcsi) {
            machine.fdc.unmountAcsi();
            cfg.acsi.clear(); saveConfig(exeDir, cfg, &machine);
            reqHardReset = true;
        }
#else
        if (g_kiosk) drawStKiosk(screen, fbw, fbh, kTop, kH);   // kiosk : zoom adaptatif
        else GlScreen::blitTexFullscreen(crtApply(screen, fbw, fbh));  // repli sans ImGui + CRT
#endif
        // Changement de moniteur (couleur/mono) → hard reset pour que TOS
        // re-détecte la résolution au boot.
        if (reqMonitor >= 0 && (reqMonitor == 1) != machine.mfp.colorMonitor()) {
            machine.mfp.setColorMonitor(reqMonitor == 1);
            machine.reset();
            cfg.mono = (reqMonitor == 0);   // mémorise le mode
            saveConfig(exeDir, cfg, &machine);
        }
        // Application des requêtes (en fin de boucle, hors rendu ImGui) :
        if (reqRebuild)   applyConfig();       // modèle/RAM/cœur/ROM → reconfig à chaud
        if (reqHardReset) machine.hardReset(); // power-cycle (RAM effacée, boot à froid)
        if (reqReset)     machine.reset();     // reset « doux » (RAM conservée)
        if (reqCapture) {                      // clic dans l'écran → on capture la souris
            g_mouseCaptured = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(window, &lastMx, &lastMy);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

        glfwSwapBuffers(window);

        // Dort jusqu'à l'échéance de la prochaine trame émulée (posée par la boucle
        // de rattrapage ci-dessus). En retard → pas de sommeil, le rattrapage du
        // prochain tour exécutera les trames dues. Le sommeil est plafonné à une
        // trame : on garde le GUI réactif même si l'horloge dérive.
        const auto now = clock::now();
        if (now < emuNext) {
            auto wake = emuNext;
            const auto cap = now + std::chrono::milliseconds(20);
            if (wake > cap) wake = cap;
            std::this_thread::sleep_until(wake);
        }
    }

    // Mémorise le dernier ROM, la disquette/cartouche montée et le moniteur.
    cfg.disk = machine.fdc.mountedPath();
    cfg.cart = machine.bus.mountedCartPath();
    cfg.mono = !machine.mfp.colorMonitor();
    cfg.showHex = g_showHex; cfg.showCpu = g_showCpu;
    cfg.showJoy = g_showJoy; cfg.showCfg = g_showCfg;
    saveConfig(exeDir, cfg, &machine);

#if defined(NEOST_WITH_IMGUI)
    // Écrit imgui.ini avant l'arrêt → garantit la sauvegarde de la taille de fenêtre
    // (et des positions de sous-fenêtres) même si rien d'autre n'a marqué les réglages.
    if (ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
