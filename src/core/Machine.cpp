// =============================================================================
//  Machine.cpp — Câblage des composants + boucle d'horloge d'une trame.
//
//  Depuis la Phase 1 de cycle-accuracy (cf. docs/CYCLE_ACCURACY.md), la trame est
//  pilotée par un ordonnanceur d'événements datés (`Scheduler`) au lieu d'une
//  boucle « 313 lignes × 512 cycles » avec des `if` en ligne. À ce stade le
//  timing produit reste STRICTEMENT IDENTIQUE (quantum CPU = la ligne) : c'est un
//  refactor de structure, validé par diff de trace. Les phases suivantes
//  resserreront le quantum et ajouteront des sources (Timers A/B/D, FDC, DMA…).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Machine.hpp"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>

// Modèle de dispatch BLOC = DÉFAUT (le sync-driven mid-instruction est RÉFUTÉ : il
// deadlockait Enchanted Land sans corriger le jitter). CPU borné à l'événement suivant
// + dispatch à la frontière via runTo, en GARDANT PT=true + RAM_SLOT (la convergence
// cycle d'instruction est indépendante du dispatch). VALIDÉ : EL remarche (jeu rendu),
// étalons 19/0, différentiel 14/14. Le sync-driven reste accessible en OPT-IN pour A/B.
static const bool g_blockDispatch = std::getenv("NEOST_SYNC_DISPATCH") == nullptr;

// Port de Hatari TOS_CheckSysConfig (sous-ensemble utile à NeoST) : abaisse la
// machine si le TOS chargé ne la supporte pas. Seul le cas « TOS <= 1.04 → ST »
// est porté (les autres règles d'Hatari visent TT/Falcon, hors champ NeoST).
MachineType Machine::adjustMachineForTos(MachineType requested, const std::string& romPath) {
    std::ifstream f(romPath, std::ios::binary);
    if (!f) return requested;                 // introuvable → loadTos signalera l'erreur
    uint8_t b[2] = {0, 0};
    f.seekg(2);                               // version TOS : mot big-endian à l'offset 2
    f.read(reinterpret_cast<char*>(b), 2);
    const uint16_t tosVer = uint16_t((b[0] << 8) | b[1]);
    // TOS <= 1.04 (TOS 1.0x ; EmuTOS 192 Ko se présente en « Atari ST » 1.4) ne gère ni
    // le STE ni le Mega STE → Hatari bascule en mode ST. machineIsSte() = STE || Mega STE
    // (le Mega ST tourne nativement sous TOS 1.0x, donc PAS de bascule).
    if (tosVer <= 0x0104 && machineIsSte(requested)) {
        std::fprintf(stderr,
            "[NeoST] TOS %X.%02X ne fonctionne qu'en mode ST (68000) — bascule %s -> ST.\n"
            "        Pour le STE/Mega STE, utiliser EmuTOS 256 Ko (etos256*) ou TOS 1.62/2.06.\n",
            tosVer >> 8, tosVer & 0xFF, machineName(requested));
        return MachineType::St;
    }
    // Le Mega STE exige un TOS ≥ 2.0x : lui seul programme le cache 16 Ko, le SCU et
    // les 16 MHz. Un TOS 1.0x/1.6x (STE) ne connaît pas ce matériel et reste bloqué au
    // boot (combo inexistant sur vrai matériel — chaque machine a son ROM). On bascule
    // donc en STE, où ce TOS tourne normalement (cf. bug cube3d : Mega STE + TOS 1.62
    // = écran vide, alors que STE + 1.62 fonctionne).
    if (tosVer < 0x0200 && requested == MachineType::MegaSte) {
        std::fprintf(stderr,
            "[NeoST] TOS %X.%02X ne gère pas le Mega STE (cache/SCU/16 MHz) — bascule Mega STE -> STE.\n"
            "        Pour un vrai Mega STE, utiliser TOS 2.06 (tos206*) ou EmuTOS 256 Ko (etos256*).\n",
            tosVer >> 8, tosVer & 0xFF);
        return MachineType::Ste;
    }
    return requested;
}

namespace { // Décalage de la position d'IRQ HBL dans la ligne, relatif à cpl.
// Hatari Hbl_Int_Pos = cpl−4 en WS1, cpl en WS2/3/4 ET sur STE (video.c:977-979,
// 1055-1057) — tranché WS3 (2026-07-08, cf. glue:: dans Shifter.cpp) → défaut 0
// (HBL à 508/512 selon la fréquence, à la FRONTIÈRE de ligne comme l'oracle).
// Le STE n'a PAS de wakestate : toujours 0, même en A/B NEOST_WS=1.
// NEOST_HBL_OFF garde la main pour l'A/B (ex. −4 = ancien hybride WS1).
// Historique : l'ancien défaut −4 citait la table WS1 ; l'A/B à 0 avait été
// écarté à l'époque car il compensait une broche IPL levée en retard au dispatch
// de bloc — corrigé depuis par le pré-armement (Cpu68k::armHblPinAt).
int kHblOff(bool isSte) {
    static const bool envSet = std::getenv("NEOST_HBL_OFF") != nullptr;
    static const int  envVal = envSet ? std::atoi(std::getenv("NEOST_HBL_OFF")) : 0;
    if (envSet) return envVal;                             // A/B explicite : prime partout
    return (!isSte && Shifter::wakestate() == 1) ? -4 : 0;
} }

Machine::Machine(std::size_t ramBytes, CpuCore cpuCore, MachineType machine)
    : bus(ramBytes), cpu(bus, cpuCore) {
    machineType_ = machine;
    bus.machine  = machine;         // profil matériel (gating MMIO / bus errors)
    glue.memConfig_ = memConfigForBytes(ramBytes);   // $FF8001 cohérent (EmuTOS recalcule)
    // Branchement des puces sur le bus (le bus ne possède pas les composants).
    bus.shifter = &shifter;
    bus.psg     = &psg;
    bus.glue    = &glue;
    bus.mfp     = &mfp;
    bus.ikbd    = &ikbd;
    bus.fdc     = &fdc;
    bus.dmasnd  = &dmasnd;
    bus.blitter = &blitter;
    bus.rtc     = &rtc;     // horloge RP5C15 (Mega ST / Mega STE)
    bus.midi    = &midi;    // ACIA MIDI ($FFFC04) — bouclage OUT→IN
    bus.cpu     = &cpu;     // pour rafraîchir l'IPL après chaque accès MMIO
    bus.scc     = (machine == MachineType::MegaSte) ? &scc : nullptr;  // SCC série (Mega STE)
    // Joypads STE ($FF9200/02) : seul le Mega STE expose les DIP switches motherboard
    // dans l'octet haut de $FF9200 (cf. StePads / IoMemTabMegaSTE_DIPSwitches_Read).
    bus.stePads.setMegaSte(machine == MachineType::MegaSte);
    // Horloge faisceau pour le compteur d'adresse vidéo $FF8205/07/09 : cycles
    // écoulés depuis le début de la trame courante (cf. Shifter::videoCounter).
    // Horloge LIVE (delta intra-quantum inclus) — INDISPENSABLE : `sched.now()` est
    // figé pendant un bloc CPU et ne bouge qu'aux frontières d'événement, donc un code
    // qui POLLE $FF8209 en boucle serrée (sync raster des démos spec512 : `tst.b (a5);
    // beq`) verrait un compteur GELÉ qui ne saute qu'aux events → la valeur lue ne
    // correspond pas au cycle réel et le stabilisateur (saut calculé dans un nop-slide)
    // se cale de travers → clignotement ±16 cyc. Avec liveNow le compteur suit le cycle
    // exact de l'accès (comme Hatari Video_GetCyclesSinceVbl_OnReadAccess).
    shifter.setBeamClock([this] { return sched.liveNow() - frameStart_; });
    // Horloge LIVE dans la trame (delta intra-quantum CPU inclus) : date au cycle
    // près chaque écriture palette pour le re-rendu spec512 (cf. Shifter::finishFrame).
    shifter.setLiveFrameClock([this] { return sched.liveNow() - frameStart_; });
    // Horloge RTC : cycle CPU ABSOLU exact, même au milieu d'une lecture MMIO.
    // L'horloge maîtresse est désormais le cœur (busClockNow), conduit par sync().
    rtc.setClock([this] { return g_blockDispatch ? (sched.now() + cpu.cyclesRunInQuantum()) : cpu.busClockNow(); });
    // Horloge « live » de l'ordonnanceur = cycle bus absolu live du cœur. Les puces
    // qui datent un événement en plein milieu d'une instruction (MFP timers, compteur
    // vidéo…) s'en servent pour le caler sur l'instant RÉEL de l'accès. C'est l'horloge
    // que sync() avance et sur laquelle il dispatche (cf. NeostMoira::sync).
    sched.setLiveClock([this] { return g_blockDispatch ? (sched.now() + cpu.cyclesRunInQuantum()) : cpu.busClockNow(); });
    // L'ordonnanceur est piloté DEPUIS le hook cycle du cœur (sync) : à chaque pas,
    // sync() avance l'horloge puis dispatche les échus → IPL posé au cycle exact.
    cpu.setScheduler(&sched);
    // Préemption conservée mais DORMANTE (beginRun n'est plus appelé) : le dispatch
    // se fait au fil de sync(), plus besoin de couper le bloc CPU.
    sched.setEndSlice([this] { cpu.endTimeslice(); });
    // V2 res-switch (opt-in NEOST_V2) : la Glue signale une impulsion hi-res PRÉCOCE
    // (cyc ≤ 56) sur la ligne courante → on raccourcit la ligne (HBL reprogrammé à la
    // position hi-res 220 au lieu de cpl-4) et on décale les lignes SUIVANTES de
    // (cpl-224) via lineCarry_. Port de Hatari HBL_Pos/nCyclesPerLine (video.c:2249,
    // Video_AddInterruptHBL 2849) : laisse dériver la phase du gestionnaire fullscreen.
    v2_ = std::getenv("NEOST_V2") != nullptr;
    // Longueurs de ligne PAR-LIGNE (port HBL_Pos/nCyclesPerLine de
    // Video_Update_Glue_State) — **ON par défaut depuis le tranchage WS3
    // (2026-07-08)** : validé étalons TOUS OK + Cuddly 190/250 vs oracle + A/B
    // interne EL/spec512 à 0 px (LX indisponible ici, à re-vérifier au premier
    // disque). NEOST_LINELEN=0 désactive (A/B). À chaque écriture freq/res dont
    // la branche « Freq_match » fixe la géométrie de la ligne HBL COURANTE :
    // l'IRQ HBL de la ligne est REPROGRAMMÉE à lineStart+hblPos (≙
    // Video_AddInterruptHBL) et la longueur (224/508/512) est retenue ; onHbl
    // cumulera le raccourcissement dans lineCarry_ (les événements des lignes
    // suivantes — HBL/Timer B/RENDER — suivent déjà −lineCarry_).
    lineLenOn_ = [] {
        const char* s = std::getenv("NEOST_LINELEN");
        return s ? (std::atoi(s) != 0) : true;
    }();
    shifter.setLineGeom([this](int line, int hblPos, int cyclesLine) {
        if (!lineLenOn_ || line != hblLine_ || line >= lpf_) return;
        const int64_t lineStart = frameStart_ + static_cast<int64_t>(line) * cpl_ - lineCarry_;
        // Ne reprogramme que si l'IRQ HBL de la ligne n'a pas encore été servie
        // (l'événement est encore armé à une position ≥ maintenant).
        sched.schedule(Scheduler::HBL, lineStart + hblPos);
        cpu.armHblPinAt(lineStart + hblPos);
        curLineLen_ = cyclesLine;
        if (std::getenv("NEOST_LINELEN_TRACE")) std::fprintf(stderr,
            "[LLEN] line=%d hblPos=%d len=%d carry=%lld\n", line, hblPos, cyclesLine,
            (long long)lineCarry_);
    });
    shifter.setHblShorten([this] {
        if (!v2_ || hblLine_ == v2ShortLine_ || hblLine_ >= lpf_) return;   // déjà raccourcie / hors trame
        // Cycle DANS la ligne courante (grille décalée par lineCarry_). Seule une
        // impulsion hi-res PRÉCOCE (≤ HDE_On_Low_50=56) raccourcit la ligne — comme
        // les branches Hatari video.c:2246/2268/2288 (pas la branche right-off tardive).
        const int64_t lineStart = static_cast<int64_t>(hblLine_) * cpl_ - lineCarry_;
        const int64_t lineCyc   = (sched.liveNow() - frameStart_) - lineStart;
        if (lineCyc < 0 || lineCyc > 56) return;
        v2ShortLine_ = hblLine_;
        const int kHblPosHi = 224 + kHblOff(machineIsSte(machineType_));            // Hbl_Int_Pos_Hi = cpl_71 + offset HBL (224 WS3, 220 WS1)
        constexpr int kLineLenHi = 224;                   // CYCLES_PER_LINE_71HZ
        sched.schedule(Scheduler::HBL, frameStart_ + lineStart + kHblPosHi);
        lineCarry_ += cpl_ - kLineLenHi;                  // lignes suivantes décalées plus tôt
        if (std::getenv("NEOST_V2_TRACE")) { static long n=0; if (++n % 500 == 0)
            std::fprintf(stderr, "[v2] shorten #%ld line=%d cyc=%lld carry=%lld\n",
                         n, hblLine_, (long long)lineCyc, (long long)lineCarry_); }
    });
    // Connecteur de bouclage RS232 : les sorties RTS (port A bit3) et DTR (bit4) du
    // PSG recopient les entrées de contrôle du MFP — RTS→CTS (GPIP2), DTR→DCD (GPIP1)
    // ET DTR→RI (GPIP6) — comme le câble de test du diagnostic « S RS232 ». Le port A
    // est actif BAS (bit=0 → ligne assertée). On rafraîchit l'IPL (un canal a pu lever).
    psg.setPortASink([this](uint8_t a) {
        if (!mfp.loopback()) return;        // connecteur non branché → lignes inertes
        const bool rts = (a & 0x08) != 0;   // bit3 = 1 → RTS assertée (repos bit=0 → désassertée)
        const bool dtr = (a & 0x10) != 0;   // bit4 = 1 → DTR assertée
        mfp.setRs232Cts(rts);
        mfp.setRs232Dcd(dtr);
        mfp.setRs232Ri(dtr);
        cpu.updateIpl();
    });
    mfp.setScheduler(&sched);   // le MFP date lui-même ses timers (A/C/D, mode délai)
    ikbd.setScheduler(&sched);  // l'IKBD diffère sa réponse de reset ($F1)
    midi.setScheduler(&sched);  // l'ACIA MIDI date son TDRE sous TIE (cf. MIDI_TX)
    // Fixture de bouclage parallèle→joystick (test « Printer/Joystick », sous
    // --loopback) : le diagnostic écrit un motif sur le port parallèle (PSG port B,
    // R15) et attend de le relire sur les lignes joystick. Câblage (décodé du test) :
    //   • bits 0-2 de B → n : direction 1<<n (nibble bas → joy0, nibble haut → joy1)
    //   • bit7 de B → bouton (feu) joystick 0 ; bit6 de B → bouton joystick 1
    // Ligne BUSY Centronics (GPIP0) : sous fixture, le port parallèle (port B) bit7
    // pilote BUSY, inversé (bit7=1 → BUSY assertée → GPIP0=0). Le test « P1 Busy ».
    psg.setPortBSink([this](uint8_t b) {
        if (mfp.loopback()) { mfp.setBusyLine((b & 0x80) != 0); cpu.updateIpl(); }
    });
    // Imprimante Centronics : sur chaque FRONT de strobe (R14 bit5), l'octet du port B
    // est capturé dans le fichier imprimante (si activé via setPrinterFile) et la ligne
    // BUSY (GPIP0) est assertée bas — port fidèle de psg.c:388-390 (Printer_TransferByteTo
    // + MFP_GPIP_Set_Line_Input LINE0 LOW). Sans fichier imprimante : no-op (le défaut, donc
    // comportement inchangé tant que l'imprimante n'est pas explicitement activée).
    psg.setPrinterSink([this](uint8_t b) {
        if (!printerFile_) return;
        std::fputc(b, printerFile_);
        std::fflush(printerFile_);
        mfp.setBusyLine(true);
        cpu.updateIpl();
    });
    ikbd.setJoystickProbe([this](uint8_t& joy0, uint8_t& joy1) {
        // Hors fixture de bouclage : conserver l'état hôte déjà amorcé par l'IKBD
        // (manette USB / émulation clavier posée par le frontend via setJoystick).
        if (!mfp.loopback()) return;
        const uint8_t b = psg.regs_[15];
        const uint8_t dir = uint8_t(1u << (b & 7));          // direction encodée sur 8 bits
        joy0 = uint8_t((dir & 0x0F) | ((b & 0x80) ? 0x80 : 0));   // nibble bas + feu (bit7)
        joy1 = uint8_t(((dir >> 4) & 0x0F) | ((b & 0x40) ? 0x80 : 0)); // nibble haut + feu (bit6)
    });
    fdc.setScheduler(&sched);   // le FDC diffère la fin de commande (BUSY → INTRQ)
    blitter.setScheduler(&sched);  // tranches non-hog du blitter (cf. Scheduler::BLITTER)
    dmasnd.setScheduler(&sched);   // le son DMA date sa consommation DAC (FIFO → Timer A)
    dmasnd.setMfp(&mfp);
    // STE/Mega STE : le YM2149 est mixé à DEMI-amplitude (marge pour le son DMA, évite la
    // saturation) ; ST/Mega ST : pleine amplitude. Cf. YM2149::setOutputScale.
    psg.setOutputScale(machineIsSte(machineType_) ? 0.5f : 1.0f);
    // Filtre de sortie YM (cf. Hatari Sound_Update_Filters) : ST/Mega ST utilisent le
    // passe-bas analogique (C10, LPF_STF) ; STE/Mega STE le PWM (front montant passe-tout).
    psg.setStfLowPass(!machineIsSte(machineType_));

    installSchedulerCallbacks();
}

// -----------------------------------------------------------------------------
//  Ordonnanceur : câblage des handlers et programmation d'une trame.
// -----------------------------------------------------------------------------
void Machine::installSchedulerCallbacks() {
    sched.setCallback(Scheduler::RENDER,  [this] { onRender(); });
    sched.setCallback(Scheduler::TIMER_B, [this] { onTimerB(); });
    sched.setCallback(Scheduler::HBL,     [this] { onHbl(); });
    sched.setCallback(Scheduler::VBL,     [this] { onVbl(); });
    // Timers MFP en mode délai : datés par le MFP, déclenchés ici (IRQ + IPL).
    sched.setCallback(Scheduler::TIMER_A, [this] { mfp.onTimerExpire(0); cpu.updateIpl(); });
    sched.setCallback(Scheduler::TIMER_C, [this] { mfp.onTimerExpire(2); cpu.updateIpl(); });
    sched.setCallback(Scheduler::TIMER_D, [this] { mfp.onTimerExpire(3); cpu.updateIpl(); });
    // Timer B en mode DÉLAI (≠ event-count) : daté par le MFP, déclenché ici.
    sched.setCallback(Scheduler::TIMER_B_DELAY, [this] { mfp.onTimerExpire(1); cpu.updateIpl(); });
    // Visibilité différée du signal IRQ MFP : le 68901 met 4 cycles à propager IRQ
    // vers le CPU (port MFP_IRQ_DELAY_TO_CPU). Le MFP arme cet événement à
    // irqTime+4 ; on recalcule alors l'IPL en mode COMMIT (frontière d'instruction,
    // délai écoulé → l'exception doit partir avant l'instruction suivante, comme
    // Hatari MFP_ProcessIRQ — sinon le délai s'additionnerait au pipeline IPL de
    // Moira et le test « T4 Video Counter » des diagnostics échoue).
    sched.setCallback(Scheduler::MFP_IRQ, [this] { cpu.updateIplNow(); });
    // Machine à états du FDC (port Hatari) : chaque phase (spin-up, head-load,
    // latence rotationnelle, transfert DMA octet par octet, INTRQ, arrêt moteur)
    // est datée et avancée ici. L'INTRQ (GPIP5 + canal 7) peut être levée/effacée.
    sched.setCallback(Scheduler::FDC,     [this] { fdc.onFdcEvent(); cpu.updateIpl(); });
    // (Scheduler::FDC_INDEX n'est plus utilisé : l'index est géré dans la machine
    // à états du FDC — comptage de tours pour spin-up / arrêt moteur, bit INDEX.)
    // (Scheduler::DMASND n'est plus armé : la fin de trame du son DMA STE est
    // détectée au FETCH de la FIFO 8 octets, cadencée par le HBL — DmaSound::onHbl.)
    // Réponse de reset du clavier ($F1) : l'IKBD l'a datée → on l'émet + IRQ ACIA.
    sched.setCallback(Scheduler::IKBD,   [this] { ikbd.onResetResponse(); cpu.updateIpl(); });
    // Livraison cadencée IKBD → ACIA (un octet série ≈ 10240 cycles) : l'octet en
    // tête de file devient visible (RDRF) et lève l'IRQ ACIA (cf. onRxDeliver).
    sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); cpu.updateIpl(); });
    // Re-remplissage du registre d'émission ACIA (TDRE→1) ~1 octet série après une
    // écriture $FFFC02 sous TIE : ré-arme l'IRQ « transmetteur prêt » (cf. onTxEmpty).
    sched.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); cpu.updateIpl(); });
    // Idem pour l'ACIA MIDI (~1 octet à 31250 bauds = 2560 cycles) : cadence l'IRQ
    // d'émission des séquenceurs MIDI (cf. MidiAcia::onTxEmpty).
    sched.setCallback(Scheduler::MIDI_TX, [this] { midi.onTxEmpty(); cpu.updateIpl(); });
    // Étape de shift série Microwire ($FF8922 → 0) du son STE.
    sched.setCallback(Scheduler::MICROWIRE, [this] { dmasnd.onMicrowireShift(); });
    // Tranche non-hog du blitter (64 accès bus / 64 accès CPU) : la fin de blit
    // peut lever l'IRQ GPIP3 (canal 3 MFP) → IPL recalculé.
    sched.setCallback(Scheduler::BLITTER, [this] { blitter.onSlice(); cpu.updateIpl(); });
    // RESTART compteur vidéo fin de trame (Video_RestartVideoCounter) : la fréquence
    // du registre sync est relue À CET INSTANT (live, comme Hatari relit $FF820A) —
    // le restart n'a lieu que si elle correspond à la ligne (50 Hz↔310, 60 Hz↔260).
    sched.setCallback(Scheduler::VC_RESTART, [this] {
        const bool is50 = (shifter.sync & 0x02) != 0;
        if ((is50 && lpf_ == 313) || (!is50 && lpf_ == 263))
            shifter.restartVideoCounter(lpf_ - 3);
    });
}

// Arme les événements VIDÉO de la trame courante, à des cycles ABSOLUS (horloge
// continue) = frameStart_ + position dans la trame. Les Timers A/C/D persistent
// d'une trame à l'autre (datés par le MFP) et ne sont PAS réarmés ici.

void Machine::scheduleFrameEvents() {
    renderLine_ = 0;
    tbLine_     = 0;
    hblLine_    = 0;
    lineCarry_   = 0;     // V2/LINELEN : aucune ligne raccourcie au début de trame
    v2ShortLine_ = -1;
    shifter.beginFrame();                          // verrouille résolution + fréquence
    // Géométrie de la trame (50/60/71 Hz) figée pour toute la trame, lue ici.
    const Shifter::Geometry g = shifter.geometry();
    cpl_       = g.cyclesPerLine;
    curLineLen_ = cpl_;   // LINELEN : longueur nominale tant qu'aucun match freq
    lpf_       = g.linesPerFrame;
    disp_      = g.displayLines;
    deEnd_     = g.lineEndCycle;
    dispStart_ = g.dispStartLine;   // VDE_On : l'affichage actif commence à cette scanline

    // Premiers événements, à leur CYCLE EXACT dans la trame. Le RENDER et le Timer B
    // (event-count sur Display-Enable) ne se déclenchent QUE sur les lignes affichées,
    // donc d'abord à la scanline VDE_On (63 en 50 Hz) — l'affichage actif est centré
    // dans la trame, encadré des bordures haut/bas (port Hatari nStartHBL). Le HBL
    // niveau 2, lui, est émis à CHAQUE scanline (0..lpf-1) comme sur le vrai matériel
    // (Video_InterruptHandler_HBL) — il restera l'ancre de Video_EndHBL (bordures H/B).
    sched.schedule(Scheduler::RENDER,  frameStart_ + static_cast<int64_t>(dispStart_) * cpl_ + deEnd_);
    // Timer B : événement à CHAQUE scanline (comme le HBL) ; le tic n'est compté que
    // si la ligne est réellement AFFICHÉE d'après la machine Glue LIVE (cf. onTimerB) —
    // un retrait de bordure haut/bas en cours de trame déplace les tics, comme Hatari.
    sched.schedule(Scheduler::TIMER_B, frameStart_ + timerBPos());
    // Position de l'IRQ HBL dans la ligne : Hatari HBL_VIDEO_CYCLE_OFFSET = 0 →
    // l'interruption tombe à la FRONTIÈRE de ligne (cycle 512 = 0 de la suivante).
    // L'ancien −4 était une calibration d'avant la refonte IACK (2026-07-02) ;
    // NEOST_HBL_OFF le rétablit pour A/B (valeur = décalage vs cpl, ex. -4).
    sched.schedule(Scheduler::HBL,     frameStart_ + (cpl_ + kHblOff(machineIsSte(machineType_))));   // HBL niveau 2 (frontière ligne 0)
    // Broche IPL pré-armée au cycle EXACT (montée mid-instruction via sync(), cf.
    // Cpu68k::armHblPinAt) — le dispatch de l'événement (frontière de bloc) arrive
    // 0..24 cyc plus tard et ne fait que re-poser la broche (idempotent).
    cpu.armHblPinAt(frameStart_ + (cpl_ + kHblOff(machineIsSte(machineType_))));
    // VBL niveau 4 — port fidèle de Hatari (Video_InterruptHandler_VBL) : l'IRQ VBL
    // est générée VBL_VIDEO_CYCLE_OFFSET cycles APRÈS la fin de la DERNIÈRE ligne de
    // la trame (313×512 + 64 en 50 Hz STF), donc au tout début du vblank = ~SOMMET de
    // la trame courante (la trame précédente vient de finir). On la cale à
    // frameStart_ + offset, et NON plus à la ligne 201 (~112 lignes / 57000 cyc trop
    // tôt) : le handler VBL du jeu (base écran, palette, sprites…) s'applique alors à
    // la trame qui VA s'afficher, comme sur le vrai matériel. Offset STF=64, STE=68.
    // VBL_VIDEO_CYCLE_OFFSET : STE = 68 ; STF = 64 en WS2/3/4 (tranché WS3), 60 en WS1.
    const int vblOffset = machineIsSte(machineType_) ? 68
                        : (Shifter::wakestate() == 1 ? 60 : 64);
    sched.schedule(Scheduler::VBL, frameStart_ + vblOffset);
    cpu.armVblPinAt(frameStart_ + vblOffset);          // broche niveau 4 au cycle exact (cf. armHblPinAt)
    // RESTART du compteur vidéo en FIN de trame (port Video_RestartVideoCounter,
    // ULM DSOTS) : ligne 310 (50 Hz, 313 lignes) / 260 (60 Hz, 263), cycle 56
    // (STF) / 60 (STE). La base $FF8201/03 est relue À CET INSTANT — après le
    // handler VBL du jeu — ce qui alimente correctement les moteurs double-buffer
    // beam-syncés (Enchanted Land en jeu). Le check de fréquence (registre sync
    // LIVE, comme Hatari qui relit $FF820A) se fait dans le callback. Pas de
    // restart en 71 Hz (Hatari : lignes définies pour 50/60 Hz seulement).
    if (lpf_ == 313 || lpf_ == 263) {
        const int restartLine = lpf_ - 3;
        const int restartPos  = machineIsSte(machineType_) ? 60 : 56;
        sched.schedule(Scheduler::VC_RESTART,
                       frameStart_ + static_cast<int64_t>(restartLine) * cpl_ + restartPos);
    }
}

void Machine::onRender() {
    // Décode la scanline à la fin de son Display-Enable (cycle 376), avec l'état
    // COURANT des registres (palette/base) — AVANT le tic Timer B (400) et le HBL
    // (508) de la même ligne, dont les handlers changeront les registres pour la
    // ligne SUIVANTE (rasters). Rendu purement « sortie » : n'altère ni CPU ni IRQ.
    const int h = shifter.activeHeight();          // lignes ACTIVES (≠ buffer overscan)
    if (renderLine_ < h) shifter.renderLine(renderLine_);
    ++renderLine_;
    // L'index actif renderLine_ correspond à la scanline (dispStart_ + renderLine_).
    if (renderLine_ < h && dispStart_ + renderLine_ < lpf_)
        sched.schedule(Scheduler::RENDER,
                       frameStart_ + static_cast<int64_t>(dispStart_ + renderLine_) * cpl_ + deEnd_ - lineCarry_);
}

void Machine::onTimerB() {
    // Timer B en event-count : décompte une fois par ligne AFFICHÉE (sur DE). La
    // fenêtre verticale est LIVE (machine Glue) : un retrait de bordure haut (60 Hz
    // vers la ligne 33 → VDE_On 34) ou bas en COURS de trame ajoute ses tics, comme
    // Hatari (Video_AddInterruptTimerB par ligne) — Enchanted Land VÉRIFIE l'effet
    // de ses impulsions en comptant les événements DE. tbLine_ = scanline ABSOLUE.
    if (shifter.liveLineDisplayed(tbLine_)) {
        mfp.hblank();
        cpu.updateIpl();                           // un underflow Timer B → IPL 6
    }
    ++tbLine_;
    if (tbLine_ < lpf_)
        sched.schedule(Scheduler::TIMER_B,                         // position recalculée → suit
                       frameStart_ + static_cast<int64_t>(tbLine_) * cpl_ + timerBPos() - lineCarry_);
}

void Machine::onHbl() {
    // DIAG (NEOST_HBL_DIAG=1) : timestamp ABSOLU de l'événement HBL (= la frontière
    // de ligne où la broche IRQ monte) — ancre le repère ligne dans les traces cyc=.
    static const long hblDiagN = []{ const char* s = std::getenv("NEOST_HBL_DIAG");
                                     return s ? std::atol(s) : 0L; }();
    if (hblDiagN) { static long n=0; if (++n % hblDiagN == 0)
        std::fprintf(stderr, "[HBLD] line=%d sched=%lld live=%lld\n",
                     hblLine_, (long long)sched.now(), (long long)sched.liveNow()); }
    cpu.raiseHbl();                                // HBL niveau 2 (gaté par le SR)
    // Son DMA STE : la FIFO 8 octets est entretenue à CHAQUE HBL (fetch au faisceau
    // + consommation DAC datée) — port de l'appel DmaSnd_STE_HBL_Update du handler
    // HBL d'Hatari (video.c:3322). Peut pulser Timer A (fin de trame) → IPL.
    if (machineHasDmaSound(machineType_)) { dmasnd.onHbl(); cpu.updateIpl(); }
    // Longueur RÉELLE de la ligne qui se termine (NEOST_LINELEN) : le cumul
    // lineCarry_ décale toutes les planifications des lignes suivantes (−carry),
    // comme la chaîne StartCycle+nCyclesPerLine de Hatari.
    if (lineLenOn_ && curLineLen_ != cpl_) {
        lineCarry_ += cpl_ - curLineLen_;
        curLineLen_ = cpl_;
    }
    ++hblLine_;
    // HBL émis à CHAQUE scanline (hblLine_ = numéro de ligne absolu 0..lpf-1), comme
    // sur le vrai matériel — y compris dans les bordures haut/bas (ancre Video_EndHBL).
    // V2 : −lineCarry_ décale la ligne suivante du cumul des raccourcissements (=0 hors V2).
    if (hblLine_ < lpf_) {
        const int64_t next = frameStart_ + static_cast<int64_t>(hblLine_) * cpl_
                           + (cpl_ + kHblOff(machineIsSte(machineType_))) - lineCarry_;
        sched.schedule(Scheduler::HBL, next);
        cpu.armHblPinAt(next);                 // broche au cycle exact (cf. armHblPinAt)
    }
}

void Machine::onVbl() {
    // DIAG beam-sync : dépassement (carry) du service VBL vs cycle théorique 64/68
    // = équivalent du pending_cyc d'Hatari (video_vbl). Compare la phase CPU↔faisceau.
    static const bool vblTrace = std::getenv("NEOST_VBL_TRACE") != nullptr;
    if (vblTrace) {
        const int64_t evt = frameStart_ + (machineIsSte(machineType_) ? 68 : 64);
        std::fprintf(stderr, "[vbl] over=%lld now=%lld\n",
                     static_cast<long long>(sched.now() - evt), static_cast<long long>(sched.now()));
    }
    cpu.raiseVbl();   // interruption trame (niveau 4) — une fois par trame
    // Tic VBL de l'IKBD (horloge interne $1B/$1C + report joystick auto). La durée
    // d'une trame en µs se déduit de la géométrie COURANTE : (lignes × cycles/ligne)
    // à 8 MHz (horloge bus du Shifter, indépendante du 8/16 MHz CPU MegaSTE).
    // ≈ 20032 µs (50 Hz) / 16700 µs (60 Hz) / 14028 µs (71 Hz mono).
    const int64_t kVblMicro = static_cast<int64_t>(lpf_) * cpl_ / 8;
    ikbd.onVbl(kVblMicro);
}

// -----------------------------------------------------------------------------
//  Une trame : horloge CONTINUE (les timers MFP la traversent). On exécute le CPU
//  d'événement en événement (carry du dépassement), puis on finit le décodage.
// -----------------------------------------------------------------------------
void Machine::runFrame() {
    // FIX1 (beam-sync) — ANCRE DE TRAME FIXE = VBL THÉORIQUE. Port de Hatari
    // VBL_ClockCounter = CyclesGlobalClockCounter − PendingCyclesOver (video.c:4964) qui
    // RETRANCHE le carry δ du dépassement de la dernière instruction enjambant frameEnd.
    // Au lieu de frameStart_ = sched.now() (qui CAPTE δ et le fait varier trame à trame),
    // on AVANCE frameStart_ de la longueur THÉORIQUE de la trame qui se termine (lpf_*cpl_
    // encore posés par le beginFrame précédent). δ ne vit plus que dans l'horloge CPU
    // (sched.now()) ; la grille d'events (scheduleFrameEvents) ET la datation
    // beamClock_/liveFrameClock_ dérivent toutes de frameStart_ → co-ancrées au théorique,
    // comme Hatari. Supprime le jitter de phase qui faisait sauter l'image (cf. workflow).
    // 1ʳᵉ trame : ancre sur sched.now() (= 0 après reset), comme l'ancien modèle, et NON
    // sur busClockNow() — le cœur a déjà avancé de ~40 cyc (lecture SSP/PC du reset) quand
    // la 1ʳᵉ trame démarre. Ancrer sur busClockNow décalerait la grille faisceau de ces 40
    // cyc → déphasage CPU↔faisceau qui casse les calibrations raster (Enchanted Land noir).
    // L'offset reset est ainsi absorbé dans la 1ʳᵉ trame, comme avant.
    // NEOST_LINELEN : la trame qui se termine était RACCOURCIE du cumul lineCarry_
    // (lignes 224/508) — l'ancre de la trame suivante avance de sa longueur RÉELLE
    // (≙ Hatari : la VBL est posée depuis le dernier HBL, la somme des longueurs
    // de lignes réelles fait la trame). Hors LINELEN, lineCarry_ vaut 0 ici (V2
    // seul le touche, opt-in) → comportement inchangé.
    if (frameStartInit_) frameStart_ += static_cast<int64_t>(lpf_) * cpl_ - (lineLenOn_ ? lineCarry_ : 0);
    else { frameStart_ = sched.now(); frameStartInit_ = true; }
    // DIAG (NEOST_FRAME_DIAG) : phase absolue de l'ancre de trame — sur le vrai
    // matériel la vidéo et les créneaux bus dérivent de la MÊME horloge, donc
    // frameStart mod 4 doit être INVARIANT. Toute bascule = bug de géométrie.
    static const bool frameDiag = std::getenv("NEOST_FRAME_DIAG") != nullptr;
    if (frameDiag)
        std::fprintf(stderr, "[FRM] start=%lld mod4=%d lpf=%d cpl=%d\n",
                     (long long)frameStart_, (int)(frameStart_ & 3), lpf_, cpl_);
    // Le RTC avance désormais en PARESSEUX à la lecture (cf. Rtc::catchUp), piloté
    // par l'horloge émulée — rien à cadencer ici.
    scheduleFrameEvents();

    const int64_t frameEnd = frameStart_ + static_cast<int64_t>(lpf_) * cpl_;
    // ORDONNANCEUR PILOTÉ PAR L'HORLOGE (modèle `do_cycles` WinUAE/Hatari) : le CPU
    // tourne jusqu'à la fin de trame, et c'est NeostMoira::sync() qui dispatche les
    // événements (HBL, Timer-B, VBL, RENDER, timers MFP) AU FIL de l'exécution, au
    // cycle exact — l'IPL est posé pendant l'instruction et vu par son POLL_IPL.
    // Plus de quantum borné à l'événement ni de préemption : le bloc = la trame
    // entière. cpu.run() termine son instruction et peut dépasser frameEnd de
    // quelques cycles (carry, comme Hatari) ; la boucle reboucle si nécessaire.
    if (g_blockDispatch) {
        // Modèle BLOC (pré-sync-driven) : run borné au prochain événement, dispatch à la
        // frontière via runTo. sync() N'avance que l'horloge (pas de dispatch mid-instruction).
        while (sched.now() < frameEnd) {
            int64_t next = sched.nextDue();
            if (next < 0 || next > frameEnd) next = frameEnd;
            const int64_t want = next - sched.now();
            sched.beginRun(next);
            const int ran = cpu.run(static_cast<int>(want > 0 ? want : 1));
            sched.endRun();
            sched.runTo(sched.now() + ran);
        }
    } else {
        while (cpu.busClockNow() < frameEnd) {
            const int64_t want = frameEnd - cpu.busClockNow();
            cpu.run(static_cast<int>(want > 0 ? want : 1));
        }
    }
    // Filet : si le CPU n'a PAS conduit l'horloge jusqu'au bout (CPU halté → run()
    // avance l'horloge par setClock sans passer par sync()), on dispatche quand même
    // les événements vidéo restants pour décoder la trame (écran figé, comme l'ancien
    // modèle). Sans effet en marche normale (sync() a déjà porté now_ ≥ frameEnd).
    if (sched.now() < frameEnd) sched.syncTo(frameEnd);

    // Lignes restantes : en haute-rés mono (400 lignes), le cadre PAL 313 lignes
    // ne fournit pas un créneau par ligne → on finit le décodage ici. En couleur
    // (≤ 200 lignes) tout a déjà été décodé au fil de la trame : rien à faire.
    const int h = shifter.activeHeight();          // lignes ACTIVES (≠ buffer overscan)
    while (renderLine_ < h) shifter.renderLine(renderLine_++);

    // Trame complète décodée : si une image Spectrum 512 a été détectée (palette
    // réécrite intra-ligne), re-rend les lignes avec la palette datée au cycle
    // (jusqu'à 512 couleurs). No-op sinon → rendu ligne-à-ligne inchangé.
    shifter.finishFrame();
}
