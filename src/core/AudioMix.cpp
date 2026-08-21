// =============================================================================
//  AudioMix.cpp — cf. AudioMix.hpp. L'ORDRE des étapes est le contrat : il vient
//  de Hatari (dmaSnd.c) et a été calé contre des WAV oracles. Ne pas réordonner.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/AudioMix.hpp"

#include "core/DmaSound.hpp"
#include "core/YM2149.hpp"

namespace neost {

float* mixEmulatedFrame(YM2149& psg, DmaSound* dma, bool dmaOn,
                        uint32_t frames, uint32_t sampleRate, int64_t frameCycles,
                        FrameMixBuffers& buf, float ymGain, float dmaGain) {
    if (frames == 0) {                      // rien à rendre : on DRAINE quand même les
        psg.clearEvents();                  // horodatages, sinon ils s'empilent sans fin
        if (dma) dma->clearEvents();
        return nullptr;
    }
    if (buf.ym.size() < frames)         buf.ym.assign(frames, 0.0f);
    if (buf.st.size() < 2u * frames)    buf.st.assign(2u * frames, 0.0f);
    float* ym = buf.ym.data();
    float* st = buf.st.data();

    // (1) PSG en REJOUANT ses écritures horodatées : c'est cette étape qui rend les
    //     modulations sous-trame — digidrums (volume écrit à plusieurs kHz) et
    //     sync-buzzer. Lire les registres « en direct » les aplatit.
    psg.synthesizeFrame(ym, frames, sampleRate, frameCycles);
    if (ymGain != 1.0f) for (uint32_t i = 0; i < frames; ++i) ym[i] *= ymGain;   // fader YM

    if (dmaOn && dma) {
        // (2) DMA STE horodaté → L/R ; le fader DMA est appliqué DANS mixSegment, sur la
        //     seule voie DMA — le YM et le bit de mixage LMC (YM+DMA / DMA seul) restent
        //     fidèles (un « YM ajouté après coup » l'aurait réveillé quand le LMC le coupe).
        dma->mixStereo(st, ym, frames, sampleRate, frameCycles, dmaGain);
        dma->applyHpfStereo(st, frames);                          // (2b) HPF sous-sonique du MIX
        const float gL = dma->gainLeft(), gR = dma->gainRight();  // (3) volume maître × G/D
        if (gL != 1.0f || gR != 1.0f)
            for (uint32_t i = 0; i < frames; ++i) { st[2 * i] *= gL; st[2 * i + 1] *= gR; }
        dma->applyToneStereo(st, frames, sampleRate);             // (4) basses/aigus LMC1992
    } else {
        if (dma) dma->clearEvents();        // machine sans DMA : drainer quand même
        for (uint32_t i = 0; i < frames; ++i) { st[2 * i] = ym[i]; st[2 * i + 1] = ym[i]; }
    }
    return st;
}

}  // namespace neost
