// =============================================================================
//  main_android.cpp — Frontend Android de NeoST (SDL2 + OpenGL ES 2).
//
//  C'est le TROISIÈME frontend, et il est calqué sur le WEB (main_web.cpp), pas
//  sur le GUI de bureau : même sortie GLES2 (le mode immédiat de main.cpp n'existe
//  pas ici), même modèle audio « push » (le son est produit PAR TRAME ÉMULÉE via
//  core/AudioMix.cpp), même cadence sur le TEMPS ÉMULÉ. La coque d'interface est
//  faite par la plateforme — HTML côté web, Android côté ici.
//
//  SDL2 fournit ce que GLFW ne sait pas faire sur Android : fenêtre, contexte
//  GLES, cycle de vie (mise en veille), tactile, manettes et sortie audio.
//
//  Données : le paquet embarque EmuTOS + une disquette dans ses `assets`, qu'on
//  déballe au PREMIER lancement dans le stockage interne de l'application. Le
//  cœur ne voit ensuite que des chemins de fichiers ordinaires.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <SDL.h>
#include <SDL_opengles2.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "android/AndroidMenu.hpp"
#include "audio/SampleRing.hpp"
#include "core/AudioMix.hpp"
#include "core/Machine.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace {

// --- Journal : passe par le logcat Android (SDL_Log), pas par stderr ---------
#define NEOLOG(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define NEOERR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)

Machine*      g_machine = nullptr;
SDL_Window*   g_window  = nullptr;
SDL_GLContext g_gl      = nullptr;

// --- Vidéo : shader GLES2, repris tel quel du frontend web -------------------
// Le framebuffer du Shifter est ARGB8888 ; téléversé en GL_RGBA/UNSIGNED_BYTE,
// les octets little-endian se lisent B,G,R,A → on rétablit l'ordre des canaux
// dans le fragment shader (.bgr) plutôt que de recopier la trame.
GLuint g_tex = 0, g_prog = 0, g_vbo = 0;
GLint  g_locPos = -1, g_locUV = -1, g_locTex = -1;
int    g_texW = 0, g_texH = 0;

const char* kVert =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

const char* kFrag =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "void main() { gl_FragColor = vec4(texture2D(uTex, vUV).bgr, 1.0); }\n";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        NEOERR("shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool initGl() {
    const GLuint vs = compile(GL_VERTEX_SHADER, kVert);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return false;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { NEOERR("program link failed"); return false; }
    g_locPos = glGetAttribLocation(g_prog, "aPos");
    g_locUV  = glGetAttribLocation(g_prog, "aUV");
    g_locTex = glGetUniformLocation(g_prog, "uTex");

    // Quad plein écran : x, y, u, v. L'image ST se lit du HAUT vers le bas, donc
    // v est inversé par rapport au repère GL.
    static const GLfloat quad[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f,
    };
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    return true;
}

void uploadFrame(const uint32_t* px, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, g_tex);
    if (w != g_texW || h != g_texH) {       // (ré)allocation au changement de résolution ST
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        g_texW = w; g_texH = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
}

// Dessine l'écran ST CENTRÉ, ratio conservé (letterbox) : un téléphone est bien
// plus large que 4/3, étirer l'image déformerait les pixels carrés du ST.
void drawScreen() {
    int fbw = 0, fbh = 0;
    SDL_GL_GetDrawableSize(g_window, &fbw, &fbh);
    glClear(GL_COLOR_BUFFER_BIT);
    if (g_texW <= 0 || g_texH <= 0 || fbw <= 0 || fbh <= 0) return;

    const float srcAspect = float(g_texW) / float(g_texH);
    const float dstAspect = float(fbw) / float(fbh);
    int vpW = fbw, vpH = fbh;
    if (dstAspect > srcAspect) vpW = int(float(fbh) * srcAspect);   // barres verticales
    else                       vpH = int(float(fbw) / srcAspect);   // barres horizontales
    glViewport((fbw - vpW) / 2, (fbh - vpH) / 2, vpW, vpH);

    glUseProgram(g_prog);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(GLuint(g_locPos));
    glVertexAttribPointer(GLuint(g_locPos), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
    glEnableVertexAttribArray(GLuint(g_locUV));
    glVertexAttribPointer(GLuint(g_locUV), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (const void*)(2 * sizeof(GLfloat)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(g_locTex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// --- Audio : modèle « push », même discipline que le GUI et le web -----------
// Le producteur (boucle d'émulation) génère le son d'UNE trame et le pousse dans
// l'anneau ; le callback SDL (thread audio) ne fait que drainer. Amorçage avant
// lecture : jouer un anneau quasi vide, c'est l'underrun permanent où seules les
// transitoires passent.
neost::FrameMixBuffers g_mixBuf;
SampleRing             g_ring{32768};        // entrelacé L/R : ~340 ms à 48 kHz
SDL_AudioDeviceID      g_audioDev  = 0;
uint32_t               g_audioRate = 0;
double                 g_sampleCarry = 0.0;
uint32_t               g_cushionFrames = 0;
bool                   g_primed = false;          // appartient au THREAD AUDIO
std::atomic<uint32_t>  g_underruns{0};            // écrit thread audio, lu principal
float                  g_masterVol = 1.0f, g_volSmooth = 1.0f;

void audioCallback(void* /*ud*/, Uint8* stream, int len) {
    float* out = reinterpret_cast<float*>(stream);
    const size_t need = size_t(len) / sizeof(float);       // floats (2 par frame)
    if (!g_primed) {
        if (g_ring.available() < size_t(g_cushionFrames) * 2) { std::memset(stream, 0, size_t(len)); return; }
        g_primed = true;
    }
    if (g_ring.pull(out, need) < need) {                    // underrun → on reconstitue le coussin
        g_primed = false;
        g_underruns.fetch_add(1, std::memory_order_relaxed);
    }
}

void produceAudioFrame() {
    Machine& m = *g_machine;
    const int64_t fc = m.frameCycles();
    if (!g_audioDev || g_audioRate == 0) {                  // pas de sortie : on DRAINE les horodatages
        neost::mixEmulatedFrame(m.psg, &m.dmasnd, false, 0, 0, fc, g_mixBuf);
        return;
    }
    static constexpr double kCpuHz = 8021248.0;
    g_sampleCarry += double(fc) * g_audioRate / kCpuHz;
    int n = int(g_sampleCarry);
    g_sampleCarry -= n;
    // Asservissement proportionnel vers le coussin (≤ 8 éch. sur ~960, soit
    // < 0,8 % de hauteur : inaudible, mais absorbe la dérive entre l'horloge du
    // périphérique audio et celle de la machine).
    int adj = (int(g_cushionFrames) - int(g_ring.available() / 2)) / 256;
    if      (adj >  8) adj =  8;
    else if (adj < -8) adj = -8;
    n += adj;
    if (n <= 0) { neost::mixEmulatedFrame(m.psg, &m.dmasnd, false, 0, 0, fc, g_mixBuf); return; }

    float* st = neost::mixEmulatedFrame(m.psg, &m.dmasnd, machineHasDmaSound(m.bus.machine),
                                        uint32_t(n), g_audioRate, fc, g_mixBuf);
    if (!st) return;
    if (g_masterVol != g_volSmooth || g_masterVol != 1.0f) {   // rampe anti-clic
        const float v0 = g_volSmooth, vt = g_masterVol;
        for (int i = 0; i < n; ++i) {
            const float v = v0 + (vt - v0) * (float(i + 1) / float(n));
            st[2 * i] *= v; st[2 * i + 1] *= v;
        }
        g_volSmooth = vt;
    }
    for (int i = 0; i < 2 * n; ++i) st[i] = std::max(-1.0f, std::min(1.0f, st[i]));
    g_ring.push(st, size_t(2 * n));

    // Diagnostic « son haché », comme le natif : un underrun isolé est bénin
    // (chargement, rotation d'écran) ; RÉPÉTÉ, c'est que l'appareil ne tient pas
    // la cadence des trames. Limité à ~1 message / 5 s.
    static uint32_t seen = 0;
    static int      mute = 0;
    if (mute > 0) --mute;
    const uint32_t u = g_underruns.load(std::memory_order_relaxed);
    if (u != seen && mute <= 0) {
        NEOERR("audio ring underrun (total %u) — emulation not keeping up", u);
        seen = u;
        mute = 250;                                   // ≈ 5 s à 50 trames/s
    }
}

bool openAudio() {
    SDL_AudioSpec want{}, have{};
    want.freq     = 48000;
    want.format   = AUDIO_F32SYS;      // on synthétise en float
    want.channels = 2;                 // STÉRÉO : DMA STE L/R + panoramique LMC1992
    want.samples  = 1024;              // ~21 ms : compromis latence / robustesse
    want.callback = audioCallback;
    g_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                                     SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_audioDev) { NEOERR("SDL_OpenAudioDevice: %s", SDL_GetError()); return false; }
    g_audioRate     = uint32_t(have.freq);
    g_cushionFrames = g_audioRate * 90 / 1000;   // 90 ms, comme le web
    SDL_PauseAudioDevice(g_audioDev, 0);
    NEOLOG("audio out: %u Hz stereo, cushion 90 ms (%u frames), buffer %u",
           g_audioRate, g_cushionFrames, have.samples);
    return true;
}

// --- Données : déballage des assets au premier lancement ---------------------
// SDL_RWops sait lire dans l'APK ; le cœur, lui, veut des fichiers ordinaires.
bool copyAsset(const std::string& name, const std::string& dest) {
    SDL_RWops* in = SDL_RWFromFile(name.c_str(), "rb");
    if (!in) { NEOERR("asset absent: %s (%s)", name.c_str(), SDL_GetError()); return false; }
    const Sint64 sz = SDL_RWsize(in);
    std::vector<uint8_t> buf(size_t(sz > 0 ? sz : 0));
    if (sz > 0) SDL_RWread(in, buf.data(), 1, buf.size());
    SDL_RWclose(in);
    SDL_RWops* out = SDL_RWFromFile(dest.c_str(), "wb");
    if (!out) { NEOERR("écriture impossible: %s", dest.c_str()); return false; }
    if (!buf.empty()) SDL_RWwrite(out, buf.data(), 1, buf.size());
    SDL_RWclose(out);
    return true;
}

bool fileExists(const std::string& p) {
    SDL_RWops* f = SDL_RWFromFile(p.c_str(), "rb");
    if (!f) return false;
    SDL_RWclose(f);
    return true;
}

// Renvoie le dossier de données de l'application (créé au besoin), assets déballés.
std::string prepareData() {
#if defined(__ANDROID__)
    const char* internal = SDL_AndroidGetInternalStoragePath();
    const std::string dir = internal ? std::string(internal) : std::string(".");
#else
    const std::string dir = ".";
#endif
    static const char* kAssets[] = {
        "etos192us.img", "etos192fr.img", "etos256us.img", "etos256fr.img", "diskA.st",
    };
    for (const char* a : kAssets) {
        const std::string dest = dir + "/" + a;
        if (!fileExists(dest)) copyAsset(a, dest);
    }
    return dir;
}

// --- Entrées tactiles --------------------------------------------------------
// Modèle v1, volontairement simple et testable :
//   · un doigt qui GLISSE  → souris relative (le bureau GEM se pilote ainsi) ;
//   · un appui BREF sans déplacement → clic gauche ;
//   · deux doigts, appui bref → clic droit ;
//   · manette physique (SDL_GameController) → joystick ST du port 1.
// Le stick virtuel viendra avec la couche d'interface — pas avant d'avoir mesuré
// ce que donne la souris relative sur un vrai appareil.
struct Touch {
    bool     active   = false;
    float    lastX = 0, lastY = 0;
    Uint64   startMs  = 0;
    float    travel   = 0.0f;      // distance parcourue (fraction d'écran)
    int      fingers  = 0;
};
Touch g_touch;

// Menu (grammaire du menu borne — cf. android/AndroidMenu.hpp) et échelle d'UI.
neost::AndroidMenu g_menu;
float              g_uiScale = 1.0f;
// Injection touche/clic de la page clavier — recette de la BORNE (main.cpp,
// g_kioskInjectHold) : MAKE (ou appui souris) MAINTENU 4 trames puis BREAK, et
// AUCUNE nouvelle injection tant que le maintien court — sinon un 2ᵉ tap < 4
// trames écrase le relâchement en attente et la touche reste « collée » côté ST.
// Le premier jet faisait le clic down+up DANS LA MÊME TRAME : un jeu qui scrute
// l'état du bouton chaque VBL pouvait ne jamais le voir.
int                g_injectHold     = 0;      // trames de maintien restantes
uint8_t            g_injectScancode = 0;      // touche à relâcher (0 = clic)
bool               g_injectClick    = false;  // le maintien est un clic souris

constexpr float kTouchSpeed   = 900.0f;   // pixels ST par unité d'écran normalisée
constexpr Uint64 kTapMaxMs    = 250;
constexpr float  kTapMaxTravel = 0.02f;

void handleTouch(const SDL_Event& e) {
    switch (e.type) {   // (non appelé quand le menu est ouvert : ImGui a la main)
    case SDL_FINGERDOWN:
        ++g_touch.fingers;
        if (!g_touch.active) {
            g_touch.active  = true;
            g_touch.lastX   = e.tfinger.x;
            g_touch.lastY   = e.tfinger.y;
            g_touch.startMs = SDL_GetTicks64();
            g_touch.travel  = 0.0f;
        }
        break;
    case SDL_FINGERMOTION: {
        if (!g_touch.active) break;
        const float dx = e.tfinger.x - g_touch.lastX;
        const float dy = e.tfinger.y - g_touch.lastY;
        g_touch.lastX = e.tfinger.x;
        g_touch.lastY = e.tfinger.y;
        g_touch.travel += SDL_fabsf(dx) + SDL_fabsf(dy);
        const int mx = int(dx * kTouchSpeed);
        const int my = int(dy * kTouchSpeed);
        if ((mx || my) && g_machine) g_machine->ikbd.mouseEvent(mx, my, false, false);
        break;
    }
    case SDL_FINGERUP: {
        const int fingers = g_touch.fingers;
        if (g_touch.fingers > 0) --g_touch.fingers;
        if (g_touch.fingers > 0) break;              // il reste un doigt : rien à conclure
        const Uint64 held = SDL_GetTicks64() - g_touch.startMs;
        const bool tap = held <= kTapMaxMs && g_touch.travel <= kTapMaxTravel;
        if (tap && g_machine) {                      // clic : appui PUIS relâchement
            const bool right = fingers >= 2;
            g_machine->ikbd.mouseEvent(0, 0, !right, right);
            g_machine->ikbd.mouseEvent(0, 0, false, false);
        }
        g_touch.active = false;
        break;
    }
    default: break;
    }
}

// Manette physique → octet IKBD (bit0 haut, 1 bas, 2 gauche, 3 droite, 7 feu).
uint8_t readPad(SDL_GameController* pad) {
    if (!pad) return 0;
    uint8_t b = 0;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= 0x01;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= 0x02;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= 0x04;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= 0x08;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))          b |= 0x80;
    // Stick analogique gauche, zone morte 30 % (anti-drift des encodeurs bon marché).
    const int dz = 32767 * 30 / 100;
    const int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    const int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -dz) b |= 0x04;
    if (ax >  dz) b |= 0x08;
    if (ay < -dz) b |= 0x01;
    if (ay >  dz) b |= 0x02;
    return b;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        NEOERR("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("NeoST", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!g_window) { NEOERR("SDL_CreateWindow: %s", SDL_GetError()); SDL_Quit(); return 1; }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) { NEOERR("SDL_GL_CreateContext: %s", SDL_GetError()); SDL_Quit(); return 1; }
    // Vsync ON : depuis que le MENU est redessiné à chaque itération, la boucle
    // tournait LIBRE (swap immédiat + Delay(1) ≈ 50 rendus par trame émulée —
    // batterie et thermique). Le swap bloquant borne le rendu au taux de l'écran ;
    // la CADENCE d'émulation, elle, reste sur le temps émulé (la boucle de
    // rattrapage exécute 0..4 trames par rendu, 50 Hz PAL sur un écran 60+ Hz).
    if (SDL_GL_SetSwapInterval(1) != 0) SDL_GL_SetSwapInterval(0);   // repli : Delay(1) borne seul
    if (!initGl()) { SDL_Quit(); return 1; }

    const std::string dataDir = prepareData();
    NEOLOG("data dir: %s", dataDir.c_str());

    // --- Dear ImGui : le menu, et RIEN d'autre ------------------------------
    // Backend SDL2 + OpenGL3 en GLSL ES 1.00 (le contexte est un ES 2). Le
    // tactile arrive en événements souris via imgui_impl_sdl2, donc les rangées
    // se tapent au doigt ; la navigation manette vient de la nav ImGui, qu'on
    // n'active QUE menu ouvert (sinon elle volerait la manette au jeu).
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;      // rien à persister sur un téléphone
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl);
    ImGui_ImplOpenGL3_Init("#version 100");
    {
        // Échelle : un téléphone a ~3 fois la densité d'un écran de bureau, et
        // ce menu se lit à bout de bras. On cale sur la largeur du drawable.
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(g_window, &dw, &dh);
        g_uiScale = std::max(1.0f, float(dw) / 640.0f);
        ImGui::GetStyle().ScaleAllSizes(g_uiScale);
        ImGui::GetIO().FontGlobalScale = g_uiScale;
    }
    g_menu.dataDir = dataDir;

    // Machine par défaut : ST 1 Mo + EmuTOS 192 Ko (le build « Atari ST »), comme
    // la démo web — c'est la machine de référence des jeux et démos de l'époque.
    static Machine machine(1024u * 1024u, CpuCore::Moira, MachineType::St);
    g_machine = &machine;
    const std::string rom  = dataDir + "/etos192us.img";
    const std::string disk = dataDir + "/diskA.st";
    if (!machine.loadTos(rom)) NEOERR("TOS introuvable: %s", rom.c_str());
    if (machine.loadDisk(disk)) g_menu.mounted = disk;
    else                        NEOLOG("pas de disquette (%s)", disk.c_str());
    machine.mfp.setColorMonitor(true);
    g_menu.refresh();

    // MODÈLE « PUSH » : sans ces deux horloges, synthesizeFrame rend un jeu de
    // registres que plus rien ne met à jour — la machine serait MUETTE.
    machine.psg.setCycleClock([] { return g_machine->frameRelCycle(); });
    machine.dmasnd.setCycleClock([] { return g_machine->frameRelCycle(); });
    machine.reset();
    openAudio();

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    // CADENCE sur le TEMPS ÉMULÉ (jamais sur l'écran) : la géométrie vidéo décide
    // (50, 60 ou 71 Hz). Un tour de boucle exécute 0, 1 ou 2 trames selon ce que
    // le temps réel réclame ; plafond à 4 pour ne pas spiraler au retour d'une
    // mise en veille.
    static constexpr double kCpuHz = 8021248.0;
    double nextMs = double(SDL_GetTicks64());
    bool running = true, paused = false;

    while (running) {
        // Le menu est OUVERT : la machine est en pause et l'interface prend
        // toutes les entrées. Fermé, c'est l'inverse — sauf la page clavier, qui
        // vit par-dessus une machine qui TOURNE (on peut donc répondre à un
        // « PRESS SPACE » sans figer le jeu).
        const bool uiHasInput = g_menu.open || g_menu.keysPage;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
                // Deux gardes, chacune pour un bug réel :
                //  · WantCaptureMouse — taper le bouton MENU envoyait AUSSI un
                //    clic gauche au ST (le tap est émis au FINGERUP, plusieurs
                //    trames après le DOWN : ImGui a eu le temps de déclarer
                //    qu'il tient le pointeur) ;
                //  · uiHasInput — un FINGERUP avalé par l'interface laissait
                //    g_touch.fingers désynchronisé, et le tap suivant passait
                //    pour un appui à deux doigts (clic droit fantôme).
                if (!uiHasInput && !ImGui::GetIO().WantCaptureMouse) handleTouch(e);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad) { SDL_GameControllerClose(pad); pad = nullptr; }
                break;
            case SDL_APP_WILLENTERBACKGROUND:
                paused = true;
                if (g_audioDev) SDL_PauseAudioDevice(g_audioDev, 1);
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                paused = false;
                // g_primed appartient au THREAD AUDIO (comme primed_ du natif) :
                // l'écrire ici serait une data race, et c'est inutile — l'anneau
                // a gardé son contenu pendant la pause du périphérique.
                nextMs = double(SDL_GetTicks64());    // horloge à resynchroniser
                // CONTEXTE GL : sur nombre d'appareils, l'EGL context est PERDU
                // en arrière-plan (SDLActivity essaie de le préserver, sans
                // garantie). Nos objets (texture, programme, VBO) seraient alors
                // des noms invalides → écran noir muet au retour. On les recrée ;
                // si le contexte a survécu, on détruit d'abord les anciens (les
                // glDelete* sur des noms morts sont silencieusement ignorés).
                glDeleteTextures(1, &g_tex);
                glDeleteProgram(g_prog);
                glDeleteBuffers(1, &g_vbo);
                g_texW = g_texH = 0;
                if (!initGl()) NEOERR("GL re-init failed after resume");
                if (g_audioDev) SDL_PauseAudioDevice(g_audioDev, 0);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                // START ouvre/ferme le menu, comme le menu borne (F9/START).
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    g_menu.keysPage = false;
                    g_menu.open = !g_menu.open;
                    if (g_menu.open) g_menu.reqRescan = true;
                }
                break;
            case SDL_KEYDOWN:
                // Retour arrière : ferme le menu s'il est ouvert, quitte sinon —
                // c'est le geste « précédent » attendu sur Android.
                if (e.key.keysym.sym == SDLK_AC_BACK) {
                    if (g_menu.open || g_menu.keysPage) { g_menu.open = g_menu.keysPage = false; }
                    else running = false;
                }
                break;
            default: break;
            }
        }
        if (uiHasInput) g_touch = Touch{};            // état tactile remis à zéro
        if (paused) { SDL_Delay(50); continue; }

        // --- Requêtes du menu (seul endroit qui touche à la machine) ---------
        if (g_menu.reqRescan) {
            g_menu.mounted = machine.fdc.mountedPath(0);
            g_menu.refresh();
            g_menu.reqRescan = false;
        }
        if (!g_menu.reqMount.empty()) {
            // Modèle « vraie machine » : on INSÈRE, on ne redémarre pas. Le
            // programme en cours continue ; c'est « RESTART » qui relance.
            if (machine.fdc.loadImage(g_menu.reqMount, 0)) g_menu.mounted = g_menu.reqMount;
            else NEOERR("image illisible: %s", g_menu.reqMount.c_str());
            g_menu.reqMount.clear();
        }
        if (g_menu.reqRestart) { machine.hardReset(); g_menu.reqRestart = false; }
        if (g_menu.reqQuit)    { running = false; g_menu.reqQuit = false; }
        // Injection : consommée SEULEMENT quand le maintien précédent est fini
        // (recette borne). La requête reste posée dans g_menu entre-temps — elle
        // est DIFFÉRÉE d'une ou deux trames, jamais perdue ni empilée.
        if (g_injectHold == 0 && g_menu.reqKeyPress >= 0) {
            const int k = g_menu.reqKeyPress;
            machine.ikbd.keyEvent(neost::kKeys[k].scancode, true);
            g_injectScancode = neost::kKeys[k].scancode;
            g_injectClick    = false;
            g_injectHold     = 4;                     // ~4 trames de maintien
            g_menu.reqKeyPress = -1;
        }
        if (g_injectHold == 0 && g_menu.reqClick) {
            const bool right = (g_menu.reqClick == 2);
            machine.ikbd.mouseEvent(0, 0, !right, right);
            g_injectClick = true;
            g_injectHold  = 4;                        // clic MAINTENU, comme la borne
            g_menu.reqClick = 0;
        }

        // Manette : au ST seulement quand l'interface ne la réclame pas.
        const uint8_t joy = uiHasInput ? 0 : readPad(pad);
        machine.ikbd.setJoystick(0, joy);             // port 1 = port « jeux »
        machine.bus.stePads.setJoystick(0, joy);
        machine.cpu.updateIpl();

        const double nowMs = double(SDL_GetTicks64());
        int ran = 0;
        if (g_menu.open) {
            // MENU OUVERT = machine EN PAUSE (modèle borne). On resynchronise
            // l'horloge en permanence : sans ça, la reprise croirait devoir
            // rattraper toutes les trames passées dans le menu.
            nextMs = nowMs;
        } else {
            while (nowMs >= nextMs && ran < 4) {
                machine.runFrame();
                produceAudioFrame();                  // le son suit la trame
                if (g_injectHold > 0 && --g_injectHold == 0) {
                    if (g_injectClick) machine.ikbd.mouseEvent(0, 0, false, false);
                    else               machine.ikbd.keyEvent(g_injectScancode, false);
                }
                nextMs += double(machine.frameCycles()) * 1000.0 / kCpuHz;
                ++ran;
            }
            if (ran == 4 && nowMs > nextMs) nextMs = nowMs;   // longue pause : resync
        }

        // On redessine même sans trame neuve : le menu, lui, doit rester vivant
        // (animations, retour tactile) au-dessus de la dernière image ST.
        uploadFrame(machine.shifter.pixels(), machine.shifter.width(), machine.shifter.height());
        drawScreen();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        // La nav manette n'est armée QUE menu ouvert : sinon ImGui capterait la
        // manette que le jeu attend.
        if (g_menu.open) ImGui::GetIO().ConfigFlags |=  ImGuiConfigFlags_NavEnableGamepad;
        else             ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        g_menu.draw(g_uiScale);
        ImGui::Render();
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(g_window, &dw, &dh);
        glViewport(0, 0, dw, dh);                     // le menu couvre TOUT l'écran,
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());   // pas la zone letterboxée

        SDL_GL_SwapWindow(g_window);
        if (ran == 0 && !g_menu.open) SDL_Delay(1);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (g_audioDev) SDL_CloseAudioDevice(g_audioDev);
    if (pad) SDL_GameControllerClose(pad);
    SDL_GL_DeleteContext(g_gl);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
