// =============================================================================
//  StScreenView.hpp — l'écran ST à l'écran de l'hôte.
//
//  Trois choses, et rien d'autre : la texture GL qui porte le framebuffer ARGB
//  décodé par le Shifter, la passe CRT qui s'applique dessus, et les deux
//  cadrages — celui de la borne (viewport GL plein écran) et celui du bureau
//  (fenêtre ImGui). Les deux appliquent le MÊME zoom adaptatif ; ils ne diffèrent
//  que par l'endroit où ils le posent.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdint>

#include "gui/GlHeaders.hpp"

struct App;
class Machine;

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
// l'écran ST brut (s.tex) si les effets sont off, indisponibles ou si process()
// échoue — passthrough sans surprise. dstW×dstH = taille écran cible.
GLuint crtApply(App& A, const GlScreen& s, int dstW, int dstH);
// Région de CONTENU (zoom adaptatif) : le calcul vit dans core/Framing.cpp,
// PARTAGÉ avec le plein écran WASM — même règle, mêmes latches d'hystérésis.
void stContentRegion(Machine& machine, int& cTop, int& cH, int& cW);
// Rendu borne : viewport GL, contenu calé sur la hauteur de l'écran.
// `cW` = largeur de contenu (zone active, ou buffer entier si une bordure est
// ouverte). Elle manquait, et son absence AMPUTAIT l'image sur tout écran plus
// étroit que le contenu — cf. StScreenView.cpp.
void drawStKiosk(App& A, GlScreen& s, int fbw, int fbh, int cTop, int cH, int cW);
// Rendu bureau : fenêtre ImGui de BASE, même zoom adaptatif cadré en UV.
void drawStScreen(App& A, const GlScreen& s, bool captured, float topOffset,
                  int cTop, int cH, int cW);
