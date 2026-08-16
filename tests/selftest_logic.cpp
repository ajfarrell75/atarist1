// =============================================================================
//  selftest_logic.cpp — auto-test de la LOGIQUE PURE (pas d'émulation).
//
//  Ce que le headless ne peut pas couvrir : les fonctions sans machine ni ROM —
//  chemins hôte, parseurs, formats. Elles se testent en microsecondes, et jusqu'ici
//  elles ne se testaient nulle part : l'issue #37 (lecteur GEMDOS mort sur tous les
//  paquets Windows) tenait en UNE fonction de résolution de chemin que personne ne
//  pouvait exercer, faute d'endroit où poser dix lignes de test.
//
//  Volontairement SANS framework (le dépôt n'en a aucun, et n'en a pas besoin) :
//  un CHECK, un compteur, un code de sortie. Même forme de rapport que les
//  auto-tests du headless : « N OK, M FAIL ».
//
//  Lancer :  ./build/neost-selftest        (câblé au palier fast de run_all.py)
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/HostPath.hpp"

#include <cstdio>
#include <string>

using namespace neost;
using hostpath::Style;

static int g_ok = 0, g_fail = 0;

static void checkStr(const char* what, const std::string& got, const std::string& want) {
    if (got == want) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s = \"%s\"\n%54s attendu \"%s\"\n",
                what, got.c_str(), "", want.c_str());
}

static void checkBool(const char* what, bool got, bool want) {
    if (got == want) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s = %s (attendu %s)\n",
                what, got ? "true" : "false", want ? "true" : "false");
}

// -----------------------------------------------------------------------------
//  hostpath — sémantique WINDOWS, exercée depuis n'importe quelle plateforme.
//  C'est tout l'intérêt du paramètre Style : ces cas-là n'étaient testables sur
//  aucune machine de développement, donc ne l'étaient pas.
// -----------------------------------------------------------------------------
static void testWindowsPaths() {
    const Style W = Style::Windows;
    std::printf("hostpath (style Windows)\n");

    checkBool("isAbsolute(C:\\Temp\\atari)",  hostpath::isAbsolute("C:\\Temp\\atari", W), true);
    checkBool("isAbsolute(C:/Temp/atari)",    hostpath::isAbsolute("C:/Temp/atari", W),   true);
    checkBool("isAbsolute(c:/x) minuscule",   hostpath::isAbsolute("c:/x", W),            true);
    checkBool("isAbsolute(\\\\srv\\share) UNC", hostpath::isAbsolute("\\\\srv\\share", W), true);
    checkBool("isAbsolute(//srv/share) UNC",  hostpath::isAbsolute("//srv/share", W),     true);
    checkBool("isAbsolute(hd) relatif",       hostpath::isAbsolute("hd", W),              false);
    checkBool("isAbsolute(..\\hd) relatif",   hostpath::isAbsolute("..\\hd", W),          false);
    checkBool("isAbsolute(\"\") vide",        hostpath::isAbsolute("", W),                false);

    checkStr("normalizeSeparators(a\\b\\c)", hostpath::normalizeSeparators("a\\b\\c", W), "a/b/c");

    // LE cas de l'issue #37 : le dossier déposé est absolu, il ne doit RIEN
    // recevoir du répertoire courant.
    checkStr("lexicalAbsolute(C:\\Temp\\atari)",
             hostpath::lexicalAbsolute("C:\\Temp\\atari", "C:\\Temp\\NeoST-0.5.2", W),
             "C:/Temp/atari");
    checkStr("lexicalAbsolute(UNC)",
             hostpath::lexicalAbsolute("\\\\srv\\part\\atari", "C:\\NeoST", W),
             "//srv/part/atari");
    checkStr("lexicalAbsolute(relatif → cwd)",
             hostpath::lexicalAbsolute("hd", "C:\\Temp\\NeoST", W), "C:/Temp/NeoST/hd");
    checkStr("lexicalAbsolute(..\\hd)",
             hostpath::lexicalAbsolute("..\\hd", "C:\\Temp\\NeoST", W), "C:/Temp/hd");
    checkStr("lexicalAbsolute(D:\\Jeux\\ST\\..\\HD)",
             hostpath::lexicalAbsolute("D:\\Jeux\\ST\\..\\HD", "C:\\NeoST", W), "D:/Jeux/HD");
    checkStr("lexicalAbsolute(.\\hd)",
             hostpath::lexicalAbsolute(".\\hd", "C:\\NeoST", W), "C:/NeoST/hd");

    // Racine de lecteur : « C:/ » rabattu en « C: » désignerait le dossier COURANT
    // du lecteur C, pas sa racine — piège classique de l'API Windows.
    checkStr("stripTrailingSep(C:/)",      hostpath::stripTrailingSep("C:/", W),      "C:/");
    checkStr("stripTrailingSep(C:/Temp/)", hostpath::stripTrailingSep("C:/Temp/", W), "C:/Temp");

    checkStr("join(base, relatif)", hostpath::join("C:/NeoST", "disks/a.st", W),
             "C:/NeoST/disks/a.st");
    checkStr("join(base/, relatif)", hostpath::join("C:/NeoST/", "disks/a.st", W),
             "C:/NeoST/disks/a.st");
    // Joindre une base à un chemin DÉJÀ absolu, c'est précisément le bug #37.
    checkStr("join(base, absolu) → absolu", hostpath::join("C:/NeoST", "C:\\Temp\\x", W),
             "C:/Temp/x");
}

// -----------------------------------------------------------------------------
//  hostpath — sémantique POSIX. Vérifie surtout qu'on n'a RIEN cassé en rendant
//  le module conscient de Windows : « C:/x » y est un chemin relatif ordinaire,
//  et '\' un caractère de nom parfaitement légal.
// -----------------------------------------------------------------------------
static void testPosixPaths() {
    const Style P = Style::Posix;
    std::printf("hostpath (style POSIX)\n");

    checkBool("isAbsolute(/usr/share)",        hostpath::isAbsolute("/usr/share", P), true);
    checkBool("isAbsolute(gemdos) relatif",    hostpath::isAbsolute("gemdos", P),     false);
    checkBool("isAbsolute(C:/x) = RELATIF",    hostpath::isAbsolute("C:/x", P),       false);
    checkBool("isAbsolute(\\\\srv) = RELATIF", hostpath::isAbsolute("\\\\srv", P),    false);

    // '\' est un caractère de nom légal sous Unix : le convertir corromprait le nom.
    checkStr("normalizeSeparators(a\\b) intact", hostpath::normalizeSeparators("a\\b", P), "a\\b");

    checkStr("lexicalAbsolute(absolu)",
             hostpath::lexicalAbsolute("/srv/gemdos", "/home/u/neost", P), "/srv/gemdos");
    checkStr("lexicalAbsolute(relatif → cwd)",
             hostpath::lexicalAbsolute("gemdos", "/home/u/neost", P), "/home/u/neost/gemdos");
    checkStr("lexicalAbsolute(../gemdos)",
             hostpath::lexicalAbsolute("../gemdos", "/home/u/neost", P), "/home/u/gemdos");
    checkStr("lexicalAbsolute(./gemdos)",
             hostpath::lexicalAbsolute("./gemdos", "/home/u/neost", P), "/home/u/neost/gemdos");
    checkStr("lexicalAbsolute(a/b/../c)",
             hostpath::lexicalAbsolute("/a/b/../c", "/cwd", P), "/a/c");

    checkStr("stripTrailingSep(/tmp/)", hostpath::stripTrailingSep("/tmp/", P), "/tmp");
    checkStr("stripTrailingSep(/)",     hostpath::stripTrailingSep("/", P),     "/");

    checkStr("join(base, relatif)",  hostpath::join("/opt/neost", "roms/a.img", P),
             "/opt/neost/roms/a.img");
    checkStr("join(base, absolu)",   hostpath::join("/opt/neost", "/srv/a.img", P), "/srv/a.img");
    checkStr("join(\"\", relatif)",  hostpath::join("", "roms/a.img", P), "roms/a.img");
}

// -----------------------------------------------------------------------------
//  Le module doit se comporter comme la plateforme QUAND ON NE PRÉCISE RIEN.
// -----------------------------------------------------------------------------
static void testNativeDefaults() {
    std::printf("hostpath (défauts natifs)\n");
#if defined(_WIN32)
    checkBool("natif = Windows", hostpath::kNative == Style::Windows, true);
    checkBool("isAbsolute(C:\\x) par défaut", hostpath::isAbsolute("C:\\x"), true);
#else
    checkBool("natif = POSIX", hostpath::kNative == Style::Posix, true);
    checkBool("isAbsolute(/x) par défaut",  hostpath::isAbsolute("/x"),   true);
    checkBool("isAbsolute(C:/x) par défaut", hostpath::isAbsolute("C:/x"), false);
#endif
    // currentDir() doit rendre un chemin absolu et normalisé, sinon tout chemin
    // relatif préfixé du cwd ressort en séparateurs mélangés (bug latent Windows).
    const std::string cwd = hostpath::currentDir();
    checkBool("currentDir() non vide", !cwd.empty(), true);
    checkBool("currentDir() absolu",   hostpath::isAbsolute(cwd), true);
    checkBool("currentDir() sans '\\'", cwd.find('\\') == std::string::npos, true);
    checkStr("makeAbsolute(chemin absolu) inchangé", hostpath::makeAbsolute(cwd), cwd);
}

int main() {
    testWindowsPaths();
    testPosixPaths();
    testNativeDefaults();
    std::printf("[selftest-logic] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
