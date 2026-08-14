// NeoST — helper de compilation/link de shader GLSL minimal.
//
// Porté depuis POM2 (VERHILLE Arnaud 2026). Utilisé par CrtEffectStack pour
// construire la passe d'effets CRT sans tirer un framework de shader complet.
// API header-only ; le .cpp possède l'include GL de la plateforme et les
// chaînes de diagnostic.

#ifndef NEOST_OPENGL_SHADER_H
#define NEOST_OPENGL_SHADER_H

#include <string>

namespace neost {

// Compile + linke un programme (vertex, fragment) unique. Renvoie l'objet
// programme GL en cas de succès, 0 en cas d'échec. La ligne #version GLSL est
// préfixée automatiquement — ne passer que le corps de chaque shader. Le dialecte
// (GLSL desktop 1.20 à 1.50, ou GLSL ES 3.00) est choisi à l'exécution
// d'après GL_SHADING_LANGUAGE_VERSION puis essayé en cascade 150 → 140 → 130
// (« 300 es » sur contexte GLES/Emscripten) : les pilotes qui plafonnent à 1.40,
// comme le V3D des Raspberry Pi, sont ainsi servis. Les erreurs de
// compilation/link sont écrites dans `errorOut` (vidé en cas de succès).
unsigned int compileShaderProgram(const char* vertexBody,
                                  const char* fragmentBody,
                                  std::string* errorOut = nullptr);

// Vrai quand le contexte GL courant est GLES (Emscripten / WebGL2).
bool shaderRunningOnGLES();

} // namespace neost

#endif // NEOST_OPENGL_SHADER_H
