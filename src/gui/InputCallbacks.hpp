// =============================================================================
//  InputCallbacks.hpp — les callbacks GLFW : clavier, souris, erreurs.
//
//  Signature IMPOSÉE par GLFW, donc sans paramètre où passer un contexte : ces
//  fonctions-là sont les seules du frontend à prendre l'état par app() plutôt
//  qu'en argument. C'est la raison d'être de l'instance unique (cf. App.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <GLFW/glfw3.h>

// Signe des deltas souris → IKBD. Vérifié en headless (injection contrôlée) :
// paquet +dx = curseur à droite, paquet +dy = curseur vers le bas. GLFW donne
// les mêmes signes (origine en haut-gauche), donc identité sur les deux axes.
constexpr int MOUSE_X_SIGN = +1;
constexpr int MOUSE_Y_SIGN = +1;

// La souris hôte atteint-elle le ST ? Question posée par les DEUX chemins d'entrée
// souris — le mouvement (scruté par trame, dans la boucle) et les boutons (callback
// événementiel) — justement parce que leur divergence est ce qui a laissé les clics
// traverser un port 0 occupé par un joystick.
bool mouseReachesSt();

void onGlfwError(int code, const char* desc);
void onKey(GLFWwindow*, int key, int scancode, int action, int mods);
void onMouseButton(GLFWwindow* w, int button, int action, int mods);
