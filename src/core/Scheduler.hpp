// =============================================================================
//  Scheduler.hpp — Ordonnanceur d'événements datés (horloge en cycles).
//
//  Calqué sur l'idée d'Hatari (`cycInt.c`) : plutôt que de tester chaque source
//  d'interruption à chaque ligne/trame, on garde, par source, le CYCLE auquel son
//  prochain événement est dû. La boucle d'horloge exécute le CPU jusqu'au plus
//  proche événement, puis déclenche les callbacks échus (qui peuvent se
//  replanifier). Voir docs/CYCLE_ACCURACY.md.
//
//  Phase 1 (refactor iso-comportement) : seules les sources actuelles existent
//  (HBL, Timer C, VBL) et le quantum CPU reste la ligne (512 cycles) — le timing
//  produit est identique au modèle « par blocs » précédent. Les phases suivantes
//  ajouteront des sources (Timers A/B/D, FDC, DMA…) et affineront le quantum.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include "core/StateArchive.hpp"

class Scheduler {
public:
    // Sources ordonnancées. L'ORDRE de l'énum = priorité à cycle égal (cf. runTo).
    // RENDER ≈ fin Display-Enable (376) ; TIMER_B = event-count sur DE (400, piloté
    // par Machine) ; HBL niveau 2 = 508 ; VBL. Les Timers A/C/D (mode délai) sont
    // datés par le MFP lui-même (période calculée des registres TxCR/TxDR).
    // FDC = fin de commande (BUSY→INTRQ) ; FDC_INDEX = impulsion d'index du
    // lecteur (1/tour, ~200 ms à 300 tr/min) tant que le moteur tourne ;
    // DMASND = fin de trame du son DMA STE (→ event-count Timer A du MFP).
    // IKBD = réponse différée du clavier (ex. $F1 ~502000 cycles après le reset
    // $80,$01, comme Hatari) : l'IRQ ACIA doit arriver PENDANT que le code attend,
    // pas instantanément (sinon elle est levée avant l'armement et perdue).
    // IKBD_TX = re-remplissage du registre d'émission de l'ACIA clavier (TDRE) ~1
    // octet série après une écriture $FFFC02, UNIQUEMENT quand l'IRQ d'émission
    // (TIE, CR bits5-6=01) est armée : re-lève l'IRQ « transmetteur prêt » qui
    // cadence l'envoi des commandes IKBD piloté par interruption (cf. Hades Nebula).
    // MICROWIRE = shift série du registre Microwire ($FF8922) du son STE : 16
    // décalages (8 cycles chacun, cf. Hatari) avant que la commande LMC1992 soit
    // décodée et que $FF8922 retombe à 0. Des diagnostics POLLENT $FF8922 jusqu'à 0.
    // TIMER_B = tic event-count de Timer B (une fois/ligne affichée, piloté par Machine).
    // TIMER_B_DELAY = Timer B en mode DÉLAI (prescaler/données comme A/C/D), daté par le
    // MFP. Les deux modes de Timer B sont exclusifs (TBCR = 8 → event-count, 1-7 → délai),
    // d'où deux sources distinctes sans conflit.
    // MFP_IRQ = instant où le signal IRQ du MFP devient VISIBLE du CPU : le 68901 met
    // 4 cycles à propager IRQ vers le 68000 (port MFP_IRQ_DELAY_TO_CPU, mfp.c:374).
    // Le MFP l'arme à irqTime+4 quand son IRQ monte ; le callback recalcule l'IPL.
    // Placé APRÈS les timers (à cycle égal, l'IPL est recalculé une fois les IRQ levées).
    // IKBD_RX = livraison cadencée IKBD → ACIA : un octet de la file ne devient
    // visible (RDRF) qu'après ~1 octet série (10 bits à 7812,5 bauds = 10240
    // cycles), comme le SCI d'Hatari. Des jeux (Vroom) identifient les octets du
    // paquet souris à leur CADENCE d'arrivée : une livraison instantanée des 3
    // octets fait perdre la synchro de leur parseur (axes souris « tournés »).
    // MIDI_TX = même rôle qu'IKBD_TX pour l'ACIA MIDI ($FFFC04/06) : TDRE se
    // re-remplit ~1 octet MIDI (10 bits à 31250 bauds = 2560 cycles) après une
    // écriture donnée sous TIE — cadence l'IRQ « transmetteur prêt » des
    // séquenceurs qui streament la sortie MIDI par interruption (cf. midi.c).
    // BLITTER = tranche non-hog suivante du blitter : 64 accès bus blitter (256
    // cycles, CPU arrêté) alternés avec 64 accès CPU (cf. Blitter::onSlice).
    // VC_RESTART = rechargement du compteur vidéo en fin de trame (port
    // Video_RestartVideoCounter : ligne 310/260, cycle 56 [+4 STE]) — cf. Machine.
    enum Source { RENDER, TIMER_A, TIMER_B, TIMER_B_DELAY, TIMER_C, TIMER_D, MFP_IRQ, FDC, FDC_INDEX, DMASND, IKBD, IKBD_RX, IKBD_TX, MIDI_TX, MICROWIRE, BLITTER, VC_RESTART, HBL, VBL, SRC_COUNT };

    using Callback = std::function<void()>;

    Scheduler() { due_.fill(kInactive); }

    void setCallback(Source s, Callback cb) { cb_[s] = std::move(cb); }

    // Horloge « live » = cycle CPU absolu EXACT, même au milieu d'une instruction
    // (now_ + cycles déjà consommés dans le quantum run() en cours). Indispensable
    // pour dater un timer programmé en plein milieu d'un bloc CPU : sans ça il
    // serait calé sur le DÉBUT du quantum (jusqu'à ~380 cycles trop tôt). C'est le
    // `Cycles_GetClockCounterImmediate()` d'Hatari (cf. cycInt.c). Sous Moira
    // (cœur cycle-exact) cette horloge est précise à la SOUS-instruction.
    void setLiveClock(std::function<int64_t()> fn) { liveClock_ = std::move(fn); }
    int64_t liveNow() const { return liveClock_ ? liveClock_() : now_; }

    // Préemption du timeslice CPU : quand un événement est posé AVANT la cible du
    // bloc CPU en cours, on coupe le bloc (le CPU finit son instruction puis rend
    // la main) pour que la boucle d'horloge re-planifie et s'arrête PRÈS de cet
    // instant → latence IRQ ~1 instruction, comme Hatari (qui re-teste
    // PendingInterruptCount à chaque instruction). Sans ça, un timer court armé en
    // plein bloc ne se déclenche qu'à la fin du bloc (jusqu'à ~380 cycles de retard).
    void setEndSlice(std::function<void()> fn) { endSlice_ = std::move(fn); }
    void beginRun(int64_t target) { runTarget_ = target; }
    void endRun() { runTarget_ = kInactive; }

    // Réarme l'horloge à 0 et désactive tous les événements (début de trame).
    void reset() {
        now_ = 0;
        for (auto& d : due_) d = kInactive;
        runTarget_ = kInactive;
        nextDue_   = kInactive;
    }

    // Programme (ou reprogramme) l'événement `s` au cycle absolu `atCycle`.
    void schedule(Source s, int64_t atCycle) {
        due_[s] = atCycle;
        // Cache O(1) du plus proche événement dû (cf. nextDue_/syncTo) : un nouvel
        // événement plus tôt avance le cache ; sinon il reste valide (recalculé au
        // dispatch ou au cancel de l'échéance minimale).
        if (nextDue_ == kInactive || atCycle < nextDue_) nextDue_ = atCycle;
        // Si on est en plein bloc CPU (runTarget_ armé) et que cet événement tombe
        // AVANT la cible du bloc, on préempte : le CPU rend la main à la prochaine
        // frontière d'instruction et la boucle d'horloge ré-évaluera nextDue().
        // (Dormant dans le modèle piloté par sync() : beginRun n'est plus appelé.)
        if (runTarget_ != kInactive && atCycle < runTarget_ && endSlice_) {
            runTarget_ = atCycle;   // nouvelle cible effective (évite des coupes redondantes)
            ++preemptions;
            endSlice_();
        }
    }
    void cancel(Source s) {
        const bool wasMin = (due_[s] != kInactive && due_[s] == nextDue_);
        due_[s] = kInactive;
        if (wasMin) nextDue_ = scanNextDue();   // l'échéance minimale disparaît → recalcul
    }

    int64_t now() const { return now_; }

    // Échéance du plus proche événement dû, en O(1) (cache). -1 si aucun. C'est le
    // pendant de PendingInterruptCount d'Hatari : Cpu68k::sync() le compare à
    // l'horloge live à CHAQUE pas pour ne dispatcher que si nécessaire.
    int64_t peekNextDue() const { return nextDue_; }

    // Chemin piloté par Cpu68k::sync() (modèle `do_cycles` WinUAE) : avance l'horloge
    // dispatchée à `cycle` (cycle bus absolu, EN COURS d'instruction) et déclenche les
    // échus. O(1) si rien n'est dû — sync() est appelé à chaque instruction/accès.
    // Les callbacks posent l'IPL au cycle exact → vu par le POLL_IPL de Moira.
    void syncTo(int64_t cycle) {
        if (nextDue_ != kInactive && cycle >= nextDue_) runTo(cycle);
        else now_ = cycle;
    }

    // Cycles CPU restants avant l'échéance de la source `s`, mesurés depuis
    // l'horloge LIVE (sous-instruction). Renvoie -1 si la source n'est pas armée.
    // Équivalent de `CycInt_FindCyclesRemaining` d'Hatari : permet à une puce (le
    // MFP) de lire le COMPTEUR VIVANT d'un timer en mode délai (cf. MFP_ReadTimer_AB/CD).
    int64_t cyclesUntil(Source s) const {
        if (due_[s] == kInactive) return -1;
        const int64_t rem = due_[s] - liveNow();
        return rem > 0 ? rem : 0;
    }

    // Variante NON écrêtée : peut renvoyer un reste NÉGATIF si l'échéance est
    // passée mais pas encore dispatchée (lecture sous-instruction entre le cycle
    // d'expiration et la fin du bloc CPU). Le MFP s'en sert pour replier le
    // compteur vivant modulo la période de RECHARGE (le matériel a déjà rechargé).
    // INT64_MIN si la source n'est pas armée.
    int64_t rawCyclesUntil(Source s) const {
        if (due_[s] == kInactive) return INT64_MIN;
        return due_[s] - liveNow();
    }

    // Cycle du prochain événement dû (>= now), ou -1 si aucun n'est armé.
    // (Scan complet ; pour le chemin chaud sync() utiliser peekNextDue() — O(1).)
    int64_t nextDue() const { return scanNextDue(); }

    // Avance l'horloge jusqu'à `cycle` puis déclenche les événements échus (due <=
    // cycle) DANS L'ORDRE CHRONOLOGIQUE de leurs échéances (comme la liste triée
    // cycInt d'Hatari) — l'ordre de l'énum ne sert que de tie-break à cycle égal.
    // (L'ancien scan par index déclenchait TIMER_A avant un HBL dû 200 cycles plus
    // tôt quand l'overshoot d'un quantum franchissait plusieurs échéances.)
    // Chaque source tire AU PLUS une fois par runTo (garde anti-livelock) ; un
    // callback peut replanifier (échéance > now : re-déclenchée au tour suivant).
    void runTo(int64_t cycle) {
        now_ = cycle;
        uint32_t fired = 0;
        static_assert(SRC_COUNT <= 32, "masque fired sur 32 bits");
        for (;;) {
            int best = -1;
            for (int s = 0; s < SRC_COUNT; ++s)
                if (!(fired & (1u << s)) && due_[s] != kInactive && due_[s] <= now_
                    && (best < 0 || due_[s] < due_[best])) best = s;
            if (best < 0) break;
            // Métrique cycle-accuracy : retard d'un timer MFP daté (now - échéance).
            // Piloté par sync(), il reste ~1 accès (sous-instruction en PRECISE_TIMING).
            if (isMfpTimer(best)) {
                const int64_t late = now_ - due_[best];
                if (late > timerMaxLate) timerMaxLate = late;
            }
            fired |= 1u << best;
            firingDue_ = due_[best];             // échéance servie (pour replanif. anti-dérive)
            due_[best] = kInactive;              // consommé avant l'appel…
            if (cb_[best]) cb_[best]();           // …le callback peut replanifier
        }
        firingDue_ = kInactive;
        nextDue_ = scanNextDue();                    // les callbacks ont pu (re)planifier
    }

    // Échéance de l'événement en cours de dispatch (valide PENDANT son callback),
    // -1 sinon. Permet à une source périodique de se replanifier ancrée sur SON
    // échéance servie (et non l'horloge courante), ne perdant pas le dépassement de
    // latence d'IRQ — port de PendingCyclesOver d'Hatari (cf. Mfp::onTimerExpire).
    int64_t firingDue() const { return firingDue_; }

    // Diagnostics (chantier précision cycle) : pire retard d'un timer MFP daté, et
    // nombre de préemptions du timeslice CPU déclenchées. Lus par le headless.
    int64_t timerMaxLate = 0;
    long    preemptions  = 0;

    // Save-state : échéances par source + horloges. Les callbacks (cb_), la liveClock_
    // et l'endSlice_ sont re-liés à la construction de Machine → PAS sérialisés.
    void serialize(StateArchive& ar) {
        ar(due_); ar(now_); ar(nextDue_); ar(runTarget_);
        // ⚠ L'horloge du scheduler pilote des boucles de rattrapage (syncTo, et côté FDC
        // le rattrapage d'impulsion index) : forgée absurde, elle les rend non bornées —
        // gel définitif reproduit. Une échéance doit être soit inactive, soit dans une
        // fenêtre plausible autour de `now_` (le passé récent existe : une échéance déjà
        // due mais pas encore servie).
        ar.check(now_ >= 0);
        for (int64_t d : due_)
            ar.check(d == kInactive || (d >= now_ - (1LL << 32) && d <= now_ + (1LL << 40)));
        ar.check(nextDue_   == kInactive || (nextDue_   >= now_ - (1LL << 32) && nextDue_   <= now_ + (1LL << 40)));
        ar.check(runTarget_ == kInactive || (runTarget_ >= now_ - (1LL << 32) && runTarget_ <= now_ + (1LL << 40)));
    }

private:
    static bool isMfpTimer(int s) {              // sources dont le retard dépend de la préemption
        return s == TIMER_A || s == TIMER_B_DELAY || s == TIMER_C || s == TIMER_D;
    }
    // Scan complet du plus proche événement dû (-1 si aucun). Sert à RAFRAÎCHIR le
    // cache nextDue_ aux rares points où l'échéance minimale peut disparaître (après
    // dispatch — les callbacks replanifient — et au cancel de l'échéance minimale).
    int64_t scanNextDue() const {
        int64_t best = -1;
        for (int s = 0; s < SRC_COUNT; ++s)
            if (due_[s] != kInactive && (best < 0 || due_[s] < best)) best = due_[s];
        return best;
    }
    static constexpr int64_t kInactive = -1;
    std::array<int64_t, SRC_COUNT>  due_{};      // (rempli à kInactive au ctor)
    std::array<Callback, SRC_COUNT> cb_{};
    int64_t now_ = 0;
    int64_t nextDue_ = kInactive;                // cache O(1) du plus proche dû (cf. syncTo)
    int64_t firingDue_ = kInactive;              // échéance de l'événement en cours de dispatch
    int64_t runTarget_ = kInactive;              // cible du bloc CPU courant (-1 = hors run)
    std::function<int64_t()> liveClock_{};       // horloge sous-quantum (cf. liveNow)
    std::function<void()>    endSlice_{};         // coupe le timeslice CPU (préemption)
};
