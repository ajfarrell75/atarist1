// =============================================================================
//  DockLayout.cpp — cf. DockLayout.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/DockLayout.hpp"

#include <cstdio>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"   // gestionnaire de réglages personnalisé (ImGuiSettingsHandler)

#include "gui/App.hpp"
#include "gui/UiCommon.hpp"

namespace {
App& A = app();   // les callbacks de réglages ImGui n'ont pas d'argument de contexte
}

// --- Persistance de la taille de la fenêtre PRINCIPALE (fenêtre GLFW) dans imgui.ini ---
// La fenêtre hôte n'est pas une fenêtre ImGui ; on enregistre donc sa taille via un
// gestionnaire de réglages ImGui personnalisé, qui écrit/relit une section
// « [NeoST][Window] Size=L,H » dans imgui.ini (à côté des positions des sous-fenêtres).
static void* WinSettings_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/) {
    return (void*)1;                           // une seule entrée → on accepte toujours
}
static void WinSettings_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    int w = 0, h = 0, v = 0;
    if (std::sscanf(line, "Size=%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
        A.iniWinW = w; A.iniWinH = h; A.iniWinValid = true;
    }
    else if (std::sscanf(line, "DockSeeded=%d", &v) == 1) A.dockSeeded = (v != 0);
}
static void WinSettings_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*) {
    if (!A.iniWinValid) return;
    A.winW = A.iniWinW; A.winH = A.iniWinH;    // taille à retrouver en quittant le kiosk
    // En kiosk la fenêtre APPARTIENT au moniteur (plein écran exclusif) : la
    // redimensionner changerait le mode vidéo. On garde la taille pour plus tard.
    if (!A.kiosk && A.window) glfwSetWindowSize(A.window, A.iniWinW, A.iniWinH);
}
static void WinSettings_WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
    if (!A.window) return;
    int w = A.winW, h = A.winH;                // en kiosk : la dernière taille FENÊTRÉE
    if (!A.kiosk) glfwGetWindowSize(A.window, &w, &h);
    buf->appendf("[%s][Window]\n", handler->TypeName);
    buf->appendf("Size=%d,%d\n", w, h);
    buf->appendf("DockSeeded=%d\n\n", A.dockSeeded ? 1 : 0);
}

// Enregistre le gestionnaire ci-dessus auprès d'ImGui. Le handler est STATIQUE :
// ImGui garde le pointeur qu'on lui donne pour toute la durée du contexte.
void registerWindowSettings(App&) {
    static ImGuiSettingsHandler h;
    h.TypeName   = "NeoST";
    h.TypeHash   = ImHashStr("NeoST");
    h.ReadOpenFn = WinSettings_ReadOpen;
    h.ReadLineFn = WinSettings_ReadLine;
    h.ApplyAllFn = WinSettings_ApplyAll;
    h.WriteAllFn = WinSettings_WriteAll;
    ImGui::AddSettingsHandler(&h);
}

// ─── Ancrage (docking) ──────────────────────────────────────────────────────
// Porté de POM2 (MainWindow::renderDockSpace / applyDockLayout).
//
// Sème la disposition par DÉFAUT : l'écran ST au centre, les bibliothèques à
// droite, les inspecteurs en onglets sous elles, le débogueur en bas. Reconstruit
// tout à partir de zéro (RemoveNode désancre d'abord : une fenêtre non citée ici
// finit flottante, jamais coincée dans un vieux nœud).
void applyDockLayout(App& A) {
#ifdef IMGUI_HAS_DOCK
    if (A.dockId == 0) return;
    ImGui::DockBuilderRemoveNode(A.dockId);
    ImGui::DockBuilderAddNode(A.dockId, ImGuiDockNodeFlags_DockSpace);
    // SetNodeSize compte : les ratios de découpe sont calculés sur la taille du
    // nœud — sans lui, la première découpe donne des tailles fantaisistes.
    ImGui::DockBuilderSetNodeSize(A.dockId, ImGui::GetMainViewport()->WorkSize);

    // `centre` est RELIÉ au reste après chaque découpe : on rogne les côtés
    // successivement et l'écran ST garde le milieu.
    ImGuiID centre = A.dockId;
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
    ImGui::DockBuilderFinish(A.dockId);
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
void renderDockSpace(App& A, bool visible) {
#ifdef IMGUI_HAS_DOCK
    // Mode ancré coupé (menu « Fenêtres ») : on ne soumet RIEN. Le viewport hôte de
    // DockSpaceOverViewport couvre toute la zone de travail — le laisser vivre sans
    // ancrage avalerait la souris au-dessus de l'écran ST.
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)) return;
    ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_PassthruCentralNode;
    if (!visible) flags |= ImGuiDockNodeFlags_KeepAliveOnly;
    A.dockId = ImGui::DockSpaceOverViewport(ImGui::GetID("NeoST_DockSpace"),
                                            ImGui::GetMainViewport(), flags);
    if (!visible) return;
    // 1re exécution avec l'ancrage : on sème. Conditionné à un drapeau PERSISTÉ et
    // non à « le nœud est-il vide » — DockSpaceOverViewport vient de le créer, donc
    // sa vacuité ne distingue pas « installation neuve » de « tout désancré exprès ».
    if (!A.dockSeeded) { A.dockSeeded = true; A.dockReset = true; ImGui::MarkIniSettingsDirty(); }
    if (A.dockReset)   { A.dockReset = false; applyDockLayout(A); }
#else
    (void)visible;
#endif
}
