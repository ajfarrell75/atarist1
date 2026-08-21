// =============================================================================
//  StbImage.cpp — Unique unité d'implémentation de stb_image (extern/stb, domaine
//  public). Décodeur JPEG/PNG pour les images du GUI (photo du clavier, pic/).
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_FAILURE_STRINGS
#include "stb_image.h"
