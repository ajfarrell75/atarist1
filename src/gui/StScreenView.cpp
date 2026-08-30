// =============================================================================
//  StScreenView.cpp — cf. StScreenView.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/StScreenView.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

#include "core/Framing.hpp"
#include "core/Machine.hpp"
#include "gui/App.hpp"
#include "gui/UiCommon.hpp"

// Applique la passe d'effets CRT si activée. Renvoie la texture à afficher :
// l'écran ST brut (s.tex) si les effets sont off, indisponibles (échec shader /
// contexte 2.1 macOS) ou si process() échoue — passthrough sans surprise.
// dstW×dstH = taille écran cible (pilote l'anti-alias analytique scanline/masque).
GLuint crtApply(App& A, const GlScreen& s, int dstW, int dstH) {
    if (!A.crtOn || s.tex == 0) return s.tex;
    if (!A.crt.available()) {
        if (A.crtInit) return s.tex;        // déjà tenté et échoué → brut
        A.crtInit = true;
        if (!A.crt.initialize()) return s.tex;
    }
    A.crt.setParams(A.crtParams);
    const GLuint out = A.crt.process(s.tex, s.w, s.h, dstW, dstH);
    return out ? out : s.tex;
}

// Région de CONTENU (zoom adaptatif) : le calcul vit dans core/Framing.cpp,
// PARTAGÉ avec le plein écran WASM — même règle, mêmes latches d'hystérésis.
// Ici on ne garde que l'adaptation de signature (Machine& → Shifter&).
void stContentRegion(Machine& machine, int& cTop, int& cH, int& cW) {
    neost::stContentRegion(machine.shifter, cTop, cH, cW);
}

// Rendu kiosk ADAPTATIF : on cale la région de contenu [cTop, cTop+cH) sur la
// HAUTEUR de l'écran (ratio pixel gardé). Contenu court → gros zoom, les bordures
// inutilisées débordent hors écran (rognées) → l'image remplit l'écran, peu de
// bandes noires. Contenu plein-cadre/overscan → tient entier (pillarbox latéral).
void drawStKiosk(App& A, GlScreen& s, int fbw, int fbh, int cTop, int cH) {
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
    const GLuint t = crtApply(A, s, std::max(1, (int)std::lround(vw)),
                                 std::max(1, (int)std::lround(vh)));
    const float cc = cTop + cH / 2.0f;                 // ligne ST au centre du contenu
    const float vy = fbh / 2.0f - vh * (1.0f - cc / s.h);       // centre le contenu à l'écran
    const float vx = (fbw - vw) / 2.0f;
    glViewport((int)std::lround(vx), (int)std::lround(vy),
               (int)std::lround(vw), (int)std::lround(vh));
    GlScreen::blitTexFullscreen(t);                    // le buffer déborde → GL rogne les bordures
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
void drawStScreen(App& A, const GlScreen& s, bool captured, float topOffset,
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
    if (A.autoZoom) {
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
    const ImTextureID id = (ImTextureID)(intptr_t)crtApply(A, s, std::max(1, fboW), std::max(1, fboH));
    ImGui::Image(id, ImVec2((float)dstW, (float)dstH), ImVec2(u0, v0), ImVec2(u1, v1));
    ImGui::End();
}
