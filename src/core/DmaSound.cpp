// =============================================================================
//  DmaSound.cpp — Implémentation du son DMA STE.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/DmaSound.hpp"
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"

#include <cmath>

// Fréquences d'échantillonnage STE (cristal 25.175 MHz / diviseurs), bits 0-1 de
// $FF8921. Réf. doc matérielle / Hatari.
static const double kRate[4] = { 6258.0, 12517.0, 25033.0, 50066.0 };

// Gain du canal numérique dans le mix (pleine échelle 8 bits ramenée sous le 0 dB
// pour laisser de la marge au YM2149 ; le volume fin relève du LMC1992, cf. TODO).
static constexpr float kDmaGain = 0.7f;

// Horloge CPU (Hz) pour dater la fin de trame en cycles (cf. Mfp / Scheduler).
static constexpr int64_t CPU_HZ = 8021248;

// Programme l'échéance « fin de trame » sur l'ordonnanceur (thread émulation) :
// durée = nombre d'échantillons de la trame × cycles CPU par échantillon.
void DmaSound::scheduleFrameEnd() {
    if (!sched_) return;
    if (endAddr_ <= startAddr_) { sched_->cancel(Scheduler::DMASND); return; }
    const uint32_t step    = (mode_ & 0x80) ? 1u : 2u;        // mono 1 octet, stéréo 2
    const int64_t  samples = (endAddr_ - startAddr_) / step;
    const int64_t  rate    = int64_t(kRate[mode_ & 0x03]);
    const int64_t  dur     = samples * CPU_HZ / rate;
    sched_->schedule(Scheduler::DMASND, sched_->now() + (dur > 0 ? dur : 1));
}

// Câble le MFP et lui annonce que cette machine possède le son DMA (STE/Mega STE) :
// seul ce signal autorise le MFP à XOR la ligne XSINT dans GPIP7 (cf. Hatari
// MFP_Main_Compute_GPIP7). Sur un ST sans son DMA, le composant n'est pas câblé ici.
void DmaSound::setMfp(Mfp* m) {
    mfp_ = m;
    if (mfp_) mfp_->setHasDmaSound(true);
}

// Met à jour la ligne XSINT et la propage au MFP. Réf. Hatari DmaSnd_Update_XSINT_Line :
// HAUT = trame en cours, BAS = son DMA inactif. La ligne est câblée à DEUX entrées du
// MFP — GPIP7 (XOR détection moniteur) ET TAI du Timer A (event-count) — donc chaque
// transition pilote les deux. C'est la transition vers 0 (fin de trame, AER GPIP4=0 par
// défaut) qui décompte le Timer A pour le double-buffering audio STE.
void DmaSound::setXsint(bool level) {
    if (level == xsint_) return;                  // pas de transition → rien à faire
    xsint_ = level;
    if (mfp_) {
        mfp_->setXsintLine(xsint_);               // → GPIP7
        mfp_->timerA_setLineInput(xsint_);        // → TAI (Timer A event-count, polarité AER)
    }
}

// (Re)démarre une trame DMA : recale le compteur sur l'adresse de début et arme
// l'échéance de fin. Port de Hatari DmaSnd_StartNewFrame (dmaSnd.c:462-480) : si
// début == fin ET repeat OFF, la trame est VIDE → on coupe la lecture SANS lever
// XSINT (sinon GPIP7 resterait figé HAUT, faussant la détection moniteur ; bug des
// demos start==end comme l'Amberstar cracktro). Repeat ON → on ne coupe pas.
void DmaSound::startNewFrame() {
    curAddr_ = startAddr_;
    phase_   = 0.0;
    haveCur_ = false;                                  // 1er octet (re)tiré et filtré au prochain mix
    if (endAddr_ <= startAddr_ && !(ctrl_ & 0x02)) {   // trame vide, pas de repeat → arrêt sec
        playing_ = false;
        ctrl_   &= ~0x01;
        setXsint(false);                               // surtout PAS de XSINT HAUT
        if (sched_) sched_->cancel(Scheduler::DMASND);
        return;
    }
    // Latch de la trame (port DmaSnd_StartNewFrame) + cycle de départ pour le
    // compteur live $FF8909+ (liveNow : un play lancé en plein bloc CPU est daté
    // au cycle exact de l'écriture, pas au début du quantum).
    frameStartAddr_  = startAddr_;
    frameEndAddr_    = endAddr_;
    frameStartCycle_ = sched_ ? sched_->liveNow() : 0;
    playing_ = true;
    recordEvent(0);                                    // PLAY start daté (modèle push audio)
    setXsint(true);                                    // début de trame : XSINT → HAUT (→ GPIP7)
    scheduleFrameEnd();                                // date la fin de trame (→ Timer A)
}

void DmaSound::onFrameEnd() {
    setXsint(false);                              // fin de trame : XSINT → BAS (compte Timer A si AER bit4=0)
    if (ctrl_ & 0x02) {
        startNewFrame();                          // repeat : nouvelle trame (→ XSINT HAUT, gère start==end)
    } else {
        // Trame one-shot terminée : le MATÉRIEL auto-efface le bit PLAY (port
        // DmaSnd_EndOfFrameReached, dmaSnd.c:510) — $FF8901 doit relire 0. Le TOS
        // (handler VBL) surveille ce bit pour sa détection moniteur/son : un PLAY
        // qui reste collé déclenche un RESET en boucle (démo STE « Faster »).
        // L'effacement vit ICI dans le moteur DMA, pas dans le mixeur audio hôte
        // (mix() ne tourne pas en headless).
        ctrl_   &= ~0x01;
        playing_ = false;
    }
}

void DmaSound::reset(bool cold) {
    playing_ = false;
    ctrl_  = 0;
    mode_  = 0;
    phase_ = 0.0;
    haveCur_ = false; dmaCur_ = 0.0f; lpW0_ = lpW1_ = 0.0f;   // FIR anti-repliement à zéro
    dmaCurL_ = dmaCurR_ = 0.0f; lpW0L_ = lpW1L_ = lpW0R_ = lpW1R_ = 0.0f;   // idem chemin stéréo
    bx1R_ = bx2R_ = by1R_ = by2R_ = 0; tx1R_ = tx2R_ = ty1R_ = ty2R_ = 0;   // filtres tonalité D
    aPlaying_ = false; aStart_ = aEnd_ = aAddr_ = 0; aMode_ = 0;            // état audio « push »
    aRepeat_ = false; aPhase_ = 0.0; aHaveCur_ = false;
    events_.clear();
    startAddr_ = endAddr_ = curAddr_ = 0;          // Hatari met start/end à 0 même au warm reset (fix 'Brace')
    mwShift_ = 0; mwSteps_ = 0;                     // transfert série Microwire éventuel annulé
    setXsint(false);                               // son DMA inactif au reset → XSINT BAS
    if (sched_) sched_->cancel(Scheduler::DMASND);
    // Le LMC1992/Microwire n'a PAS de broche de reset : ses registres (volumes, basses/
    // aigus, mixage) PERSISTENT au reset à chaud (cf. Hatari, bloc `if (bCold)`). On ne
    // les réinitialise qu'à FROID. NeoST garde des défauts à 0 dB (et non muets comme le
    // vrai HW/Hatari) pour rester audible si l'OS ne programme pas le LMC.
    if (cold) {
        mwMaster_ = 40; mwLeft_ = 20; mwRight_ = 20;   // LMC1992 à 0 dB
        mwBass_ = 6; mwTreble_ = 6; mwMixing_ = 0;
    }
}

// Décode une commande LMC1992 reçue par microwire (port dmaSnd.c:DmaSnd_InterruptHandler_Microwire).
// Une commande = un run CONTIGU de bits à 1 dans le masque ($FF8924) ; les bits à 0 du masque
// terminent la commande. Si le run ne forme pas une adresse LMC valide (%10 + ≥9 bits), on
// reprend la recherche au prochain '1' du masque (commandes invalides ignorées).
void DmaSound::decodeMicrowire() {
    uint16_t cmd = 0;
    int      cmdLen = 0;
    for (int i = 15; i >= 0; --i) {
        if (!(mwMask_ & (1u << i))) continue;
        do {
            cmd = uint16_t(cmd << 1);
            ++cmdLen;
            if (mwData_ & (1u << i)) cmd |= 1;
            --i;
        } while (i >= 0 && (mwMask_ & (1u << i)));

        if (cmdLen >= 11 && ((cmd >> (cmdLen - 2)) & 0x03) == 0x02)
            break;

        if (i < 0) return;
        cmd = 0;
        cmdLen = 0;
    }

    if (cmdLen < 11 || ((cmd >> (cmdLen - 2)) & 0x03) != 0x02)
        return;

    switch ((cmd >> 6) & 0x07) {
        case 0: mwMixing_ = cmd & 0x03; break;
        case 1: mwBass_   = cmd & 0x0F; break;
        case 2: mwTreble_ = cmd & 0x0F; break;
        case 3: mwMaster_ = cmd & 0x3F; break;   // 0..40 → -80..0 dB
        case 4: mwRight_  = cmd & 0x1F; break;   // 0..20 → -40..0 dB
        case 5: mwLeft_   = cmd & 0x1F; break;
        default: break;
    }
}

// Une étape du transfert série Microwire (datée toutes les 8 cycles par le
// Scheduler). $FF8922 lit `data << (16 - étapes_restantes)` → atteint 0 après 16
// décalages (port de Hatari DmaSnd_InterruptHandler_Microwire). À 0, on décode la
// commande LMC1992. Le masque ($FF8924) tourne mais revient identique → decode OK.
void DmaSound::onMicrowireShift() {
    if (mwSteps_ <= 0) return;
    --mwSteps_;
    mwShift_ = uint16_t(mwData_ << (16 - mwSteps_));    // étapes=0 → <<16 = 0
    if (mwSteps_ > 0) {
        if (sched_) sched_->schedule(Scheduler::MICROWIRE, sched_->now() + 8);
    } else {
        decodeMicrowire();                              // transfert fini → applique la commande
    }
}

// Coefficients d'un filtre en plateau (shelving) d'après le « Audio EQ Cookbook »
// de R. Bridson-Robert (pente S=1), normalisés par a0. lowShelf=true → basses.
static void shelfCoeffs(bool lowShelf, double dB, double f0, double fs,
                        double& b0, double& b1, double& b2, double& a1, double& a2) {
    const double A  = std::pow(10.0, dB / 40.0);
    const double w0 = 2.0 * M_PI * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / 2.0 * std::sqrt(2.0);          // S=1
    const double tsa   = 2.0 * std::sqrt(A) * alpha;
    double B0, B1, B2, A0, A1, A2;
    if (lowShelf) {
        B0 =    A * ((A + 1) - (A - 1) * cw + tsa);
        B1 =  2 * A * ((A - 1) - (A + 1) * cw);
        B2 =    A * ((A + 1) - (A - 1) * cw - tsa);
        A0 =        (A + 1) + (A - 1) * cw + tsa;
        A1 =   -2 * ((A - 1) + (A + 1) * cw);
        A2 =        (A + 1) + (A - 1) * cw - tsa;
    } else {                                                 // plateau aigus
        B0 =    A * ((A + 1) + (A - 1) * cw + tsa);
        B1 = -2 * A * ((A - 1) + (A + 1) * cw);
        B2 =    A * ((A + 1) + (A - 1) * cw - tsa);
        A0 =        (A + 1) - (A - 1) * cw + tsa;
        A1 =    2 * ((A - 1) - (A + 1) * cw);
        A2 =        (A + 1) - (A - 1) * cw - tsa;
    }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
}

void DmaSound::applyTone(float* out, uint32_t frames, uint32_t sampleRate) {
    if ((mwBass_ == 6 && mwTreble_ == 6) || sampleRate == 0) return;   // 0/0 dB → bypass
    // Codes 0-12 → -12..+12 dB (pas de 2 dB) ; codes 13-15 SATURENT à +12 dB, comme la
    // table LMC1992_Bass_Treble_Table de Hatari (dmaSnd.c:211-214) — sans ça les codes
    // 13-15 donnaient +14/+16/+18 dB.
    const int bassCode = mwBass_   < 12 ? mwBass_   : 12;
    const int trebCode = mwTreble_ < 12 ? mwTreble_ : 12;
    const double bassDb = (bassCode - 6) * 2.0;
    const double trebDb = (trebCode - 6) * 2.0;
    double bb0, bb1, bb2, ba1, ba2, tb0, tb1, tb2, ta1, ta2;
    shelfCoeffs(true,  bassDb, 200.0,  sampleRate, bb0, bb1, bb2, ba1, ba2);   // basses ~200 Hz
    shelfCoeffs(false, trebDb, 8000.0, sampleRate, tb0, tb1, tb2, ta1, ta2);   // aigus  ~8 kHz

    for (uint32_t i = 0; i < frames; ++i) {
        const double x = out[i];
        const double yb = bb0 * x + bb1 * bx1_ + bb2 * bx2_ - ba1 * by1_ - ba2 * by2_;
        bx2_ = bx1_; bx1_ = x;  by2_ = by1_; by1_ = yb;
        const double yt = tb0 * yb + tb1 * tx1_ + tb2 * tx2_ - ta1 * ty1_ - ta2 * ty2_;
        tx2_ = tx1_; tx1_ = yb; ty2_ = ty1_; ty1_ = yt;
        out[i] = static_cast<float>(yt);
    }
}

// Idem que applyTone mais sur le tampon ENTRELACÉ L/R : les coefficients sont calculés
// une fois (basses+aigus identiques sur les deux canaux), chaque canal gardant son
// propre état de filtre (G = sans suffixe, D = *R_) pour ne pas mélanger les retards.
void DmaSound::applyToneStereo(float* st, uint32_t frames, uint32_t sampleRate) {
    if ((mwBass_ == 6 && mwTreble_ == 6) || sampleRate == 0) return;   // 0/0 dB → bypass
    const int bassCode = mwBass_   < 12 ? mwBass_   : 12;
    const int trebCode = mwTreble_ < 12 ? mwTreble_ : 12;
    const double bassDb = (bassCode - 6) * 2.0;
    const double trebDb = (trebCode - 6) * 2.0;
    double bb0, bb1, bb2, ba1, ba2, tb0, tb1, tb2, ta1, ta2;
    shelfCoeffs(true,  bassDb, 200.0,  sampleRate, bb0, bb1, bb2, ba1, ba2);
    shelfCoeffs(false, trebDb, 8000.0, sampleRate, tb0, tb1, tb2, ta1, ta2);

    for (uint32_t i = 0; i < frames; ++i) {
        // Canal gauche (état sans suffixe).
        const double xl = st[2 * i];
        const double ybl = bb0 * xl + bb1 * bx1_ + bb2 * bx2_ - ba1 * by1_ - ba2 * by2_;
        bx2_ = bx1_; bx1_ = xl;  by2_ = by1_; by1_ = ybl;
        const double ytl = tb0 * ybl + tb1 * tx1_ + tb2 * tx2_ - ta1 * ty1_ - ta2 * ty2_;
        tx2_ = tx1_; tx1_ = ybl; ty2_ = ty1_; ty1_ = ytl;
        st[2 * i] = static_cast<float>(ytl);
        // Canal droit (état *R_).
        const double xr = st[2 * i + 1];
        const double ybr = bb0 * xr + bb1 * bx1R_ + bb2 * bx2R_ - ba1 * by1R_ - ba2 * by2R_;
        bx2R_ = bx1R_; bx1R_ = xr;  by2R_ = by1R_; by1R_ = ybr;
        const double ytr = tb0 * ybr + tb1 * tx1R_ + tb2 * tx2R_ - ta1 * ty1R_ - ta2 * ty2R_;
        tx2R_ = tx1R_; tx1R_ = ybr; ty2R_ = ty1R_; ty1R_ = ytr;
        st[2 * i + 1] = static_cast<float>(ytr);
    }
}

float DmaSound::masterGain() const {
    // Pas de 2 dB ; valeurs au-delà du max = 0 dB. Sortie mono → moyenne G/D.
    const double mdB = (mwMaster_ >= 40 ? 0 : (mwMaster_ - 40) * 2);
    const double ldB = (mwLeft_   >= 20 ? 0 : (mwLeft_   - 20) * 2);
    const double rdB = (mwRight_  >= 20 ? 0 : (mwRight_  - 20) * 2);
    const double gL = std::pow(10.0, (mdB + ldB) / 20.0);
    const double gR = std::pow(10.0, (mdB + rdB) / 20.0);
    return static_cast<float>((gL + gR) * 0.5);
}

// Gains linéaires par canal (volume maître × gauche / × droite) — réalisent le
// panoramique STE. Réf. Hatari : lmc1992.left_gain = leftVolume × masterVolume.
float DmaSound::gainLeft() const {
    const double mdB = (mwMaster_ >= 40 ? 0 : (mwMaster_ - 40) * 2);
    const double ldB = (mwLeft_   >= 20 ? 0 : (mwLeft_   - 20) * 2);
    return static_cast<float>(std::pow(10.0, (mdB + ldB) / 20.0));
}
float DmaSound::gainRight() const {
    const double mdB = (mwMaster_ >= 40 ? 0 : (mwMaster_ - 40) * 2);
    const double rdB = (mwRight_  >= 20 ? 0 : (mwRight_  - 20) * 2);
    return static_cast<float>(std::pow(10.0, (mdB + rdB) / 20.0));
}

// Lecture d'un échantillon (mono -128..127). En stéréo, moyenne L+R (sortie mono).
// Le DMA son traverse le plan mémoire complet (traduction MMU / aliasing de
// banques) comme le DMA disque — port STMemory_DMA_ReadByte, cf. Bus::dmaRead8.
int DmaSound::sampleAt(uint32_t addr, bool stereo) const {
    const auto rd = [&](uint32_t a) -> int {
        return static_cast<int8_t>(bus_.dmaRead8(a));
    };
    return stereo ? (rd(addr) + rd(addr + 1)) / 2 : rd(addr);
}

// Lecture STÉRÉO : octets pairs/impairs → L/R séparés (mono → L=R). Conserve la
// séparation au lieu de la moyenner (cf. sampleAt). Port STMemory_DMA_ReadByte.
void DmaSound::sampleAtLR(uint32_t addr, bool stereo, int& l, int& r) const {
    const int b0 = static_cast<int8_t>(bus_.dmaRead8(addr));
    if (stereo) { l = b0; r = static_cast<int8_t>(bus_.dmaRead8(addr + 1)); }
    else        { l = r = b0; }
}

// FIR anti-repliement (1,2,1)/4 à état EXTERNE (cf. lowPassPull) — partagé par les
// deux canaux du chemin stéréo, chacun avec ses propres retards w0/w1.
static inline float fir121(float& w0, float& w1, int in, bool enabled) {
    const float x = float(in);
    const float y = enabled ? (w0 + 2.0f * w1 + x) : (w1 * 4.0f);
    w0 = w1; w1 = x;
    return y;
}

// Enregistre une transition de trame DMA datée au cycle frame-relatif courant (modèle
// push, cf. mixStereo). PLAY start (kind 0) latche start/end/mode/repeat ; STOP (kind 1)
// = effacement CPU du bit PLAY. Sans horloge câblée (headless/WASM) : no-op.
void DmaSound::recordEvent(uint8_t kind) {
    if (!cycleClock_) return;
    int64_t c = cycleClock_();
    if (c < 0) c = 0;
    events_.push_back({ uint32_t(c), kind, startAddr_, endAddr_, mode_, bool(ctrl_ & 0x02) });
}

// Repli SANS horloge push : aligne l'état audio sur l'état CPU live au début de trame.
// Reproduit l'ancien rendu « échantillonné par trame » (le bit PLAY pilote tout).
void DmaSound::syncAudioFromCpu() {
    if (playing_ && !aPlaying_) {                  // démarrage vu côté CPU → (re)cale l'audio
        aStart_ = startAddr_; aEnd_ = endAddr_; aMode_ = mode_;
        aRepeat_ = ctrl_ & 0x02; aAddr_ = curAddr_; aPhase_ = phase_; aHaveCur_ = false;
    }
    aPlaying_ = playing_;
}

// Position live du compteur de trame DMA — forme fermée depuis le cycle de début
// de trame : octets lus = échantillons écoulés × octets/échantillon, où
// échantillons = (cycles écoulés) × fréquence / horloge CPU. Bornée à la fin de
// trame latchée : le franchissement réel (repeat → nouveau latch, arrêt) est géré
// par l'événement DMASND de l'ordonnanceur. NB : un changement de fréquence EN
// COURS de trame fausserait la forme fermée (cas pathologique, les lecteurs
// posent $FF8921 avant play) — Hatari avance incrémentalement via Sound_Update.
uint32_t DmaSound::liveCounter() const {
    if (!playing_) return startAddr_;                  // à l'arrêt : adresse de DÉBUT
    if (!sched_)   return curAddr_;                    // sans ordonnanceur (tests)
    const int64_t  elapsed = sched_->liveNow() - frameStartCycle_;
    if (elapsed <= 0) return frameStartAddr_;
    const uint32_t step    = (mode_ & 0x80) ? 1u : 2u; // octets par échantillon
    const int64_t  rate    = int64_t(kRate[mode_ & 0x03]);
    int64_t addr = int64_t(frameStartAddr_) + (elapsed * rate / CPU_HZ) * step;
    if (addr > int64_t(frameEndAddr_)) addr = frameEndAddr_;
    return uint32_t(addr);
}

uint8_t DmaSound::read8(uint32_t addr) {
    switch (addr & 0xFF) {
        case 0x01: return ctrl_;
        case 0x03: return uint8_t(startAddr_ >> 16);
        case 0x05: return uint8_t(startAddr_ >> 8);
        case 0x07: return uint8_t(startAddr_);
        // Compteur courant (position de lecture) : position LIVE cycle-exacte
        // dérivée de l'horloge émulée (cf. liveCounter — port DmaSnd_GetFrameCount
        // qui appelle Sound_Update en tête). À l'ARRÊT, le vrai HW renvoie
        // l'adresse de DÉBUT, pas la dernière position (dmaSnd.c:756-759).
        case 0x09: return uint8_t(liveCounter() >> 16);
        case 0x0B: return uint8_t(liveCounter() >> 8);
        case 0x0D: return uint8_t(liveCounter());
        case 0x0F: return uint8_t(endAddr_ >> 16);
        case 0x11: return uint8_t(endAddr_ >> 8);
        case 0x13: return uint8_t(endAddr_);
        case 0x21: return mode_;
        case 0x22: return uint8_t(mwShift_ >> 8);     // microwire data : valeur EN COURS de shift
        case 0x23: return uint8_t(mwShift_);          // (→ 0 quand le transfert est fini)
        case 0x24: return uint8_t(mwMask_ >> 8);      // microwire mask
        case 0x25: return uint8_t(mwMask_);
        default:   return 0xFF;
    }
}

void DmaSound::write8(uint32_t addr, uint8_t v) {
    switch (addr & 0xFF) {
        case 0x01: {                                  // contrôle : play / repeat
            const bool wasPlaying = ctrl_ & 0x01;
            ctrl_ = v & 0x03;
            if ((ctrl_ & 0x01) && !wasPlaying) {       // 0→1 : (re)démarre la trame
                startNewFrame();                       // recale + arme la fin (gère start==end)
            } else if (!(ctrl_ & 0x01)) {              // bit play à 0 : arrêt
                playing_ = false;
                recordEvent(1);                        // STOP daté (modèle push audio)
                setXsint(false);                       // arrêt : XSINT → BAS
                if (sched_) sched_->cancel(Scheduler::DMASND);
            }
            break;
        }
        // Adresses 24 bits (paires forcées : bit0 de l'octet bas ignoré).
        case 0x03: startAddr_ = (startAddr_ & 0x00FFFF) | (uint32_t(v) << 16); break;
        case 0x05: startAddr_ = (startAddr_ & 0xFF00FF) | (uint32_t(v) << 8);  break;
        case 0x07: startAddr_ = (startAddr_ & 0xFFFF00) | (uint32_t(v) & 0xFE); break;
        case 0x0F: endAddr_   = (endAddr_   & 0x00FFFF) | (uint32_t(v) << 16); break;
        case 0x11: endAddr_   = (endAddr_   & 0xFF00FF) | (uint32_t(v) << 8);  break;
        case 0x13: endAddr_   = (endAddr_   & 0xFFFF00) | (uint32_t(v) & 0xFE); break;
        case 0x21: mode_ = v; break;                   // fréquence + mono/stéréo
        // Microwire : mots 16 bits ($FF8922 data, $FF8924 mask). On décode la
        // commande LMC1992 quand l'octet bas de la donnée est écrit (mot complet).
        case 0x22: mwData_ = uint16_t((mwData_ & 0x00FF) | (v << 8)); break;
        case 0x23:
            mwData_ = uint16_t((mwData_ & 0xFF00) | v);
            // Écriture d'un mot data → démarre un transfert série Microwire (16
            // décalages de 8 cycles, cf. Hatari DmaSnd_MicrowireData_WriteWord), SAUF
            // si un transfert est déjà en cours. $FF8922 lira la valeur décalée
            // jusqu'à 0, puis decodeMicrowire(). Sans scheduler → décodage immédiat.
            if (mwSteps_ == 0) {
                mwShift_ = mwData_;
                if (sched_) { mwSteps_ = 16; sched_->schedule(Scheduler::MICROWIRE, sched_->now() + 8); }
                else        { mwShift_ = 0; decodeMicrowire(); }
            }
            break;
        case 0x24: mwMask_ = uint16_t((mwMask_ & 0x00FF) | (v << 8)); break;
        case 0x25: mwMask_ = uint16_t((mwMask_ & 0xFF00) | v); break;
        default: break;                                // compteur courant : lecture seule
    }
}

// Filtre anti-repliement du canal DMA — port du DmaSnd_LowPassFilter d'Hatari
// (dmaSnd.c:1316-1349) : FIR 3 points (1,2,1)/4 appliqué à CHAQUE octet tiré à la
// cadence DMA quand elle dépasse la fréquence de sortie hôte (50066 Hz → 48 kHz :
// sans lui, le repliement siffle). Filtre coupé : simple retard d'un échantillon
// (×4), pour garder gain et latence constants — le /4 est appliqué au mixage,
// comme le « Divide by 4 to account for DmaSnd_LowPassFilter » d'Hatari.
float DmaSound::lowPassPull(int in, bool enabled) {
    const float x = float(in);
    const float y = enabled ? (lpW0_ + 2.0f * lpW1_ + x) : (lpW1_ * 4.0f);
    lpW0_ = lpW1_;
    lpW1_ = x;
    return y;
}

void DmaSound::mix(float* out, uint32_t frames, uint32_t sampleRate) {
    if (!playing_ || sampleRate == 0) return;
    const double inc    = kRate[mode_ & 0x03] / sampleRate;   // pas de rééchantillonnage
    const bool   stereo = !(mode_ & 0x80);                    // bit7=0 → stéréo entrelacé
    const uint32_t step = stereo ? 2u : 1u;                   // octets par trame DMA
    const bool lowPass  = kRate[mode_ & 0x03] > double(sampleRate);   // anti-repliement

    // Registre de mixage du LMC1992 (commande 0) : seul mixing==1 mélange YM2149 + DMA ;
    // 0/2/3 routent le DMA SEUL → il écrase le YM (cf. Hatari DmaSnd_GenerateSamples,
    // dmaSnd.c:555-568). N'a d'effet qu'ici, trame en cours : DMA à l'arrêt → `mix` sort
    // plus haut et le YM passe intact (pas de mute du YM hors lecture DMA).
    const bool addYm = (mwMixing_ == 1);
    for (uint32_t i = 0; i < frames; ++i) {
        if (!haveCur_) {                                      // 1er échantillon de la trame
            dmaCur_  = lowPassPull(sampleAt(curAddr_, stereo), lowPass);
            haveCur_ = true;
        }
        const float dma = (dmaCur_ / 4.0f / 128.0f) * kDmaGain;   // /4 = gain du FIR
        if (addYm) out[i] += dma;                             // YM2149 + DMA
        else       out[i]  = dma;                             // DMA seul (écrase le YM)
        phase_ += inc;
        while (phase_ >= 1.0) {                                // avance dans la RAM
            phase_ -= 1.0;
            curAddr_ += step;
            if (curAddr_ >= endAddr_) {                        // fin de trame
                if (ctrl_ & 0x02) {                            // repeat : reboucle
                    curAddr_ = startAddr_;
                } else {                                       // sinon : arrêt
                    playing_ = false;
                    ctrl_ &= ~0x01;
                    return;                                    // reste de `out` = silence
                }
            }
            // Octet suivant tiré à la cadence DMA → filtré ICI (pas à la cadence
            // hôte) : c'est ce qui fait l'anti-repliement.
            dmaCur_ = lowPassPull(sampleAt(curAddr_, stereo), lowPass);
        }
    }
}

// Rend `count` échantillons stéréo (entrelacés dans `st`) depuis l'état AUDIO (aXxx_),
// qu'il avance. `ym` est aligné sur le 1er échantillon du segment. DMA audio inactif →
// YM recopié L=R. Même moteur que mix() (resample bloquant-zéro + FIR anti-repliement),
// mais sur l'état audio découplé du CPU (cf. en-tête mixStereo).
void DmaSound::mixSegment(float* st, const float* ym, uint32_t count, uint32_t sampleRate) {
    if (!aPlaying_ || sampleRate == 0) {                      // DMA inactif → YM seul (L=R)
        for (uint32_t i = 0; i < count; ++i) { st[2 * i] = ym[i]; st[2 * i + 1] = ym[i]; }
        return;
    }
    const double inc    = kRate[aMode_ & 0x03] / sampleRate;
    const bool   stereo = !(aMode_ & 0x80);
    const uint32_t step = stereo ? 2u : 1u;
    const bool lowPass  = kRate[aMode_ & 0x03] > double(sampleRate);
    const bool addYm    = (mwMixing_ == 1);                   // 1 = YM+DMA, sinon DMA écrase YM (live)

    for (uint32_t i = 0; i < count; ++i) {
        if (!aHaveCur_) {
            int l, r; sampleAtLR(aAddr_, stereo, l, r);
            dmaCurL_ = fir121(lpW0L_, lpW1L_, l, lowPass);
            dmaCurR_ = fir121(lpW0R_, lpW1R_, r, lowPass);
            aHaveCur_ = true;
        }
        const float dl = (dmaCurL_ / 4.0f / 128.0f) * kDmaGain;
        const float dr = (dmaCurR_ / 4.0f / 128.0f) * kDmaGain;
        if (addYm) { st[2 * i] = ym[i] + dl; st[2 * i + 1] = ym[i] + dr; }
        else       { st[2 * i] = dl;         st[2 * i + 1] = dr; }
        aPhase_ += inc;
        while (aPhase_ >= 1.0) {
            aPhase_ -= 1.0;
            aAddr_ += step;
            if (aAddr_ >= aEnd_) {
                if (aRepeat_) {
                    aAddr_ = aStart_;
                } else {                                       // one-shot fini : YM passe sur le reste
                    aPlaying_ = false;
                    for (uint32_t j = i + 1; j < count; ++j) { st[2 * j] = ym[j]; st[2 * j + 1] = ym[j]; }
                    return;
                }
            }
            int l, r; sampleAtLR(aAddr_, stereo, l, r);
            dmaCurL_ = fir121(lpW0L_, lpW1L_, l, lowPass);
            dmaCurR_ = fir121(lpW0R_, lpW1R_, r, lowPass);
        }
    }
}

// Version stéréo « push » : rejoue les transitions PLAY/STOP de la trame à leur cycle
// exact (segments entre événements), comme YM2149::synthesizeFrame. Sans horloge
// câblée : repli sur un segment unique aligné sur l'état CPU live (ancien comportement).
void DmaSound::mixStereo(float* st, const float* ym, uint32_t frames, uint32_t sampleRate, int64_t frameCycles) {
    if (!cycleClock_) {                                       // repli legacy (pas de timeline)
        syncAudioFromCpu();
        mixSegment(st, ym, frames, sampleRate);
        return;
    }
    if (frameCycles <= 0) frameCycles = 1;
    uint32_t pos = 0;
    for (const DmaEvent& e : events_) {
        uint32_t off = uint32_t(int64_t(e.cycle) * frames / frameCycles);
        if (off > frames) off = frames;
        if (off > pos) { mixSegment(st + 2 * pos, ym + pos, off - pos, sampleRate); pos = off; }
        if (e.kind == 0) {                                    // PLAY start : (re)latche et recale l'audio
            aPlaying_ = true; aStart_ = e.start; aEnd_ = e.end; aMode_ = e.mode; aRepeat_ = e.repeat;
            aAddr_ = e.start; aPhase_ = 0.0; aHaveCur_ = false;
        } else {                                              // STOP (effacement CPU du bit PLAY)
            aPlaying_ = false;
        }
    }
    if (pos < frames) mixSegment(st + 2 * pos, ym + pos, frames - pos, sampleRate);
    events_.clear();
}
