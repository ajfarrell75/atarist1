// =============================================================================
//  StateArchive.hpp — sérialiseur/désérialiseur SYMÉTRIQUE pour les save-states.
//
//  Une SEULE méthode `serialize(StateArchive&)` par composant gère les DEUX sens
//  (save = append dans un buffer, load = lecture depuis un buffer) → impossible de
//  désynchroniser l'ordre save/load. Format binaire brut, little-endian de l'hôte
//  (les save-states ne sont pas portables entre architectures — hypothèse assumée).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

class StateArchive {
public:
    static StateArchive saver(std::vector<uint8_t>& out) {
        StateArchive a; a.loading_ = false; a.out_ = &out; return a;
    }
    static StateArchive loader(const uint8_t* p, size_t n) {
        StateArchive a; a.loading_ = true; a.in_ = p; a.inEnd_ = p + n; return a;
    }

    bool loading() const { return loading_; }
    bool ok()      const { return ok_; }

    // Valide un invariant AU CHARGEMENT (no-op en save) : un champ restauré hors
    // bornes (fichier forgé/corrompu passé le CRC) met l'archive en échec →
    // Machine::loadState rejoue le backup au lieu de servir des index toxiques.
    void check(bool cond) { if (loading_ && !cond) ok_ = false; }

    // Taille écrite jusqu'ici (mode save uniquement — diagnostic NEOST_STATE_MAP).
    size_t saveSize() const { return out_ ? out_->size() : 0; }

    // Transfère n octets bruts (save : append ; load : lit, borne vérifiée).
    void raw(void* p, size_t n) {
        if (n == 0) return;
        if (loading_) {
            if (in_ + n > inEnd_) { ok_ = false; return; }
            std::memcpy(p, in_, n);
            in_ += n;
        } else {
            const uint8_t* c = static_cast<const uint8_t*>(p);
            out_->insert(out_->end(), c, c + n);
        }
    }

    // Un scalaire/POD (uint32_t, int64_t, bool, un struct trivial…).
    template <class T>
    void operator()(T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "StateArchive : type non trivial");
        raw(&v, sizeof v);
    }

    // Un tableau C d'éléments POD (ex. due_[SRC_COUNT], reg.d[8]).
    template <class T, size_t N>
    void arr(T (&v)[N]) { raw(v, sizeof v); }

    // Un vector<uint8_t> de taille variable (préfixé par sa longueur) — ex. la RAM.
    void vec(std::vector<uint8_t>& v) {
        uint32_t n = static_cast<uint32_t>(v.size());
        (*this)(n);
        if (loading_) {
            // Borne AVANT resize : un préfixe corrompu (ex. 0xFFFFFFFF) ne doit
            // jamais allouer plus que les octets restants du buffer d'entrée.
            if (!ok_ || n > static_cast<size_t>(inEnd_ - in_)) { ok_ = false; return; }
            v.resize(n);
        }
        raw(v.data(), v.size());
    }

    // Un vector<T> dont chaque élément est sérialisé CHAMP PAR CHAMP via `each(ar, e)`.
    // OBLIGATOIRE pour les structs à padding (SyncWrite, GlueLine, ColorWrite,
    // DmaEvent, RegEvent…) : podVec copierait leurs octets de padding NON INITIALISÉS
    // → save-states non byte-déterministes (le test --save-state-test et le CRC
    // divergeraient sur des octets morts). `elemBytes` = taille sérialisée MINIMALE
    // d'un élément, pour borner l'allocation face à un préfixe corrompu.
    template <class T, class F>
    void objVec(std::vector<T>& v, size_t elemBytes, F&& each) {
        uint32_t n = static_cast<uint32_t>(v.size());
        (*this)(n);
        if (loading_) {
            const uint64_t need = static_cast<uint64_t>(n) * elemBytes;
            if (!ok_ || need > static_cast<uint64_t>(inEnd_ - in_)) { ok_ = false; return; }
            v.resize(n);
        }
        for (T& e : v) { each(*this, e); if (!ok_) return; }
    }

    // Un vector<T> d'éléments POD (taille préfixée) — ex. les logs d'écritures horodatées
    // (events_ du YM2149/DMA sound). Vidés à chaque trame → généralement vides à la
    // frontière de save, mais sérialisés par sûreté.
    // ⚠ RÉSERVÉ aux T SANS padding interne (sizeof(T) == somme des membres) —
    // sinon utiliser objVec (cf. ci-dessus).
    template <class T>
    void podVec(std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>, "podVec : élément non trivial");
        uint32_t n = static_cast<uint32_t>(v.size());
        (*this)(n);
        if (loading_) {
            // Même garde que vec() : n éléments ne peuvent pas dépasser les octets
            // restants (le calcul est fait en 64 bits, pas de débordement possible).
            const uint64_t need = static_cast<uint64_t>(n) * sizeof(T);
            if (!ok_ || need > static_cast<uint64_t>(inEnd_ - in_)) { ok_ = false; return; }
            v.resize(n);
        }
        raw(v.data(), sizeof(T) * v.size());
    }

private:
    bool loading_ = false;
    bool ok_      = true;
    std::vector<uint8_t>* out_ = nullptr;   // mode save
    const uint8_t* in_    = nullptr;         // mode load
    const uint8_t* inEnd_ = nullptr;
};
