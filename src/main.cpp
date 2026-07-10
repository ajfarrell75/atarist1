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
#include <vector>
#include <sys/stat.h>

#include "core/Machine.hpp"
#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "io/JoystickInput.hpp"

namespace fs = std::filesystem;

// Résout un chemin de données indépendamment du répertoire courant : tel quel,
// puis relatif au répertoire de l'exécutable (utile quand on lance depuis build/).
static bool fileExists(const std::string& p) { struct stat s; return ::stat(p.c_str(), &s) == 0; }
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
struct Config { std::string rom; std::string disk; std::string cart; bool mono = false;
                std::string gemdos;   // HD GEMDOS : dossier hôte monté en C: (vide = off)
                std::string acsi;     // image disque dur ACSI cible 0 (vide = off)
                std::string cpu = "moira"; std::string machine = "st";
                std::string mem = "512k"; bool fpu = false;   // MC68881 Mega STE (cf. Fpu.hpp)
                int joyport = 1;
                float joydeadzone = 0.30f; bool fastfdc = false;
                float volume = 1.0f;   // volume maître de la sortie audio (0..1, barre de menu)
                bool showDisk = true, showCart = true, showHex = true, showCpu = true;
                bool showJoy = false;
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
static Config loadConfig(const std::string& exeDir) {
    Config c;
    std::ifstream f(cfgPath(exeDir));
    if (!f) f.open("neost.cfg");
    std::string line;
    while (std::getline(f, line)) {
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
        else if (line.rfind("joydeadzone=", 0) == 0) c.joydeadzone = std::strtof(line.substr(12).c_str(), nullptr);
        else if (line.rfind("fastfdc=", 0) == 0) c.fastfdc = (line.substr(8) == "1");
        else if (line.rfind("volume=", 0) == 0) {
            c.volume = std::strtof(line.substr(7).c_str(), nullptr);
            if (c.volume < 0.0f) c.volume = 0.0f;
            if (c.volume > 1.0f) c.volume = 1.0f;
        }
        else if (line.rfind("showDisk=", 0) == 0) c.showDisk = (line.substr(9) == "1");
        else if (line.rfind("showCart=", 0) == 0) c.showCart = (line.substr(9) == "1");
        else if (line.rfind("showHex=", 0) == 0) c.showHex = (line.substr(8) == "1");
        else if (line.rfind("showCpu=", 0) == 0) c.showCpu = (line.substr(8) == "1");
        else if (line.rfind("showJoy=", 0) == 0) c.showJoy = (line.substr(8) == "1");
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
    return c;
}
// Mode kiosk (borne/expo) : plein écran sans chrome, config figée, sortie par chord.
// Activé par --kiosk. Déclaré ici car saveConfig doit le consulter (gel de la config).
static bool g_kiosk = false;
// Zoom kiosk adaptatif (cale le contenu réel sur la hauteur) : ON par défaut,
// basculable à chaud par F10. OFF = cadre complet fixe (pillarbox, rien ne déborde).
static bool g_kioskAdaptive = true;
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
       KIOSK_PAGE_BROWSE = 3, KIOSK_PAGE_ROMDIRS = 4 };
static bool g_kioskDiskMenu = false;          // menu ouvert
static int  g_kioskPage     = KIOSK_PAGE_LIST;
static int  g_kioskDiskSel  = 0;              // index disquette sélectionnée (menu INTÉRIEUR)
// Page liste = DEUX menus qu'on bascule avec gauche/droite : INTÉRIEUR (liste des
// jeux) et EXTÉRIEUR (Redémarrer / Clavier / Quitter). g_kioskZone = quel menu a le
// focus ; le FEU valide l'item surligné du menu focalisé.
enum { KIOSK_ZONE_LIST = 0, KIOSK_ZONE_ACTIONS = 1 };
static int  g_kioskZone   = KIOSK_ZONE_LIST;
static int  g_kioskActSel = 0;                // index action (menu EXTÉRIEUR, 0..2)
static int  g_kioskKeySel   = 0;              // page clavier : touche/clic sélectionné
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
// force=true : écrit la config MÊME en kiosk (normalement figé). Utilisé pour le seul
// réglage que la borne a le droit de persister : le dossier ROM additionnel choisi via
// le menu in-game (le reste de la config kiosk reste identique à ce qui a été chargé).
static void saveConfig(const std::string& exeDir, Config& c, Machine* machine = nullptr, bool force = false) {
    if (g_kiosk && !force) return;   // kiosk : configuration figée — la borne repart toujours identique
    if (machine) snapshotRtc(*machine, c);
    std::ofstream f(cfgPath(exeDir));
    if (!f) f.open("neost.cfg");
    if (f) f << "rom=" << c.rom << "\ndisk=" << c.disk << "\ncart=" << c.cart
             << "\ngemdos=" << c.gemdos << "\nacsi=" << c.acsi
             << "\nmono=" << (c.mono ? 1 : 0)
             << "\ncpu=" << c.cpu << "\nmachine=" << c.machine << "\nmem=" << c.mem
             << "\nfpu=" << (c.fpu ? 1 : 0)
             << "\njoyport=" << c.joyport
             << "\njoydeadzone=" << c.joydeadzone << "\nfastfdc=" << (c.fastfdc ? 1 : 0)
             << "\nvolume=" << c.volume
             << "\nshowDisk=" << (c.showDisk ? 1 : 0)
             << "\nshowCart=" << (c.showCart ? 1 : 0)
             << "\nshowHex=" << (c.showHex ? 1 : 0)
             << "\nshowCpu=" << (c.showCpu ? 1 : 0)
             << "\nshowJoy=" << (c.showJoy ? 1 : 0)
             << "\ncrt=" << (c.crt ? 1 : 0)
             << "\ncrt_bright=" << c.crtParams.brightness
             << "\ncrt_contrast=" << c.crtParams.contrast
             << "\ncrt_sat=" << c.crtParams.saturation
             << "\ncrt_hue=" << c.crtParams.hue
             << "\ncrt_sharp=" << c.crtParams.sharpness
             << "\ncrt_persist=" << c.crtParams.persistence
             << "\ncrt_scanlines=" << c.crtParams.scanlines
             << "\ncrt_barrel=" << c.crtParams.barrel
             << "\ncrt_mask=" << static_cast<int>(c.crtParams.shadowMask)
             << "\ncrt_maskstr=" << c.crtParams.shadowMaskStrength
             << "\ncrt_lumgain=" << c.crtParams.luminanceGain
             << "\ncrt_center=" << c.crtParams.centerLighting
             << "\ncrt_gamma=" << c.crtParams.phosphorGamma
             << "\nrtc=" << c.rtc << "\nrtc_saved=" << c.rtcSaved << "\n";
    // Dossiers ROM additionnels (0..N) : une ligne kiosk_romdir= par dossier.
    if (f) for (const auto& d : c.romDirs) f << "kiosk_romdir=" << d << "\n";
}

#if defined(NEOST_WITH_IMGUI)
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

static void* WinSettings_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/) {
    return (void*)1;                           // une seule entrée → on accepte toujours
}
static void WinSettings_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    int w = 0, h = 0;
    if (std::sscanf(line, "Size=%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
        g_iniWinW = w; g_iniWinH = h; g_iniWinValid = true;
    }
}
static void WinSettings_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*) {
    if (g_iniWinValid && g_window) glfwSetWindowSize(g_window, g_iniWinW, g_iniWinH);
}
static void WinSettings_WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
    if (!g_window) return;
    int w = 0, h = 0;
    glfwGetWindowSize(g_window, &w, &h);
    buf->appendf("[%s][Window]\n", handler->TypeName);
    buf->appendf("Size=%d,%d\n\n", w, h);
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
bool  g_showDisk = true, g_showCart = true, g_showHex = true, g_showCpu = true;  // fenêtres masquables
bool  g_showJoy = false;               // fenêtre joystick (visualisation live)

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
bool  g_joyCfgDirty = false;           // un réglage joystick a changé → resauver neost.cfg
// Champs de saisie du menu Machine → Disque dur (dossier HD GEMDOS / image ACSI).
// Globaux (et non statiques du menu) pour que le sous-menu Profils puisse les
// resynchroniser : g_*Init = false → relecture de cfg à la prochaine ouverture.
char g_gdBuf[512] = {0}, g_hdBuf[512] = {0};
bool g_gdInit = false, g_hdInit = false;

void onGlfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW erreur %d : %s\n", code, desc);
}

// Callback bouton souris : ÉVÉNEMENTIEL (capte chaque transition, même un
// double-clic rapide qu'une scrutation par trame manquerait). Envoie un paquet
// IKBD sans mouvement portant l'état courant des boutons.
void onMouseButton(GLFWwindow* w, int /*button*/, int /*action*/, int /*mods*/) {
    if (!g_ikbd || !g_mouseCaptured) return;
    const bool l = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    const bool r = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (g_dbgMouse) std::fprintf(stderr, "[souris] bouton  L=%d R=%d\n", l, r);
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
    const GLuint t = crtApply(s, fbw, fbh);
    // Aspect pixel : basse rés (≤480 px de large) et 200 lignes = pixels doublés.
    const float sx = (s.w <= 480) ? 2.f : 1.f;
    const float sy = (s.h <= 300) ? 2.f : 1.f;
    const float scale = (float)fbh / (cH * sy);        // px écran par px ST logique (vertical)
    const float vw = s.w * sx * scale, vh = s.h * sy * scale;   // cadre COMPLET à cette échelle
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
    std::fprintf(stderr, "[kbd] disposition TOS : %s (mapping symbolique)\n",
                 g_kbdCountry >= 0 && g_kbdCountry <= 3 ? names[g_kbdCountry]
                 : g_kbdCountry == 127 ? "multilangue (défaut)" : "autre (défaut)");
}

// Callback clavier GLFW → IKBD. La touche Suppr (DEL) est réservée à l'hôte (elle
// libère la souris capturée), donc jamais transmise au ST. Échap, lui, est bien
// envoyé au ST (beaucoup de jeux/applications s'en servent).
void onKey(GLFWwindow*, int key, int scancode, int action, int /*mods*/) {
    if (!g_ikbd || action == GLFW_REPEAT) return;   // TOS gère sa propre répétition (pas l'IKBD)
    if (key == GLFW_KEY_DELETE) return;             // touche hôte (libération souris)
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
    ImGui::Begin("Mémoire (hex)");
    ImGui::InputInt("Adresse base", &base, 16, 256,
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
    if (IconButton(ICON_FA_POWER_OFF, "Reset (RESET physique)")) reqReset = true;
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
    if (ImGui::Checkbox("Émulation clavier (flèches + Ctrl droit)", &g_kbdJoy)) g_joyCfgDirty = true;
    ImGui::SameLine(); ImGui::TextDisabled("(F11)");
    ImGui::Text("Port émulé :"); ImGui::SameLine();
    if (ImGui::RadioButton("1 (jeux)", g_kbdJoyPort == 1)) { g_kbdJoyPort = 1; g_joyCfgDirty = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("0 (souris)", g_kbdJoyPort == 0)) { g_kbdJoyPort = 0; g_joyCfgDirty = true; }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Zone morte", &g_joyDeadzone, 0.0f, 0.95f, "%.2f")) {
        if (g_joyDeadzone < 0.0f) g_joyDeadzone = 0.0f;
        if (g_joyDeadzone > 0.95f) g_joyDeadzone = 0.95f;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) g_joyCfgDirty = true;

    ImGui::Separator();

    // --- Sortie réellement envoyée au ST (le plus important) --------------------
    auto decodeRow = [](const char* who, uint8_t v) {
        ImGui::Text("%s  $%02X :", who, v); ImGui::SameLine();
        drawJoyDirLed("HAUT",   v & stjoy::UP);
        drawJoyDirLed("BAS",    v & stjoy::DOWN);
        drawJoyDirLed("GAUCHE", v & stjoy::LEFT);
        drawJoyDirLed("DROITE", v & stjoy::RIGHT);
        drawJoyDirLed("FEU",    v & stjoy::FIRE);
        ImGui::NewLine();
    };
    ImGui::TextDisabled("→ Envoyé à l'IKBD (ST) :");
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
        ImGui::Text("Manette %d : %s", jid, nm ? nm : "?");
        if (stPort >= 0) { ImGui::SameLine(); ImGui::TextDisabled("→ port ST %d", stPort); }

        GLFWgamepadstate gs;
        if (glfwGetGamepadState(jid, &gs)) {
            ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f), "  reconnue gamepad (mapping SDL)");
            ImGui::Indent(8.0f);
            drawJoystickAxisBar("LX", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X],  g_joyDeadzone);
            drawJoystickAxisBar("LY", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y],  g_joyDeadzone);
            drawJoystickAxisBar("RX", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], g_joyDeadzone);
            drawJoystickAxisBar("RY", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], g_joyDeadzone);
            ImGui::Unindent(8.0f);
        } else {
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f), "  NON reconnue gamepad → lecture brute");
        }

        // Axes bruts (toujours affichés : révèlent un axe non centré au repos).
        int axN = 0, btN = 0, hatN = 0;
        const float*         ax  = glfwGetJoystickAxes(jid, &axN);
        const unsigned char* bt  = glfwGetJoystickButtons(jid, &btN);
        const unsigned char* hat = glfwGetJoystickHats(jid, &hatN);
        ImGui::Text("  Axes bruts (%d) :", axN);
        for (int i = 0; i < axN && ax; ++i) {
            char lbl[24]; std::snprintf(lbl, sizeof lbl, "a%d%s", i,
                                        (i == 0 ? " (X?)" : i == 1 ? " (Y?)" : ""));
            ImGui::Indent(8.0f); drawJoystickAxisBar(lbl, ax[i], g_joyDeadzone); ImGui::Unindent(8.0f);
        }
        ImGui::Text("  Boutons (%d) :", btN); ImGui::SameLine();
        for (int i = 0; i < btN && bt; ++i)
            if (bt[i]) { ImGui::SameLine(); ImGui::Text("%d", i); }
        if (hat && hatN >= 1)
            ImGui::Text("  Hat0 : %s%s%s%s", (hat[0]&GLFW_HAT_UP)?"H":"", (hat[0]&GLFW_HAT_DOWN)?"B":"",
                        (hat[0]&GLFW_HAT_LEFT)?"G":"", (hat[0]&GLFW_HAT_RIGHT)?"D":"");
        // Décomposition analogique / numérique + effet du filtre anti-bloqué.
        const float thr = (g_joyDeadzone < 0.0f) ? 0.0f : (g_joyDeadzone > 0.95f ? 0.95f : g_joyDeadzone);
        uint8_t an = 0, dg = 0; stjoy::readStickRaw(jid, thr, an, dg);
        const uint8_t fin = stjoy::readStick(jid, g_joyDeadzone);
        ImGui::Text("  analogique $%02X | numérique brut $%02X", an, dg);
        if ((dg & ~fin) & ~an)
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f),
                               "  filtre anti-bloqué : bits numériques collés ignorés ($%02X)",
                               uint8_t((dg & ~fin) & ~an));
        ImGui::Text("  → octet ST envoyé : $%02X", fin);
        ImGui::Separator();
    }
    if (nPresent == 0) ImGui::TextDisabled("Aucune manette détectée. (Clavier : active l'émulation ci-dessus.)");
    (void)win;
    ImGui::End();
}

// Fenêtre de réglages des effets CRT (façade moniteur). Modifie g_crtOn /
// g_crtParams ; pose `changed`=true si l'utilisateur a touché quelque chose
// (l'appelant recopie alors dans neost.cfg et resauve). Les presets écrivent
// les mêmes champs numériques → une fois figés ils survivent au save.
void drawCrtSettings(bool& changed) {
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Effets CRT", &g_showCrt);

    if (ImGui::Checkbox("Activer les effets CRT", &g_crtOn)) {
        changed = true;
        if (g_crtOn && !g_crt.available() && !g_crtInit) { g_crtInit = true; g_crt.initialize(); }
    }
    // Diagnostic : shader indisponible (ex. contexte GL 2.1 sur macOS legacy).
    if (g_crtOn && g_crtInit && !g_crt.available()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Shader indisponible :");
        ImGui::TextWrapped("%s", g_crt.lastError().c_str());
        ImGui::TextDisabled("→ écran ST présenté brut (passthrough).");
    }

    ImGui::TextDisabled("Presets :");
    ImGui::SameLine();
    if (ImGui::SmallButton("Léger"))    { applyCrtPreset("leger",    g_crtParams, g_crtOn); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Arcade"))   { applyCrtPreset("arcade",   g_crtParams, g_crtOn); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Phosphore")){ applyCrtPreset("phosphor", g_crtParams, g_crtOn); changed = true; }

    ImGui::Separator();
    ImGui::BeginDisabled(!g_crtOn);
    neost::CrtParams& p = g_crtParams;
    bool ch = false;
    ch |= ImGui::SliderFloat("Luminosité",  &p.brightness, -0.5f, 0.5f);
    ch |= ImGui::SliderFloat("Contraste",   &p.contrast,    0.5f, 1.5f);
    ch |= ImGui::SliderFloat("Saturation",  &p.saturation,  0.0f, 2.0f);
    ch |= ImGui::SliderFloat("Teinte",      &p.hue,        -0.5f, 0.5f);
    ImGui::Separator();
    ch |= ImGui::SliderFloat("Netteté",     &p.sharpness,   0.0f, 1.0f);
    ch |= ImGui::SliderFloat("Rémanence",   &p.persistence, 0.0f, 0.98f);
    ImGui::Separator();
    ch |= ImGui::SliderFloat("Scanlines",   &p.scanlines,   0.0f, 1.0f);
    ch |= ImGui::SliderFloat("Baril",       &p.barrel,      0.0f, 0.30f);

    ImGui::Separator();
    static const char* kMaskNames[] = {
        "Off", "Triade (3 bandes)", "Grille d'ouverture (Trinitron)", "Points (triades décalées)"
    };
    int maskIdx = static_cast<int>(p.shadowMask);
    if (ImGui::Combo("Shadow mask", &maskIdx, kMaskNames, IM_ARRAYSIZE(kMaskNames))) {
        p.shadowMask = static_cast<neost::CrtParams::ShadowMask>(maskIdx);
        ch = true;
    }
    ImGui::BeginDisabled(p.shadowMask == neost::CrtParams::ShadowMask::Off);
    ch |= ImGui::SliderFloat("Force du masque", &p.shadowMaskStrength, 0.0f, 1.0f);
    ImGui::EndDisabled();
    ch |= ImGui::SliderFloat("Gain de luminance", &p.luminanceGain, 1.0f, 2.0f);
    ch |= ImGui::SliderFloat("Vignette",          &p.centerLighting, 0.5f, 1.0f);
    ch |= ImGui::SliderFloat("Gamma phosphore",   &p.phosphorGamma, 0.6f, 2.6f);
    ImGui::EndDisabled();

    if (ch) changed = true;
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
    ImGui::Begin(ICON_FA_BUG " Débogueur", &g_showDbg);

    // --- État + transport -----------------------------------------------------
    if (g_dbgPaused) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           ICON_FA_PAUSE " EN PAUSE  \xe2\x80\x94  PC=$%06X%s",
                           cpu.pc(), symLabel(cpu.pc()).c_str());
        if (ImGui::Button(ICON_FA_PLAY " Continuer")) {
            cpu.clearBreakpointHit();   // arme le skip-once de l'adresse courante
            g_dbgPaused = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STEP_FORWARD " Pas (1 instr)")) g_dbgStepInstr = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STEP_FORWARD " Pas (1 trame)")) g_dbgStepFrame = true;
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), ICON_FA_PLAY " En cours");
        if (ImGui::Button(ICON_FA_PAUSE " Pause")) g_dbgPaused = true;
    }
    ImGui::Separator();

    // --- Symboles : chargement (.sym nm-style ou exécutable TOS) + bp par nom --
    ImGui::Text("Symboles (%zu)", g_symbols.count());
    static char symPath[512] = "";
    static char symBaseBuf[16] = "";
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##sympath", "chemin .sym ou .TOS", symPath, sizeof symPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputTextWithHint("##symbase", "base hex", symBaseBuf, sizeof symBaseBuf,
                             ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Charger") && symPath[0]) {
        const uint32_t base = (uint32_t)std::strtoul(symBaseBuf, nullptr, 16);
        g_symbols.load(symPath, base);   // auto-détecte nm-style vs exécutable TOS
    }
    // Breakpoint par symbole (nom → adresse via la table).
    static char symBp[64] = "";
    ImGui::SetNextItemWidth(220.0f);
    const bool symEnter = ImGui::InputTextWithHint("##symbp", "nom de symbole", symBp, sizeof symBp,
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("BP symbole") || symEnter) && symBp[0]) {
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
    if ((ImGui::Button("Ajouter") || entered) && bpBuf[0]) {
        cpu.setBreakpoint((uint32_t)std::strtoul(bpBuf, nullptr, 16));
        bpBuf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Tout effacer")) cpu.clearAllBreakpoints();

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

    // --- Désassemblage autour du PC (clic sur une ligne = toggle breakpoint) ---
    ImGui::TextDisabled("Désassemblage (clic = poser/retirer un breakpoint)");
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
        for (const auto& e : fs::recursive_directory_iterator(dir, e2)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx") {
                const std::string p = e.path().string();
                if (std::find(g_kioskDisks.begin(), g_kioskDisks.end(), p) == g_kioskDisks.end())
                    g_kioskDisks.push_back(p);
            }
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
    if (const char* home = std::getenv("HOME"))
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
static const int KIOSK_KEY_COUNT = (int)(sizeof(KIOSK_KEYS) / sizeof(KIOSK_KEYS[0]));
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
        const float footer = 4.0f * (bf * 2.3f + sp)         // 4 rangées d'action @2.3
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
        actionRow(2, ImVec4(0.6f, 0.95f, 0.6f, 1.0f),  ICON_FA_FOLDER_OPEN " ROM FOLDERS");
        actionRow(3, ImVec4(1.0f, 0.5f, 0.4f, 1.0f),   ICON_FA_SIGN_OUT_ALT " QUIT NEOST");
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

// Fenêtre de l'écran ST : fenêtre de BASE (toujours là, jamais au premier plan).
// Placée sous les barres au 1er lancement, puis DÉPLAÇABLE par glissé de sa barre de
// titre (ImGui mémorise sa position). La taille d'affichage suit la résolution
// COURANTE du buffer (bordures overscan INCLUSES) en respectant l'aspect pixel ST :
// basse rés ×2/×2, moyenne ×1/×2, mono ×1/×1 — l'écran actif occupe donc toujours
// ~640×400 et les bordures s'ajoutent autour (low res bordée = 416×276 → 832×552).
// Clic dans l'image = capture souris.
void drawStScreen(const GlScreen& s, bool captured, bool& reqCapture, float topOffset) {
    // FirstUseEver (et non Always) : on ne fixe la position qu'au tout 1er affichage,
    // sinon la fenêtre serait re-ancrée à chaque trame et impossible à déplacer.
    ImGui::SetNextWindowPos(ImVec2(0.0f, topOffset), ImGuiCond_FirstUseEver);
    // Aspect pixel ST : la basse rés a des pixels 2× plus larges/hauts que la mono
    // (320×200 et 640×400 couvrent la même surface écran). On dérive l'échelle des
    // dimensions du buffer (overscan inclus) : largeur ×2 si ≤ 480 px (classe basse
    // rés), hauteur ×2 si ≤ 300 lignes (classe 200 lignes).
    const float sx = (s.w <= 480) ? 2.0f : 1.0f;
    const float sy = (s.h <= 300) ? 2.0f : 1.0f;
    const float nativeW = s.w * sx, nativeH = s.h * sy;   // taille « moniteur » corrigée
    const float aspect  = (nativeH > 0.f) ? nativeW / nativeH : 4.f / 3.f;
    // Taille par défaut = native (au 1er affichage) ; ensuite LIBREMENT redimensionnable.
    ImGui::SetNextWindowSize(ImVec2(nativeW, nativeH + 34.f), ImGuiCond_FirstUseEver);
    // Contrainte de ratio : la FENÊTRE garde l'aspect ST (l'image remplit alors sans
    // bandes). ImGui appelle ce callback pendant le redimensionnement.
    static float s_aspect = aspect;   // capté pour le callback (mono/couleur → maj)
    s_aspect = aspect;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(160.f, 120.f), ImVec2(FLT_MAX, FLT_MAX),
        [](ImGuiSizeCallbackData* d) {
            const float extra = 34.f;   // barre de titre + ligne d'aide (approx.)
            const float a = *static_cast<float*>(d->UserData);
            d->DesiredSize.y = (d->DesiredSize.x / a) + extra;
        }, &s_aspect);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus;
    // Souris capturée → tout le mouvement va au ST (curseur verrouillé) : on FIGE la
    // fenêtre (pas de glissé). Une fois libérée (DEL), elle redevient déplaçable.
    if (captured) flags |= ImGuiWindowFlags_NoMove;
    ImGui::Begin("Atari ST Screen", nullptr, flags);
    ImGui::TextDisabled(captured ? "Souris capturée — Suppr (DEL) pour la libérer"
                                 : "Clic dans l'écran pour capturer la souris (curseur GEM)");
    // On AJUSTE l'image à la zone dispo en gardant le ratio ST (letterbox centré) :
    // la fenêtre est libre, l'image suit sans jamais se déformer.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float dw = avail.x, dh = dw / aspect;
    if (dh > avail.y) { dh = avail.y; dw = dh * aspect; }   // limité par la hauteur
    dw = std::max(1.f, dw); dh = std::max(1.f, dh);
    // Centre l'image dans la zone dispo (bandes égales si la fenêtre n'a pas le ratio).
    const ImVec2 cur = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cur.x + (avail.x - dw) * 0.5f,
                               cur.y + (avail.y - dh) * 0.5f));
    // Passe CRT (ou texture brute si off/indispo), rendue à la taille affichée (arrondie).
    const int dstW = (int)std::lround(dw), dstH = (int)std::lround(dh);
    const ImTextureID id = (ImTextureID)(intptr_t)crtApply(s, dstW, dstH);
    ImGui::Image(id, ImVec2((float)dstW, (float)dstH));
    if (!captured && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        reqCapture = true;
    ImGui::End();
}

// Bibliothèque de disquettes : liste les images montables du dossier disks/,
// monte/éjecte sur le lecteur A. reqMount = chemin à monter (sinon vide),
// reqEject = éjection.
void drawDiskLibrary(const std::string& disksDir, const std::string& mounted,
                     std::string& reqMount, bool& reqEject) {
    ImGui::Begin("Disk Library");
    const std::string curName = mounted.empty() ? "(vide)"
                                                 : fs::path(mounted).filename().string();
    // Bouton Éjecter COMPLÈTEMENT À GAUCHE, puis le nom du disque monté à sa droite.
    if (!mounted.empty()) {
        if (IconButton(ICON_FA_EJECT, "Éjecter")) reqEject = true;
        ImGui::SameLine();
    }
    ImGui::Text("Lecteur A : %s", curName.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Images dans %s/", disksDir.c_str());

    std::error_code ec;
    if (fs::is_directory(disksDir, ec)) {
        const fs::path base(disksDir);
        const std::string mountedName = mounted.empty() ? "" : fs::path(mounted).filename().string();
        // Récolte RÉCURSIVE des images .st/.msa/.dim/.stx, triées par ordre alphabétique de
        // DOSSIER puis de FICHIER (insensible à la casse) sur le chemin relatif à disks/.
        std::vector<fs::path> images;
        for (const auto& e : fs::recursive_directory_iterator(base, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx")
                images.push_back(e.path());
        }
        auto sortKey = [&](const fs::path& p) {
            std::string rel = fs::relative(p, base, ec).generic_string();   // "sous-dossier/fichier"
            for (auto& ch : rel) ch = (char)std::tolower((unsigned char)ch);
            return rel;
        };
        std::sort(images.begin(), images.end(),
                  [&](const fs::path& a, const fs::path& b) { return sortKey(a) < sortKey(b); });

        for (const auto& p : images) {
            const std::string rel = fs::relative(p, base, ec).generic_string();  // affiché (montre le dossier)
            ImGui::PushID(p.string().c_str());
            if (!mountedName.empty() && p.filename().string() == mountedName) {
                ImGui::TextDisabled("●");                  // montée
            } else if (ImGui::SmallButton("Monter")) {
                reqMount = p.string();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(rel.c_str());
            ImGui::PopID();
        }
    } else {
        ImGui::TextDisabled("(dossier disks/ introuvable)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Monter puis Reset pour démarrer une disquette amorçable.");
    ImGui::End();
}

// Bibliothèque de cartouches : liste les images du dossier carts/ et branche le
// port $FA0000. Un reset reste nécessaire pour que le TOS relise le magic de boot.
void drawCartLibrary(const std::string& cartsDir, const std::string& mounted,
                     std::string& reqMount, bool& reqEject) {
    ImGui::Begin("Cart Library");
    const std::string curName = mounted.empty() ? "(vide)"
                                                 : fs::path(mounted).filename().string();
    ImGui::Text("Port cartouche : %s", curName.c_str());
    if (!mounted.empty()) {
        ImGui::SameLine();
        if (IconButton(ICON_FA_EJECT, "Éjecter")) reqEject = true;
    }
    ImGui::Separator();
    ImGui::TextDisabled("Images dans %s/", cartsDir.c_str());

    std::error_code ec;
    if (fs::is_directory(cartsDir, ec)) {
        const std::string mountedName = mounted.empty() ? "" : fs::path(mounted).filename().string();
        for (const auto& e : fs::directory_iterator(cartsDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            if (ext != ".bin" && ext != ".img" && ext != ".rom") continue;
            const std::string name = e.path().filename().string();
            ImGui::PushID(name.c_str());
            if (name == mountedName) {
                ImGui::TextDisabled("●");                  // branchée
            } else if (ImGui::SmallButton("Brancher")) {
                reqMount = e.path().string();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
            ImGui::PopID();
        }
    } else {
        ImGui::TextDisabled("(dossier carts/ introuvable)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Brancher/éjecter relance la machine pour re-détecter la cartouche.");
    ImGui::End();
}
#endif // NEOST_WITH_IMGUI
} // namespace

int main(int argc, char** argv) {
    // Répertoire de l'exécutable (pour retrouver roms/ et disk/ depuis build/).
    const std::string exeDir = [&] {
        const std::string a0 = argv[0] ? argv[0] : "";
        const auto i = a0.find_last_of('/');
        return (i == std::string::npos) ? std::string(".") : a0.substr(0, i);
    }();
    // Préférences mémorisées (dernier ROM + type de moniteur).
    Config cfg = loadConfig(exeDir);
    g_showDisk = cfg.showDisk; g_showCart = cfg.showCart; g_showHex = cfg.showHex;
    g_showCpu  = cfg.showCpu;  g_showJoy  = cfg.showJoy;
    g_crtOn    = cfg.crt;      g_crtParams = cfg.crtParams;   // effets CRT (figés en kiosk)
    g_kioskRomDirs = cfg.romDirs;   // dossiers ROM additionnels du menu kiosk (persistés)
    const std::string defRom = cfg.rom.empty() ? std::string("roms/etos192us.img") : cfg.rom;
    // Ligne de commande : arguments POSITIONNELS (ROM, disque) + DRAPEAUX.
    //   --kiosk            : borne plein écran « borderless-windowed » (cf. g_kiosk).
    //   --kiosk-exclusive  : vrai plein écran EXCLUSIF (reste au-dessus de tout, ne
    //                        peut pas être recouvert par une autre fenêtre) ; implique --kiosk.
    //   --kiosk-monitor N  : moniteur cible (0 = principal ; défaut 0).
    int  kioskMonitor = 0;
    bool kioskExclusive = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i] ? argv[i] : "";
        if      (a == "--kiosk")           g_kiosk = true;
        else if (a == "--kiosk-exclusive") { g_kiosk = true; kioskExclusive = true; }
        else if (a == "--kiosk-monitor" && i + 1 < argc) kioskMonitor = std::atoi(argv[++i]);
        //   --crt              : active les effets CRT (façade moniteur).
        //   --crt-preset NAME  : preset (off|leger|arcade|phosphor) ; implique --crt.
        else if (a == "--crt")             g_crtOn = true;
        else if (a == "--crt-preset" && i + 1 < argc) {
            const std::string name = argv[++i];
            if (!applyCrtPreset(name, g_crtParams, g_crtOn))
                std::fprintf(stderr, "[main] preset CRT inconnu : '%s' "
                             "(off|leger|arcade|phosphor)\n", name.c_str());
        }
        else if (!a.empty() && a[0] != '-') pos.push_back(a);
    }
    // Les overrides CLI priment sur le cfg (et resteront cohérents si le panneau
    // déclenche un save ultérieur en mode fenêtré).
    cfg.crt = g_crtOn; cfg.crtParams = g_crtParams;
    // Sans argument positionnel, ./neost recharge le dernier ROM (ou EmuTOS US).
    const std::string romLogical = !pos.empty() ? pos[0] : defRom;
    const std::string tosPath  = resolveData(romLogical, exeDir);
    const std::string defDisk  = cfg.disk.empty() ? std::string("disks/diskA.st") : cfg.disk;
    const std::string diskPath = resolveData(pos.size() > 1 ? pos[1] : defDisk, exeDir);
    const std::string cartPath = cfg.cart.empty() ? std::string() : resolveData(cfg.cart, exeDir);
    const std::string disksDir = resolveData("disks", exeDir);   // dossier pour la Disk Library
    const std::string cartsDir = resolveData("carts", exeDir);   // dossier pour la Cart Library
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
        GLFWmonitor* mon = (mons && kioskMonitor < nmon) ? mons[kioskMonitor]
                                                         : glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        if (kioskExclusive) {
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
            // « Borderless-windowed » : fenêtre SANS bordure à la taille du moniteur,
            // posée dessus, toujours au premier plan. Pas de changement de mode vidéo
            // (meilleur alt-tab / multi-écran) mais peut être masquée par un panneau
            // override-redirect d'un bureau type GNOME Shell → préférer --kiosk-exclusive.
            int mx = 0, my = 0; glfwGetMonitorPos(mon, &mx, &my);
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
            glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
            window = glfwCreateWindow(vm ? vm->width : 1280, vm ? vm->height : 860,
                                      "NeoST", nullptr, nullptr);
            if (window) glfwSetWindowPos(window, mx, my);
        }
    } else {
        window = glfwCreateWindow(1280, 860, "NeoST — Atari ST", nullptr, nullptr);
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
    std::fprintf(stderr, "[main] cœur CPU : %s | machine : %s | RAM : %s\n",
                 Cpu68k::coreName(machine.cpu.core()),
                 machineName(machType0), cfg.mem.c_str());
    if (!machine.loadTos(tosPath))
        std::fprintf(stderr, "[main] Démarrage sans TOS (le CPU tournera à vide).\n");
    updateKbdCountry(machine.bus.rom);    // pays du TOS → surcharges keymap (FR/DE/UK…)
    if (!machine.loadDisk(diskPath))
        std::fprintf(stderr, "[main] Aucune disquette montée (%s).\n", diskPath.c_str());
    if (!cartPath.empty() && !machine.loadCart(cartPath))
        std::fprintf(stderr, "[main] Aucune cartouche montée (%s).\n", cartPath.c_str());
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
            std::fprintf(stderr, "[main] cartouche ignorée : incompatible avec GEMDOS HD\n");
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
    bool driveSoundOn = drive.init(resolveData("roms/drivesound/epson_smd480l", exeDir), 48000);
    if (driveSoundOn)
        machine.fdc.setSoundSink([&drive](FdcSound e) { drive.onEvent(e); });
    Audio audio(machine.psg, driveSoundOn ? &drive : nullptr, &machine.dmasnd);
    audio.start();   // échec silencieux possible (CI / pas de carte son)
    audio.setMasterVolume(cfg.volume);   // volume maître mémorisé (menu Son, neost.cfg)
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
        machine.loadTos(romP);
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
        std::fprintf(stderr, "[main] reconfig à chaud : cœur %s | machine %s | RAM %s\n",
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
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);

#if defined(NEOST_WITH_IMGUI)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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
            std::fprintf(stderr, "[main] police %s introuvable — police ImGui par défaut.\n",
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
            std::fprintf(stderr, "[main] police d'icônes %s introuvable — pas de pictogrammes.\n",
                         faPath.c_str());
        }
    }
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
#endif

    std::printf("[main] Clic dans l'écran : capture souris | Suppr (DEL) : libère | "
                "bouton Reset dans la fenêtre CPU | fermer la fenêtre : quitter\n");
    std::printf("[main] Joystick : manette USB auto (port 1) | F11 = émulation "
                "clavier (flèches + Ctrl droit) | menu « Joystick »\n");

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
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();                      // les transitions de boutons → onMouseButton

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
                if (g_dbgMouse) std::fprintf(stderr, "[souris] mvt dx=%d dy=%d L=%d R=%d\n", dx, dy, l, r);
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
                g_kioskAdaptive = !g_kioskAdaptive;
                std::fprintf(stderr, "[kiosk] zoom adaptatif %s\n", g_kioskAdaptive ? "ON" : "OFF");
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
                std::fprintf(stderr, "[joystick] émulation clavier %s (port %d)\n",
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
            stjoy::compose(window, kbd, g_kbdJoyPort, g_joyDeadzone, joy0, joy1);
            // Overlay kiosk ouvert : la manette pilote l'overlay → on n'envoie
            // rien au ST (sinon le jeu bougerait pendant la navigation).
            if (g_kioskDiskMenu) { joy0 = 0; joy1 = 0; }
            machine.ikbd.setJoystick(joy0, joy1);
            machine.bus.stePads.setJoystick(joy0, joy1);   // joypads STE ($FF9200/02) — même état
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
        screen.update(machine.shifter.pixels(), machine.shifter.width(), machine.shifter.height());

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        if (g_kiosk) glClearColor(0.f, 0.f, 0.f, 1.f);   // kiosk : barres noires
        else         glClearColor(0.10f, 0.10f, 0.12f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Zoom kiosk : DEUX cadrages francs, jamais au pixel (→ zéro saccade).
        //  · Défaut (99 % des jeux) : cadre FIXE sur la ZONE ACTIVE (rectangle net
        //    donné par le matériel — activeTop/activeHeight), qui ne bouge JAMAIS.
        //    Un champ d'étoiles, un fond noir : rien ne fait « respirer » le zoom.
        //  · Overscan (démos, ouvertures de bordures — Enchanted Land, Lethal Xcess) :
        //    quand la Glue signale une bordure retirée, on montre le BUFFER ENTIER.
        //    Hystérésis (latch) pour ne pas basculer sur un retrait d'une seule trame.
        // F10 (g_kioskAdaptive OFF) force le cadre complet en permanence.
        int kTop = 0, kH = machine.shifter.height();
        if (g_kiosk && g_kioskAdaptive) {
            static int overscanLatch = 0;
            if (machine.shifter.bordersOpen()) overscanLatch = 30;   // ~0,6 s de maintien
            else if (overscanLatch > 0)        --overscanLatch;
            if (overscanLatch == 0) {           // zone active fixe (cas normal)
                kTop = machine.shifter.activeTop();
                kH   = machine.shifter.activeHeight();
            }                                    // sinon : kTop=0, kH=hauteur (buffer entier)
        }

        bool reqReset = false, reqHardReset = false, reqRebuild = false, reqCapture = false;
        int  reqMonitor = -1;
#if defined(NEOST_WITH_IMGUI)
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        std::string reqMount; bool reqEject = false;
        std::string reqMountCart; bool reqEjectCart = false;
        std::string reqMountGemdos, reqMountAcsi; bool reqEjectGemdos = false, reqEjectAcsi = false;
        if (!g_kiosk) {                          // KIOSK : aucun chrome ImGui (menu/toolbar/fenêtres)
        const bool color = machine.mfp.colorMonitor();

        // --- Menu (haut) -----------------------------------------------------
        float menuH = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            menuH = ImGui::GetWindowSize().y;
            if (ImGui::BeginMenu(ICON_FA_MICROCHIP " Machine")) {
                if (ImGui::MenuItem(ICON_FA_REDO " Reset"))      reqReset = true;
                if (ImGui::MenuItem(ICON_FA_POWER_OFF " Hard Reset")) reqHardReset = true;
                // Modèle / RAM / cœur / ROM : appliqués À CHAUD (hard reset avec les
                // nouveaux paramètres) — aucun redémarrage de l'appli. Mémorisés dans
                // neost.cfg. `reqRebuild` déclenche la reconfiguration en fin de boucle.
                if (ImGui::BeginMenu(ICON_FA_SERVER " Modèle")) {
                    const char* const ids[]   = { "st", "megast", "ste", "megaste" };
                    const char* const labels[] = { "ST", "Mega ST", "STE", "Mega STE" };
                    for (int i = 0; i < 4; ++i)
                        if (ImGui::MenuItem(labels[i], nullptr, cfg.machine == ids[i])) {
                            cfg.machine = ids[i];
                            // Mega STE : charge auto un TOS ≥ 2.06 si le ROM courant ne
                            // le gère pas → un VRAI Mega STE (au lieu de basculer en STE).
                            std::string autoRom = pickTosForMachine(cfg.machine, cfg.rom, exeDir, romsDir);
                            if (!autoRom.empty()) cfg.rom = autoRom;
                            saveConfig(exeDir, cfg, &machine); reqRebuild = true;
                        }
                    // Socket MC68881 du Mega STE ($FFFA40, émulation fonctionnelle —
                    // cf. Fpu.hpp). Décoché (défaut) : bus error → « FPU not found ».
                    if (cfg.machine == "megaste") {
                        ImGui::Separator();
                        if (ImGui::MenuItem("FPU 68881", nullptr, cfg.fpu)) {
                            cfg.fpu = !cfg.fpu; saveConfig(exeDir, cfg, &machine); reqRebuild = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(ICON_FA_MEMORY " Mémoire")) {
                    const char* const mids[]   = { "256k", "512k", "1m", "2m", "4m" };
                    const char* const mlabels[] = { "256 Ko", "512 Ko", "1 Mo", "2 Mo", "4 Mo" };
                    for (int i = 0; i < 5; ++i)
                        if (ImGui::MenuItem(mlabels[i], nullptr, cfg.mem == mids[i])) {
                            cfg.mem = mids[i]; saveConfig(exeDir, cfg, &machine); reqRebuild = true;
                        }
                    ImGui::EndMenu();
                }
                // Image TOS/EmuTOS (.img/.rom du dossier roms/), chargée à chaud.
                if (ImGui::BeginMenu(ICON_FA_SAVE " ROM")) {
                    std::error_code ec;
                    const std::string curRom = fs::path(cfg.rom).filename().string();
                    if (fs::is_directory(romsDir, ec)) {
                        std::vector<fs::path> roms;
                        for (const auto& e : fs::directory_iterator(romsDir, ec)) {
                            if (!e.is_regular_file()) continue;
                            std::string ext = e.path().extension().string();
                            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                            if (ext != ".img" && ext != ".rom") continue;
                            roms.push_back(e.path());
                        }
                        auto romSortKey = [](const fs::path& p) {
                            std::string name = p.filename().string();
                            for (auto& ch : name) ch = (char)std::tolower((unsigned char)ch);
                            return name;
                        };
                        std::sort(roms.begin(), roms.end(),
                                  [&](const fs::path& a, const fs::path& b) {
                                      return romSortKey(a) < romSortKey(b);
                                  });
                        for (const auto& p : roms) {
                            const std::string name = p.filename().string();
                            if (ImGui::MenuItem(name.c_str(), nullptr, name == curRom)) {
                                cfg.rom = p.string(); saveConfig(exeDir, cfg, &machine); reqRebuild = true;
                            }
                        }
                    } else {
                        ImGui::TextDisabled("(dossier roms/ introuvable)");
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(ICON_FA_COMPACT_DISC " Cartouche")) {
                    if (machine.bus.mountedCartPath().empty()) {
                        ImGui::TextDisabled("(aucune)");
                    } else if (ImGui::MenuItem(ICON_FA_EJECT " Éjecter")) {
                        reqEjectCart = true;
                    }
                    std::error_code ec;
                    const std::string curCart = fs::path(machine.bus.mountedCartPath()).filename().string();
                    if (fs::is_directory(cartsDir, ec)) {
                        for (const auto& e : fs::directory_iterator(cartsDir, ec)) {
                            if (!e.is_regular_file()) continue;
                            std::string ext = e.path().extension().string();
                            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                            if (ext != ".bin" && ext != ".img" && ext != ".rom") continue;
                            const std::string name = e.path().filename().string();
                            if (ImGui::MenuItem(name.c_str(), nullptr, name == curCart))
                                reqMountCart = e.path().string();
                        }
                    } else {
                        ImGui::TextDisabled("(dossier carts/ introuvable)");
                    }
                    ImGui::EndMenu();
                }
                // Disque dur : HD GEMDOS (dossier hôte → C:) et image ACSI (cible 0).
                // Montage/éjection à chaud suivi d'un hard reset (le TOS ne sonde les
                // disques qu'au boot). Chemins mémorisés dans neost.cfg.
                if (ImGui::BeginMenu(ICON_FA_HDD " Disque dur")) {
                    ImGui::TextDisabled(ICON_FA_FOLDER_OPEN " GEMDOS : dossier hôte monté en C:");
                    if (!g_gdInit) { std::snprintf(g_gdBuf, sizeof g_gdBuf, "%s", cfg.gemdos.c_str()); g_gdInit = true; }
                    ImGui::SetNextItemWidth(280);
                    ImGui::InputText("##gemdosDir", g_gdBuf, sizeof g_gdBuf);
                    ImGui::SameLine();
                    if (machine.gemdos.active()) {
                        if (ImGui::Button(ICON_FA_EJECT " Éjecter##gd")) reqEjectGemdos = true;
                        ImGui::TextDisabled("(actif — exclusif avec la cartouche)");
                    } else if (ImGui::Button("Monter##gd") && g_gdBuf[0]) {
                        reqMountGemdos = g_gdBuf;
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled(ICON_FA_HDD " ACSI : image disque dur (cible 0)");
                    if (!g_hdInit) { std::snprintf(g_hdBuf, sizeof g_hdBuf, "%s", cfg.acsi.c_str()); g_hdInit = true; }
                    ImGui::SetNextItemWidth(280);
                    ImGui::InputText("##acsiImg", g_hdBuf, sizeof g_hdBuf);
                    ImGui::SameLine();
                    if (machine.fdc.acsiActive()) {
                        if (ImGui::Button(ICON_FA_EJECT " Éjecter##hd")) reqEjectAcsi = true;
                        ImGui::TextDisabled("(%d partition(s) détectée(s))",
                                            machine.fdc.acsiPartitionCount());
                    } else if (ImGui::Button("Monter##hd") && g_hdBuf[0]) {
                        reqMountAcsi = g_hdBuf;
                    }
                    // GEMDOS et ACSI montés ensemble : NeoST ne décale pas le lecteur
                    // GEMDOS derrière les partitions ACSI (contrairement à Hatari) →
                    // les deux revendiquent C:.
                    if (machine.gemdos.active() && machine.fdc.acsiActive())
                        ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f),
                                           "GEMDOS et ACSI revendiquent C: tous les deux !");
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                // FDC rapide (équivalent hatari --fastfdc) : accès disque ÷10. Prend effet
                // immédiatement (pas de reset), mémorisé dans neost.cfg.
                if (ImGui::MenuItem(ICON_FA_BOLT " FDC rapide (accès disque ÷10)", nullptr, cfg.fastfdc)) {
                    cfg.fastfdc = !cfg.fastfdc;
                    machine.fdc.setFastFdc(cfg.fastfdc);
                    saveConfig(exeDir, cfg, &machine);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_SIGN_OUT_ALT " Quitter")) glfwSetWindowShouldClose(window, 1);
                ImGui::EndMenu();
            }
            // Profil : préréglages machine complets (modèle + RAM + ROM + disque dur),
            // appliqués à chaud comme un changement de modèle. Les profils STE et Mega STE
            // montent le HD GEMDOS (dossier gemdos/ → C:) ; le profil 520 ST démonte tout
            // disque dur (GEMDOS et ACSI).
            if (ImGui::BeginMenu(ICON_FA_STAR " Profil")) {
                struct Profil { const char* label; const char* machine; const char* mem;
                                const char* rom; const char* gemdos; };
                static const Profil profils[] = {
                    { "520 ST (512 Ko, TOS 1.02 US)",             "st",      "512k", "roms/tos102us.img", ""       },
                    { "1040 STE (1 Mo, TOS 1.62 FR, disque dur)", "ste",     "1m",   "roms/tos162fr.img", "gemdos" },
                    { "Mega STE (4 Mo, TOS 2.06 FR, disque dur)", "megaste", "4m",   "roms/tos206fr.img", "gemdos" },
                };
                for (const auto& p : profils) {
                    // Coché si la config courante correspond au profil (ROM comparée
                    // par nom de fichier : cfg.rom peut être un chemin résolu).
                    const bool cur = cfg.machine == p.machine && cfg.mem == p.mem
                                  && fs::path(cfg.rom).filename() == fs::path(p.rom).filename()
                                  && cfg.gemdos == p.gemdos && cfg.acsi.empty();
                    if (ImGui::MenuItem(p.label, nullptr, cur)) {
                        cfg.machine = p.machine; cfg.mem = p.mem; cfg.rom = p.rom;
                        cfg.gemdos = p.gemdos; cfg.acsi.clear();
                        if (!cfg.gemdos.empty()) cfg.cart.clear();   // HD GEMDOS exclusif avec la cartouche
                        g_gdInit = g_hdInit = false;   // resynchronise les champs du menu Disque dur
                        saveConfig(exeDir, cfg, &machine); reqRebuild = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_DESKTOP " Résolution")) {
                if (ImGui::MenuItem(ICON_FA_PALETTE " Couleur (basse rés)", nullptr,  color)) reqMonitor = 1;
                if (ImGui::MenuItem(ICON_FA_ADJUST " Mono (haute rés)",    nullptr, !color)) reqMonitor = 0;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_GAMEPAD " Joystick")) {
                // Émulation au clavier (flèches + Ctrl droit). F11 bascule aussi.
                // Réglage de SESSION : jamais persisté (redémarre toujours OFF).
                ImGui::MenuItem(ICON_FA_KEYBOARD " Émulation clavier (flèches + Ctrl droit)", "F11", &g_kbdJoy);
                if (ImGui::BeginMenu(ICON_FA_GAMEPAD " Port émulé au clavier")) {
                    if (ImGui::MenuItem("Port 1 (jeux)", nullptr, g_kbdJoyPort == 1)) {
                        g_kbdJoyPort = cfg.joyport = 1; saveConfig(exeDir, cfg, &machine);
                    }
                    if (ImGui::MenuItem("Port 0 (souris)", nullptr, g_kbdJoyPort == 0)) {
                        g_kbdJoyPort = cfg.joyport = 0; saveConfig(exeDir, cfg, &machine);
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                // Zone morte centrale des sticks analogiques (anti-drift). Le D-pad
                // numérique n'est pas concerné. Mémorisée à la validation du slider.
                ImGui::TextDisabled("Zone morte (sticks)");
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::SliderFloat("##deadzone", &g_joyDeadzone, 0.0f, 0.95f, "%.2f")) {
                    if (g_joyDeadzone < 0.0f) g_joyDeadzone = 0.0f;
                    if (g_joyDeadzone > 0.95f) g_joyDeadzone = 0.95f;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    cfg.joydeadzone = g_joyDeadzone; saveConfig(exeDir, cfg, &machine);
                }
                ImGui::Separator();
                // Manettes USB détectées (la 1re → port 1, la 2e → port 0).
                ImGui::TextDisabled("Manettes USB détectées :");
                int nPad = 0;
                for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                    if (!glfwJoystickPresent(jid)) continue;
                    const char* nm = glfwGetGamepadName(jid);
                    if (!nm) nm = glfwGetJoystickName(jid);
                    ImGui::BulletText("Port %d : %s", (nPad == 0) ? 1 : 0, nm ? nm : "?");
                    ++nPad;
                }
                if (nPad == 0) ImGui::BulletText("(aucune)");
                ImGui::EndMenu();
            }
            // --- Volume maître (sortie hôte, indépendant du LMC1992 émulé) : icône
            // haut-parleur selon le niveau, slider 0-100 % + Muet (qui mémorise le
            // niveau et le restaure). Persisté dans neost.cfg (volume=) en fin de
            // glissé — pas à chaque frame de drag.
            {
                static float volBeforeMute = 1.0f;         // niveau restauré au dé-mute
                const float  vol   = audio.masterVolume();
                const bool   muted = vol <= 0.0f;
                const char*  vicon = muted      ? ICON_FA_VOLUME_MUTE
                                   : vol < 0.5f ? ICON_FA_VOLUME_DOWN : ICON_FA_VOLUME_UP;
                char vlabel[48];
                std::snprintf(vlabel, sizeof vlabel, "%s Son###menuSon", vicon);
                if (ImGui::BeginMenu(vlabel)) {
                    int pct = int(vol * 100.0f + 0.5f);
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::SliderInt("##volume", &pct, 0, 100, "%d %%"))
                        audio.setMasterVolume(float(pct) / 100.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {   // fin de glissé → persiste
                        cfg.volume = audio.masterVolume();
                        saveConfig(exeDir, cfg, &machine);
                    }
                    if (ImGui::MenuItem(ICON_FA_VOLUME_MUTE " Muet", nullptr, muted)) {
                        if (muted) {
                            audio.setMasterVolume(volBeforeMute > 0.0f ? volBeforeMute : 1.0f);
                        } else {
                            volBeforeMute = vol;
                            audio.setMasterVolume(0.0f);
                        }
                        cfg.volume = audio.masterVolume();
                        saveConfig(exeDir, cfg, &machine);
                    }
                    ImGui::EndMenu();
                }
            }
            if (ImGui::BeginMenu(ICON_FA_CLONE " Fenêtres")) {
                ImGui::MenuItem(ICON_FA_SAVE " Disk Library",  nullptr, &g_showDisk);
                ImGui::MenuItem(ICON_FA_COMPACT_DISC " Cart Library",  nullptr, &g_showCart);
                ImGui::MenuItem(ICON_FA_MEMORY " Mémoire (hex)", nullptr, &g_showHex);
                ImGui::MenuItem(ICON_FA_MICROCHIP " CPU 68000",     nullptr, &g_showCpu);
                ImGui::MenuItem(ICON_FA_GAMEPAD " Joystick",      nullptr, &g_showJoy);
                ImGui::MenuItem(ICON_FA_BUG " Débogueur",         nullptr, &g_showDbg);
                ImGui::MenuItem(ICON_FA_DESKTOP " Effets CRT",     nullptr, &g_showCrt);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // --- Barre de boutons (sous le menu) ---------------------------------
        ImGui::SetNextWindowPos(ImVec2(0.0f, menuH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##toolbar", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::Checkbox("Disk", &g_showDisk);  ImGui::SameLine();
        ImGui::Checkbox("Cart", &g_showCart);  ImGui::SameLine();
        ImGui::Checkbox("Hex",  &g_showHex);   ImGui::SameLine();
        ImGui::Checkbox("CPU",  &g_showCpu);   ImGui::SameLine();
        ImGui::Checkbox("Joy",  &g_showJoy);
        if (drive.ok()) {
            ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
            if (ImGui::Checkbox("Son lecteur", &driveSoundOn)) drive.setEnabled(driveSoundOn);
        }
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        if (IconButton(color ? ICON_FA_ADJUST : ICON_FA_PALETTE, color ? "Passer en Mono" : "Passer en Couleur"))
            reqMonitor = color ? 0 : 1;
        ImGui::SameLine();
        if (IconButton(ICON_FA_REDO, "Reset")) reqReset = true;
        ImGui::SameLine();
        // Reset à froid : efface la ST-RAM → EmuTOS/TOS refait un boot complet.
        if (IconButton(ICON_FA_POWER_OFF, "Hard Reset")) reqHardReset = true;
        const float toolH = ImGui::GetWindowSize().y;
        ImGui::End();

        // --- Fenêtre écran (base) + fenêtres masquables ----------------------
        drawStScreen(screen, g_mouseCaptured, reqCapture, menuH + toolH);
        if (g_showDisk) drawDiskLibrary(disksDir, machine.fdc.mountedPath(), reqMount, reqEject);
        if (g_showCart) drawCartLibrary(cartsDir, machine.bus.mountedCartPath(), reqMountCart, reqEjectCart);
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

        // --- Kiosk : menu in-game (START manette ou F9), jeu en PAUSE ------------
        // Modèle « vraie machine » : (A) INSÈRE la disquette choisie SANS jamais
        // rebooter (le jeu en cours continue) ; (X) REDÉMARRE la machine (bouton
        // reset) → reboot sur la disquette insérée ; (Y) quitte avec confirmation.
        if (g_kiosk) {
            auto padBtn = [&](int b) {
                for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
                    GLFWgamepadstate gs;
                    if (glfwJoystickPresent(j) && glfwGetGamepadState(j, &gs) && gs.buttons[b])
                        return true;
                }
                return false;
            };
            auto padAxisY = [&]() {
                for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
                    GLFWgamepadstate gs;
                    if (glfwJoystickPresent(j) && glfwGetGamepadState(j, &gs))
                        return gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
                }
                return 0.0f;
            };
            auto padAxisX = [&]() {
                for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
                    GLFWgamepadstate gs;
                    if (glfwJoystickPresent(j) && glfwGetGamepadState(j, &gs))
                        return gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
                }
                return 0.0f;
            };
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
                                glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS;
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
                            if (std::find(g_kioskRomDirs.begin(), g_kioskRomDirs.end(), g_browseDir)
                                    == g_kioskRomDirs.end())
                                g_kioskRomDirs.push_back(g_browseDir);
                            cfg.romDirs = g_kioskRomDirs;
                            saveConfig(exeDir, cfg, &machine, true);   // persiste MÊME en kiosk
                            kioskScanDisks(disksDir, machine.fdc.mountedPath());
                            g_kioskDiskSel = 0; g_romDirSel = 0;
                            g_kioskPage = KIOSK_PAGE_ROMDIRS;          // retour au gestionnaire
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
                            g_kioskActSel = (g_kioskActSel % 4 + 4) % 4;
                        }
                    }
                    // FEU (A/Entrée) : déclenche l'item surligné du menu focalisé.
                    if (okNow && !pOk) {
                        if (g_kioskZone == KIOSK_ZONE_LIST) {
                            if (nd > 0) reqMount = g_kioskDisks[g_kioskDiskSel];  // INSÉRER à chaud
                        } else {
                            switch (g_kioskActSel) {
                                case 0: reqHardReset = true;              // Redémarrer
                                        g_kioskDiskMenu = false; break;
                                case 1: g_kioskPage = KIOSK_PAGE_KEYS;    // Clavier & souris
                                        g_kioskKeySel = 0; break;
                                case 2:                                   // Dossiers ROM (gestion)
                                    if (kioskPruneRomDirs()) {           // retire les disparus + persiste
                                        cfg.romDirs = g_kioskRomDirs;
                                        saveConfig(exeDir, cfg, &machine, true);
                                        kioskScanDisks(disksDir, machine.fdc.mountedPath());
                                    }
                                    g_romDirSel = 0;
                                    g_kioskPage = KIOSK_PAGE_ROMDIRS; break;
                                case 3: g_kioskPage = KIOSK_PAGE_QUIT; break;  // Quitter
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

        ImGui::Render();
        if (g_kiosk) drawStKiosk(screen, fbw, fbh, kTop, kH);   // rendu adaptatif (ImGui vide au-dessus)
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        // Disk Library : montage / éjection à chaud du lecteur A.
        if (!reqMount.empty()) {
            machine.fdc.loadImage(reqMount);
            cfg.disk = reqMount; saveConfig(exeDir, cfg, &machine);
        }
        if (reqEject) {
            machine.fdc.eject();
            cfg.disk.clear(); saveConfig(exeDir, cfg, &machine);
        }
        // Cart Library : branchement / éjection à chaud du port cartouche.
        if (!reqMountCart.empty()) {
            if (machine.gemdos.active()) {  // $FA0000 occupé par la cartouche système GEMDOS
                machine.gemdos.unmount();
                cfg.gemdos.clear();
            }
            if (machine.loadCart(reqMountCart)) {
                cfg.cart = reqMountCart; saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;       // le TOS sonde le port cartouche au boot
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
            if (!machine.bus.mountedCartPath().empty()) {   // exclusif avec la cartouche
                machine.ejectCart();
                cfg.cart.clear();
            }
            if (machine.gemdos.setDirectory(resolvePath(reqMountGemdos))) {
                cfg.gemdos = reqMountGemdos; saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;
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
    cfg.showDisk = g_showDisk; cfg.showCart = g_showCart; cfg.showHex = g_showHex;
    cfg.showCpu  = g_showCpu;  cfg.showJoy  = g_showJoy;
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
