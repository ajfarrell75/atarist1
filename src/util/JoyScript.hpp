#pragma once
// =============================================================================
//  JoyScript.hpp — grammaire des scripts joystick datés du headless
//  (--joy-script / --joy-script-file). LOGIQUE PURE : ni machine, ni ROM, ni
//  E/S — c'est pourquoi elle vit ici et non dans main_headless.cpp, et qu'elle
//  est exercée par neost-selftest (cf. CLAUDE.md § « Tester = le headless »).
//
//  Un script compile vers UN MASQUE PAR TRAME. Bits ST (comme --joy) :
//  haut $01, bas $02, gauche $04, droite $08, feu $80.
//
//  Grammaire — sur-ensemble RÉTRO-COMPATIBLE de l'ancien « une lettre = une trame » :
//    U D L R F   une direction, ou le feu, pour une trame
//    .           neutre, une trame
//    [UF] [DL]   COMBINAISON sur UNE trame (feu + direction, diagonales).
//                L'ancien script ne posait QU'UN bit à la fois : ni tir en
//                mouvement, ni dynamite de Rick Dangerous (feu+bas). Des jeux
//                entiers restaient donc hors de portée du pilotage headless.
//    [$88]       la même trame en masque hexadécimal brut. Le préfixe $ (ou 0x)
//                est OBLIGATOIRE : sans lui « DF » est la combinaison bas+feu,
//                pas la valeur $DF — les lettres de direction sont aussi des
//                chiffres hexa, l'ambiguïté se tranche à l'écriture.
//    TOKEN*N     répète le token N fois AU TOTAL (« R*30 » = 30 trames à droite),
//                pour qu'un rollout de milliers de trames tienne sur une ligne.
//  Le total compilé est borné (kMaxFrames) : le plafond PAR TOKEN ne bornait rien,
//  « R*10000000 » répété 200 fois compilait 2 milliards de trames et 2,5 Go de RSS.
//  Espaces, tabulations et sauts de ligne ignorés ; « # » commente jusqu'à la fin
//  de la ligne (confort des scripts en fichier).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neost::joyscript {

inline uint8_t bitFor(char c) {
    switch (c) {
        case 'U': case 'u': return 0x01;
        case 'D': case 'd': return 0x02;
        case 'L': case 'l': return 0x04;
        case 'R': case 'r': return 0x08;
        case 'F': case 'f': return 0x80;
        default:            return 0x00;
    }
}

inline int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Entier hexadécimal, préfixe « $ » ou « 0x » optionnel. Refuse le vide, un
// caractère non hexa, et plus de 8 chiffres (débordement silencieux).
inline bool parseHexU32(const std::string& txt, uint32_t& out) {
    std::string t = txt;
    if (!t.empty() && t[0] == '$') t.erase(0, 1);
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.erase(0, 2);
    if (t.empty() || t.size() > 8) return false;
    uint32_t v = 0;
    for (const char c : t) {
        const int d = hexDigit(c);
        if (d < 0) return false;
        v = v * 16 + uint32_t(d);
    }
    out = v;
    return true;
}

// Compile `src` en un masque par trame. Renvoie false + `err` lisible sur script
// fautif : un script se valide AVANT de démarrer la machine, pas au bout de
// 20 000 trames de boot.
// Plafond du script COMPILÉ : 10 M de trames, soit ~55 heures de temps ST — très
// au-delà de tout usage, et 10 Mo de mémoire au pire.
inline constexpr std::size_t kMaxFrames = 10u * 1000u * 1000u;

inline bool parse(const std::string& src, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '#') { while (i + 1 < src.size() && src[i + 1] != '\n') ++i; continue; }
        uint8_t mask = 0;
        if (c == '[') {
            const std::size_t end = src.find(']', i);
            if (end == std::string::npos) { err = "unterminated '[' in the joystick script"; return false; }
            const std::string grp = src.substr(i + 1, end - i - 1);
            if (grp.empty()) { err = "empty '[]' group in the joystick script"; return false; }
            if (grp[0] == '$' || (grp.size() > 2 && grp[0] == '0' && (grp[1] == 'x' || grp[1] == 'X'))) {
                uint32_t hex = 0;
                if (!parseHexU32(grp, hex) || hex > 0xFF) {
                    err = "bad hex mask '" + grp + "' in the joystick script";
                    return false;
                }
                mask = uint8_t(hex);
            } else {
                for (const char g : grp) {
                    const uint8_t b = bitFor(g);
                    if (!b) {
                        err = std::string("unknown joystick token '") + g + "' in a '[]' group";
                        return false;
                    }
                    mask = uint8_t(mask | b);
                }
            }
            i = end;
        } else if (c == '.') {
            mask = 0;
        } else {
            mask = bitFor(c);
            if (!mask) { err = std::string("unknown joystick token '") + c + "'"; return false; }
        }
        // « TOKEN*N » : N est le nombre TOTAL de trames du token, pas un ajout.
        std::size_t count = 1;
        if (i + 1 < src.size() && src[i + 1] == '*') {
            std::size_t j = i + 2, n = 0;
            bool any = false;
            while (j < src.size() && src[j] >= '0' && src[j] <= '9') {
                n = n * 10 + std::size_t(src[j] - '0');
                if (n > kMaxFrames) { err = "repetition count too large in the joystick script"; return false; }
                any = true;
                ++j;
            }
            if (!any) { err = "'*' without a repetition count in the joystick script"; return false; }
            count = n;
            i = j - 1;
        }
        if (out.size() + count > kMaxFrames) {
            err = "joystick script longer than 10M frames";
            out.clear();
            return false;
        }
        out.insert(out.end(), count, mask);
    }
    return true;
}

}  // namespace neost::joyscript
