// =============================================================================
//  StKeys — touche GLFW → scancode « make » du clavier Atari ST.
//
//  Module PARTAGÉ par les frontends GLFW (bureau src/main.cpp ET web
//  src/web/main_web.cpp — Emscripten fournit le même GLFW). A21, audit
//  2026-08-27 : cette table vivait en DEUX copies, et la copie web s'arrêtait
//  avant le pavé numérique, Undo et Help — et n'avait pas du tout le keymap
//  international (un hôte AZERTY sous TOS FR tapait en QWERTY dans le
//  navigateur). Une seule définition, deux appelants — même recette que
//  core/AudioMix et util/HostPath, pour la même raison.
//
//  Deux étages, port de Hatari sdl/keymap.c :
//   · SYMBOLIQUE d'abord (touches imprimables) : le CARACTÈRE produit par la
//     disposition HÔTE (glfwGetKeyName) → scancode ST, à travers une table par
//     défaut + des surcharges selon le PAYS du TOS chargé (setCountryFromTos,
//     pays lu dans l'en-tête ROM, mot os_conf à $1C >> 1).
//   · POSITIONNEL sinon (fonctions, flèches, pavé, modificateurs — et sur les
//     hôtes où glfwGetKeyName ne répond pas, dont certains navigateurs).
//
//  L'AUTOREPEAT n'est PAS l'affaire de ce module : l'appelant ignore les
//  GLFW_REPEAT (sur un vrai ST c'est le TOS qui répète, l'IKBD n'émet qu'un
//  make par appui, comme Hatari).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace neost::stkeys {

// Mapping POSITIONNEL seul : touche GLFW → scancode ST (0 = ignorée). Suppose
// un hôte QWERTY US ; préférer scancodeFor() qui tente d'abord le symbolique.
uint8_t positionalScancode(int glfwKey);

// Scancode ST d'une touche GLFW : symbolique (layout hôte + pays TOS chargé)
// avec repli positionnel. `scancode` est le scancode GLFW natif (2e paramètre
// du callback clavier), transmis à glfwGetKeyName.
uint8_t scancodeFor(int glfwKey, int glfwScancode);

// Lit le pays du TOS chargé dans son en-tête ROM et arme les surcharges
// symboliques. À rappeler à CHAQUE changement de ROM (le pays peut changer).
void setCountryFromTos(const std::vector<uint8_t>& rom);

// Exposés pour un futur auto-test (tables pures, sans GLFW à l'exécution) :
// premier point de code d'une chaîne UTF-8, et mapping symbolique (0xFF = pas
// de correspondance → repli positionnel).
uint32_t utf8First(const char* s);
uint8_t  symbolicForCountry(uint32_t cp);

} // namespace neost::stkeys
