// =============================================================================
//  Audio.hpp — Backend de sortie son (miniaudio) : YM2149 + bruits de lecteur.
//
//  miniaudio ouvre UN périphérique de lecture et appelle, depuis un thread
//  dédié, un callback qui : (1) synthétise le PSG, puis (2) additionne la sortie
//  de DriveSound (ronron/clics/seek/index). Un seul flux mono float sort donc —
//  c'est le point de mixage du YM2149 et des bruits mécaniques du lecteur.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

#include "audio/SampleRing.hpp"

class YM2149;
class DriveSound;
class DmaSound;

class Audio {
public:
    // drive / dma peuvent être nuls — seul le PSG sort alors.
    explicit Audio(YM2149& psg, DriveSound* drive = nullptr, DmaSound* dma = nullptr);
    ~Audio();

    bool start();              // ouvre et démarre le périphérique
    void stop();
    bool ok() const { return started_; }

    // Consommateur (thread audio miniaudio) : recopie l'anneau dans `out`, silence si
    // underrun. Ne synthétise plus rien — toute la génération est faite en amont par
    // produceFrame sur le thread d'émulation (modèle « push » de la Phase C).
    void render(float* out, uint32_t frames, uint32_t sampleRate);

    // Producteur (thread d'émulation) : génère le son d'UNE trame (PSG horodaté + son DMA
    // + LMC1992 + bruits lecteur, clampé) et le pousse dans l'anneau. `frameCycles` = durée
    // de la trame en cycles CPU (pour dater les écritures PSG et calibrer le nombre
    // d'échantillons). À appeler APRÈS Machine::runFrame.
    void produceFrame(int64_t frameCycles);

    // Volume maître de la SORTIE (0..1) — réglage utilisateur (barre de menu, persisté
    // dans neost.cfg), appliqué au mix final avant clamp. Indépendant du LMC1992 ÉMULÉ
    // (qui, lui, appartient à la machine). Lu/écrit sur le seul thread d'émulation.
    void  setMasterVolume(float v) { masterVol_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    float masterVolume() const { return masterVol_; }

private:
    YM2149&     psg_;
    DriveSound* drive_   = nullptr;
    DmaSound*   dma_     = nullptr;
    bool        started_ = false;
    void*       device_  = nullptr;   // ma_device opaque (évite d'inclure miniaudio ici)

    // --- Modèle « push » (Phase C) : anneau émulation → audio --------------------
    // L'anneau stocke des échantillons ENTRELACÉS L/R (2 floats par frame). `primeSamples_`,
    // `n`, `sampleCarry_` sont comptés en FRAMES (par canal) ; ring_.available()/space()
    // comptent en floats → toujours convertir (×2 / ÷2) au passage de frontière.
    SampleRing         ring_{32768};     // SPSC entrelacé : ~340 ms de marge à 48 kHz stéréo
    std::vector<float> scratch_;         // sortie stéréo entrelacée (2×frames) de produceFrame
    std::vector<float> ymScratch_;       // voie YM mono intermédiaire (frames)
    std::vector<float> driveScratch_;    // bruits lecteur mono intermédiaires (frames)
    uint32_t           rate_ = 48000;    // fréquence de sortie réelle du périphérique (frames/s)
    float              masterVol_ = 1.0f; // volume maître utilisateur (cf. setMasterVolume)
    double             sampleCarry_ = 0.0; // report fractionnaire (nb d'échantillons/trame exact à long terme)
    uint32_t           primeSamples_ = 4000; // coussin cible (≈ latence visée, ~85 ms) — amorçage + asservissement
    bool               primed_ = false;  // (thread audio) : l'anneau a-t-il atteint le coussin ? sinon → silence

    // Diagnostic « son haché » : nombre d'underruns de l'anneau (le thread audio a
    // voulu drainer plus que produit → trou de ~85 ms le temps de re-amorcer).
    // Incrémenté par render() (thread audio), surveillé par produceFrame() (thread
    // émulation) qui avertit sur stderr — un underrun RÉPÉTÉ signifie que la boucle
    // d'émulation ne tient pas la cadence des trames (cf. bridage dans main.cpp).
    std::atomic<uint32_t> underruns_{0};
    uint32_t              underrunsSeen_ = 0;   // (thread émulation) dernier total signalé
    int64_t               underrunMuteFrames_ = 0; // anti-spam : trames restantes avant re-signalement
};
