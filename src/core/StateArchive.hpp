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
        if (loading_) { if (!ok_) return; v.resize(n); }
        raw(v.data(), v.size());
    }

private:
    bool loading_ = false;
    bool ok_      = true;
    std::vector<uint8_t>* out_ = nullptr;   // mode save
    const uint8_t* in_    = nullptr;         // mode load
    const uint8_t* inEnd_ = nullptr;
};
