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
#include "net/NetBackend.hpp"
#ifdef NEOST_WITH_NET
#include "net/HayesModem.hpp"
#endif
#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "io/JoystickInput.hpp"
#include "io/MediaScan.hpp"
#include "io/DongleTable.hpp"
#include "core/Framing.hpp"
#include "util/HostPath.hpp"   // chemins hôte : UNE définition d'« absolu »
#include "gui/AppConfig.hpp"   // neost.cfg : structure, analyse, écriture, profils

namespace fs = std::filesystem;
// Configuration : extraite dans gui/AppConfig (logique pure, donc testable).
// Importée sans qualifier pour que les sites d'appel restent inchangés.
using namespace neost::appconfig;

// Résout un chemin de données indépendamment du répertoire courant : tel quel,
// puis relatif au répertoire de l'exécutable (utile quand on lance depuis build/).
// std::filesystem et non stat() : <sys/stat.h> n'existe pas partout, et la
// surcharge à error_code ne LANCE jamais (un chemin illisible = « absent »).
static bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec;
}
// Un chemin ABSOLU ne se combine avec rien : le préfixer d'un dossier de base est
// exactement ce qui produisait « C:\…\NeoST\C:\Temp\atari » sous Windows (issue #37).
// La règle « absolu » vit dans util/HostPath, seule et testée (tests/selftest_logic.cpp).
static std::string resolveData(const std::string& given, const std::string& exeDir) {
    if (neost::hostpath::isAbsolute(given)) return given;
    const std::string cands[] = { given,
                                  neost::hostpath::join(exeDir, given),
                                  neost::hostpath::join(exeDir + "/..", given),
                                  neost::hostpath::join("..", given) };
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
static bool g_saveStateReq = false;    // F5 latché dans onKey (cf. F8)
static bool g_loadStateReq = false;    // F7 latché dans onKey
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
// jeux) et EXTÉRIEUR (Redémarrer / Clavier & souris / Joysticks / Dossiers ROM /
// Mode bureau / Quitter). g_kioskZone = quel menu a le focus ; le FEU valide l'item
// surligné du menu focalisé.
enum { KIOSK_ZONE_LIST = 0, KIOSK_ZONE_ACTIONS = 1 };
static int  g_kioskZone   = KIOSK_ZONE_LIST;
static int  g_kioskActSel = 0;                // index action (menu EXTÉRIEUR, 0..5)
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
#include "imgui.h"
#include "imgui_internal.h"   // gestionnaire de réglages personnalisé (ImGuiSettingsHandler)
#include "imgui_impl_glfw.h"
#include "gui/KeyboardWindow.hpp"
#include "audio/MidiOutHost.hpp"
#include "audio/Mt32Synth.hpp"
#include "imgui_impl_opengl2.h"
#include "gui/UiCommon.hpp"    // pictogrammes Font Awesome + IconButton
#include "gui/MediaPages.hpp"  // pages Disquettes / Cartouche / Disque dur / Réseau
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
bool  g_mouseCaptureToggleReq = false; // molette / Ctrl+Alt+G → bascule dans la boucle
bool  g_dbgMouse = false;              // NEOST_DEBUG_MOUSE=1 → trace les paquets souris
bool  g_dbgJoy = false;                // NEOST_DEBUG_JOY=1 → trace l'état brut des manettes
bool  g_kbdJoy = false;                // émulation joystick au clavier (flèches + Ctrl droit)
int   g_kbdJoyPort = 1;                // port ST visé par l'émulation clavier (0/1)
bool  g_port0Auto = false;             // port 0 : "auto" (2e manette y va seule) vs "mouse" (défaut)
bool  g_port0Joystick = false;         // calculé chaque trame : un joystick OCCUPE le port 0 → souris débranchée
// La souris hôte atteint-elle le ST ? Question posée par les DEUX chemins d'entrée
// souris — le mouvement (scruté par trame) et les boutons (callback événementiel) —
// justement parce que leur divergence est ce qui a laissé les clics traverser un
// port 0 occupé par un joystick. Deux raisons de couper :
//  · g_port0Joystick : un joystick occupe le port souris (comme sur un vrai ST) ;
//  · g_kioskDiskMenu : l'overlay borne a la main — le clavier (onKey) et la manette
//    sont déjà avalés au même titre, et le ST est GELÉ dessous (kioskPaused), donc
//    tout Δ envoyé s'empilerait dans l'accumulateur IKBD pour ressortir d'un bloc.
static bool mouseReachesSt() { return !g_port0Joystick && !g_kioskDiskMenu; }
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
bool  g_showKbd = false;               // fenêtre clavier virtuel (photo pic/, touches cliquables)
bool  g_showCfg = true;                // fenêtre Configuration
bool  g_showFloppy = true;             // fenêtre indépendante des disquettes
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

void onGlfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// --- Auto-plug (disks/dongles.txt) : mémoire de ce QU'ON a branché ------------------
// Le montage à chaud enchaîne les jeux (ludothèque, borne). L'auto-plug ne remplissait
// que les emplacements vides et ne retirait JAMAIS rien : la clé du jeu précédent
// restait en place. Une clé Leader Board oubliée sur le port 1 force HAUT+BAS en
// permanence (PortDevices::joyOverlay) — la manette du jeu suivant est cassée, et
// cfg.joy1 ayant été persisté, ça survit au redémarrage.
// On note donc ce que l'auto-plug a posé, pour le retirer au montage suivant. On ne
// retire QUE ce qu'on a posé ET qui n'a pas bougé depuis : la page Dongles reste
// souveraine.
static PortDevices::Device g_autoPortDev[int(PortDevices::Port::Count)] = {};
static CartridgeKey::Model g_autoCartKey = CartridgeKey::Model::None;

static void autoDongleRetract(Machine& machine, Config& cfg) {
    std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
    for (int p = 0; p < int(PortDevices::Port::Count); ++p) {
        const PortDevices::Device d = g_autoPortDev[p];
        if (d == PortDevices::Device::None) continue;
        g_autoPortDev[p] = PortDevices::Device::None;
        if (machine.ports.at(PortDevices::Port(p)) != d) continue;   // l'utilisateur l'a changé
        machine.plugPort(PortDevices::Port(p), PortDevices::Device::None);
        slots[p]->clear();
    }
    if (g_autoCartKey != CartridgeKey::Model::None) {
        const CartridgeKey::Model k = g_autoCartKey;
        g_autoCartKey = CartridgeKey::Model::None;
        if (machine.dongle.model() == k) { machine.setDongle(CartridgeKey::Model::None); cfg.dongle.clear(); }
    }
}

// Callback bouton souris : ÉVÉNEMENTIEL (capte chaque transition, même un
// double-clic rapide qu'une scrutation par trame manquerait). Envoie un paquet
// IKBD sans mouvement portant l'état courant des boutons.
void onMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    // Le bouton central est un interrupteur hôte : il accroche/décroche la souris
    // sans envoyer de clic à l'Atari. La borne conserve son invariant de capture.
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS && !g_kiosk) g_mouseCaptureToggleReq = true;
        return;
    }
    if (!g_ikbd || !g_mouseCaptured || !mouseReachesSt()) return;
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

// Région de CONTENU (zoom adaptatif) : le calcul vit dans core/Framing.cpp,
// PARTAGÉ avec le plein écran WASM — même règle, mêmes latches d'hystérésis.
// Ici on ne garde que l'adaptation de signature (Machine& → Shifter&).
static void stContentRegion(Machine& machine, int& cTop, int& cH, int& cW) {
    neost::stContentRegion(machine.shifter, cTop, cH, cW);
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

// Callback clavier GLFW → IKBD. Les touches Atari, dont Suppr et Échap, sont
// transmises au ST (beaucoup de jeux/applications s'en servent).
void onKey(GLFWwindow*, int key, int scancode, int action, int mods) {
    if (!g_ikbd || action == GLFW_REPEAT) return;   // TOS gère sa propre répétition (pas l'IKBD)
    // Secours pour les trackpads et souris à deux boutons : même bascule que le clic
    // molette. Ctrl+Alt+G est réservé à l'hôte, mais G SEUL poursuit normalement
    // vers l'IKBD. Le latch absorbe aussi le BREAK du raccourci si Ctrl/Alt sont
    // relâchés avant G. En kiosk la capture reste imposée.
    static bool mouseCaptureChordHeld = false;
    if (key == GLFW_KEY_G) {
        if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
            mouseCaptureChordHeld = true;
            if (!g_kiosk) g_mouseCaptureToggleReq = true;
            return;
        }
        if (action == GLFW_RELEASE && mouseCaptureChordHeld) {
            mouseCaptureChordHeld = false;
            return;
        }
    }
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
    // Ctrl+Alt+F : même bascule bureau ⇄ borne que F8, sous forme de chord hôte
    // (même discipline que Ctrl+Alt+G : F SEUL continue vers l'IKBD, le latch
    // absorbe le BREAK du chord si Ctrl/Alt sont relâchés avant F).
    static bool kioskChordHeld = false;
    if (key == GLFW_KEY_F) {
        if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
            kioskChordHeld = true;
            g_kioskSwitchReq = g_kiosk ? 2 : 1;
            return;
        }
        if (action == GLFW_RELEASE && kioskChordHeld) {
            kioskChordHeld = false;
            return;
        }
    }
    // F5/F7 : latchés ICI comme F8 (même justification anti-scrutation — un appui
    // bref entre deux tours de boucle était perdu, l'utilisateur croyait l'état
    // sauvé sans qu'il le soit). Consommés à la frontière de trame.
    if (key == GLFW_KEY_F5) { if (action == GLFW_PRESS) g_saveStateReq = true; return; }
    if (key == GLFW_KEY_F7) { if (action == GLFW_PRESS) g_loadStateReq = true; return; }
    if (key == GLFW_KEY_F11) return;
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
    // Port affiché = affectation EFFECTIVE (joymap + AUTO), comme la page kiosque —
    // l'ordre d'énumération mentait dès qu'un rôle était épinglé (PORT 0/OFF).
    int8_t joyRoles[GLFW_JOYSTICK_LAST + 1];
    joyResolveRoles(joyRoles);
    int8_t joyAssign[GLFW_JOYSTICK_LAST + 1];
    stjoy::resolveAssign(joyRoles, joyAssign, g_port0Auto);
    int nPresent = 0;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickPresent(jid)) continue;
        ++nPresent;
        const char* nm = glfwGetJoystickName(jid);
        const int stPort = joyAssign[jid];
        ImGui::Text("Pad %d: %s", jid, nm ? nm : "?");
        if (stPort >= 0) { ImGui::SameLine(); ImGui::TextDisabled("→ ST port %d", stPort); }
        else             { ImGui::SameLine(); ImGui::TextDisabled("(off)"); }

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

// Recense les images montables sous disks/ (+ dossiers ROM additionnels) →
// g_kioskDisks, TRIÉES par proximité au disque courant. Le MODÈLE (scan borné,
// détection des suites, ordre de tri) vit dans io/MediaScan.cpp : il est partagé
// avec le frontend Android, dont le menu reprend cette ludothèque.
static void kioskScanDisks(const std::string& disksDir, const std::string& mounted) {
    std::vector<std::string> dirs{ disksDir };
    for (const auto& d : g_kioskRomDirs) dirs.push_back(d);
    g_kioskDisks = neost::scanDiskImages(dirs, mounted);
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
            const bool sibling = !mrefL.empty() && !cur && neost::areSiblingImages(lower(fn), mrefL);
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
        stjoy::resolveAssign(roles, assign, g_port0Auto);
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
    ImGui::DockBuilderDockWindow(ICON_FA_SAVE " Floppies",      right);
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
// Le clic sur la molette ou Ctrl+Alt+G accroche/décroche la souris.
//
// [cTop, cTop+cH) = région de CONTENU (cf. stContentRegion) : le bureau applique le
// MÊME zoom adaptatif que le kiosk, à ceci près qu'il le cadre en UV de l'image et
// non en viewport GL — les bordures inutilisées sortent du cadre au lieu d'ajouter
// des bandes noires. Zoom auto OFF → cTop=0, cH=hauteur du buffer (cadre entier).
void drawStScreen(const GlScreen& s, bool captured, float topOffset,
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
    // fenêtre (pas de glissé). Une fois libérée (molette/Ctrl+Alt+G), elle redevient déplaçable.
    if (captured) flags |= ImGuiWindowFlags_NoMove;
    ImGui::Begin("Atari ST Screen", nullptr, flags);
#ifdef IMGUI_HAS_DOCK
    s_docked = ImGui::IsWindowDocked();   // pour la trame SUIVANTE (cf. plus haut)
#endif
    ImGui::TextDisabled(captured ? "Mouse captured — middle-click or Ctrl+Alt+G to release"
                                 : "Middle-click or Ctrl+Alt+G to capture the mouse");
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
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Contenu des fenêtres de supports et pages de « Configuration ». Ce sont des
// FRAGMENTS (pas de Begin/End). Aucun ne monte ni ne démonte quoi que ce soit —
// elles remplissent des requêtes consommées en fin de trame, seul endroit qui sait
// enchaîner un reset et persister neost.cfg.
// ─────────────────────────────────────────────────────────────────────────────

// Page « Disquettes » : lecteurs A et B + la ludothèque de disks/.
// mountedA/mountedB = chemins montés (vides si lecteur vide).
// ─────────────────────────────────────────────────────────────────────────────
// Fenêtre « Configuration » — réglages de la machine.
//
// Elle remplace six sous-menus (Modèle, Mémoire, ROM, Cartouche, Disque dur,
// Résolution/Joystick/Son). Les disquettes, que l'on change couramment en jouant,
// vivent volontairement dans leur propre fenêtre indépendante.
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
    std::string curSd2;               // image du slot 2 de l'UltraSatan (résolue)

    // Réglages matériels EN ATTENTE (cf. « Appliquer et redémarrer »).
    std::string pendMachine, pendMem, pendRom;
    bool pendFpu  = false;
    bool pendInit = false;        // faux tant que les champs ci-dessus n'ont pas été semés

    // Requêtes sortantes (toutes consommées puis remises à zéro par l'appelant).
    std::string reqMountA, reqMountB, reqMountCart, reqMountGemdos, reqMountAcsi;
    bool reqEjectA = false, reqEjectB = false, reqEjectCart = false;
    bool reqEjectGemdos = false, reqEjectAcsi = false;
    int  reqModem = -1;                   // modem Hayes RS-232 (0/1)
    int  reqEther = -1;                   // EtherNEC NE2000 (0/1)
    // Sorties MIDI (page Sound) : synthé GM, port CoreMIDI, MT-32 (0/1) ; modèle 0 auto/1 MT-32/2 CM-32L.
    int  reqMidiOutGm = -1, reqMidiOutPort = -1, reqMidiOutMt32 = -1, reqMt32Model = -1;
    bool reqMidiPanic = false;            // bouton « All notes off » de la page MIDI
    int  reqMidiLoopback = -1;            // case OUT->IN de la page MIDI
    int  reqDongle = -1;                  // clé cartouche : 0 none, 1 cubase2, 2 cubase3, 3 auto, 4 notator
    int  reqPlugPort = -1, reqPlugDev = -1; // page Dongles : brancher reqPlugDev sur reqPlugPort
    bool reqPortButton = false;           // bouton Multiface / Ultimate Ripper (page Dongles)
    std::string mt32Status;               // lecture : modèle chargé ou erreur
    // Mixeur (page Sound) : édité EN PLACE par la page ; mixDirty = appliquer, mixDone = persister.
    float mixYm = 1.0f, mixDma = 1.0f, mixDrive = 1.0f, mixMt32 = 1.0f, mixDac = 1.0f;
    bool  mixInit = false, mixDirty = false, mixDone = false;
    int  reqNetUsbee = -1;                // NetUSBee NE2000 + ISP1160 (0/1)
    int  reqUltraSatan = -1;              // UltraSatan sur les IDs ACSI 0-1 (0/1)
    std::string reqMountSd2;              // image SD du slot 2 (ID 1)
    bool reqEjectSd2 = false;
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
    kCfgMachine = 0, kCfgMem, kCfgRom, kCfgHd, kCfgCart, kCfgNet, kCfgDongle,
    kCfgScreen, kCfgSound, kCfgMidi, kCfgInput, kCfgEmul, kCfgProfiles, kCfgKiosk, kCfgCount
};
int g_cfgPage = kCfgMachine;
bool g_profilesDirty = false; // un profil vient d'être écrit/supprimé → relire le dossier

// Fenêtre autonome : les disquettes sont des supports manipulés en jouant, pas un
// réglage matériel. Les requêtes restent consommées par la boucle principale.
void drawFloppyWindow(ConfigUi& ui) {
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_SAVE " Floppies", &g_showFloppy,
                      ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::End();
        return;
    }
    drawFloppyPage(ui.disksDir,
                   ui.machine->fdc.mountedPath(0), ui.machine->fdc.mountedPath(1),
                   ui.reqMountA, ui.reqMountB, ui.reqEjectA, ui.reqEjectB);
    ImGui::End();
}

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
    // Chaque préréglage nomme le TOS d'ORIGINE de la machine, PUIS des replis EmuTOS.
    // Sans repli, un préréglage pointait sur une ROM absente du paquet livré : « Mega STE »
    // demande tos206fr, qui n'a JAMAIS été empaquetée, et « 520 ST »/« 1040 STE » perdent
    // la leur dès qu'un paquet est construit sans les TOS Atari
    // (NEOST_PACKAGE_NO_ATARI_TOS=1, cf. packaging/stage_free_data.sh). Le repli garde le
    // pays/la fréquence quand il le peut : tos102uk et tos162uk sont PAL → etos*fr (PAL).
    struct Profil { const char* label; const char* machine; const char* mem;
                    const char* rom; const char* rom2; const char* rom3; };
    static const Profil kProfils[] = {
        { "520 ST",   "st",      "512k", "roms/tos102uk.img", "roms/etos192fr.img", "roms/etos192us.img" },
        { "1040 STE", "ste",     "1m",   "roms/tos162uk.img", "roms/etos256fr.img", "roms/etos256us.img" },
        { "Mega STE", "megaste", "4m",   "roms/tos206fr.img", "roms/etos256fr.img", "roms/etos256us.img" },
    };
    // Premier candidat PRÉSENT dans roms/ ; à défaut le premier (message d'erreur explicite
    // au chargement plutôt qu'un chemin silencieusement faux).
    auto pickPresetRom = [&](const Profil& p) {
        std::error_code ec;
        for (const char* cand : { p.rom, p.rom2, p.rom3 })
            if (fs::exists(fs::path(ui.romsDir) / fs::path(cand).filename(), ec)) return std::string(cand);
        return std::string(p.rom);
    };
    ImGui::TextDisabled("Presets:");
    for (const auto& p : kProfils) {
        ImGui::SameLine();
        const std::string rom = pickPresetRom(p);
        const bool cur = ui.pendMachine == p.machine && ui.pendMem == p.mem
                      && fs::path(ui.pendRom).filename() == fs::path(rom).filename();
        if (cur) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(p.label)) {
            ui.pendMachine = p.machine; ui.pendMem = p.mem; ui.pendRom = rom;
            ui.pendFpu = false;
        }
        if (cur) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s, %s, %s%s", p.machine, p.mem, rom.c_str(),
                              rom == p.rom ? "" : "\n(original TOS not installed - using EmuTOS)");
    }
    ImGui::Separator();

    // ── Colonne de navigation + page ──────────────────────────────────────
    static const char* kPageNames[kCfgCount] = {
        ICON_FA_MICROCHIP " Machine",  ICON_FA_MEMORY " Memory",
        ICON_FA_SAVE " ROM / TOS",     ICON_FA_HDD " Hard disks",
        ICON_FA_COMPACT_DISC " Cartridge",
        ICON_FA_WIFI " Network",
        ICON_FA_KEY " Dongles",
        ICON_FA_DESKTOP " Screen",     ICON_FA_VOLUME_UP " Sound",
        ICON_FA_MUSIC " MIDI",
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
    case kCfgHd:
        drawHardDiskPage(ui.hdDir, ui.gemdosDir,
                         ui.curGemdos, ui.machine->gemdos.active(),
                         ui.curAcsi, ui.machine->fdc.acsiActive(),
                         ui.machine->fdc.acsiPartitionCount(),
                         ui.machine->ultraSatanEnabled(), ui.curSd2,
                         ui.reqMountGemdos, ui.reqEjectGemdos,
                         ui.reqMountAcsi,   ui.reqEjectAcsi,
                         ui.reqUltraSatan, ui.reqMountSd2, ui.reqEjectSd2);
        break;
    case kCfgCart:
        drawCartPage(ui.cartsDir, ui.machine->bus.mountedCartPath(),
                     ui.reqMountCart, ui.reqEjectCart);
        break;
    case kCfgNet:
        drawNetworkPage(ui.cfg && ui.cfg->modem, ui.machine->ne2000.enabled(),
                        ui.machine->netUsbeeEnabled(),
                        !ui.machine->bus.mountedCartPath().empty(),
                        ui.reqModem, ui.reqEther, ui.reqNetUsbee);
        break;
    // ── Dongles ───────────────────────────────────────────────────────────────
    // Tout ce qui se branchait sur un port pour qu'un logiciel le sonde : les clés du
    // port cartouche (io/CartridgeKey.hpp) et un périphérique par autre port
    // (io/PortDevices.hpp) — modèle physique : ils coexistent.
    case kCfgDongle: {
        ImGui::TextDisabled("CARTRIDGE PORT KEY (/ROM3 $FB0000, invisible to TOS)");
        int dk = cfg.dongle == "cubase2" ? 1 : cfg.dongle == "cubase3" ? 2 : cfg.dongle == "auto" ? 3
               : cfg.dongle == "notator" ? 4 : 0;
        bool dkCh = false;
        dkCh |= ImGui::RadioButton("None##key", &dk, 0); ImGui::SameLine();
        dkCh |= ImGui::RadioButton("Red key (Cubase 3.1 / Score / Audio)", &dk, 2);
        dkCh |= ImGui::RadioButton("Black key (Cubase 2.01)", &dk, 1); ImGui::SameLine();
        dkCh |= ImGui::RadioButton("Auto (red/black)", &dk, 3);
        dkCh |= ImGui::RadioButton("C-Lab key (Notator / Creator, Unitor-N)", &dk, 4);
        if (dkCh) ui.reqDongle = dk;
        ImGui::TextDisabled("  PAL16R8 / 5C060 / EP600 state machines (MiSTery + TPH equations).");
        ImGui::TextDisabled("  Cubase Lite needs none. Black key: clocked by every CPU bus cycle - best effort.");
        // Observabilité : ce que la clé a vu. Le jour où un logiciel dit « dongle not
        // found », c'est ici qu'on regarde d'abord (puis --key-log / --key-replay).
        if (ui.machine->dongle.attached()) {
            const auto& k = ui.machine->dongle;
            ImGui::Text("  Probes: %u   last byte: $%02X   state: $%04X%s",
                        unsigned(k.probes()), unsigned(k.lastByte()), unsigned(k.state()),
                        k.model() == CartridgeKey::Model::Notator ? (k.armed() ? "   armed" : "   not armed") : "");
        }
        ImGui::Separator();

        ImGui::TextDisabled("ONE DEVICE PER PORT (they coexist, like real hardware)");
        static const char* const portNames[] = { "Joystick 0 (mouse port)", "Joystick 1", "RS-232", "Printer", "Cartridge button" };
        for (int pi = 0; pi < int(PortDevices::Port::Count); ++pi) {
            const auto port = PortDevices::Port(pi);
            const PortDevices::Device cur = ui.machine->ports.at(port);
            ImGui::SetNextItemWidth(260);
            if (ImGui::BeginCombo(portNames[pi], PortDevices::label(cur))) {
                for (int di = 0; di < int(PortDevices::Device::Count); ++di) {
                    const auto d = PortDevices::Device(di);
                    if (!PortDevices::fits(port, d)) continue;
                    const bool home = d == PortDevices::Device::None || PortDevices::defaultPort(d) == port;
                    char lab[96];
                    std::snprintf(lab, sizeof lab, "%s%s", PortDevices::label(d), home ? "" : "  (wrong port for this game)");
                    if (ImGui::Selectable(lab, cur == d)) { ui.reqPlugPort = pi; ui.reqPlugDev = di; }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Spacing();
        if (ui.machine->ports.hasButton()) {
            const bool mf = ui.machine->ports.at(PortDevices::Port::CartButton) == PortDevices::Device::Multiface;
            if (ImGui::Button(mf ? "Press FREEZE button" : "Press RIPPER button")) ui.reqPortButton = true;
            ImGui::SameLine();
            ImGui::TextDisabled(mf ? "pulls GPIP7 (monitor) low until next VBL - load the ROM as a cartridge"
                                   : "pulls RI (GPIP6) until next VBL - load the ROM as a cartridge");
        }
        ImGui::TextWrapped("Joystick keys override the directions the IKBD reports (Leader Board: up+down "
                           "at once; Cricket & co: an oscillator) - a game only looks at one port, plug the "
                           "key where it expects it. Serial keys drive CTS/DCD from RTS/DTR. Pro Sound "
                           "Designer is not a key: an 8-bit DAC on the printer port (Wings of Death / "
                           "Lethal Xcess on an STF), with its own fader on the Sound page. disks/dongles.txt "
                           "plugs keys automatically when a matching disk is mounted. Protocols from Steem "
                           "SSE and WinUAE; not emulated (no public dump): Log 3, Pro-24, Avalon, Zodiac.");
        break;
    }

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
        // --- Mixeur : un fader par source de NeoST ---------------------------------
        ImGui::Separator();
        ImGui::TextDisabled("Mixer (per-source gains, 100 %% = as on the hardware)");
        {
            auto fader = [&](const char* label, float& v, bool enabled, const char* why) {
                int pct = int(v * 100.0f + 0.5f);
                ImGui::BeginDisabled(!enabled);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderInt(label, &pct, 0, 200, "%d %%")) { v = float(pct) / 100.0f; ui.mixDirty = true; }
                if (ImGui::IsItemDeactivatedAfterEdit()) ui.mixDone = true;
                ImGui::EndDisabled();
                if (!enabled && why) { ImGui::SameLine(); ImGui::TextDisabled("%s", why); }
            };
            const bool isSte = ui.machine && (ui.machine->machineType() == MachineType::Ste
                                               || ui.machine->machineType() == MachineType::MegaSte);
            fader("YM2149 (PSG)", ui.mixYm, true, nullptr);
            fader("DMA sound (STE)", ui.mixDma, isSte, "(ST: no DMA sound)");
            fader("Floppy drive", ui.mixDrive, ui.driveSoundAvail, "(no samples)");
            fader("Roland MT-32 / CM-32L", ui.mixMt32, Mt32Synth::available(), "(no libmt32emu)");
            fader("Pro Sound DAC (printer port)", ui.mixDac, ui.machine && ui.machine->ports.usesPortBDac(), "(none plugged - Dongles page)");
            if (ImGui::SmallButton("Reset mixer")) {
                ui.mixYm = ui.mixDma = ui.mixDrive = ui.mixMt32 = ui.mixDac = 1.0f; ui.mixDirty = ui.mixDone = true;
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("MIDI OUT has its own page (MIDI).");
        break;
    }

    // ── MIDI ──────────────────────────────────────────────────────────────────
    // Page à part : la sortie MIDI n'est pas un réglage de volume, c'est un CÂBLAGE
    // vers l'extérieur — et elle est le meilleur moyen d'entendre correctement du
    // General MIDI, que le MT-32 ne sait pas jouer (cf. la note sur les cartes).
    case kCfgMidi: {
        ImGui::TextDisabled("MIDI OUT of the ST (ACIA 6850)");
        ImGui::Separator();

        // (a) Port virtuel : la voie RECOMMANDÉE pour du General MIDI.
        if (MidiOutHost::portAvailable()) {
            bool port = cfg.midiOutPort;
            char lbl[96];
            std::snprintf(lbl, sizeof lbl, "Virtual MIDI port \"NeoST MIDI OUT\" (%s)",
                          MidiOutHost::portKindName());
            if (ImGui::Checkbox(lbl, &port)) ui.reqMidiOutPort = port ? 1 : 0;
            ImGui::TextDisabled("  For FluidSynth, Qsynth, a DAW, real gear.");
            ImGui::TextDisabled("  Best choice for General MIDI files.");
        } else {
            ImGui::BeginDisabled(true);
            bool no = false; ImGui::Checkbox("Virtual MIDI port \"NeoST MIDI OUT\"", &no);
            ImGui::EndDisabled();
            ImGui::TextDisabled("  Not in this build (needs libasound2-dev).");
        }
        ImGui::Separator();

        // (b) Synthé GM intégré : macOS uniquement, et on le DIT au lieu d'une case morte.
        if (MidiOutHost::synthAvailable()) {
            bool gm = cfg.midiOutGm;
            if (ImGui::Checkbox("Built-in General MIDI synth", &gm)) ui.reqMidiOutGm = gm ? 1 : 0;
            ImGui::TextDisabled("  Apple DLSMusicDevice, nothing to install.");
        } else {
            ImGui::BeginDisabled(true);
            bool no = false; ImGui::Checkbox("Built-in General MIDI synth", &no);
            ImGui::EndDisabled();
            ImGui::TextDisabled("  macOS only. Elsewhere: use the port above");
            ImGui::TextDisabled("  with FluidSynth.");
        }
        ImGui::Separator();

        // (c) MT-32 / CM-32L.
        if (Mt32Synth::available()) {
            bool mt = cfg.midiOutMt32;
            if (ImGui::Checkbox("Roland MT-32 / CM-32L (Munt, emulated)", &mt))
                ui.reqMidiOutMt32 = mt ? 1 : 0;
            const int cur = cfg.mt32Model == "mt32" ? 1 : cfg.mt32Model == "cm32l" ? 2 : 0;
            ImGui::TextDisabled("  Model:"); ImGui::SameLine();
            if (ImGui::RadioButton("Auto", cur == 0))   ui.reqMt32Model = 0; ImGui::SameLine();
            if (ImGui::RadioButton("MT-32", cur == 1))  ui.reqMt32Model = 1; ImGui::SameLine();
            if (ImGui::RadioButton("CM-32L", cur == 2)) ui.reqMt32Model = 2;
            ImGui::TextDisabled("  ROMs: %s  -  %s", cfg.mt32Roms.c_str(),
                                ui.mt32Status.empty() ? "(off)" : ui.mt32Status.c_str());
            ImGui::TextDisabled("  Auto = CM-32L if its ROMs are there.");
            ImGui::TextDisabled("  For era patches (Cubase .ALL, 1991).");
            ImGui::TextColored(ImVec4(1.f, .7f, .35f, 1.f),
                               "  GM files play WRONG here: LA map != GM map.");
        } else {
            ImGui::TextDisabled("Roland MT-32 / CM-32L: built without libmt32emu.");
        }
        ImGui::Separator();

        // MIDI IN : la fiche de bouclage, jusqu'ici cachée dans le menu Machine.
        ImGui::TextDisabled("MIDI IN");
        bool loop = cfg.midiLoopback;   // cfg est CONST ici : on passe par une requête
        if (ImGui::Checkbox("Loopback cable OUT->IN", &loop)) ui.reqMidiLoopback = loop ? 1 : 0;
        ImGui::TextDisabled("  A real ST has none. Cubase MIDI Thru = feedback.");
        ImGui::Separator();

        ImGui::TextDisabled("Steinberg key (Cubase 2/3): see the Dongles page.");
        ImGui::Separator();

        // Panique : indispensable dès qu'on coupe une sortie en plein accord.
        if (ImGui::Button("Panic - all notes off")) ui.reqMidiPanic = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(CC 120/121/123 on the 16 channels)");
        ImGui::TextWrapped("A synth never releases a note by itself: stop a program "
                           "mid-chord and the notes hang until told otherwise.");
        break;
    }
    case kCfgInput: {
        // Modèle PHYSIQUE des deux ports DE-9 : on choisit ce qu'on y BRANCHE. Port 0 =
        // port souris (la souris par défaut ; un joystick l'y remplace, comme sur un vrai
        // ST) ; port 1 = port jeux (la 1re manette par défaut). Les choix s'expriment
        // sur le mécanisme existant (rôles par GUID — joymap —, émulation clavier,
        // port0=) : la page Joystick et le menu borne voient la même chose.
        ImGui::TextDisabled("WHAT IS PLUGGED INTO THE TWO JOYSTICK PORTS");
        int8_t roles[GLFW_JOYSTICK_LAST + 1]; joyResolveRoles(roles);
        int8_t assign[GLFW_JOYSTICK_LAST + 1]; stjoy::resolveAssign(roles, assign, g_port0Auto);
        auto padName = [](int jid) { const char* n = glfwGetGamepadName(jid); if (!n) n = glfwGetJoystickName(jid); return n ? n : "?"; };
        auto unpin = [&](int port) {   // toute manette épinglée sur `port` repasse en AUTO
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
                if (glfwJoystickPresent(jid) && roles[jid] == port) g_joyRoleByGuid.erase(joyGuid(jid));
        };
        for (int port = 0; port < 2; ++port) {
            // Valeur courante : manette ÉPINGLÉE > clavier > auto/souris.
            int pinnedJid = -1, autoJid = -1;
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                if (!glfwJoystickPresent(jid)) continue;
                if (roles[jid] == port) pinnedJid = jid;
                else if (assign[jid] == port && roles[jid] == stjoy::ROLE_AUTO) autoJid = jid;
            }
            const bool kbdHere = g_kbdJoy && g_kbdJoyPort == port;
            char cur[160];
            if (pinnedJid >= 0)      std::snprintf(cur, sizeof cur, "Pad: %s", padName(pinnedJid));
            else if (kbdHere)        std::snprintf(cur, sizeof cur, "Keyboard joystick (arrows + right Ctrl)");
            else if (port == 0)      std::snprintf(cur, sizeof cur, g_port0Auto ? (autoJid >= 0 ? "Auto: 2nd pad (%s)" : "Auto: 2nd pad (none yet)") : "Mouse", autoJid >= 0 ? padName(autoJid) : "");
            else                     std::snprintf(cur, sizeof cur, autoJid >= 0 ? "Auto: first pad (%s)" : "Auto: first pad (none detected)", autoJid >= 0 ? padName(autoJid) : "");
            ImGui::SetNextItemWidth(320);
            if (ImGui::BeginCombo(port == 0 ? "Port 0 (mouse port)" : "Port 1 (joystick port)", cur)) {
                if (port == 0) {
                    if (ImGui::Selectable("Mouse", pinnedJid < 0 && !kbdHere && !g_port0Auto)) {
                        unpin(0); g_port0Auto = false; if (kbdHere) g_kbdJoyPort = 1; g_joyCfgDirty = true;
                    }
                    if (ImGui::Selectable("Auto: 2nd pad takes the mouse port", pinnedJid < 0 && !kbdHere && g_port0Auto)) {
                        unpin(0); g_port0Auto = true; if (kbdHere) g_kbdJoyPort = 1; g_joyCfgDirty = true;
                    }
                } else {
                    if (ImGui::Selectable("Auto: first free pad", pinnedJid < 0 && !kbdHere)) {
                        unpin(1); if (kbdHere) g_kbdJoyPort = 0; g_joyCfgDirty = true;
                    }
                }
                if (ImGui::Selectable("Keyboard joystick (arrows + right Ctrl)", kbdHere && pinnedJid < 0)) {
                    unpin(port); g_kbdJoy = true; g_kbdJoyPort = port; g_joyCfgDirty = true;
                }
                for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                    if (!glfwJoystickPresent(jid)) continue;
                    char lab[200]; std::snprintf(lab, sizeof lab, "Pad: %s##%d", padName(jid), jid);
                    if (ImGui::Selectable(lab, pinnedJid == jid)) {
                        unpin(port);                                  // un seul épinglé par port
                        g_joyRoleByGuid[joyGuid(jid)] = int8_t(port); // cette manette, ici
                        if (kbdHere) g_kbdJoyPort = 1 - port;         // le clavier cède la place
                        g_joyCfgDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            // Clé de protection branchée sur ce port (page Dongles) : elle s'ajoute.
            const auto key = ui.machine->ports.at(port == 0 ? PortDevices::Port::Joy0 : PortDevices::Port::Joy1);
            if (key != PortDevices::Device::None) { ImGui::SameLine(); ImGui::TextDisabled("+ %s (Dongles page)", PortDevices::label(key)); }
        }
        if (g_port0Joystick) ImGui::TextDisabled("  A joystick occupies port 0: the host mouse is unplugged from the ST.");
        ImGui::TextDisabled("  Two players: put a pad on port 0 (games disable the mouse themselves).");
        ImGui::Separator();
        if (ImGui::Checkbox("Keyboard joystick emulation active", &g_kbdJoy)) g_joyCfgDirty = true;
        ImGui::SameLine(); ImGui::TextDisabled("(F11 - session only: it swallows the arrow keys)");
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
        ImGui::TextDisabled("USB pads detected (effective assignment):");
        int nPad = 0;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (!glfwJoystickPresent(jid)) continue;
            const char* how = roles[jid] == stjoy::ROLE_OFF ? " (off)" : roles[jid] == stjoy::ROLE_AUTO ? " (auto)" : " (pinned)";
            if (assign[jid] >= 0) ImGui::BulletText("Port %d: %s%s", assign[jid], padName(jid), how);
            else                  ImGui::BulletText("Unused: %s%s", padName(jid), how);
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
    g_showJoy = cfg.showJoy; g_showCfg = cfg.showCfg; g_showFloppy = cfg.showFloppy;
    g_showKbd = cfg.showKbd;
    // Disposition ancrée : un imgui.ini écrit par une version antérieure garde des
    // nœuds pour des fenêtres qui n'existent plus (Disk/Cart Library) et ne connaît
    // pas la fenêtre Configuration — qui flotterait alors au-dessus de l'écran ST.
    // On resème la disposition UNE fois, puis on note la version dans neost.cfg.
    static constexpr int kUiVersion = 4;
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
    // --run-frames / --shot (cf. le parseur ci-dessous).
    static long        g_runFrames = -1;   // -1 = illimité (comportement normal)
    static std::string g_shotPath;
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
        // --help : sortie AVANT toute création de fenêtre (comme --version), ce qui la
        // rend testable sans écran — c'est la seule surface du GUI que la CI peut
        // exercer aujourd'hui (chantier A8).
        if (a == "--help" || a == "-h") {
            std::printf(
                "NeoST - Atari ST emulator\n"
                "Usage: neost [options] [<rom.img> [<disk.st>]]\n"
                "\n"
                "  --help, -h            this help\n"
                "  --version             build identity\n"
                "  --kiosk               kiosk (arcade cabinet) mode\n"
                "  --kiosk-monitor N     monitor index for kiosk mode\n"
                "  --audio-latency MS    audio cushion in ms (default 85, clamped 20-250;\n"
                "                        raise to 120-150 on a tight machine - an underrun\n"
                "                        costs an audible gap, a little more latency does not)\n"
                "  --crt                 enable CRT effects\n"
                "  --crt-preset NAME     off|light|arcade|phosphor (implies --crt)\n"
                "  --run-frames N        quit after N emulated frames (harness use)\n"
                "  --shot PATH           dump the ST framebuffer as PPM before that exit\n"
                "\n"
                "Without a positional argument, the last ROM from neost.cfg is reloaded\n"
                "(or EmuTOS US). Headless runs: see neost-headless --help.\n");
            return 0;
        }
        if      (a == "--kiosk")           g_kiosk = g_kioskLaunched = true;
        //   --run-frames N : quitte proprement après N trames ÉMULÉES (pas affichées).
        //   --shot PATH    : dump du framebuffer ST en PPM juste avant cette sortie.
        //   Chantier A8 : c'est la brique qui rend le GUI observable par un harnais —
        //   un job CI sous xvfb peut désormais bâtir la cible `neost`, la faire tourner
        //   N trames et comparer la capture, là où le GUI n'était testable que sur ses
        //   arguments. En interactif, permet aussi de capturer l'état exact d'un rapport
        //   utilisateur (« l'écran est cassé à tel moment ») sans outil externe.
        else if (a == "--run-frames" && i + 1 < argc) g_runFrames = std::atol(argv[++i]);
        else if (a == "--shot" && i + 1 < argc)       g_shotPath = argv[++i];
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
        else if (!a.empty() && a[0] == '-') {
            // ⚠ AVALÉ EN SILENCE jusqu'au 2026-08-26 (chantier A8) : une faute de frappe
            // (`--kisok`, `--crt-presets`) partait sans le moindre message et l'option
            // était simplement ignorée — l'utilisateur croyait l'avoir activée. On refuse
            // maintenant, AVANT d'ouvrir la moindre fenêtre, et on renvoie 2 pour qu'un
            // script le voie.
            std::fprintf(stderr, "[main] unknown option: '%s'\n"
                                 "Try 'neost --help' for the list of options.\n", a.c_str());
            return 2;
        }
        else if (!a.empty()) pos.push_back(a);
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
    // Table disks/dongles.txt : créée avec les titres connus si absente (jamais en
    // borne : config figée), relue à chaque montage (on peut l'éditer à chaud).
    auto loadDongleTable = [&]() -> std::vector<neost::DongleRule> {
        const std::string path = disksDir + "/dongles.txt";
        std::string text;
        if (std::ifstream in(path); in) text.assign(std::istreambuf_iterator<char>(in), {});
        else {
            text = neost::defaultDongleTable();
            if (!g_kiosk) if (std::ofstream out(path); out) out << text;
        }
        return neost::parseDongleTable(text);
    };
    loadDongleTable();   // crée disks/dongles.txt (titres connus) dès le premier lancement
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
    // VSync : évite le tearing hôte (une coupure horizontale rare à une hauteur
    // variable, très visible sur les rasters de Super Hang-On). L'ancien bridage
    // dormait 20 ms APRÈS le swap bloquant et tombait donc à 30-37 fps sur un écran
    // 60 Hz ; ce n'est plus le cas : le sommeil ci-dessous vise une échéance ABSOLUE
    // (`emuNext`) et soustrait implicitement le temps passé dans swapBuffers. Si un
    // écran lent fait malgré tout manquer une échéance, la boucle de rattrapage
    // exécute plusieurs trames avant la présentation suivante, donc le temps émulé
    // et la production audio restent à 50/60/71 Hz.
    glfwSwapInterval(1);

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
        // Absolu = déjà résolu (cf. resolveData et util/HostPath) : c'est le cas du
        // dossier glissé-déposé, qui arrive TOUJOURS en absolu depuis GLFW.
        if (neost::hostpath::isAbsolute(given)) return given;
        const std::string cands[] = { given,
                                      neost::hostpath::join(exeDir, given),
                                      neost::hostpath::join(exeDir + "/..", given),
                                      neost::hostpath::join("..", given) };
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
    // UltraSatan (ultrasatan=/sd2= dans neost.cfg) : interface SD sur les IDs 0-1 —
    // slot 1 = l'image acsi= (ID 0), slot 2 = sd2= (ID 1). À rejouer après TOUT
    // montage/démontage ACSI : unmountAcsi() vide toutes les cibles, slot 2 compris.
    auto usatanApply = [&]() {
        if (!cfg.ultrasatan) {
            if (machine.ultraSatanEnabled()) machine.disableUltraSatan();
            return;
        }
        if (!machine.ultraSatanEnabled()) machine.enableUltraSatan(0);
        if (!cfg.sd2.empty()) {
            const std::string want = resolvePath(cfg.sd2);
            if (machine.fdc.acsiMountedPath(1) != want && !machine.fdc.mountAcsi(want, 1))
                cfg.sd2.clear();               // image invalide → on ne la mémorise pas
        }
    };
    usatanApply();
#ifdef NEOST_WITH_NET
    // Modem Hayes (modem= dans neost.cfg) : commandes AT sur l'USART → pont TCP.
    std::unique_ptr<HayesModem> hayesModem;
    auto modemApply = [&](bool on) {
        if (on && !hayesModem) {
            hayesModem = std::make_unique<HayesModem>(machine.mfp);
            HayesModem* m = hayesModem.get();
            machine.mfp.setSerialSink([m](uint8_t b) { m->onTx(b); });
        } else if (!on && hayesModem) {
            machine.mfp.setSerialSink({});
            hayesModem.reset();
        }
    };
    if (cfg.modem) modemApply(true);
#endif
    // EtherNEC (ethernec= dans neost.cfg) : NE2000 sur le port cartouche. Backend
    // par défaut = boucle locale (NetBackendNull une fois un pont SLIRP/pcap
    // absent) : la carte est DÉTECTÉE par le pilote, sans trafic hôte v1.
    NetBackendNull etherNull;
    // NetUSBee (netusbee=) = la même NE2000 + l'hôte USB ISP1160 ; exclusif d'EtherNEC
    // (un seul montage sur le port cartouche) — netusbee= prime sur ethernec=.
    auto etherApply = [&](bool on) {
        if (on) {
            if (!machine.bus.cart.empty()) {
                g_stateMsg = "EtherNEC needs the cartridge port free";
                g_stateMsgFrames = 150;
                cfg.ethernec = false;
                return;
            }
            if (machine.netUsbeeEnabled()) return;   // la NE2000 est déjà là (NetUSBee)
            // Même pré-test que pour la cartouche : la clé Steinberg est exclusive de
            // l'EtherNEC (enableEtherNec la refuse). Sans ce message, la case restait
            // cochée alors que rien ne s'activait.
            if (machine.dongle.attached()) {
                g_stateMsg = "EtherNEC needs the cartridge port free (a Steinberg key is plugged)";
                g_stateMsgFrames = 150;
                cfg.ethernec = false;
                return;
            }
            machine.ne2000.setBackend(&etherNull);
            machine.enableEtherNec();
        } else if (!machine.netUsbeeEnabled()) {
            machine.disableEtherNec();
        }
    };
    auto netUsbeeApply = [&](bool on) {
        if (on) {
            if (!machine.bus.cart.empty()) {
                g_stateMsg = "NetUSBee needs the cartridge port free";
                g_stateMsgFrames = 150;
                cfg.netusbee = false;
                return;
            }
            if (machine.netUsbeeEnabled()) return;
            if (machine.dongle.attached()) {           // cf. etherApply : exclusivité clé/réseau
                g_stateMsg = "NetUSBee needs the cartridge port free (a Steinberg key is plugged)";
                g_stateMsgFrames = 150;
                cfg.netusbee = false;
                return;
            }
            machine.disableEtherNec();                 // la NE2000 repart avec le NetUSBee
            machine.ne2000.setBackend(&etherNull);
            machine.enableNetUsbee();
        } else if (machine.netUsbeeEnabled()) {
            machine.disableNetUsbee();
            if (cfg.ethernec) etherApply(true);        // EtherNEC seul reprend la NE2000
        }
    };
    if (cfg.netusbee) netUsbeeApply(true);
    if (cfg.ethernec) etherApply(true);
    machine.midi.setLoopback(cfg.midiLoopback);   // fiche MIDI OUT→IN (OFF par défaut)
    // Clé Steinberg (dongle= dans neost.cfg) : /ROM3, cohabite avec le HD GEMDOS.
    if (!cfg.dongle.empty()) {
        const CartridgeKey::Model dm = cfg.dongle == "cubase2" ? CartridgeKey::Model::Cubase2
                                     : cfg.dongle == "cubase3" ? CartridgeKey::Model::Cubase3
                                     : cfg.dongle == "auto"    ? CartridgeKey::Model::Auto
                                     : cfg.dongle == "notator" ? CartridgeKey::Model::Notator
                                     : CartridgeKey::Model::None;
        if (!machine.setDongle(dm)) {
            g_stateMsg = "Steinberg key needs the cartridge port free (EtherNEC/NetUSBee)";
            g_stateMsgFrames = 150;
            cfg.dongle.clear();
        }
    }
    // Périphériques des ports (joy0= … cartbutton= dans neost.cfg).
    {
        std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
        for (int pi = 0; pi < int(PortDevices::Port::Count); ++pi) {
            if (slots[pi]->empty()) continue;
            const PortDevices::Device d = PortDevices::fromId(slots[pi]->c_str());
            if (d == PortDevices::Device::None || !machine.plugPort(PortDevices::Port(pi), d))
                slots[pi]->clear();   // identifiant inconnu ou connecteur inadapté : on oublie
        }
        machine.psg.setPortBDacGain(cfg.mixDac);
    }
    // Un périphérique que dongles.txt EXPLIQUE pour la disquette montée au démarrage
    // est réputé image-dérivé : on le marque comme auto-branché (sans rien brancher ni
    // débrancher ici) pour qu'un montage à chaud ultérieur le retire. Sans ça, la clé
    // qu'un auto-plug d'une session PRÉCÉDENTE a persistée dans neost.cfg est
    // indiscernable d'un choix de l'utilisateur, et resterait collée au jeu suivant.
    if (cfg.autoDongle && !diskPath.empty()) {
        for (const auto& r : neost::matchDongleRules(loadDongleTable(), diskPath)) {
            if (r.cart) { if (machine.dongle.model() == r.key) g_autoCartKey = r.key; }
            else if (machine.ports.at(r.port) == r.dev) g_autoPortDev[int(r.port)] = r.dev;
        }
    }
    // Sortie MIDI hôte (macOS) : synthé GM intégré et/ou port CoreMIDI virtuel. Dès
    // qu'une sortie est ouverte, l'ACIA y envoie MIDI OUT (au lieu du bouclage).
    MidiOutHost midiOut;
    // Roland MT-32/CM-32L (Munt) : rendu DANS la sortie audio, daté au cycle (pas de gigue).
    Mt32Synth mt32;
    // Fréquence de sortie visée. Ce n'est PAS forcément celle qu'on obtiendra : le
    // périphérique en négocie une (cf. Audio::rate()), et cette lambda s'exécute AVANT
    // que l'objet Audio n'existe. On la corrige juste après audio.start().
    uint32_t audioRate = 48000;
    auto midiOutApply = [&]() {
        if (cfg.midiOutGm)   { if (!midiOut.openSynth()) cfg.midiOutGm = false; } else midiOut.closeSynth();
        if (cfg.midiOutPort) {
            if (!midiOut.openVirtualPort()) {
                cfg.midiOutPort = false;
                g_stateMsg = "MIDI OUT: cannot create the CoreMIDI port (see console)";
                g_stateMsgFrames = 300;
            }
        } else midiOut.closeVirtualPort();
        if (cfg.midiOutMt32) {
            if (!mt32.isOpen() && !mt32.open(resolveData(cfg.mt32Roms, exeDir), audioRate, cfg.mt32Model)) {
                g_stateMsg = "MT-32: " + mt32.lastError(); g_stateMsgFrames = 300;
                std::fprintf(stderr, "[mt32] %s\n", mt32.lastError().c_str());
                cfg.midiOutMt32 = false;
            }
        } else mt32.close();
        if (midiOut.anyOpen() || mt32.isOpen())
            machine.midi.setMidiSinkTimed([&midiOut, &mt32](uint8_t b, int64_t c) {
                if (midiOut.anyOpen()) midiOut.byteAt(b, c);
                mt32.byteAt(b, c);
            });
        else machine.midi.setMidiSinkTimed({});
    };
    midiOutApply();
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
    audio.setMt32(&mt32);                // Roland MT-32/CM-32L (Munt) mixé dans la sortie
    audio.setMixGains(cfg.mixYm, cfg.mixDma, cfg.mixDrive, cfg.mixMt32);   // mixeur (page Sound)
    audio.setLatencyMs(uint32_t(cfg.audioLatencyMs < 0 ? 0 : cfg.audioLatencyMs));  // AVANT start (borné dans Audio)
    audio.start();   // échec silencieux possible (CI / pas de carte son)
    // Le périphérique peut rendre une AUTRE fréquence que les 48 kHz demandés (44,1 kHz
    // sur un matériel qui n'accepte que ça). Le reste de la chaîne suit déjà rate_, mais
    // le MT-32 et les bruits de lecteur avaient été initialisés sur l'hypothèse 48 kHz :
    // eux seuls sortiraient alors désaccordés (~8,8 % à 44,1 kHz) et dériveraient du
    // mixage. On les réaligne — chemin rare, sans effet quand 48 kHz est accordé.
    if (audio.ok() && audio.rate() != audioRate) {
        std::fprintf(stderr, "[Audio] device negotiated %u Hz (not %u) — realigning MT-32 "
                             "and drive sounds\n", audio.rate(), audioRate);
        audioRate = audio.rate();
        if (driveSoundAvail) drive.init(resolveData("roms/drivesound/epson_smd480l", exeDir), audioRate);
        if (mt32.isOpen()) { mt32.close(); midiOutApply(); }
    }
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
        else {
            // Remonter aussi quand la config désigne une AUTRE image : le seul test
            // « déjà actif » laissait l'ancienne image montée alors que la barre de
            // statut et neost.cfg affichaient la nouvelle (profil chargé).
            const std::string want = resolvePath(cfg.acsi);
            if (!machine.fdc.acsiActive() || machine.fdc.acsiMountedPath() != want)
                machine.fdc.mountAcsi(want);
        }
        usatanApply();                         // slot 2 + attache (unmountAcsi a tout vidé)
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
    g_port0Auto  = (cfg.port0 == "auto");
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

    std::printf("[main] Middle mouse button or Ctrl+Alt+G: capture/release mouse | "
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
        keyboardWindowReleaseAll(machine.ikbd);   // rien ne reste enfoncé à travers la bascule
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
            cfg.showKbd  = g_showKbd;
            cfg.showCfg  = g_showCfg;  cfg.showFloppy = g_showFloppy;
            saveConfig(exeDir, cfg, &machine);
            // ⚠ La référence PRISTINE doit devenir CE qui vient d'être persisté, pas la
            // config lue au démarrage. Sinon le gel kiosk se retourne contre la ligne
            // ci-dessus : plus tard, n'importe quel saveConfig(force=true) de la borne
            // (ajout/retrait d'un dossier ROM, réaffectation manette, ou simple
            // auto-purge d'un dossier ROM disparu) reconstruit le fichier depuis
            // g_cfgPristine et RÉÉCRIT par-dessus machine/mem/rom/disk/crt/dock/show*
            // avec les valeurs du lancement — les préférences de la séance, que ce
            // saveConfig venait d'enregistrer, sont perdues sans le moindre message.
            // ⚠ MAIS seulement si le saveConfig ci-dessus a réellement écrit : lancé
            // en --kiosk (g_kioskLaunched), il est un no-op — remplacer quand même la
            // référence pristine par la cfg salie pendant l'excursion bureau (F8)
            // ferait persister la machine du visiteur au premier save forcé
            // (auto-purge ROM, réaffectation manette) : l'exact contraire du gel.
            if (!g_kioskLaunched) g_cfgPristine = cfg;
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

        // Le callback ne touche pas aux coordonnées locales de cette boucle : il
        // pose une requête, puis la bascule est faite ici avant de lire un delta.
        if (g_mouseCaptureToggleReq) {
            g_mouseCaptureToggleReq = false;
            g_mouseCaptured = !g_mouseCaptured;
            glfwSetInputMode(window, GLFW_CURSOR,
                             g_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            if (g_mouseCaptured) {
                glfwGetCursorPos(window, &lastMx, &lastMy);
                if (glfwRawMouseMotionSupported())
                    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
        }

        // Bascule GUI ⇄ kiosk demandée (F8 / menus) : appliquée ICI, en tête de tour,
        // donc entre deux trames émulées — la seule fenêtre où l'instantané se recharge.
        if (g_kioskSwitchReq) {
            const bool on = (g_kioskSwitchReq == 1);
            g_kioskSwitchReq = 0;
            switchKioskMode(on);
        }


        if (g_mouseCaptured) {                  // mouvement relatif → paquet IKBD (boutons inclus)
            double mx, my; glfwGetCursorPos(window, &mx, &my);
            const int dx = int(mx - lastMx), dy = int(my - lastMy);
            if (dx || dy) {
                lastMx += dx; lastMy += dy;     // on ne consomme QUE l'entier → le reste
                                                // fractionnaire s'accumule (drags lents)
                // Souris débranchée du ST (joystick sur le port 0, ou overlay borne
                // ouvert) : on CONSOMME quand même le delta, sinon il s'accumule et
                // part en un saut géant au retour.
                if (mouseReachesSt()) {
                    const bool l = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
                    const bool r = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                    if (g_dbgMouse) std::fprintf(stderr, "[mouse] move dx=%d dy=%d L=%d R=%d\n", dx, dy, l, r);
                    machine.ikbd.mouseEvent(dx * MOUSE_X_SIGN, dy * MOUSE_Y_SIGN, l, r);
                }
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
            stjoy::compose(window, kbd, g_kbdJoyPort, g_joyDeadzone, joy0, joy1, joyRoles, g_port0Auto);
            // Port 0 occupé par un joystick (manette affectée, ou clavier qui le vise) :
            // sur un vrai ST il a pris la place de la souris — on la débranche.
            {
                int8_t asg[GLFW_JOYSTICK_LAST + 1];
                stjoy::resolveAssign(joyRoles, asg, g_port0Auto);
                bool occ = kbd && g_kbdJoyPort == 0;
                for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) if (asg[jid] == 0) occ = true;
                g_port0Joystick = occ;
            }
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
                uint8_t aux = g_kioskDiskMenu ? 0 : stjoy::composeAux(joyRoles, g_port0Auto);
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
                audio.produceFrame(machine.frameCycles(), machine.sched.now());
            }
            // Pas-à-pas INSTRUCTION : une seule instruction, ordonnanceur en lockstep
            // (pas de produceFrame — trop court, le son reste muet en pas-à-pas).
            // Les écritures PSG/DMA du pas restent horodatées sur des trames périmées :
            // on les jette (clearEvents re-synchronise aussi le miroir DMA), sinon la
            // reprise les rejouait toutes en rafale écrêtée sur UNE trame.
            if (g_dbgPaused && g_dbgStepInstr) {
                g_dbgStepInstr = false;
                machine.stepInstruction();
                machine.psg.clearEvents();
                machine.dmasnd.clearEvents();
            }
        } else {
            // 6 trames max ≈ 120 ms de retard résorbable d'un coup : un stall GUI
            // ponctuel (drag de fenêtre, rafale disque) plus court que ça se rattrape
            // SANS trou audible (le coussin de l'anneau fait ~85 ms).
            int ran = 0;
            while (clock::now() >= emuNext && ran < 6) {
#ifdef NEOST_WITH_NET
                if (hayesModem) hayesModem->poll();   // TCP entrant → file RX du MFP
#endif
                if (machine.ne2000.enabled()) machine.ne2000.poll();   // trames RX → anneau
                if (machine.isp1160.enabled()) machine.isp1160.poll(); // trame USB (ATL → done)
                // Sortie MIDI horodatée : cette trame DOIT commencer à emuNext (temps réel).
                if (midiOut.anyOpen()) midiOut.anchor(machine.sched.now(), emuNext);
                machine.runFrame();                          // une trame (timing + décodage)
                audio.produceFrame(machine.frameCycles(), machine.sched.now());   // son de la trame → anneau (push)
                // --run-frames : sortie AUTOMATIQUE après N trames émulées (chantier A8).
                // Le décompte est fait ICI, au site d'émulation nominal — le pas-à-pas du
                // débogueur (sites runFrame du mode pausé) ne compte pas, c'est voulu :
                // l'option sert un harnais, pas une session de débogage.
                if (g_runFrames > 0 && --g_runFrames == 0) {
                    if (!g_shotPath.empty()) {
                        const uint32_t* px = machine.shifter.pixels();
                        const int w = machine.shifter.width(), h = machine.shifter.height();
                        std::FILE* f = std::fopen(g_shotPath.c_str(), "wb");
                        bool ok = f != nullptr;
                        if (f) {
                            std::fprintf(f, "P6\n%d %d\n255\n", w, h);
                            for (int k = 0; k < w * h && ok; ++k) {
                                const uint32_t c = px[k];               // ARGB8888
                                const unsigned char rgb[3] = {
                                    (unsigned char)((c >> 16) & 0xFF),
                                    (unsigned char)((c >> 8)  & 0xFF),
                                    (unsigned char)( c        & 0xFF) };
                                ok = std::fwrite(rgb, 1, 3, f) == 3;
                            }
                            if (std::fclose(f) != 0) ok = false;   // disque plein : échec au flush
                        }
                        std::fprintf(stderr, ok ? "[main] shot -> %s (%dx%d)\n"
                                                : "[main] FAILED shot %s (%dx%d)\n",
                                     g_shotPath.c_str(), w, h);
                    }
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
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
            // Demandes latchées dans onKey (cf. le commentaire F8 : la scrutation
            // glfwGetKey ratait un appui bref posé/relâché entre deux tours).
            const std::string statePath = exeDir + "/../neost.state";
            if (g_saveStateReq) {
                g_saveStateReq = false;
                const bool ok = machine.saveStateFile(statePath);
                g_stateMsg = ok ? "\xef\x83\x87 State saved (F5)" : "Save failed";
                g_stateMsgFrames = 120;
                std::fprintf(stderr, "[state] save %s → %s\n", ok ? "OK" : "FAILED", statePath.c_str());
            }
            if (g_loadStateReq) {
                g_loadStateReq = false;
                const bool ok = machine.loadStateFile(statePath);
                g_stateMsg = ok ? "\xef\x80\x9e State restored (F7)" : "No state / failed";
                g_stateMsgFrames = 120;
                std::fprintf(stderr, "[state] load %s ← %s\n", ok ? "OK" : "FAILED", statePath.c_str());
            }
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

        bool reqReset = false, reqHardReset = false, reqRebuild = false;
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
                // Fiche de bouclage MIDI OUT→IN : débranchée par défaut (un vrai ST n'a
                // rien de branché ; branchée, Cubase/MROS avec MIDI Thru part en larsen).
                if (ImGui::MenuItem("MIDI loopback cable (OUT" "\xe2\x86\x92" "IN)", nullptr, &cfg.midiLoopback)) {
                    machine.midi.setLoopback(cfg.midiLoopback);
                    saveConfig(exeDir, cfg, &machine);
                }
                if (MidiOutHost::available()) {
                    if (ImGui::MenuItem("MIDI OUT " "\xe2\x86\x92" " built-in General MIDI synth", nullptr, &cfg.midiOutGm)) {
                        midiOutApply(); saveConfig(exeDir, cfg, &machine);
                    }
                    if (ImGui::MenuItem("MIDI OUT " "\xe2\x86\x92" " CoreMIDI port \"NeoST MIDI OUT\"", nullptr, &cfg.midiOutPort)) {
                        midiOutApply(); saveConfig(exeDir, cfg, &machine);
                    }
                }
                if (Mt32Synth::available()) {
                    if (ImGui::MenuItem("MIDI OUT " "\xe2\x86\x92" " Roland MT-32 / CM-32L (Munt, roms/mt32/)", nullptr, &cfg.midiOutMt32)) {
                        midiOutApply(); saveConfig(exeDir, cfg, &machine);
                    }
                } else {
                    ImGui::MenuItem("MIDI OUT " "\xe2\x86\x92" " Roland MT-32 (needs libmt32emu at build)", nullptr, false, false);
                }
                if (ImGui::MenuItem(ICON_FA_REDO " Reset"))            reqReset = true;
                if (ImGui::MenuItem(ICON_FA_POWER_OFF " Hard Reset"))  reqHardReset = true;
                ImGui::Separator();
                ImGui::MenuItem(ICON_FA_SAVE " Floppies…", nullptr, &g_showFloppy);
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
            // Fenêtres indépendantes et outils d'inspection.
            if (ImGui::BeginMenu(ICON_FA_CLONE " Windows")) {
                ImGui::MenuItem(ICON_FA_SAVE " Floppies",       nullptr, &g_showFloppy);
                ImGui::MenuItem(ICON_FA_MEMORY " Memory (hex)", nullptr, &g_showHex);
                ImGui::MenuItem(ICON_FA_MICROCHIP " CPU 68000",  nullptr, &g_showCpu);
                ImGui::MenuItem(ICON_FA_GAMEPAD " Joystick",     nullptr, &g_showJoy);
                ImGui::MenuItem(ICON_FA_KEYBOARD " Keyboard",     nullptr, &g_showKbd);
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
                    { "F8",  "kiosk mode (toggle) — also Ctrl+Alt+F" },
                    { "F11", "keyboard joystick emulation" },
                    // F12 n'a JAMAIS eu de handler bureau (historique vérifié) : il
                    // n'existe qu'en kiosque — documenter la réalité, pas l'intention.
                    { "F12", "kiosk: keyboard & mouse overlay" },
                    { "MMB", "capture/release the mouse (middle button)" },
                    { "Ctrl+Alt+G", "capture/release the mouse (keyboard fallback)" },
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
        if (IconButton(ICON_FA_SAVE, "Floppies")) g_showFloppy = !g_showFloppy;
        ImGui::SameLine();
        if (IconButton(ICON_FA_COG, "Configuration")) g_showCfg = !g_showCfg;
        ImGui::SameLine();
        // Accès direct à la borne : le menu Machine et F8 restent disponibles, mais
        // la bascule principale ne doit pas être enfouie dans un sous-menu.
        if (IconButton(ICON_FA_DESKTOP, "Switch to kiosk mode (F8)"))
            g_kioskSwitchReq = 1;
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
            // page == -1 ouvre la fenêtre Floppies ; les autres valeurs ouvrent
            // la page correspondante de Configuration.
            auto seg = [&](const std::string& text, int page, const char* tip, bool warn = false) {
                if (!first) { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); }
                first = false;
                if (warn) ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f), "%s", text.c_str());
                else      ImGui::TextUnformatted(text.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n(click: open %s)", tip,
                                      page < 0 ? "Floppies" : "the configuration");
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (page < 0) g_showFloppy = true;
                        else { g_showCfg = true; g_cfgPage = page; }
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
            // 71 Hz = mono haute résolution, ni NTSC ni un problème : l'avertissement
            // orange ne vise que le 60 Hz (déchirement des démos européennes).
            std::snprintf(hzbuf, sizeof hzbuf, "%d Hz %s",
                          hz, hz == 60 ? "NTSC" : hz >= 70 ? "mono" : "PAL");
            seg(hzbuf, kCfgRom, "Scan rate (set by the ROM)", hz == 60);
            seg("A: " + shortName(machine.fdc.mountedPath(0)), -1, "Floppy drive A");
            seg("B: " + shortName(machine.fdc.mountedPath(1)), -1, "Floppy drive B");
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
        drawStScreen(screen, g_mouseCaptured, menuH + toolH, kTop, kH, kW);

        // État commun aux deux fenêtres. Floppies reste pleinement fonctionnelle
        // lorsque Configuration est fermée.
        cfgUi.cfg     = &cfg;
        cfgUi.machine = &machine;
        cfgUi.color   = color;
        cfgUi.volume  = audio.masterVolume();
        cfgUi.driveSound = driveSoundOn;
        cfgUi.driveSoundAvail = driveSoundAvail;
        cfgUi.curGemdos = cfg.gemdos.empty() ? std::string() : resolvePath(cfg.gemdos);
        cfgUi.curAcsi   = cfg.acsi.empty()   ? std::string() : resolvePath(cfg.acsi);
        cfgUi.curSd2    = cfg.sd2.empty()    ? std::string() : resolvePath(cfg.sd2);
        cfgUi.mt32Status = mt32.isOpen() ? (mt32.model() + " running") : mt32.lastError();
        if (!cfgUi.mixInit) {            // sème les faders depuis la config (une fois)
            cfgUi.mixYm = cfg.mixYm; cfgUi.mixDma = cfg.mixDma; cfgUi.mixDac = cfg.mixDac;
            cfgUi.mixDrive = cfg.mixDrive; cfgUi.mixMt32 = cfg.mixMt32; cfgUi.mixInit = true;
        }

        if (g_showFloppy) drawFloppyWindow(cfgUi);
        if (g_showCfg) {
            // La fenêtre ne monte/démonte/redémarre rien : elle remplit `cfgUi`, qu'on
            // déverse dans les requêtes de la boucle juste après. Les chemins de disque
            // dur sont lus dans `cfg` (tenu à jour par les montages) — ni GemdosHd ni
            // Acsi n'exposent le leur.
            drawConfigWindow(cfgUi);

            // Déversement des requêtes de la fenêtre dans celles de la boucle.
            if (!cfgUi.reqMountCart.empty())   { reqMountCart   = cfgUi.reqMountCart;   cfgUi.reqMountCart.clear(); }
            if (!cfgUi.reqMountGemdos.empty()) { reqMountGemdos = cfgUi.reqMountGemdos; cfgUi.reqMountGemdos.clear(); }
            if (!cfgUi.reqMountAcsi.empty())   { reqMountAcsi   = cfgUi.reqMountAcsi;   cfgUi.reqMountAcsi.clear(); }
            if (cfgUi.reqEjectCart)   { reqEjectCart   = true; cfgUi.reqEjectCart = false; }
            if (cfgUi.reqEjectGemdos) { reqEjectGemdos = true; cfgUi.reqEjectGemdos = false; }
            if (cfgUi.reqEjectAcsi)   { reqEjectAcsi   = true; cfgUi.reqEjectAcsi = false; }
            // UltraSatan : bascule + slot 2, appliqués ici (même discipline qu'EtherNEC :
            // le TOS ne sonde le bus ACSI qu'au boot → hard reset).
            if (cfgUi.reqUltraSatan >= 0) {
                cfg.ultrasatan = (cfgUi.reqUltraSatan == 1);
                usatanApply();
                saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;
                cfgUi.reqUltraSatan = -1;
            }
            if (!cfgUi.reqMountSd2.empty()) {
                if (machine.fdc.mountAcsi(resolvePath(cfgUi.reqMountSd2), 1)) {
                    cfg.sd2 = cfgUi.reqMountSd2; saveConfig(exeDir, cfg, &machine);
                    reqHardReset = true;
                } else {
                    g_stateMsg = "Unreadable SD image"; g_stateMsgFrames = 120;
                }
                cfgUi.reqMountSd2.clear();
            }
            if (cfgUi.reqEjectSd2) {
                cfg.sd2.clear();
                // Pas d'éjection d'une seule cible dans Acsi : on vide tout et on remonte
                // ce que la config garde (image ACSI du slot 1).
                machine.fdc.unmountAcsi();
                if (!cfg.acsi.empty()) machine.fdc.mountAcsi(resolvePath(cfg.acsi));
                usatanApply();
                saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;
                cfgUi.reqEjectSd2 = false;
            }
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
            if (cfgUi.reqModem >= 0) {
                cfg.modem = (cfgUi.reqModem == 1);
#ifdef NEOST_WITH_NET
                modemApply(cfg.modem);
#else
                g_stateMsg = "This build has no network backend";
                g_stateMsgFrames = 120;
                cfg.modem = false;
#endif
                saveConfig(exeDir, cfg, &machine);
                cfgUi.reqModem = -1;
            }
            if (cfgUi.reqEther >= 0) {
                cfg.ethernec = (cfgUi.reqEther == 1);
                etherApply(cfg.ethernec);        // etherApply peut refuser (cartouche)
                saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;             // le pilote sonde la carte au boot
                cfgUi.reqEther = -1;
            }
            if (cfgUi.mixDirty) {
                cfg.mixYm = cfgUi.mixYm; cfg.mixDma = cfgUi.mixDma; cfg.mixDac = cfgUi.mixDac;
                cfg.mixDrive = cfgUi.mixDrive; cfg.mixMt32 = cfgUi.mixMt32;
                audio.setMixGains(cfg.mixYm, cfg.mixDma, cfg.mixDrive, cfg.mixMt32);
                machine.psg.setPortBDacGain(cfg.mixDac);
                cfgUi.mixDirty = false;
            }
            if (cfgUi.mixDone) { saveConfig(exeDir, cfg, &machine); cfgUi.mixDone = false; }
            if (cfgUi.reqMidiOutGm >= 0)   { cfg.midiOutGm   = cfgUi.reqMidiOutGm   == 1; cfgUi.reqMidiOutGm   = -1; midiOutApply(); saveConfig(exeDir, cfg, &machine); }
            if (cfgUi.reqMidiOutPort >= 0) { cfg.midiOutPort = cfgUi.reqMidiOutPort == 1; cfgUi.reqMidiOutPort = -1; midiOutApply(); saveConfig(exeDir, cfg, &machine); }
            if (cfgUi.reqMidiOutMt32 >= 0) { cfg.midiOutMt32 = cfgUi.reqMidiOutMt32 == 1; cfgUi.reqMidiOutMt32 = -1; midiOutApply(); saveConfig(exeDir, cfg, &machine); }
            if (cfgUi.reqMidiLoopback >= 0) { cfg.midiLoopback = cfgUi.reqMidiLoopback == 1;
                                              cfgUi.reqMidiLoopback = -1;
                                              machine.midi.setLoopback(cfg.midiLoopback);
                                              saveConfig(exeDir, cfg, &machine); }
            if (cfgUi.reqPlugPort >= 0) {
                const auto p = PortDevices::Port(cfgUi.reqPlugPort);
                const auto d = PortDevices::Device(cfgUi.reqPlugDev);
                cfgUi.reqPlugPort = cfgUi.reqPlugDev = -1;
                if (machine.plugPort(p, d)) {
                    std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
                    *slots[int(p)] = d == PortDevices::Device::None ? "" : PortDevices::id(d);
                    saveConfig(exeDir, cfg, &machine);
                }
            }
            if (cfgUi.reqPortButton) { cfgUi.reqPortButton = false; machine.pressPortButton(); }
            if (cfgUi.reqDongle >= 0) {
                static const char* const names[] = { "", "cubase2", "cubase3", "auto", "notator" };
                const int d = std::min(cfgUi.reqDongle, 4); cfgUi.reqDongle = -1;
                if (machine.setDongle(CartridgeKey::Model(d))) {
                    cfg.dongle = names[d];
                    saveConfig(exeDir, cfg, &machine);
                } else {
                    g_stateMsg = "Steinberg key needs the cartridge port free (EtherNEC/NetUSBee)";
                    g_stateMsgFrames = 150;
                }
            }
            // Panique : on la passe AUX DEUX destinations. Le MT-32 est un synthé à part
            // (il ne voit pas le flux de MidiOutHost), donc lui envoyer les mêmes
            // contrôleurs est le seul moyen de le faire taire.
            if (cfgUi.reqMidiPanic) {
                cfgUi.reqMidiPanic = false;
                midiOut.panic();
                for (int ch = 0; ch < 16; ++ch) {
                    const uint8_t st = uint8_t(0xB0 | ch);
                    for (uint8_t cc : {uint8_t(120), uint8_t(121), uint8_t(123)}) {
                        mt32.byteAt(st, 0); mt32.byteAt(cc, 0); mt32.byteAt(0, 0);
                    }
                }
                g_stateMsg = "MIDI panic: all notes off"; g_stateMsgFrames = 120;
            }
            if (cfgUi.reqMt32Model >= 0) {
                cfg.mt32Model = cfgUi.reqMt32Model == 1 ? "mt32" : cfgUi.reqMt32Model == 2 ? "cm32l" : "auto";
                cfgUi.reqMt32Model = -1;
                mt32.close();                    // rouvre avec le modèle demandé
                midiOutApply(); saveConfig(exeDir, cfg, &machine);
            }
            if (cfgUi.reqNetUsbee >= 0) {
                cfg.netusbee = (cfgUi.reqNetUsbee == 1);
                netUsbeeApply(cfg.netusbee);     // peut refuser (cartouche)
                saveConfig(exeDir, cfg, &machine);
                reqHardReset = true;
                cfgUi.reqNetUsbee = -1;
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
                cfg.joymap   = joymapSerialize(); cfg.port0 = g_port0Auto ? "auto" : "mouse";
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
                    g_port0Auto   = (cfg.port0 == "auto");   // cf. init au démarrage
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
                    // Réseau : le profil porte modem=/ethernec= mais ni applyConfig
                    // ni le rebuild ne les branchent — sans ces appels, les cases
                    // affichaient l'état du profil alors que le matériel restait
                    // celui d'avant (modem coché mais AT dans le vide…).
#ifdef NEOST_WITH_NET
                    modemApply(cfg.modem);
#endif
                    netUsbeeApply(cfg.netusbee);
                    etherApply(cfg.ethernec);
                    // Son/MIDI du profil : sorties (GM/CoreMIDI/MT-32 + modèle), câble de
                    // bouclage, faders — rejoués ici, et la page Sound ressème ses faders.
                    mt32.close();
                    midiOutApply();
                    machine.midi.setLoopback(cfg.midiLoopback);
                    audio.setMixGains(cfg.mixYm, cfg.mixDma, cfg.mixDrive, cfg.mixMt32);
                    machine.psg.setPortBDacGain(cfg.mixDac);
                    cfgUi.mixInit = false;
                    saveConfig(exeDir, cfg, &machine);
                    reqRebuild = true;        // modèle/RAM/FPU/ROM/cartouche/HD/moniteur/FDC (+ UltraSatan)
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
        // Requêtes propres à la fenêtre Floppies, consommées indépendamment de
        // l'ouverture de Configuration et avant le traitement des montages ci-dessous.
        if (!cfgUi.reqMountA.empty()) { reqMount  = cfgUi.reqMountA; cfgUi.reqMountA.clear(); }
        if (!cfgUi.reqMountB.empty()) { reqMountB = cfgUi.reqMountB; cfgUi.reqMountB.clear(); }
        if (cfgUi.reqEjectA) { reqEject  = true; cfgUi.reqEjectA = false; }
        if (cfgUi.reqEjectB) { reqEjectB = true; cfgUi.reqEjectB = false; }

        if (g_showHex)  drawHexViewer(machine.bus);
        if (g_showCpu)  drawCpuState(machine.cpu, reqReset);
        if (g_showJoy)  drawJoystickWindow(window, g_lastJoy0, g_lastJoy1);
        if (g_showKbd)  drawKeyboardWindow(&g_showKbd, machine.ikbd,
                                           resolveData("pic/Black_Keyboard_AtariST.jpeg", exeDir));
        else            keyboardWindowReleaseAll(machine.ikbd);   // fenêtre masquée : rien d'enfoncé
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
            cfg.port0 = g_port0Auto ? "auto" : "mouse"; cfg.joymap = joymapSerialize();
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
            stjoy::resolveAssign(navRoles, navAssign, g_port0Auto);
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
                // La clé auto-branchée pour l'image PRÉCÉDENTE s'en va d'abord (cf.
                // autoDongleRetract) : elle n'a rien à faire dans le jeu suivant.
                autoDongleRetract(machine, cfg);
                // Clé du jeu (disks/dongles.txt) : on ne remplit que les emplacements VIDES.
                if (cfg.autoDongle) {
                    std::string plugged;
                    for (const auto& r : neost::matchDongleRules(loadDongleTable(), reqMount)) {
                        if (r.cart) {
                            if (machine.dongle.attached() || !machine.setDongle(r.key)) continue;
                            static const char* const kn[] = { "", "cubase2", "cubase3", "auto", "notator" };
                            cfg.dongle = kn[int(r.key)]; g_autoCartKey = r.key;
                            plugged += std::string(plugged.empty() ? "" : ", ") + "cartridge key " + cfg.dongle;
                        } else {
                            if (machine.ports.at(r.port) != PortDevices::Device::None || !machine.plugPort(r.port, r.dev)) continue;
                            std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
                            *slots[int(r.port)] = PortDevices::id(r.dev);
                            g_autoPortDev[int(r.port)] = r.dev;
                            plugged += std::string(plugged.empty() ? "" : ", ") + PortDevices::label(r.dev) + " on " + PortDevices::portId(r.port);
                        }
                    }
                    if (!plugged.empty()) {
                        g_stateMsg = "Auto-plugged: " + plugged + " (disks/dongles.txt)"; g_stateMsgFrames = 240;
                        if (!g_kiosk && !g_kioskLaunched) saveConfig(exeDir, cfg, &machine);
                    }
                }
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
            autoDongleRetract(machine, cfg);   // la clé partait avec la disquette
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
            machine.fdc.unmountAcsi();          // vide TOUTES les cibles (slot 2 compris)
            cfg.acsi.clear(); saveConfig(exeDir, cfg, &machine);
            usatanApply();                      // ré-attache l'UltraSatan + remonte le slot 2
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
    cfg.showJoy = g_showJoy; cfg.showCfg = g_showCfg; cfg.showFloppy = g_showFloppy;
    cfg.showKbd = g_showKbd;
    saveConfig(exeDir, cfg, &machine);

#if defined(NEOST_WITH_IMGUI)
    // Écrit imgui.ini avant l'arrêt → garantit la sauvegarde de la taille de fenêtre
    // (et des positions de sous-fenêtres) même si rien d'autre n'a marqué les réglages.
    if (ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    // Arrêt : plus aucun octet MIDI vers des objets en cours de destruction (sink → midiOut/mt32).
    machine.midi.setMidiSinkTimed({});
    audio.setMt32(nullptr);
    mt32.close();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
