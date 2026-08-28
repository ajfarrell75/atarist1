// =============================================================================
//  DiskImageCodec.hpp — point d'entrée TESTABLE des décodeurs d'images disquette.
//
//  `decodeMsa` et `decodeDim` vivent dans Fdc.cpp, en `static` : parfait pour le
//  code de production, inaccessible pour un harnais. Or ce sont des fonctions
//  PURES `octets → bool` qui digèrent un fichier venu de l'extérieur — exactement
//  la surface qu'on veut marteler (chantier A30). Cette façade les expose sans
//  déplacer le code ni ouvrir l'implémentation.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>

namespace diskimg {

// Décode un CONTENEUR (.msa compressé ou .dim à en-tête 32 o) vers l'image BRUTE,
// exactement comme le fait Fdc::loadImage : MSA d'abord, DIM ensuite. Renvoie
// false si `raw` n'est ni l'un ni l'autre (une .st brute passe donc par false —
// elle n'a pas de conteneur à défaire).
//
// ⚠ Contrat attendu de ces deux décodeurs, et c'est ce que le harnais vérifie :
// sur une entrée QUELCONQUE ils rendent true ou false, ils ne lisent jamais hors
// de `raw`, et quand ils rendent true `out` porte une géométrie cohérente.
bool decodeContainer(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out);

}  // namespace diskimg
