// =============================================================================
//  GlHeaders.hpp — l'inclusion GL, au même endroit pour tout le monde.
//
//  glext.h est REQUIS hors macOS : le <GL/gl.h> livré par Windows est figé en
//  OpenGL 1.1 et ignore GL_BGRA / GL_UNSIGNED_INT_8_8_8_8_REV (GL 1.2), dont le
//  téléversement du framebuffer ARGB du Shifter a besoin. Seul l'EN-TÊTE est
//  ancien : tout pilote réel expose ces formats depuis vingt ans. Même schéma
//  que gui/CrtEffectStack.cpp et gui/OpenGLShader.cpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
