// =============================================================================
//  DmaSound.hpp — Son DMA de l'Atari STE ($FF8900-$FF8925).
//
//  Le STE ajoute un canal de son NUMÉRIQUE : un DMA lit des échantillons signés
//  8 bits en RAM, d'une adresse de début à une adresse de fin, à une fréquence
//  choisie (6.25 / 12.5 / 25 / 50 kHz), en mono ou stéréo entrelacé. À la fin de
//  la trame : arrêt, ou rebouclage si le bit « repeat » est posé.
//
//  Modèle (comme le YM2149) : la lecture des échantillons se fait sur le thread
//  audio (mix), par rééchantillonnage bloquant-zéro vers la fréquence de sortie.
//  Le compteur d'adresse courant est exposé (lecture CPU). Le mixage final
//  (YM2149 + ce canal) est fait par Audio (GUI) / neost_audio_render (WASM).
//
//  Réf. : Hatari dmaSnd.c, doc registres STE. (c) 2026 VERHILLE Arnaud — NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

class Bus;
class Mfp;
class Scheduler;

class DmaSound {
public:
    explicit DmaSound(Bus& bus) : bus_(bus) {}

    // L'ordonnanceur date la fin de trame (→ event-count Timer A du MFP), sur le
    // thread d'émulation : c'est là que doit tomber l'interruption, pas sur le
    // thread audio (qui ne fait que générer le son). Branchés par Machine.
    void setScheduler(Scheduler* s) { sched_ = s; }
    // Câble le MFP et lui SIGNALE la présence du son DMA (modèle STE/Mega STE) :
    // le MFP n'XOR la ligne XSINT dans GPIP7 que si ce flag est posé (cf. Hatari
    // MFP_Main_Compute_GPIP7 : XOR réservé à Config_IsMachineSTE()/TT()).
    void setMfp(Mfp* m);

    // Échéance « fin de trame DMA » : pulse Timer A (TAI), reboucle si repeat.
    void onFrameEnd();

    // MMIO $FF8900-$FF8925 (octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Additionne le canal numérique dans `out` (mono float) — thread audio. Chemin
    // LEGACY conservé pour la sortie mono WASM (neost_audio_render).
    void    mix(float* out, uint32_t frames, uint32_t sampleRate);

    // Version STÉRÉO (chemin natif GUI) : écrit `st` ENTRELACÉ L/R (2×frames floats)
    // à partir de la voie YM mono `ym` (frames échantillons). Le DMA STE porte une
    // vraie image L/R (octets pairs/impairs en stéréo) → la séparation est PRÉSERVÉE
    // jusqu'à la sortie au lieu d'être moyennée. Applique le routage de mixage LMC1992
    // (mixing==1 → YM+DMA ; sinon DMA seul écrase YM) comme `mix`. DMA à l'arrêt → YM
    // recopié sur les deux canaux (choix NeoST : pas de mute du YM hors lecture DMA).
    //
    // Modèle « push » horodaté (comme le YM2149) : si une horloge frame-relative est
    // câblée (setCycleClock), les transitions PLAY/STOP de la trame sont rejouées à leur
    // cycle EXACT — un bruitage one-shot très court (démarré ET fini dans la même trame
    // émulée) n'est plus avalé, et la queue d'un sample n'est plus écrêtée par l'effacement
    // CPU du bit PLAY. `frameCycles` = durée de la trame en cycles CPU (mappe cycle→échantillon).
    void    mixStereo(float* st, const float* ym, uint32_t frames, uint32_t sampleRate, int64_t frameCycles);

    // Branche l'horloge frame-relative (cycles CPU depuis le début de la trame), posée
    // par le frontend audio « push » (cf. main.cpp, comme YM2149::setCycleClock). Tant
    // qu'elle n'est pas posée (headless/WASM), AUCUN événement n'est enregistré (pas de
    // fuite mémoire) et mixStereo retombe sur le rendu live (état CPU courant).
    void    setCycleClock(std::function<int64_t()> c) { cycleClock_ = std::move(c); events_.reserve(256); }

    // Jette les événements de la trame sans rendre (frontend audio non démarré / trame
    // sautée) : borne la mémoire. Appelé par Audio::produceFrame sur les chemins early-out.
    void    clearEvents() { events_.clear(); }

    // Gain de sortie linéaire (LMC1992 : volume maître + gauche/droite, en mono).
    // S'applique à TOUT le son STE (YM2149 + DMA), comme la puce réelle. 1.0 par
    // défaut (0 dB) → aucun effet tant que le microwire n'est pas programmé.
    float   masterGain() const;

    // Gains LINÉAIRES par canal du LMC1992 (volume maître × gauche, × droite). C'est
    // ce qui réalise le PANORAMIQUE STE : un jeu qui programme leftVolume≠rightVolume
    // pousse le son à gauche/droite. 1.0 par défaut (0 dB). Réf. Hatari lmc1992.left/right_gain.
    float   gainLeft() const;
    float   gainRight() const;

    // Correcteur de tonalité du LMC1992 (basses/aigus, ±12 dB) appliqué au mix
    // complet via deux filtres en plateau (shelving). Bypass total au défaut
    // (0/0 dB) → aucun coût ni risque tant que la tonalité n'est pas programmée.
    void    applyTone(float* out, uint32_t frames, uint32_t sampleRate);
    // Idem en stéréo entrelacé (état de filtre L/R indépendant).
    void    applyToneStereo(float* st, uint32_t frames, uint32_t sampleRate);

    // RESET machine : coupe la lecture. `cold` = reset à FROID (power-cycle) → ré-init
    // du LMC1992 ; à chaud (Ctrl+reset) le Microwire n'a PAS de signal de reset et
    // conserve ses volumes/mixage (cf. Hatari DmaSnd_Reset, bloc `if (bCold)`).
    void    reset(bool cold = false);
    bool    playing() const { return playing_; }

private:
    int     sampleAt(uint32_t addr, bool stereo) const;   // octet(s) RAM → -128..127 mono
    void    sampleAtLR(uint32_t addr, bool stereo, int& l, int& r) const;  // octets RAM → L/R séparés
    // Rend `count` échantillons stéréo dans `st` (entrelacé) à partir de l'état audio
    // (aPlaying_/aAddr_…) en avançant ce dernier ; `ym` aligné sur le 1er échantillon.
    void    mixSegment(float* st, const float* ym, uint32_t count, uint32_t sampleRate);
    // Enregistre un événement de trame DMA daté (si l'horloge push est câblée).
    void    recordEvent(uint8_t kind);
    // Synchronise l'état audio (aXxx_) sur l'état CPU live (repli sans horloge push).
    void    syncAudioFromCpu();
    void    decodeMicrowire();                            // décode la commande LMC1992
    void    scheduleFrameEnd();                           // date la prochaine fin de trame
    void    startNewFrame();                              // (re)démarre une trame (gère start==end)
    void    setXsint(bool level);                         // pilote la ligne XSINT (→ MFP GPIP7)
    // Position LIVE cycle-exacte du compteur de trame ($FF8909/0B/0D) : calculée
    // depuis l'horloge ÉMULÉE (équivalent du Sound_Update en tête de
    // DmaSnd_GetFrameCount chez Hatari), pas depuis la production audio hôte —
    // indispensable en headless (mix() n'y tourne jamais → compteur figé avant)
    // et pour les programmes qui POLLENT le compteur pour se synchroniser.
    uint32_t liveCounter() const;

public:
    // Une étape du shift série Microwire (datée par le Scheduler, source MICROWIRE) :
    // décale $FF8922 vers 0 (16 étapes) puis, à 0, décode la commande LMC1992. Sans
    // ce shift, les diagnostics qui pollent $FF8922 jusqu'à 0 (STE_Test) bouclent.
    void    onMicrowireShift();
private:

    Bus&        bus_;
    Mfp*        mfp_   = nullptr;
    Scheduler*  sched_ = nullptr;

    // Registres. Adresses sur 24 bits (octets haut/moyen/bas, paires forcées).
    uint8_t  ctrl_ = 0;              // $FF8901 : bit0 = play, bit1 = repeat (loop)
    uint8_t  mode_ = 0;              // $FF8921 : bits0-1 fréquence, bit7 = mono
    uint32_t startAddr_ = 0;         // $FF8903/05/07
    uint32_t endAddr_   = 0;         // $FF890F/11/13
    uint32_t curAddr_   = 0;         // position de la SYNTHÈSE audio hôte (cf. mix)
    // Trame DMA en cours, LATCHÉE au démarrage (port DmaSnd_StartNewFrame : le HW
    // fige début/fin à l'ouverture de la trame ; réécrire $FF8903+ pendant la
    // lecture ne vaut que pour la trame SUIVANTE). Sert au compteur live $FF8909+.
    uint32_t frameStartAddr_  = 0;   // adresse de début latchée
    uint32_t frameEndAddr_    = 0;   // adresse de fin latchée
    int64_t  frameStartCycle_ = 0;   // cycle (horloge émulée live) du début de trame
    uint16_t mwData_ = 0, mwMask_ = 0;  // microwire $FF8922/$FF8924 (mots 16 bits)
    uint16_t mwShift_ = 0;              // valeur LUE en $FF8922 pendant le shift (→ 0)
    int      mwSteps_ = 0;              // décalages restants (16 au départ, 0 = fini)

    // LMC1992 décodé (volumes en pas de 2 dB). Défauts = 0 dB (aucune atténuation).
    int      mwMaster_ = 40;         // 0..40 → -80..0 dB (volume maître)
    int      mwLeft_   = 20;         // 0..20 → -40..0 dB
    int      mwRight_  = 20;
    int      mwBass_   = 6;          // stockés ; filtrage tonalité = TODO
    int      mwTreble_ = 6;
    int      mwMixing_ = 0;

    // État de lecture (thread audio).
    bool     playing_ = false;
    double   phase_   = 0.0;         // accumulateur de rééchantillonnage
    // Échantillon DMA COURANT, filtré à la cadence DMA (cf. lowPassPull) — tenu
    // entre deux octets (zéro-bloquant côté sortie, FIR côté entrée).
    float    lowPassPull(int in, bool enabled);
    float    dmaCur_  = 0.0f;
    bool     haveCur_ = false;
    float    lpW0_ = 0.0f, lpW1_ = 0.0f;   // retards du FIR (1,2,1)/4 anti-repliement (chemin mono)
    // Chemin STÉRÉO : échantillon courant et retards FIR séparés par canal.
    float    dmaCurL_ = 0.0f, dmaCurR_ = 0.0f;
    float    lpW0L_ = 0.0f, lpW1L_ = 0.0f, lpW0R_ = 0.0f, lpW1R_ = 0.0f;

    // --- État de lecture côté AUDIO (modèle push horodaté) -------------------------
    // Miroir de la trame DMA vu par le RENDU audio, distinct de l'état CPU (playing_,
    // curAddr_…) qui pilote la logique du jeu (compteur $FF8909, Timer A). Avancé par
    // mixSegment et (re)posé par les événements rejoués → permet de finir un sample
    // après l'effacement CPU du bit PLAY, et de jouer un one-shot intra-trame. PERSISTE
    // entre les trames (sample multi-trames).
    bool     aPlaying_ = false;
    uint32_t aStart_ = 0, aEnd_ = 0, aAddr_ = 0;
    uint8_t  aMode_ = 0;
    bool     aRepeat_ = false;
    double   aPhase_ = 0.0;
    bool     aHaveCur_ = false;

    // Événement de trame DMA horodaté (cycle CPU frame-relatif). kind : 0 = PLAY start
    // (start/end/mode/repeat latchés), 1 = STOP (bit PLAY effacé par le CPU).
    struct DmaEvent { uint32_t cycle; uint8_t kind; uint32_t start, end; uint8_t mode; bool repeat; };
    std::vector<DmaEvent>    events_;            // transitions de la trame courante
    std::function<int64_t()> cycleClock_;        // cycle CPU frame-relatif (posé par le frontend push)

    // Ligne XSINT (External Sound INTerrupt) du son DMA STE : HAUT pendant qu'une
    // trame joue, BAS à l'arrêt / fin de trame. Câblée à TAI (Timer A event-count,
    // déjà géré via onFrameEnd) ET à GPIP7 du MFP (XOR avec la détection moniteur).
    // Réf. Hatari DmaSnd_Update_XSINT_Line / DmaSnd_Get_XSINT_Line.
    bool     xsint_ = false;

    // État des filtres de tonalité (biquads Direct Form I), thread audio. Le jeu
    // « sans suffixe » sert le canal GAUCHE (et le chemin mono WASM) ; le jeu *R_ le
    // canal DROIT en stéréo. Les deux ne tournent jamais ensemble dans un même binaire.
    double   bx1_ = 0, bx2_ = 0, by1_ = 0, by2_ = 0;   // plateau basses (G / mono)
    double   tx1_ = 0, tx2_ = 0, ty1_ = 0, ty2_ = 0;   // plateau aigus  (G / mono)
    double   bx1R_ = 0, bx2R_ = 0, by1R_ = 0, by2R_ = 0;   // plateau basses (D)
    double   tx1R_ = 0, tx2R_ = 0, ty1R_ = 0, ty2R_ = 0;   // plateau aigus  (D)
};
