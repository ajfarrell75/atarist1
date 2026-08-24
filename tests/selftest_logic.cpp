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
#include "gui/AppConfig.hpp"
#include "util/HostPath.hpp"
#include "io/CartridgeKey.hpp"
#include "io/PortDevices.hpp"
#include "io/DongleTable.hpp"
#include "io/Mfp.hpp"
#include "core/YM2149.hpp"
#include "core/StateArchive.hpp"
#include "io/MidiAcia.hpp"
#include "io/Ikbd.hpp"
#include "io/Rtc.hpp"

#include <cstdio>
#include <cstring>
#include <sstream>
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

// -----------------------------------------------------------------------------
//  neost.cfg — le parseur et l'écrivain doivent se répondre CLÉ POUR CLÉ. Un
//  réglage écrit mais jamais relu (ou l'inverse) est muet : il ne casse rien au
//  démarrage, il rend juste le réglage inopérant à la session suivante. C'est
//  exactement le genre de défaut qu'aucun test d'émulation ne peut voir.
// -----------------------------------------------------------------------------
static void testConfigParser() {
    using namespace neost::appconfig;
    std::printf("neost.cfg (analyse / écriture)\n");

    // CRLF : un fichier passé par Windows, un éditeur ou un partage réseau. Le '\r'
    // collé faisait tomber CHAQUE clé sur son défaut, en silence (machine ST demandée,
    // STE démarrée) — et saveConfig réécrivait ensuite les '\r', rendant la panne
    // définitive. Le garde-fou est dans parseConfigLine ; il n'était testé nulle part.
    {
        Config c;
        parseConfigLine(c, "machine=megaste\r");
        parseConfigLine(c, "mem=4m\r");
        parseConfigLine(c, "rom=roms/tos206fr.img\r");
        checkStr("CRLF : machine",  c.machine, "megaste");
        checkStr("CRLF : mem",      c.mem,     "4m");
        checkStr("CRLF : rom",      c.rom,     "roms/tos206fr.img");
    }
    // Espaces de fin : même traitement que le '\r'.
    {
        Config c;
        parseConfigLine(c, "machine=ste   ");
        checkStr("espaces de fin", c.machine, "ste");
    }
    // Le rognage est exposé (trimConfigLine) parce que le HEADLESS lit le même
    // fichier avec son propre lecteur (--from-cfg). Tant que la règle y était
    // recopiée, elle a divergé : le headless ne retirait que le '\r', donc un
    // « machine=st » suivi d'une espace repartait en silence sur la machine par
    // défaut (STE) — le défaut même que le rognage GUI corrige. Une définition,
    // deux appelants ; ces cas verrouillent la définition.
    {
        auto trimmed = [](std::string s) { trimConfigLine(s); return s; };
        checkStr("trim : CRLF",            trimmed("machine=st\r"),     "machine=st");
        checkStr("trim : espaces",         trimmed("machine=st   "),    "machine=st");
        checkStr("trim : tabulations",     trimmed("machine=st\t\t"),   "machine=st");
        checkStr("trim : mélange CR+blancs", trimmed("mem=4m \t\r"),    "mem=4m");
        checkStr("trim : rien à retirer",  trimmed("mem=4m"),           "mem=4m");
        checkStr("trim : ligne vide",      trimmed(""),                 "");
        checkStr("trim : que des blancs",  trimmed("  \t\r"),           "");
        // Un blanc INTERNE appartient à la valeur (un chemin peut en contenir).
        checkStr("trim : blanc interne gardé",
                 trimmed("disk=disks/my game.st "), "disk=disks/my game.st");
    }
    // Valeurs hostiles bornées : une deadzone > 0.95 rendait le menu de la borne
    // incontrôlable, un volume hors [0,1] saturait la sortie.
    {
        Config c;
        parseConfigLine(c, "joydeadzone=9.5");
        checkBool("deadzone aberrante ramenée au défaut", c.joydeadzone == 0.30f, true);
        parseConfigLine(c, "joydeadzone=nan");
        checkBool("deadzone NaN ramenée au défaut",       c.joydeadzone == 0.30f, true);
        parseConfigLine(c, "volume=42");
        checkBool("volume borné à 1.0",                   c.volume == 1.0f, true);
        parseConfigLine(c, "volume=-3");
        checkBool("volume borné à 0.0",                   c.volume == 0.0f, true);
        // ⚠ NaN : toute comparaison est fausse, donc « if (v<0) … if (v>1) … » le
        // laissait passer INTACT. Un gain NaN fige les filtres du YM (hpfX1_/hpfY0_)
        // → plus aucun son jusqu'au reset, même après avoir remis le curseur.
        parseConfigLine(c, "volume=nan");
        checkBool("volume NaN ramené au défaut",          c.volume == 1.0f, true);
        // Faders du mixeur (page Sound, curseur 0..200 %) : mêmes garde-fous.
        parseConfigLine(c, "mix_ym=nan");
        checkBool("mix_ym NaN ramené au défaut",          c.mixYm == 1.0f, true);
        parseConfigLine(c, "mix_dac=nan");
        checkBool("mix_dac NaN ramené au défaut",         c.mixDac == 1.0f, true);
        parseConfigLine(c, "mix_dma=1e30");
        checkBool("mix_dma aberrant borné à 2.0",         c.mixDma == 2.0f, true);
        parseConfigLine(c, "mix_drive=-1");
        checkBool("mix_drive négatif borné à 0.0",        c.mixDrive == 0.0f, true);
        parseConfigLine(c, "mix_mt32=1.5");
        checkBool("mix_mt32 valide conservé",             c.mixMt32 == 1.5f, true);
    }
    // Une clé inconnue (fichier d'une version future) ne doit RIEN faire.
    {
        Config c;
        parseConfigLine(c, "cleQuiNExistePas=1");
        parseConfigLine(c, "");
        checkStr("clé inconnue ignorée", c.machine, Config().machine);
    }
    // ALLER-RETOUR : tout ce que writeConfigKeys écrit doit se relire à l'identique.
    // C'est le contrat qui lie les deux fonctions, et le seul moyen d'attraper une
    // clé ajoutée d'un seul côté.
    {
        Config a;
        a.rom = "roms/tos206fr.img"; a.disk = "disks/x.st"; a.diskb = "disks/y.st";
        a.cart = "carts/z.img";      a.gemdos = "/srv/gemdos"; a.acsi = "hd/c.img";
        a.machine = "megaste";       a.mem = "4m";  a.mono = true;  a.fpu = true;
        a.modem = true;              a.ethernec = true;
        a.joyport = 0;               a.joymap = "GUID1:0,GUID2:x";
        a.joydeadzone = 0.42f;       a.fastfdc = true;
        a.volume = 0.5f;             a.audioLatencyMs = 40;  a.driveSound = false;
        a.autoZoom = false;          a.crt = true;
        a.crtParams.scanlines = 0.75f;

        std::ostringstream out;
        writeConfigKeys(out, a, /*full=*/true);
        Config b;
        std::istringstream in(out.str());
        std::string line;
        while (std::getline(in, line)) parseConfigLine(b, line);

        checkStr("aller-retour rom",      b.rom,     a.rom);
        checkStr("aller-retour gemdos",   b.gemdos,  a.gemdos);
        checkStr("aller-retour machine",  b.machine, a.machine);
        checkStr("aller-retour mem",      b.mem,     a.mem);
        checkStr("aller-retour joymap",   b.joymap,  a.joymap);
        checkBool("aller-retour mono",     b.mono     == a.mono,     true);
        checkBool("aller-retour fpu",      b.fpu      == a.fpu,      true);
        checkBool("aller-retour modem",    b.modem    == a.modem,    true);
        checkBool("aller-retour ethernec", b.ethernec == a.ethernec, true);
        checkBool("aller-retour joyport",  b.joyport  == a.joyport,  true);
        checkBool("aller-retour deadzone", b.joydeadzone == a.joydeadzone, true);
        checkBool("aller-retour fastfdc",  b.fastfdc  == a.fastfdc,  true);
        checkBool("aller-retour volume",   b.volume   == a.volume,   true);
        checkBool("aller-retour latence",  b.audioLatencyMs == a.audioLatencyMs, true);
        checkBool("aller-retour drivesound", b.driveSound == a.driveSound, true);
        checkBool("aller-retour autozoom", b.autoZoom == a.autoZoom, true);
        checkBool("aller-retour crt",      b.crt      == a.crt,      true);
        checkBool("aller-retour crt_scanlines", b.crtParams.scanlines == a.crtParams.scanlines, true);
    }
    // Un PROFIL (full=false) ne doit PAS emporter l'horloge ni la disposition de
    // l'interface : charger un profil ne déplace pas les fenêtres de l'utilisateur.
    {
        Config a; a.rtc = "1,2,3,4,5,6,26"; a.rtcSaved = 12345; a.uiVersion = 7;
        a.romDirs.push_back("/srv/roms");
        std::ostringstream out;
        writeConfigKeys(out, a, /*full=*/false);
        const std::string s = out.str();
        checkBool("profil sans rtc=",          s.find("rtc=") == std::string::npos, true);
        checkBool("profil sans uiVersion=",    s.find("uiVersion=") == std::string::npos, true);
        checkBool("profil sans kiosk_romdir=", s.find("kiosk_romdir=") == std::string::npos, true);
        checkBool("profil avec machine=",      s.find("machine=") != std::string::npos, true);
    }
    // Nom de fichier de profil : il devient un chemin, donc aucun séparateur ni
    // « .. » ne doit survivre (un profil nommé « ../../etc/x » écrirait hors du
    // dossier des profils).
    {
        checkStr("profil : séparateurs retirés",   profileFileName("a/b\\c"), "abc");
        checkStr("profil : caractères Windows interdits retirés",
                 profileFileName("a:b*c?d\"e<f>g|h"), "abcdefgh");
        checkStr("profil : « ../../x » ne remonte nulle part",
                 profileFileName("../../x"), "x");
        checkStr("profil : points et espaces de tête/queue rognés",
                 profileFileName("  .mon profil.  "), "mon profil");
        checkBool("profil : nom borné à 64 caractères",
                  profileFileName(std::string(200, 'a')).size() == 64, true);
    }
    // L'horloge sérialisée doit se relire (sinon la date repart à zéro à chaque boot).
    {
        Rtc::DateTime dt{};
        checkBool("rtc= bien formée",   parseRtcConfig("1,2,3,4,5,6,26", dt), true);
        checkBool("rtc= valeurs lues",  dt.sec == 1 && dt.min == 2 && dt.hour == 3
                                     && dt.wday == 4 && dt.day == 5 && dt.month == 6
                                     && dt.year == 26, true);
        checkBool("rtc= tronquée refusée", parseRtcConfig("1,2,3", dt), false);
    }
}

// -----------------------------------------------------------------------------
//  CartridgeKey — machines d'état des clés Steinberg (logique pure, pas de bus).
//  Les valeurs épinglées ont été produites par cette implémentation (aucune séquence
//  de référence n'est publiée) : elles gardent la TRANSCRIPTION des équations à
//  l'identique, elles ne prouvent pas la clé. Les propriétés, elles, sont connues.
// -----------------------------------------------------------------------------
static void testCartridgeKey() {
    std::printf("CartridgeKey (clés Steinberg /ROM3)\n");
    {   // Clé noire : registres à 0 au reset, le motif %11011000 sur A8..A1 ramène à 0.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Cubase2);
        checkBool("cubase2 : sortie au reset = $00 (octet fort)", k.cartRead(0xFB0000, true) == 0x00, true);
        checkBool("cubase2 : octet faible = $FF", k.cartRead(0xFB0001, false) == 0xFF, true);
        k.udsCycle(0x1000);   // fetch quelconque, A8..A1 = 0 : aucun terme vrai → $FF
        checkBool("cubase2 : un cycle /UDS hors clé cadence la PAL", k.state() == 0xFF, true);
        for (uint32_t a = 1; a < 40; ++a) k.udsCycle(0x1000 + a * 2);
        k.udsCycle(0xFB01B0);   // A8..A1 = %11011000 → tous les termes vrais → 0
        checkBool("cubase2 : motif de reset logiciel → état 0", k.state() == 0, true);
        // Épinglage : 8 lectures « défi » successives A8..A1 = i*37, même défi deux fois.
        std::string got;
        for (int i = 0; i < 8; ++i) {
            const uint32_t a = 0xFB0000 | ((uint32_t(i * 37) & 0xFF) << 1);
            char b[8]; std::snprintf(b, sizeof b, "%02X", k.cartRead(a, true)); got += b;
            k.udsCycle(a);
        }
        checkStr("cubase2 : séquence épinglée", got, "00FF00FF00000000");
    }
    {   // Clé rouge : 1 bit (D8) ; D9-D15 à 1 ; horloge sur chaque accès $FBxxxx seulement.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Cubase3);
        checkBool("cubase3 : sortie au reset = $FE (D8=0, D9-15=1)", k.cartRead(0xFB0000, true) == 0xFE, true);
        const uint16_t s0 = k.state();
        k.udsCycle(0x1234); k.udsCycle(0xFB0100);
        checkBool("cubase3 : /UDS seul ne cadence pas", k.state() == s0, true);
        std::string got;
        for (int i = 0; i < 48; ++i) {
            const uint32_t a = 0xFB0000 | (((i * 7) & 1) ? 0x100u : 0u);   // A8 pseudo-aléatoire
            got += (k.cartRead(a, true) & 1) ? '1' : '0';
        }
        checkStr("cubase3 : 48 bits épinglés", got, "000011100010001000100010001000100010001000100010");
    }
    {   // Clé Notator : armement par /ROM4 à $FA00EA (STER), données par /ROM3 ensuite.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Notator);
        checkBool("notator : reset → 0, désarmée, crochet /UDS demandé", k.state() == 0 && !k.armed() && k.wantsUds(), true);
        k.udsCycle(0x1002);    // désarmée : UDS cadence les données (A1=1 → D14 ← 1)
        checkBool("notator : désarmée, UDS cadence (A1 → D14)", k.state() == 0x40, true);
        k.rom4Listen(0xFA00EA, true); k.udsCycle(0xFA00EA);   // accès d'armement : STER → 0 puis FEEDB1
        checkBool("notator : $FA00EA arme et remet à 0", k.armed() && k.state() == 0, true);
        k.udsCycle(0x1002); k.udsCycle(0x1008);
        checkBool("notator : armée, UDS sans effet", k.state() == 0, true);
        const uint8_t v = k.cartRead(0xFB0002, true);       // /ROM3 descend : horloge AVANT lecture
        checkBool("notator : lecture $FB0002 = état APRÈS horloge (D14)", v == 0x40 && k.state() == 0x40, true);
        checkBool("notator : octet faible $FF", k.cartRead(0xFB0003, false) == 0xFF, true);
        k.rom4Listen(0xFA0000, true); k.udsCycle(0xFA0000);   // tout autre accès /ROM4 désarme
        checkBool("notator : accès /ROM4 hors STER désarme", !k.armed(), true);
        k.rom4Listen(0xFA00EA, true); k.udsCycle(0xFA00EA);
        std::string got;
        for (int i = 0; i < 12; ++i) {
            const uint32_t a = 0xFB0000 | ((uint32_t(i * 53) & 0xFF) << 1);
            char b[8]; std::snprintf(b, sizeof b, "%02X", k.cartRead(a, true)); got += b;
        }
        checkStr("notator : séquence épinglée", got, "FF00FD0000400100FF000040");
        k.cartRead(0xFB00EA, true);
        checkBool("notator : $FB00EA (STER) remet à 0", k.state() == 0, true);
        k.cartRead(0xFB0002, true);                          // D14 ← 1 (A1)
        k.cartRead(0xFB0014, true);                          // A4·A2 : reset asynchrone de D9 ; A1=0
        checkBool("notator : reset asynchrone D9 sous A4·A2", (k.state() & 0x02) == 0, true);
    }
    {   // Oracle de rejeu : une trace écrite par setLog se rejoue sans écart ; une
        // trace altérée signale le premier écart (c'est ce qu'une capture matérielle
        // divergente produira).
        const char* path = "neost_selftest_key.trace";
        {
            CartridgeKey k; k.setModel(CartridgeKey::Model::Notator);
            FILE* f = std::fopen(path, "w"); k.setLog(f);
            k.udsCycle(0x1002);
            k.rom4Listen(0xFA00EA, true); k.udsCycle(0xFA00EA);
            for (int i = 0; i < 12; ++i) { const uint32_t a = 0xFB0000 | ((uint32_t(i * 53) & 0xFF) << 1); k.cartRead(a, true); k.udsCycle(a); }
            k.setLog(nullptr); std::fclose(f);
        }
        CartridgeKey k; k.setModel(CartridgeKey::Model::Notator);
        char err[128];
        checkBool("replay : trace propre → 0 écart", k.replay(path, err, sizeof err) == 0, true);
        if (FILE* f = std::fopen(path, "a")) { std::fputs("R3 00 12\n", f); std::fclose(f); }
        const int n = k.replay(path, err, sizeof err);
        checkBool("replay : ligne altérée → 1 écart localisé", n == 1 && std::strstr(err, "expected 12") != nullptr, true);
        checkBool("replay : fichier absent → -1", k.replay("/nonexistent/x.trace", err, sizeof err) == -1, true);
        std::remove(path);
    }
    {   // Auto : premier accès avec A7..A1 = 0 → rouge ; ≠ 0 → noire.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Auto);
        checkBool("auto : crochet /UDS demandé tant que non tranché", k.wantsUds(), true);
        k.cartRead(0xFB0100, true);
        checkBool("auto : A7..A1 = 0 → clé rouge", k.cartRead(0xFB0000, true) == 0xFE && !k.wantsUds(), true);
        CartridgeKey k2; k2.setModel(CartridgeKey::Model::Auto);
        k2.cartRead(0xFB00B4, true);
        checkBool("auto : A7..A1 ≠ 0 → clé noire", k2.wantsUds(), true);
    }
}

// -----------------------------------------------------------------------------
//  PortDevices — un périphérique par port (protocoles Steem SSE / WinUAE).
// -----------------------------------------------------------------------------
static void testPortDevices() {
    std::printf("PortDevices (clés joystick / série, DAC, boutons)\n");
    using PD = PortDevices;
    checkBool("ids : aller-retour sur tous les périphériques et ports", [] {
        for (int i = 0; i < int(PD::Device::Count); ++i)
            if (PD::fromId(PD::id(PD::Device(i))) != PD::Device(i)) return false;
        for (int i = 0; i < int(PD::Port::Count); ++i) { bool ok; if (PD::portFromId(PD::portId(PD::Port(i)), &ok) != PD::Port(i) || !ok) return false; }
        bool ok = true; PD::portFromId("bogus", &ok);
        return PD::fromId("bogus") == PD::Device::None && PD::fromId(nullptr) == PD::Device::None && !ok;
    }(), true);
    {   // Connecteurs : une clé joystick entre dans les deux ports DE-9, pas ailleurs.
        PD d;
        checkBool("fits : leaderboard → joy0/joy1 oui, rs232 non",
                  d.plug(PD::Port::Joy0, PD::Device::LeaderBoard) && d.plug(PD::Port::Joy1, PD::Device::LeaderBoard)
                  && !d.plug(PD::Port::Rs232, PD::Device::LeaderBoard) && !d.plug(PD::Port::Joy0, PD::Device::Bat2), true);
        checkBool("defaultPort : leaderboard → joy1, cricket → joy0, prosound → printer",
                  PD::defaultPort(PD::Device::LeaderBoard) == PD::Port::Joy1 && PD::defaultPort(PD::Device::Cricket) == PD::Port::Joy0
                  && PD::defaultPort(PD::Device::ProSound) == PD::Port::Printer, true);
        d.unplugAll(); checkBool("unplugAll", !d.any(), true);
    }
    {   // Leader Board dans le port 1 : haut+bas, port 0 intact ; dans le port 0 : l'inverse.
        PD d; d.plug(PD::Port::Joy1, PD::Device::LeaderBoard);
        uint8_t j0 = 0x80, j1 = 0x04; d.onJoystick(j0, j1);
        checkBool("leaderboard (joy1) : joy1 |= haut+bas, joy0 intact", j0 == 0x80 && j1 == 0x07, true);
        PD w; w.plug(PD::Port::Joy0, PD::Device::LeaderBoard);
        j0 = 0; j1 = 0; w.onJoystick(j0, j1);
        checkBool("leaderboard (mauvais port) : joy0 seulement", j0 == 0x03 && j1 == 0, true);
    }
    {   // Cricket + Leader Board coexistent ; oscillateur indépendant par port.
        PD d; d.plug(PD::Port::Joy0, PD::Device::Cricket); d.plug(PD::Port::Joy1, PD::Device::Rugby);
        uint8_t j0 = 0, j1 = 0; d.onJoystick(j0, j1); const uint8_t a0 = j0, a1 = j1;
        j0 = j1 = 0; d.onJoystick(j0, j1);
        checkBool("cricket/rugby : oscillent entre $C et $D sur leur port", a0 + j0 == 0xC + 0xD && a1 + j1 == 0xC + 0xD, true);
    }
    {   // B.A.T. II : CTS (bit2) forcé à 0, le reste intact.
        PD d; d.plug(PD::Port::Rs232, PD::Device::Bat2);
        uint8_t v = 0xFF; d.gpipRead(v, 0);
        checkBool("bat2 : GPIP bit2 (CTS) = 0", v == 0xFB && d.hasSerial(), true);
    }
    {   // Music Master : DCD (bit1) suit DTR avec 200 cycles de retard.
        Mfp mfp; PD d; d.plug(PD::Port::Rs232, PD::Device::MusicMaster);
        d.onPortA(0x10, 1000, mfp);
        uint8_t v = 0xFD; d.gpipRead(v, 1100); const bool early = (v & 0x02) == 0;
        v = 0xFD; d.gpipRead(v, 1300);         const bool late  = (v & 0x02) != 0;
        d.onPortA(0x00, 2000, mfp);
        v = 0xFF; d.gpipRead(v, 2100); const bool early2 = (v & 0x02) != 0;
        v = 0xFF; d.gpipRead(v, 2300); const bool late2  = (v & 0x02) == 0;
        checkBool("musicmaster : DTR → DCD retardé de 200 cycles", early && late && early2 && late2, true);
    }
    {   // Jeanne d'Arc : DCD assertée quand (RTS|DTR) décroît sans s'annuler.
        Mfp mfp; PD d; d.plug(PD::Port::Rs232, PD::Device::JeanneDArc);
        d.onPortA(0x18, 0, mfp); const bool a = (mfp.read8(0xFFFA01) & 0x02) != 0;
        d.onPortA(0x08, 0, mfp); const bool b = (mfp.read8(0xFFFA01) & 0x02) == 0;
        d.onPortA(0x00, 0, mfp); const bool c = (mfp.read8(0xFFFA01) & 0x02) != 0;
        checkBool("jeannedarc : DCD = !(new && new < old)", a && b && c, true);
    }
    {   // Multiface : bouton → GPIP7 à 0 jusqu'à la VBL, quel que soit le moniteur.
        Mfp mfp; mfp.setColorMonitor(true);
        PD d; d.plug(PD::Port::CartButton, PD::Device::Multiface);
        const bool before = (mfp.read8(0xFFFA01) & 0x80) != 0;
        d.pressButton(mfp);
        const bool during = (mfp.read8(0xFFFA01) & 0x80) == 0 && d.buttonPressed();
        d.onVbl(mfp);
        const bool after = (mfp.read8(0xFFFA01) & 0x80) != 0 && !d.buttonPressed();
        checkBool("multiface : GPIP7 bas pendant l'appui, relâché à la VBL", before && during && after, true);
        PD n; n.plug(PD::Port::Rs232, PD::Device::Bat2); n.pressButton(mfp);
        checkBool("bouton : sans effet sans bouton branché", !n.buttonPressed(), true);
        checkBool("reset : relâche le bouton, garde le périphérique", [&] { d.pressButton(mfp); d.reset(); return !d.buttonPressed() && d.hasButton(); }(), true);
    }
}

// -----------------------------------------------------------------------------
//  DongleTable — disks/dongles.txt (motif → branchement).
// -----------------------------------------------------------------------------
static void testDongleTable() {
    std::printf("DongleTable (disks/dongles.txt)\n");
    int bad = 0;
    const auto rules = neost::parseDongleTable(neost::defaultDongleTable(), &bad);
    checkBool("table livrée : toutes les lignes valides", bad == 0 && rules.size() >= 15, true);
    const auto hits = neost::matchDongleRules(rules, "disks/st/Leader Board (1986)(Access)[cr].st");
    checkBool("leader board → joy1:leaderboard", hits.size() == 1 && !hits[0].cart
              && hits[0].port == PortDevices::Port::Joy1 && hits[0].dev == PortDevices::Device::LeaderBoard, true);
    const auto n = neost::matchDongleRules(rules, "/x/NOTATOR_SL_3.21.msa");
    checkBool("NOTATOR (majuscules) → cart:notator", n.size() == 1 && n[0].cart && n[0].key == CartridgeKey::Model::Notator, true);
    checkBool("sans correspondance → vide", neost::matchDongleRules(rules, "disks/diskA.st").empty(), true);
    const auto r2 = neost::parseDongleTable("# c\n  foo = joy0:leaderboard # ok\nbar = rs232:leaderboard\nbaz\nqux = cart:nope\n", &bad);
    checkBool("analyse : commentaires, espaces, lignes invalides comptées", r2.size() == 1 && bad == 3 && r2[0].pattern == "foo", true);
}

// -----------------------------------------------------------------------------
//  YM2149 — file d'écritures horodatées : le DOMAINE des registres empilés par
//  write8 et la garde qui le vérifie au chargement doivent coïncider. Ni machine
//  ni ROM : un YM2149 nu et une StateArchive suffisent.
// -----------------------------------------------------------------------------
static void testYmEventDomain() {
    std::printf("YM2149 (domaine des écritures horodatées)\n");
    // Aller-retour d'un événement portant `reg`, comme le fait Machine::saveState.
    auto roundTrip = [](uint8_t reg) {
        YM2149 psg;
        psg.reset();
        psg.setCycleClock([] { return int64_t(0); });   // active le modèle « push »
        if (reg == 15) psg.setPortBDac(true);           // DAC Pro Sound branché
        psg.write8(0xFF8800, reg);
        psg.write8(0xFF8802, 0x40);
        std::vector<uint8_t> buf;
        { StateArchive s = StateArchive::saver(buf); psg.serialize(s); }
        YM2149 back;
        StateArchive l = StateArchive::loader(buf.data(), buf.size());
        back.serialize(l);
        return l.ok();
    };
    checkBool("R13 (enveloppe) : etat accepte", roundTrip(13), true);
    // ⚠ Régression : le DAC Pro Sound a élargi le push de write8 à R15, mais la
    // garde de relecture exigeait encore reg < 14 — un save-state LÉGITIME pris avec
    // une écriture R15 en file était refusé au chargement (« invariant rejected »).
    checkBool("R15 sous DAC Pro Sound : etat accepte", roundTrip(15), true);
}

// -----------------------------------------------------------------------------
//  ACIA MIDI 6850 — TDRE (« émetteur prêt ») doit tomber à CHAQUE écriture de
//  données, que l'IRQ d'émission soit armée ou non : c'est le seul frein d'un
//  pilote qui SCRUTE le statut. Ni machine ni ROM : un Mfp, un Scheduler, une ACIA.
// -----------------------------------------------------------------------------
static void testMidiTdre() {
    std::printf("ACIA MIDI (TDRE, frein de l'émetteur)\n");
    Mfp mfp; Scheduler sched; MidiAcia midi(mfp);
    midi.setScheduler(&sched);
    constexpr uint32_t kCtrl = 0xFFFC04, kData = 0xFFFC06;
    auto tdre = [&] { return (midi.read8(kCtrl) >> 1) & 1; };

    // `cr` = valeur du registre de contrôle (bits 5-6 = 01 → IRQ d'émission armée).
    auto ecritUnOctet = [&](uint8_t cr) {
        midi.reset();
        midi.write8(kCtrl, 0x03);            // master reset
        midi.write8(kCtrl, cr);
        midi.write8(kData, 0x90);
        return tdre();
    };
    checkBool("TIE armé : TDRE tombe",   ecritUnOctet(0x20) == 0, true);
    // ⚠ Régression : TDRE n'était modélisé QUE sous TIE. Un pilote scrutant le
    // statut (le cas de Notator, cf. Hatari midi.c:243) le voyait éternellement à
    // 1 et émettait sans jamais attendre — 1000 octets collés au même cycle.
    checkBool("TIE coupé : TDRE tombe aussi", ecritUnOctet(0x00) == 0, true);
    checkBool("RIE seul : TDRE tombe aussi",  ecritUnOctet(0x80) == 0, true);

    // Le master reset, LUI, remet bien l'émetteur au repos.
    midi.reset(); midi.write8(kCtrl, 0x00); midi.write8(kData, 0x90);
    checkBool("émetteur occupé après écriture", tdre() == 0, true);
    midi.write8(kCtrl, 0x03);
    checkBool("master reset : émetteur au repos", tdre() == 1, true);

    // Un pilote qui scrute n'enchaîne pas les octets sans attendre.
    midi.reset(); midi.write8(kCtrl, 0x03); midi.write8(kCtrl, 0x00);
    int envoyes = 0;
    for (int i = 0; i < 50 && tdre(); ++i) { midi.write8(kData, uint8_t(i)); ++envoyes; }
    checkBool("scrutation : 1 seul octet avant l'attente", envoyes == 1, true);

    // Débordement du récepteur : le 6850 perd le NOUVEL octet et garde l'ancien
    // (acia.c, état STOP_BIT). Jeter le plus ancien faisait disparaître le STATUS
    // d'un message MIDI et laissait des octets de données orphelins.
    midi.setLoopback(true);
    midi.reset(); midi.write8(kCtrl, 0x03); midi.write8(kCtrl, 0x00);
    midi.write8(kData, 0x90); midi.write8(kData, 0x3C); midi.write8(kData, 0x40);
    const uint8_t o1 = midi.read8(kData), o2 = midi.read8(kData);
    checkBool("débordement : le status $90 survit", o1 == 0x90, true);
    checkBool("débordement : puis $3C",             o2 == 0x3C, true);
}

// -----------------------------------------------------------------------------
//  ACIA du CLAVIER — même 6850, même règle : acia.c ACIA_Write_TDR efface TDRE
//  hors de toute condition, et il sert AUSSI cette ACIA-là (acia.c:643). C'est le
//  frein de la boucle d'attente d'Ikbdws (TOS), qui scrute au lieu d'interrompre.
// -----------------------------------------------------------------------------
static void testIkbdTdre() {
    std::printf("ACIA clavier (TDRE, frein de l'émetteur)\n");
    Mfp mfp; Scheduler sched; Ikbd ikbd(mfp);
    ikbd.setScheduler(&sched);
    constexpr uint32_t kCtrl = 0xFFFC00, kData = 0xFFFC02;
    auto tdre = [&] { return (ikbd.read8(kCtrl) >> 1) & 1; };

    auto ecritUnOctet = [&](uint8_t cr) {
        ikbd.write8(kCtrl, 0x03);            // master reset
        ikbd.write8(kCtrl, cr);
        ikbd.write8(kData, 0x12);            // commande IKBD quelconque
        return tdre();
    };
    checkBool("TIE armé : TDRE tombe",        ecritUnOctet(0x20) == 0, true);
    // ⚠ Régression : TDRE n'était modélisé QUE sous TIE — la boucle « btst #1 »
    // d'Ikbdws passait sans attendre, tous les octets d'une commande au même cycle.
    checkBool("TIE coupé : TDRE tombe aussi", ecritUnOctet(0x96) == 0, true);
    ikbd.write8(kCtrl, 0x03);
    checkBool("master reset : émetteur au repos", tdre() == 1, true);
}

// -----------------------------------------------------------------------------
//  RTC RP5C15 — la seconde se compte en CYCLES. Elle doit valoir une seconde de la
//  base de temps de la MACHINE (trame × Hz), pas une constante : sinon l'horloge
//  dérive contre le reste, et un logiciel qui attend « une seconde » en comptant
//  ses trames retombe juste avant le tic. Ni machine ni ROM : une horloge factice.
// -----------------------------------------------------------------------------
static void testRtcSecond() {
    std::printf("RTC RP5C15 (une seconde = base de temps machine)\n");
    const int64_t kSec = 160256 * 50;        // PAL 50 Hz : cycles d'une trame × Hz
    int64_t clk = 0;
    Rtc rtc;
    rtc.setClock([&clk] { return clk; });
    rtc.setSecondCycles(kSec);
    Rtc::DateTime dt{};
    dt.sec = 59; dt.min = 59; dt.hour = 23; dt.wday = 6; dt.day = 31; dt.month = 12; dt.year = 99;
    rtc.setDateTime(dt);
    (void)rtc.getDateTime();                 // amorce la phase du diviseur sur clk = 0

    clk = kSec - 1;
    checkBool("juste avant la seconde : pas de tic", rtc.getDateTime().sec == 59, true);
    // ⚠ Régression : avec la constante figée d'avant (8021248 au lieu de 8012800),
    // ce tic tombait 8448 cycles TROP TARD — la cartouche Atari Field Service (test
    // L, Mega ST) attendait une seconde comptée en trames et lisait « C1 clock
    // increment error ».
    clk = kSec;
    const Rtc::DateTime a = rtc.getDateTime();
    checkBool("à la seconde pile : tic",          a.sec == 0 && a.min == 0 && a.hour == 0, true);
    checkBool("débordement calendaire complet",   a.day == 1 && a.month == 1 && a.year == 0, true);
    checkBool("jour de semaine avancé",           a.wday == 0, true);
    // Trois secondes machine = exactement trois tics (pas d'arrondi qui en perde un).
    clk = 4 * kSec;
    checkBool("3 s de plus = 3 tics", rtc.getDateTime().sec == 3, true);

    // TIMER EN = bit3 du registre MODE ($FFFC3B) : à 0 le COMPTEUR est arrêté.
    // Vérifié sur machine réelle émulée : TOS 1.02 et EmuTOS écrivent $9 puis $8 au
    // boot — ils ne basculent que le bit0 (banque) et laissent TIMER EN posé. Seule
    // la cartouche Atari Field Service le coupe, pour figer l'heure avant relecture.
    constexpr uint32_t kMode = 0xFFFC21 + 13 * 2;
    rtc.write8(kMode, 0x00);                       // compteur arrêté
    const int fige = rtc.getDateTime().sec;
    clk += 5 * kSec;
    checkBool("TIMER EN coupé : l'heure est figée", rtc.getDateTime().sec == fige, true);
    rtc.write8(kMode, 0x08);                       // compteur réarmé
    clk += kSec;
    checkBool("TIMER EN réarmé : l'heure repart", rtc.getDateTime().sec == fige + 1, true);
}

int main() {
    testDongleTable();
    testCartridgeKey();
    testPortDevices();
    testYmEventDomain();
    testMidiTdre();
    testIkbdTdre();
    testRtcSecond();
    testWindowsPaths();
    testPosixPaths();
    testNativeDefaults();
    testConfigParser();
    std::printf("[selftest-logic] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
