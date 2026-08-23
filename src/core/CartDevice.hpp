// =============================================================================
//  CartDevice — un périphérique du port cartouche, vu par les SIGNAUX du port.
// =============================================================================
//
//  Le port cartouche du ST n'expose que des lignes : A1-A15, D0-D15, /ROM3
//  ($FB0000-$FBFFFF), /ROM4 ($FA0000-$FAFFFF), /UDS, /LDS — pas d'écriture. Tout ce
//  qui s'y branche (clé de protection, ROM, interface) ne voit que ça. Plutôt qu'une
//  chaîne de `if` par matériel dans Bus::read8, les périphériques s'abonnent ici :
//  le Bus les interroge dans l'ordre d'enregistrement (le premier qui pilote D0-D15
//  gagne, comme le premier qui répond sur un bus réel), et ne coûte un appel /UDS
//  par cycle CPU QUE si l'un d'eux le demande (wantsUds). Plusieurs périphériques
//  coexistent — un MIDEX ou un Combiner C-Lab empilaient plusieurs clés.
//
//  NetUSBee (ISP1160) et EtherNEC (NE2000) gardent leur décodage dédié dans Bus
//  (ils décodent des sous-fenêtres /ROM4 avec des verrous, cf. io/Isp1160.hpp) et
//  sont consultés AVANT ces abonnés.
// =============================================================================
#pragma once
#include <cstdint>

class CartDevice {
public:
    virtual ~CartDevice() = default;
    // Lecture CPU sous /ROM3 ou /ROM4. `first` = premier octet de l'accès (octet
    // pair d'un mot, ou accès octet) : c'est LE cycle bus, l'octet impair d'un mot
    // en est la seconde moitié. Renvoie true et pose `out` si le périphérique
    // pilote le bus de données pour cet octet.
    virtual bool rom3Read(uint32_t /*addr*/, bool /*first*/, uint8_t& /*out*/) { return false; }
    virtual bool rom4Read(uint32_t /*addr*/, bool /*first*/, uint8_t& /*out*/) { return false; }
    // Fin d'un cycle bus CPU ayant activé /UDS (mot, ou octet à adresse paire), où
    // que soit l'adresse — fetchs compris. N'est appelé que si wantsUds().
    virtual void udsCycle(uint32_t /*addr*/) {}
    virtual bool wantsUds() const { return false; }
};
