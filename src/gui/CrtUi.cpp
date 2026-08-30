// =============================================================================
//  CrtUi.cpp — cf. CrtUi.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/CrtUi.hpp"

#include <cstdio>

#include "imgui.h"

#include "gui/App.hpp"
#include "gui/UiCommon.hpp"

// Presets CRT nommés (kiosk / --crt-preset / neost.cfg). Renseigne `p` et `on`.
// Un preset n'est qu'un point de départ : le panneau de réglage peut ensuite
// tout ajuster, et les valeurs numériques figées écrasent le nom au save.
// Renvoie false si le nom est inconnu (params laissés intacts).
bool applyCrtPreset(const std::string& name, neost::CrtParams& p, bool& on) {
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

// Fenêtre de réglages des effets CRT (façade moniteur). Modifie A.crtOn /
// A.crtParams ; pose `changed`=true si l'utilisateur a touché quelque chose
// (l'appelant recopie alors dans neost.cfg et resauve). Les presets écrivent
// les mêmes champs numériques → une fois figés ils survivent au save.
// Contrôles CRT SANS fenêtre : partagés par la fenêtre flottante « Effets CRT »
// et par la page Écran de la fenêtre Configuration (proposition B) — une seule
// définition des réglages, deux endroits où les afficher.
void drawCrtControls(App& A, bool& changed) {
    if (ImGui::Checkbox("Enable CRT effects", &A.crtOn)) {
        changed = true;
        if (A.crtOn && !A.crt.available() && !A.crtInit) { A.crtInit = true; A.crt.initialize(); }
    }
    // Diagnostic : shader indisponible (ex. contexte GL 2.1 sur macOS legacy).
    if (A.crtOn && A.crtInit && !A.crt.available()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Shader unavailable:");
        ImGui::TextWrapped("%s", A.crt.lastError().c_str());
        ImGui::TextDisabled("→ ST screen shown raw (passthrough).");
    }

    ImGui::TextDisabled("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Light"))    { applyCrtPreset("light",    A.crtParams, A.crtOn); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Arcade"))   { applyCrtPreset("arcade",   A.crtParams, A.crtOn); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Phosphor")) { applyCrtPreset("phosphor", A.crtParams, A.crtOn); changed = true; }

    ImGui::Separator();
    ImGui::BeginDisabled(!A.crtOn);
    neost::CrtParams& p = A.crtParams;
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
void drawCrtSettings(App& A, bool& changed) {
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("CRT Effects", &A.showCrt);
    drawCrtControls(A, changed);
    ImGui::End();
}
