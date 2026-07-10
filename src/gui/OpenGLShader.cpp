// NeoST — helper de compilation/link de shader GLSL (porté de POM2).
//
// On a besoin des points d'entrée GL 2.0+ (glCreateShader, glCompileShader…)
// absents du <GL/gl.h> 1.1 de Linux/Windows. Stratégie :
//   * macOS  — <OpenGL/gl3.h> les déclare directement.
//   * Emscripten / WebGL2 — <GLES3/gl3.h> les déclare directement.
//   * Linux / Windows — typedefs PFN via <GL/glext.h> + résolution paresseuse
//                       par glfwGetProcAddress (GLFW est déjà lié partout).
//
// Autonome : évite de tirer GLEW/GLAD pour une poignée de points d'entrée.
// Sur le contexte compat legacy de NeoST (Mesa/Linux) ces symboles GL 3.x
// restent exposés — l'immediate mode et le shader coexistent.

#include "OpenGLShader.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(__EMSCRIPTEN__)
#  include <GLES3/gl3.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#  include <GLFW/glfw3.h>

namespace {
PFNGLCREATESHADERPROC      glCreateShader_      = nullptr;
PFNGLSHADERSOURCEPROC      glShaderSource_      = nullptr;
PFNGLCOMPILESHADERPROC     glCompileShader_     = nullptr;
PFNGLGETSHADERIVPROC       glGetShaderiv_       = nullptr;
PFNGLGETSHADERINFOLOGPROC  glGetShaderInfoLog_  = nullptr;
PFNGLDELETESHADERPROC      glDeleteShader_      = nullptr;
PFNGLCREATEPROGRAMPROC     glCreateProgram_     = nullptr;
PFNGLATTACHSHADERPROC      glAttachShader_      = nullptr;
PFNGLLINKPROGRAMPROC       glLinkProgram_       = nullptr;
PFNGLGETPROGRAMIVPROC      glGetProgramiv_      = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = nullptr;
PFNGLDELETEPROGRAMPROC     glDeleteProgram_     = nullptr;
PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_ = nullptr;
bool entryPointsLoaded_ = false;

bool loadEntryPoints()
{
    if (entryPointsLoaded_) return true;
    auto get = [](const char* name) {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    };
    glCreateShader_      = reinterpret_cast<PFNGLCREATESHADERPROC>     (get("glCreateShader"));
    glShaderSource_      = reinterpret_cast<PFNGLSHADERSOURCEPROC>     (get("glShaderSource"));
    glCompileShader_     = reinterpret_cast<PFNGLCOMPILESHADERPROC>    (get("glCompileShader"));
    glGetShaderiv_       = reinterpret_cast<PFNGLGETSHADERIVPROC>      (get("glGetShaderiv"));
    glGetShaderInfoLog_  = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC> (get("glGetShaderInfoLog"));
    glDeleteShader_      = reinterpret_cast<PFNGLDELETESHADERPROC>     (get("glDeleteShader"));
    glCreateProgram_     = reinterpret_cast<PFNGLCREATEPROGRAMPROC>    (get("glCreateProgram"));
    glAttachShader_      = reinterpret_cast<PFNGLATTACHSHADERPROC>     (get("glAttachShader"));
    glLinkProgram_       = reinterpret_cast<PFNGLLINKPROGRAMPROC>      (get("glLinkProgram"));
    glGetProgramiv_      = reinterpret_cast<PFNGLGETPROGRAMIVPROC>     (get("glGetProgramiv"));
    glGetProgramInfoLog_ = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(get("glGetProgramInfoLog"));
    glDeleteProgram_     = reinterpret_cast<PFNGLDELETEPROGRAMPROC>    (get("glDeleteProgram"));
    glBindAttribLocation_ = reinterpret_cast<PFNGLBINDATTRIBLOCATIONPROC>(get("glBindAttribLocation"));
    entryPointsLoaded_ =
        glCreateShader_ && glShaderSource_ && glCompileShader_ &&
        glGetShaderiv_ && glGetShaderInfoLog_ && glDeleteShader_ &&
        glCreateProgram_ && glAttachShader_ && glLinkProgram_ &&
        glGetProgramiv_ && glGetProgramInfoLog_ && glDeleteProgram_ &&
        glBindAttribLocation_;
    return entryPointsLoaded_;
}
} // namespace

#  define glCreateShader      glCreateShader_
#  define glShaderSource      glShaderSource_
#  define glCompileShader     glCompileShader_
#  define glGetShaderiv       glGetShaderiv_
#  define glGetShaderInfoLog  glGetShaderInfoLog_
#  define glDeleteShader      glDeleteShader_
#  define glCreateProgram     glCreateProgram_
#  define glAttachShader      glAttachShader_
#  define glLinkProgram       glLinkProgram_
#  define glGetProgramiv      glGetProgramiv_
#  define glGetProgramInfoLog glGetProgramInfoLog_
#  define glDeleteProgram     glDeleteProgram_
#  define glBindAttribLocation glBindAttribLocation_
#endif

namespace neost {

bool shaderRunningOnGLES()
{
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
static bool loadEntryPoints() { return true; }
#endif

static unsigned int compileOne(unsigned int kind,
                               const char* versionLine,
                               const char* precisionLine,
                               const char* body,
                               std::string* errorOut)
{
    unsigned int sh = glCreateShader(kind);
    if (!sh) {
        if (errorOut) *errorOut = "glCreateShader returned 0";
        return 0;
    }
    const char* parts[3] = { versionLine, precisionLine, body };
    glShaderSource(sh, 3, parts, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetShaderInfoLog(sh, sizeof(log) - 1, &len, log);
        std::string msg = "shader compile failed: ";
        msg.append(log, len);
        if (errorOut) *errorOut = msg;
        std::fprintf(stderr, "[CRT] %s\n", msg.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

unsigned int compileShaderProgram(const char* vertexBody,
                                  const char* fragmentBody,
                                  std::string* errorOut)
{
#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
    if (!loadEntryPoints()) {
        if (errorOut) *errorOut = "GL 3.x entry points unavailable";
        std::fprintf(stderr, "[CRT] GL 3.x entry points unavailable — "
                             "effets CRT désactivés\n");
        return 0;
    }
#endif

#if defined(__EMSCRIPTEN__)
    const char* versionLine   = "#version 300 es\n";
    const char* precisionLine = "precision highp float;\nprecision highp int;\n";
#else
    const char* versionLine   = "#version 150\n";
    const char* precisionLine = "\n";
#endif

    unsigned int vs = compileOne(GL_VERTEX_SHADER,
                                 versionLine, precisionLine, vertexBody, errorOut);
    if (!vs) return 0;
    unsigned int fs = compileOne(GL_FRAGMENT_SHADER,
                                 versionLine, precisionLine, fragmentBody, errorOut);
    if (!fs) { glDeleteShader(vs); return 0; }

    unsigned int prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (errorOut) *errorOut = "glCreateProgram returned 0";
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Épingle l'attribut position du quad plein écran sur l'emplacement 0
    // avant le link (les appelants codent glVertexAttribPointer(0, ...)).
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetProgramInfoLog(prog, sizeof(log) - 1, &len, log);
        std::string msg = "shader link failed: ";
        msg.append(log, len);
        if (errorOut) *errorOut = msg;
        std::fprintf(stderr, "[CRT] %s\n", msg.c_str());
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

} // namespace neost
