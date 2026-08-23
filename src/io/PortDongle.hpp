// =============================================================================
//  PortDongle — « adaptateurs spéciaux » sur les ports joystick / série / parallèle
// =============================================================================
//
//  Les clés Steinberg (port cartouche, machines d'état PAL/EPLD) vivent dans
//  CubaseDongle. Ici : tout ce qui se branchait sur les AUTRES ports et qu'un jeu
//  ou un utilitaire allait sonder. Inventaire repris de Steem SSE 3.9+ (stports.h
//  TDongle, ior.cpp/iow.cpp/ikbd.cpp/run.cpp, SSE_DONGLE_*) et, pour les protocoles
//  d'origine Amiga, de WinUAE (dongle.cpp, Toni Wilen 2009) dont Steem s'est inspiré.
//  Hatari n'émule aucun de ces adaptateurs.
//
//  Port joystick (lu par l'IKBD — les bits sont ceux des rapports $FE/$FF) :
//   · Leader Board, 10th Frame (Access) — un cavalier relie HAUT et BAS sur le
//     port 1 : une combinaison qu'un vrai joystick ne peut pas produire. Le jeu
//     lit les deux bits à 1 (WinUAE : JOY1DAT == 0x0101).
//   · Cricket Captain (port 0), Multi Player Soccer Manager (port 0), Rugby Coach
//     (port 1) (D&H Games) — un oscillateur : le nibble direction alterne entre
//     %1100 et %1101 à chaque lecture (WinUAE : « 10 01 11 allowed, must continuously
//     change state »). Reproduit à chaque sonde de l'IKBD.
//
//  Port série (RS-232 : sorties RTS/DTR du PSG port A → entrées CTS/DCD du MFP) :
//   · B.A.T. II (Ubi Soft) — sur ST, le jeu lit CTS (GPIP2) à 0 en permanence ;
//     Steem force le bit (l'Amiga, lui, exige une impulsion DTR puis un délai).
//   · Music Master (Computer's Dream) — DTR est recopié sur DCD (GPIP1) avec un
//     RETARD : ~200 cycles après l'écriture on lit encore l'ancienne valeur (Steem,
//     « inspired by WinUAE » : first read must be zero, following reads nonzero).
//   · Jeanne d'Arc (Chip) — DCD suit les bits RTS/DTR : la ligne est assertée quand
//     le mot (RTS|DTR) décroît sans s'annuler (Steem iow.cpp : !(New && New<Old)).
//
//  Port parallèle (PSG port B = données Centronics) :
//   · Pro Sound Designer (Eidersoft) — un DAC 8 bits R-2R sur le port imprimante ;
//     Wings of Death et Lethal Xcess (Thalion) le proposent pour jouer leurs
//     samples sur un STF sans son DMA. Pas une protection : un PÉRIPHÉRIQUE, que le
//     YM2149 mixe à partir des écritures horodatées de R15 (cf. YM2149::setPortBDac).
//
//  Boutons de cartouche (ce ne sont pas des clés, mais Steem les range ici) :
//   · Multiface ST (Romantic Robot) — le bouton « freeze » tire la ligne du moniteur
//     (GPIP7) à 0 le temps de l'appui : c'est le rôle du câble qui intercepte la
//     prise moniteur. La ROM (à charger avec --cart) prend l'IRQ niveau 7.
//   · Ultimate Ripper (Gotcha) — même idée, sur la ligne RI (GPIP6) du port série.
//     Dans les deux cas l'appui est relâché à la VBL suivante (comme Steem).
//
//  Non émulés faute de relevé public : Notator/Creator et Log 3 (C-Lab/Emagic,
//  EP600 décapsulé en 2023 mais équations non publiées ; lecture dans les IRQ MIDI),
//  Pro-24 / Twenty Four (GAL16V8), Avalon, Synthworks (clé noire — même famille que
//  Cubase 2 mais équations distinctes), Zodiac (LED sur le port joystick 1),
//  Dames Grand-Maître (potentiomètres POTX/POTY — l'Amiga seulement), NeoN Grafix
//  (port LAN Falcon : simple boucle TX→RX). Cf. docs/EXTENSIONS.md.
//
//  OFF par défaut ; aucun effet sur les étalons. État volatil (un bit d'oscillateur,
//  une date) : NON sérialisé dans le save-state — le type est une CONFIG, pas un état.
// =============================================================================
#pragma once
#include <cstdint>

class Mfp;

class PortDongle {
public:
    enum class Type : uint8_t {
        None = 0,
        Bat2,          // B.A.T. II : CTS (GPIP2) forcé à 0
        MusicMaster,   // DTR → DCD (GPIP1) avec retard ~200 cycles
        JeanneDArc,    // DCD suit la décroissance de (RTS|DTR)
        LeaderBoard,   // joystick 1 : haut+bas
        TenthFrame,    // idem (même éditeur, même clé)
        Cricket,       // joystick 0 : %1100/%1101 alternés
        Rugby,         // joystick 1 : idem
        Soccer,        // joystick 0 : idem
        ProSound,      // DAC 8 bits sur le port parallèle
        Multiface,     // bouton freeze → GPIP7
        Urc,           // bouton → RI (GPIP6)
        Count
    };

    // Identifiants texte (neost.cfg `adapter=`, headless `--adapter`) et libellés GUI.
    static const char* id(Type t);
    static const char* label(Type t);
    static Type        fromId(const char* s);   // None si inconnu

    void setType(Type t) { type_ = t; reset(); }
    Type type() const { return type_; }
    bool attached() const { return type_ != Type::None; }
    bool hasButton() const { return type_ == Type::Multiface || type_ == Type::Urc; }
    bool usesPortBDac() const { return type_ == Type::ProSound; }

    void reset() { value_ = 0; timing_ = 0; pressed_ = false; }

    // --- Port série ---------------------------------------------------------
    // Écriture du port A du PSG (R14) : bits RTS (3) / DTR (4). `now` = cycle CPU.
    // Peut poser des lignes MFP (Jeanne d'Arc) ; le MFP est passé pour cela.
    void onPortA(uint8_t a, int64_t now, Mfp& mfp);
    // Lecture GPIP ($FFFA01) : recouvre les bits que la clé pilote (B.A.T. II,
    // Music Master). Appelé par le MFP avec la valeur calculée, APRÈS le DDR.
    void gpipRead(uint8_t& v, int64_t now) const;

    // --- Port joystick ------------------------------------------------------
    // Sonde IKBD : recouvre les nibbles de direction (bits 0-3) des deux ports.
    void onJoystick(uint8_t& joy0, uint8_t& joy1);

    // --- Boutons de cartouche -----------------------------------------------
    // Appui (GUI / headless) : tire la ligne correspondante, relâchée à la VBL.
    void pressButton(Mfp& mfp);
    void onVbl(Mfp& mfp);
    bool buttonPressed() const { return pressed_; }

private:
    Type     type_    = Type::None;
    uint16_t value_   = 0;     // Music Master : bit0 = DTR courant, bit1 = précédent ;
                               // Jeanne d'Arc : dernier (RTS|DTR) ; Cricket & co : nibble
    int64_t  timing_  = 0;     // Music Master : cycle de la dernière écriture DTR
    bool     pressed_ = false; // Multiface/URC : bouton enfoncé jusqu'à la VBL
};
