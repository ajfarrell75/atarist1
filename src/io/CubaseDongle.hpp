// =============================================================================
//  CubaseDongle — clés de protection Steinberg sur le port cartouche (/ROM3)
// =============================================================================
//
//  Deux clés matérielles ont réellement existé ; toutes deux vivent dans la banque
//  /ROM3 = $FB0000-$FBFFFF (le TOS ne sonde que /ROM4 = $FA0000 pour le magic de
//  cartouche : une clé est donc invisible du système, et cohabite avec le lecteur
//  GEMDOS de NeoST qui occupe $FA0000).
//
//  · Clé NOIRE (Cubase 2.01) — PAL16R8 : 8 bascules D, entrées A1-A8, sorties
//    D8-D15 (octet fort), horloge = front montant de /UDS. ⚠ /UDS est le strobe
//    d'octet fort de CHAQUE cycle bus du 68000, où qu'il aille : la PAL avance aussi
//    sur les fetchs d'opcode entre deux lectures de la clé. Le motif exact d'accès
//    du CPU fait donc partie du secret — Cubase coupe les interruptions et exécute
//    sa routine depuis une adresse fixe. Émulable seulement si le cœur a le motif
//    bus d'un vrai 68000 (Moira modélise le prefetch) : « au mieux » ici.
//  · Clé ROUGE (Cubase 3.10, Cubase Score 2.0x, Cubase Audio Falcon) — EPLD Intel
//    5C060 : 16 bascules T, UNE entrée (A8), UNE sortie (D8), horloge = front
//    montant de /ROM3, c'est-à-dire la fin de chaque accès à $FBxxxx et rien
//    d'autre. Émulable fidèlement.
//
//  Dans les deux cas, une lecture renvoie l'état COURANT des registres (ils ne
//  basculent qu'à la fin du cycle, après l'échantillonnage par le CPU) ; l'octet
//  faible lit $FF (rien sur D0-D7). Le motif d'adresse A8..A1 = %11011000 rend tous
//  les termes vrais sur la clé noire → état suivant 0 : c'est le « reset logiciel »
//  de Cubase 2. À la mise sous tension, les registres sont à 0 (datasheet 5C060 ;
//  pour la PAL, hypothèse reprise de MiSTery).
//
//  Équations : clé noire relevée par force brute sur une clé réelle (MasterOfGizmo,
//  2022, Arduino + Espresso) ; clé rouge = JED de la puce décapsulée (UnnamedVillain),
//  décompilé par troed (« No rights reserved »). Transcrites de cubase2_dongle.v /
//  cubase3_dongle.v du cœur FPGA MiSTery (gyurco). Hatari n'émule aucune clé.
//
//  OFF par défaut ; sans clé, $FBxxxx lit $FF comme avant. Aucun logiciel à clé
//  n'est livré avec NeoST (Cubase Lite n'en a pas besoin) : l'implémentation est
//  fidèle aux équations, mais n'a pas encore été confrontée à un Cubase 3.10.
// =============================================================================
#pragma once
#include <cstdint>

class StateArchive;

class CubaseDongle {
public:
    enum class Model : uint8_t { None = 0, Cubase2 = 1, Cubase3 = 2, Auto = 3 };

    void setModel(Model m) { model_ = m; reset(); }
    Model model() const { return model_; }
    bool  attached() const { return model_ != Model::None; }
    // Vrai si la clé doit voir CHAQUE cycle bus du CPU (clé noire, ou Auto non
    // encore tranché) — le Bus n'installe le crochet /UDS que dans ce cas.
    bool  wantsUds() const { return chosen_ == Model::Cubase2 || !locked_; }

    // Mise sous tension / reset : registres à 0 (cf. en-tête).
    void reset();

    // Lecture CPU dans $FB0000-$FBFFFF. `first` = premier octet de l'accès (octet
    // pair d'un mot, ou accès octet) : c'est là que l'on fait avancer la clé ROUGE
    // (/ROM3 remonte en fin de cycle, après que la valeur a été échantillonnée).
    uint8_t cartRead(uint32_t addr, bool first);

    // Fin d'un cycle bus CPU ayant activé /UDS (mot, ou octet à adresse paire) :
    // horloge de la clé NOIRE. Sans effet sur la rouge.
    void udsCycle(uint32_t addr);

    void serialize(StateArchive& ar);

    // Pour les auto-tests : état interne (8 bits clé noire ; 16 bits clé rouge).
    uint16_t state() const { return model_ == Model::Cubase2 ? d_ : r_; }

private:
    uint8_t  outputByte() const;   // D15..D8 vus par le CPU
    void     clock2(uint8_t a);    // clé noire : A8..A1 → nouvel état
    void     clock3(bool a8);      // clé rouge : A8 → nouvel état

    Model    model_  = Model::None;
    // Auto (heuristique MiSTery) : au PREMIER accès $FBxxxx après reset, A7..A1 ≠ 0
    // → clé noire (Cubase 3 lit toujours avec A7..A1 = 0), puis verrouillé.
    Model    chosen_ = Model::None;
    bool     locked_ = false;
    uint8_t  d_ = 0;               // clé noire : D15..D8 (bit7 = D15)
    uint16_t r_ = 0;               // clé rouge : bits 0-15 = pin03..pin10, pin15..pin21, pin22(d8)
};
