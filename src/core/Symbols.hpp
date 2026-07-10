// =============================================================================
//  Symbols.hpp — Table de symboles pour le débogueur (noms ↔ adresses 68000).
//
//  Deux sources :
//    · fichier TEXTE « nm-style » : une ligne « ADDR [TYPE] NAME » (hex), comme la
//      sortie de `nm`/Hatari (ex. « 00fc0058 T reset »). Adresses ABSOLUES.
//    · exécutable TOS (.PRG/.TOS/.APP, magic $601A) : symboles DRI (+ noms étendus
//      GST) parsés depuis la table du programme, DÉCALÉS d'une base de chargement.
//
//  Sert à : --break-sym NAME (headless / GUI), et à annoter le désassemblage
//  (« $ADDR <name+off> »). Composant du cœur → partagé par les deux frontends.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

class SymbolTable {
public:
    struct Sym { std::string name; uint32_t addr; };

    // Auto-détecte le format (magic $601A = exécutable TOS, sinon texte nm-style).
    // baseOffset : ajouté aux valeurs des symboles d'un exécutable TOS (relocation ;
    // sans effet sur un fichier texte à adresses absolues). Renvoie false si illisible.
    bool load(const std::string& path, uint32_t baseOffset = 0);

    bool loadSymText(const std::string& path);                    // nm-style, adresses absolues
    bool loadTosProgram(const std::string& path, uint32_t base);  // DRI/GST embarqués + base

    // Résolution nom → adresse (exacte, insensible à la casse). false si inconnu.
    bool lookup(const std::string& name, uint32_t& outAddr) const;

    // Nom pour une adresse : symbole EXACT, sinon le plus proche STRICTEMENT ≤ addr
    // (avec *outOffset = addr - sym.addr). Chaîne vide si aucun symbole ≤ addr.
    std::string nameFor(uint32_t addr, uint32_t* outOffset = nullptr) const;

    void   clear();
    size_t count() const { return syms_.size(); }
    const std::vector<Sym>& all() const { return syms_; }

private:
    void finalize();   // trie par adresse + (re)construit l'index par nom

    std::vector<Sym> syms_;                                // trié par adresse (nameFor)
    std::unordered_map<std::string, uint32_t> byName_;     // nom minuscule → adresse
};
