// =============================================================================
//  Cpu68k.cpp — Liaison Moira <-> Bus NeoST.
//
//  Moira (cœur 68000 cycle-exact, MIT) est intégré en sous-module. Cette façade
//  route ses accès mémoire vers un Bus unique pointé par g_bus et reproduit le
//  vectoring ST (MFP vectorisé niveau 6, VBL/HBL auto-vectorisés). C'est le seul
//  couplage « global » du projet (les callbacks CPU n'ont qu'un Bus actif).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Cpu68k.hpp"
#include "core/Blitter.hpp"
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/Tracer.hpp"
#include "io/Mfp.hpp"
#include "io/GemdosHd.hpp"
#include "io/Scc.hpp"

#include <cstdio>

#include "Moira.h"

namespace {
    Bus*    g_bus = nullptr;        // bus actif vu par les callbacks CPU
    Scheduler* g_sched = nullptr;   // ordonnanceur piloté par sync() (cf. NeostMoira::sync)
    bool    g_vblPending = false;   // VBL (niveau 4) en attente d'acquittement
    bool    g_hblPending = false;   // HBL (niveau 2) en attente d'acquittement
    Tracer* g_tracer = nullptr;     // traceur optionnel (nullptr = aucun surcoût)
    // Garde « double bus fault » : armée quand on déclenche une bus error, désarmée
    // au début de l'instruction SUIVANTE. Si une NOUVELLE bus error survient alors
    // qu'elle est armée, c'est qu'un accès a fauté PENDANT l'empilement de la trame
    // d'exception (SSP/PC corrompus, code parti en vrille) : sur un vrai 68000 cela
    // halte le CPU. On reproduit ce halt au lieu de récurser → l'hôte ne segfault
    // plus et le mode headless peut vider sa trace/série.
    bool    g_inBusError = false;
    // Pendant cpu.reset() (lecture SSP/PC) le cœur consomme ~40 cyc via sync(). On NE
    // dispatche PAS l'ordonnanceur alors : sinon sched.now() serait traîné à 40 et la 1ʳᵉ
    // trame s'ancrerait là (frameStart_=40) au lieu de 0 → grille faisceau décalée de 40 cyc
    // → calibrations raster cassées (Enchanted Land noir). L'ancien modèle n'avançait
    // l'ordonnanceur qu'aux frontières de bloc, jamais pendant le reset.
    bool    g_inReset = false;
    // Préemption du timeslice : posé par endTimeslice() (depuis un callback de
    // l'ordonnanceur, en plein milieu d'une instruction), testé après chaque
    // instruction dans la boucle run() pour rendre la main à l'horloge.
    bool    g_endSlice = false;
    // ---- Bascule 8/16 MHz du Mega STE ($FF8E21 bit1, cf. Cpu68k::setMegaSteSpeed) --
    // L'ordonnanceur et toutes les puces vivent en cycles BUS (8 MHz) ; le cœur
    // CPU, lui, compte ses propres cycles. À 16 MHz : 1 cycle bus = 2 cycles CPU.
    // La conversion est : bus = (clock + g_cpuBias) / g_cpuMul, le biais étant rebasé
    // à chaque bascule pour que l'horloge bus reste CONTINUE (port de l'esprit de
    // Hatari cpucycleunit = CYCLE_UNIT/2 dans clocks_timings.c/newcpu).
    int     g_cpuMul  = 1;   // 1 = 8 MHz, 2 = 16 MHz
    int64_t g_cpuBias = 0;   // biais de conversion (0 tant qu'on reste à 8 MHz)
    // Synchro E-clock à l'entrée des IRQ auto-vectorisées (cf. NeostMoira::willInterrupt).
    // OPT-IN (`NEOST_ECLOCK_ON`) : mécanisme FIDÈLE (Moira, 68000 générique, n'a pas la
    // synchro E-clock Atari de l'IACK auto-vecteur), mais NON validable en jeu headless
    // (écrans cassés EL/LX/Cuddly/SHO inatteignables sans navigation ; overscan_lr rendu
    // à plat). Étalons pixel-exact INCHANGÉS dans les deux états. Risque résiduel de
    // double-comptage avec les hacks de datation vidéo empiriques (mêmes que chipWait8) →
    // resté gated tant que l'oracle in-game ne le tranche pas. À activer/calibrer lors de
    // la refonte coordonnée (retrait des hacks + ajout des mécanismes fidèles ensemble).
    bool    g_eclockOn    = []{ const char* s = std::getenv("NEOST_ECLOCK_ON"); return s && std::atoi(s) != 0; }();
    int     g_eclockPhase = []{ const char* s = std::getenv("NEOST_ECLOCK_PHASE"); return s ? std::atoi(s) : 0; }();
    // DEEP convergence WinUAE (opt-in NEOST_IACK) : timing d'IACK FIDÈLE. Hatari applique
    // l'attente E-clock + un bloc « occupé » au CYCLE D'IACK (iack_cycle, newcpu.c:2958-3019),
    // PAS avant l'empilement de trame. Moira appelle readIrqUserVector EXACTEMENT à l'IACK
    // (MoiraExceptions_cpp.h:538, après SYNC(6)+write PClo+SYNC(4)) → on y déplace l'E-clock
    // (phase correcte, inclut la latence d'amorce) ET on ajoute le bloc occupé absent de Moira :
    // CPU_IACK_CYCLES_VIDEO_CE(10)+idle(4)=14 (auto-vecteur HBL/VBL), CPU_IACK_CYCLES_MFP_CE=12
    // (MFP niv6, sans E-clock). Magnitude calibrable à l'oracle (la comptabilité SYNC d'entrée
    // d'exception de Moira diffère un peu de WinUAE). Avec NEOST_IACK, willInterrupt ne fait plus
    // rien (E-clock relocalisé). Corrige le résidu de PHASE d'entrée d'IRQ (HBL period, poll-beat).
    bool    g_iackOn      = []{ const char* s = std::getenv("NEOST_IACK"); return s && std::atoi(s) != 0; }();
    int     g_iackVideo   = []{ const char* s = std::getenv("NEOST_IACK_VIDEO"); return s ? std::atoi(s) : 14; }();
    int     g_iackMfp     = []{ const char* s = std::getenv("NEOST_IACK_MFP");   return s ? std::atoi(s) : 12; }();
    // Lead-in willInterrupt → IACK réel : SYNC(6)+write PClo(4)+SYNC(4)=14 cyc (MoiraExceptions
    // _cpp.h:533-537). On l'ajoute à la phase E-clock pour la calculer comme au cycle d'IACK,
    // bien que le setClock soit fait depuis willInterrupt (seul point fiable hors mid-accès).
    int     g_iackLead    = []{ const char* s = std::getenv("NEOST_IACK_LEAD"); return s ? std::atoi(s) : 14; }();
    // DEEP (opt-in NEOST_IPLDELAY) : règle cpuipldelay de WinUAE (ipl_fetch_next, newcpu.c:4982).
    // Moira reconnaît l'IPL IMMÉDIATEMENT (POLL_IPL = reg.ipl = ipl) ; WinUAE DIFFÈRE la
    // reconnaissance d'une instruction si le pin IPL a changé < 4 cyc avant le point
    // d'échantillonnage de l'instruction. On le reproduit en RETARDANT la propagation du niveau
    // DÉSIRÉ vers la broche Moira de 4 cyc (via sync()) : le POLL_IPL voit l'ancien niveau jusqu'à
    // +4 cyc → reconnaissance reportée à la frontière suivante. Corrige le multiset du beat
    // (poll-oracle {2,4,6}→{0,2,4,6} = Hatari) et vise le deadlock EL ($EE = attente IRQ mal datée).
    bool    g_iplDelay    = []{ const char* s = std::getenv("NEOST_IPLDELAY"); return s && std::atoi(s) != 0; }();
    int     g_iplDelayCyc = []{ const char* s = std::getenv("NEOST_IPLDELAY_CYC"); return s ? std::atoi(s) : 4; }();
    // DEEP convergence WinUAE (opt-in NEOST_RAM_SLOT) : alignement créneau bus 4 cyc à
    // 8 MHz pour la RAM ST (CHIP16 < $400000), port FIDÈLE de wait_cpu_cycle_read/write
    // (custom.c:148-153). Avant un accès RAM, si l'horloge bus n'est pas sur la grille
    // de 4, attendre (4 - slot) cycles. ⚠ CONTRAIREMENT au chipWait16 (16 MHz), AUCUN +4
    // additionnel : à 8 MHz Moira facture DÉJÀ les 4 cyc de l'accès. L'ancienne tentative
    // « chipWait8 » (mémoire beamsync-busalign-falsified) ajoutait ce +4 parasite (miroir
    // erroné du 16 MHz) → sur-comptait (overscan_top 32→36). Align-only NE sur-compte pas
    // (boucle 4-phasée → +0 ; boucle non-phasée comme `bra.s self` → +2 = WinUAE). ROM/
    // cartouche/IO sont FAST (pas d'alignement, mesuré sur STF). NEOST_RAM_SLOT_PHASE
    // décale la phase de la grille (calibration oracle si offset constant Moira↔WinUAE).
    bool    g_ramSlot     = []{ const char* s = std::getenv("NEOST_RAM_SLOT"); return s && std::atoi(s) != 0; }();
    int     g_ramSlotPhase= []{ const char* s = std::getenv("NEOST_RAM_SLOT_PHASE"); return s ? std::atoi(s) : 0; }();
    int     g_desiredIpl  = 0;       // niveau IPL calculé (immédiat, broche « réelle »)
    int     g_appliedIpl  = 0;       // niveau dernier PROPAGÉ à la broche Moira (reg.ipl via POLL_IPL)
    int64_t g_iplChgClock = -1000;   // horloge (cœur) du dernier changement de g_desiredIpl
    inline int64_t busOfClock(int64_t c) {
        return g_cpuMul == 1 ? c + g_cpuBias : (c + g_cpuBias) >> 1;
    }
    inline int64_t cpuClockForBus(int64_t b) {
        return g_cpuMul == 1 ? b - g_cpuBias : (b << 1) - g_cpuBias;
    }
    void    neostUpdateIpl(bool commit = false);   // recalcule l'IPL présenté au cœur
    void    noteBlitterPreStart();   // accès CPU pendant la fenêtre PRE_START du blitter ?
}

// -----------------------------------------------------------------------------
//  Backend Moira (cœur 68000 cycle-exact, MIT) — sous-classe routant la mémoire
//  vers le Bus et reproduisant le vectoring ST (MFP vectorisé niveau 6,
//  VBL/HBL auto-vectorisés) via readIrqUserVector (irqMode USER).
// -----------------------------------------------------------------------------
namespace {
class NeostMoira : public moira::Moira {
public:
    NeostMoira() {
        setModel(moira::Model::M68000);
        irqMode = moira::IrqMode::USER;
        // Syntaxe Musashi : conserve le format de trace historique (comparaison MAME).
        setDasmSyntax(moira::Syntax::MUSASHI);
    }

    // Une adresse non décodée déclenche une bus error : on lève l'exception
    // moira::BusError (rattrapée par Moira::execute → execBusError) avec une trame
    // d'exception de groupe 0 identique à celle que construit Moira::makeFrame
    // (privée, donc reproduite ici). C'est ainsi qu'EmuTOS sonde le matériel
    // optionnel — sans ça, Moira lit l'adresse fantôme et la détection HW d'EmuTOS
    // part en vrille (bureau GEM sans menu ni curseur).
    [[noreturn]] void raiseBusError(moira::u32 addr, bool write) const {
        moira::StackFrame f{};
        const moira::u16 ird = getIRD();
        // code = IR(15..5) | function-code(2..0) | bit4 R/W (1 = lecture sur 68000).
        f.code = (ird & 0xFFE0) | readFC() | (write ? 0 : 0x10);
        f.addr = addr;
        f.ird  = ird;
        f.sr   = getSR();
        f.pc   = getPC();
        f.fc   = readFC();
        f.ssw  = f.fc;
        throw moira::BusError(f);
    }

    // Déclenche la bus error, SAUF si une faute est déjà en cours (double bus
    // fault pendant l'empilement de trame) → on halte le CPU comme le vrai 68000,
    // au lieu de relancer une exception (qui aborterait l'hôte). Renvoie true si
    // halté (l'appelant doit alors fournir une valeur neutre).
    bool faultOrHalt(moira::u32 a, bool write) const {
        if (g_inBusError) { const_cast<NeostMoira*>(this)->flags |= moira::State::HALTED; return true; }
        g_bus->megaSteCacheFlushIfEnabled();   // une bus error invalide le cache Mega STE (Hatari)
        g_inBusError = true;
        raiseBusError(a, write);            // [[noreturn]] : lève moira::BusError
        return true;                        // inatteignable
    }

    // ---- Mega STE 16 MHz : accès mémoire cadencés bus + cache 16 Ko ------------
    // Port des mem_access_delay_*_megaste_16 de Hatari. Moira facture déjà 4 cycles
    // CPU par accès bus ; à 16 MHz un accès RAM ST réel en coûte 8 (le bus reste à
    // 8 MHz) APRÈS attente du créneau CPU/Shifter (le GSTMCU partage la RAM par
    // créneaux de 4 cycles bus = 8 cycles CPU 16 MHz). D'où : attente d'alignement
    // + 4 cycles additionnels, sauf hit du cache 16 Ko (RAM rapide dédiée, 4 cycles
    // = rien à ajouter). ROM/cartouche/IO sont « FAST » (aucun wait state, mesuré
    // sur vrai matériel par Hatari) → plein débit 16 MHz.
    void chipWait16() const {
        auto* self = const_cast<NeostMoira*>(this);
        const moira::i64 c = self->getClock();
        const int slot = int((c + g_cpuBias) & 7);       // position dans le créneau bus
        self->setClock(c + ((8 - slot) & 7) + 4);
    }
    bool superNow() const { return (getSR() & 0x2000) != 0; }

    moira::u16 readMste16Mhz(moira::u32 a, int size) const {
        a &= 0x00FFFFFF;
        uint16_t v;
        if (a >= 0x400000) {                 // ROM/cartouche/IO : « FAST », plein 16 MHz
            v = size == 2 ? g_bus->read16(a) : g_bus->read8(a);
            if (g_bus->megaSteCacheEnabled())
                g_bus->megaSteCacheUpdate(a, size, v, false, superNow());
            return v;
        }
        const bool super = superNow();       // RAM ST, partagée avec le Shifter
        if (g_bus->megaSteCacheEnabled() && g_bus->megaSteCacheRead(a, size, v, super))
            return v;                        // hit : 4 cycles CPU (déjà facturés par Moira)
        chipWait16();                        // miss / cache off : accès cadencé bus 8 MHz
        v = size == 2 ? g_bus->read16(a) : g_bus->read8(a);
        if (g_bus->megaSteCacheEnabled()) {
            if (size == 2) g_bus->megaSteCacheUpdate(a, 2, v, false, super);
            // Lecture octet : le bus porte le MOT entier à cette adresse → la ligne
            // est remplie avec le mot pair complet (si cachable sans bus error).
            else if (g_bus->megaSteCacheable(a & ~1u, 2, false, super))
                g_bus->megaSteCacheUpdate(a & ~1u, 2, g_bus->read16(a & ~1u), false, super);
        }
        return v;
    }

    void writeMste16Mhz(moira::u32 a, int size, moira::u16 v) const {
        a &= 0x00FFFFFF;
        if (a < 0x400000) chipWait16();      // écriture RAM ST : toujours cadencée bus
        if (size == 2) g_bus->write16(a, v); else g_bus->write8(a, moira::u8(v));
        if (g_bus->megaSteCacheEnabled())    // write-through : maj du mot déjà caché
            g_bus->megaSteCacheUpdate(a, size, v, true, superNow());
    }

    // Chaque accès CPU abouti latche le « dernier mot du bus de données » dans
    // Bus::cpuDb (≈ regs.db du cœur UAE : mot = valeur, octet = dupliqué sur les
    // deux voies — cf. cpu_prefetch.h). Les lectures en RAM « void » le relisent.
    void latchDb(moira::u16 v) const { g_bus->cpuDb = v; }
    void latchDb8(moira::u8 v) const { g_bus->cpuDb = moira::u16((moira::u16(v) << 8) | v); }

    // Alignement créneau bus 4 cyc (8 MHz) sur la RAM ST avant un accès — cf. g_ramSlot.
    // slot = horloge bus & 3 ; si non nul, attendre (4 - slot) cycles. Port fidèle de
    // wait_cpu_cycle_read (custom.c) : la GLUE partage la RAM en créneaux de 4 cyc bus,
    // le CPU attend son tour. Pas de +4 (l'accès lui-même est déjà facturé par Moira).
    void chipWait8(moira::u32 a) const {
        if (!g_ramSlot || (a & 0x00FFFFFFu) >= 0x400000u) return;
        auto* self = const_cast<NeostMoira*>(this);
        const moira::i64 c = self->getClock();
        const int slot = int((c + g_cpuBias + g_ramSlotPhase) & 3);
        if (slot) self->setClock(c + (4 - slot));
    }

    moira::u8  read8 (moira::u32 a) const override { if (g_bus->blitterWinEnd >= 0) noteBlitterPreStart(); if (g_bus->busFaultN(a, 1, false) && faultOrHalt(a, false)) return 0; moira::u8 v; if (g_cpuMul == 2) v = moira::u8(readMste16Mhz(a, 1)); else { chipWait8(a); v = g_bus->read8(a); } latchDb8(v); return v; }
    moira::u16 read16(moira::u32 a) const override { if (g_bus->blitterWinEnd >= 0) noteBlitterPreStart(); if (g_bus->busFaultN(a, 2, false) && faultOrHalt(a, false)) return 0; moira::u16 v; if (g_cpuMul == 2) v = readMste16Mhz(a, 2); else { chipWait8(a); v = g_bus->read16(a); } latchDb(v); return v; }
    void write8 (moira::u32 a, moira::u8  v) const override { if (g_bus->blitterWinEnd >= 0) noteBlitterPreStart(); if (g_bus->busFaultN(a, 1, true)) { if (faultOrHalt(a, true)) return; } latchDb8(v); if (g_cpuMul == 2) { writeMste16Mhz(a, 1, v); return; } chipWait8(a); g_bus->write8(a, v); }
    void write16(moira::u32 a, moira::u16 v) const override { if (g_bus->blitterWinEnd >= 0) noteBlitterPreStart(); if (g_bus->busFaultN(a, 2, true)) { if (faultOrHalt(a, true)) return; } latchDb(v); if (g_cpuMul == 2) { writeMste16Mhz(a, 2, v); return; } chipWait8(a); g_bus->write16(a, v); }
    // Lecture du vecteur de reset (SSP/PC) via l'overlay ROM : jamais de bus error.
    moira::u16 read16OnReset(moira::u32 a) const override { const moira::u16 v = g_bus->read16(a); latchDb(v); return v; }
    // Lecture pour le désassembleur : pas d'effet de bord MMIO ni de bus error
    // (équivaut aux anciens m68k_read_disassembler_* de Musashi).
    moira::u16 read16Dasm(moira::u32 a) const override { return g_bus->read16(a); }

    // Le 68000 est-il en attente (instruction STOP) ? Permet à la boucle d'horloge
    // de SAUTER l'attente au lieu de la simuler cycle par cycle (cf. run()).
    bool isStopped() const { return (flags & moira::State::STOPPED) != 0; }

    // ---- Ordonnanceur PILOTÉ PAR L'HORLOGE (modèle `do_cycles` WinUAE/Hatari) ----
    // Hook cycle de Moira. Moira l'appelle à chaque pas : en PRECISE_TIMING, AVANT
    // chaque accès mémoire (sous-instruction) ; sinon, en fin d'instruction (cf.
    // MoiraMacros SYNC/CYCLES). On avance l'horloge du cœur de `n`, PUIS on dispatche
    // les événements échus de l'ordonnanceur — EN COURS d'instruction. L'IPL est donc
    // posé au CYCLE EXACT de l'événement (Timer-B/VBL/HBL) et vu par le POLL_IPL de
    // l'instruction COURANTE, au lieu d'être posé ~1 instruction plus tard à la
    // frontière de bloc (cause racine du jitter beam-sync). syncTo est O(1) tant que
    // rien n'est dû (cache nextDue_) — sync() est appelé très souvent. Pas de
    // réentrance : les callbacks de l'ordonnanceur n'exécutent pas le CPU.
    void sync(int n) override {
        setClock(getClock() + n);             // défaut Moira (avance l'horloge du cœur)
        if (g_sched && !g_inReset) g_sched->syncTo(busOfClock(static_cast<int64_t>(getClock())));
        // DEEP cpuipldelay : propage le niveau IPL désiré vers la broche une fois le délai de
        // reconnaissance (4 cyc) écoulé depuis son changement → POLL_IPL le voit alors, comme
        // WinUAE ipl_fetch_next. Gated NEOST_IPLDELAY ; sinon setIPL est immédiat (cf. neostUpdateIpl).
        if (g_iplDelay && g_desiredIpl != g_appliedIpl
            && getClock() - g_iplChgClock >= static_cast<int64_t>(g_iplDelayCyc) * g_cpuMul) {
            setIPL(static_cast<moira::u8>(g_desiredIpl));
            g_appliedIpl = g_desiredIpl;
        }
    }

    // Committe l'IPL : broche + registre échantillonné (reg.ipl). setIPL ne pose que
    // la broche, que Moira n'échantillonne qu'au point de poll de l'instruction
    // SUIVANTE (pipeline IPL fidèle au 68000) — correct pour un changement en plein
    // accès MMIO, mais à une frontière d'instruction (événement daté MFP_IRQ, délai
    // 4 cyc déjà écoulé) l'exception doit partir AVANT l'instruction suivante,
    // comme Hatari (MFP_ProcessIRQ au test de frontière). Cf. Cpu68k::updateIplNow.
    void commitIpl(moira::u8 lvl) {
        setIPL(lvl);                       // broche (+ CHECK_IRQ si changement)
        reg.ipl = lvl;                     // déjà échantillonné : visible immédiatement
        flags |= moira::State::CHECK_IRQ;  // force le re-test même si la broche n'a pas bougé
    }

    // Synchro E-clock à l'ENTRÉE d'exception (port de Hatari M68000_WaitEClock,
    // m68000.c:810 + iack_cycle newcpu.c:2971-2990) : les IRQ AUTO-VECTORISÉES HBL
    // (niv 2) et VBL (niv 4) attendent le prochain front de l'E-clock (bus/10 =
    // 0,8 MHz) avant de lire leur vecteur → 0..8 cyc selon la phase de l'horloge
    // BUS au moment de l'IACK (motif déterministe {0,8,6,4,2}). Les IRQ VECTORISÉES
    // MFP (niv 6) et SCC (niv 5) n'ont PAS d'attente E-clock (elles fournissent leur
    // vecteur). C'est CE jitter d'entrée qui manquait à NeoST et déphasait le CPU
    // vis-à-vis du faisceau trame à trame. willInterrupt est appelé AVANT
    // l'empilement de trame (MoiraExceptions_cpp.h:508) → latence d'entrée PURE,
    // le timing des instructions reste intact.
    void willInterrupt(moira::u8 level) override {
        if (g_iackOn) {                                          // IACK fidèle WinUAE (E-clock @ IACK + bloc occupé)
            // setClock fiable ICI (≠ readIrqUserVector qui est mid-accès → écrasé par les SYNC du read<>).
            if (level == 2 || level == 4) {                      // auto-vecteur HBL/VBL : E-clock + bloc vidéo
                const int64_t busClk = busOfClock(getClock() + g_iackLead) + g_eclockPhase;
                int wait = 10 - static_cast<int>(((busClk % 10) + 10) % 10);
                if (wait == 10) wait = 0;
                const int add = wait + g_iackVideo;
                if (add) setClock(getClock() + static_cast<int64_t>(add) * g_cpuMul);
            } else if (level == 6) {                             // MFP vectorisé : bloc MFP, pas d'E-clock
                if (g_iackMfp) setClock(getClock() + static_cast<int64_t>(g_iackMfp) * g_cpuMul);
            }
            return;
        }
        if (!g_eclockOn || (level != 2 && level != 4)) return;    // opt-in ; auto-vecteurs HBL/VBL seulement
        const int64_t busClk = busOfClock(static_cast<int64_t>(getClock())) + g_eclockPhase;
        int wait = 10 - static_cast<int>(busClk % 10);           // cycles BUS jusqu'au prochain front E
        if (wait == 10) wait = 0;                                // déjà aligné
        if (wait) setClock(getClock() + static_cast<int64_t>(wait) * g_cpuMul);
    }

    moira::u16 readIrqUserVector(moira::u8 level) const override {
        if (level == 6 && g_bus->mfp) {                 // MFP : vecteur fourni par le 68901
            const int v = g_bus->mfp->iack();
            if (g_tracer) g_tracer->onInterrupt(level, v);
            neostUpdateIpl();
            return (v >= 0) ? moira::u16(v) : moira::u16(24);
        }
        if (level == 5 && g_bus->scc) {                 // SCC série : vecteur vectorisé (IACK)
            const int v = g_bus->scc->processIack();
            if (g_tracer) g_tracer->onInterrupt(level, v);
            neostUpdateIpl();
            return (v >= 0) ? moira::u16(v) : moira::u16(24);  // NV armé → vecteur spurious 24 ($60),
                                                               // comme le MFP et Hatari (iack_cycle : vector<0 → 24)
        }
        if (g_tracer) g_tracer->onInterrupt(level, 24 + level);   // VBL/HBL auto-vectorisés
        if (level == 4) g_vblPending = false; else if (level == 2) g_hblPending = false;
        neostUpdateIpl();
        return moira::u16(24 + level);
    }
};
NeostMoira* g_moira = nullptr;     // cœur Moira actif
}

namespace {
// Bug « 63 accès » du blitter (port Blitter_HOG_CPU_mem_access_before, phase
// PRE_START) : si cet accès bus CPU tombe dans la fenêtre de 4 cycles précédant
// la prise de bus du blitter non-hog, le blitter le compte à tort comme un de SES
// accès (la tranche suivante n'en fera que 63). Date de l'accès = horloge bus
// absolue (busOfClock, même domaine que Scheduler::liveNow).
void noteBlitterPreStart() {
    if (!g_moira || !g_bus->blitter) return;
    const int64_t t = busOfClock(static_cast<int64_t>(g_moira->getClock()));
    if (t >= g_bus->blitterWinStart && t < g_bus->blitterWinEnd)
        g_bus->blitter->notePreStartCpuAccess();
}

// Recalcule l'IPL présenté au CPU : MFP (6) > VBL (4) > HBL (2).
// `commit` (frontière d'instruction UNIQUEMENT) : pose aussi reg.ipl (valeur déjà
// échantillonnée) pour que l'exception parte avant l'instruction suivante — cf.
// NeostMoira::commitIpl.
void neostUpdateIpl(bool commit) {
    const bool mfp6 = g_bus && g_bus->mfp && g_bus->mfp->irqPending();
    int lvl;
    // MegaSTE : TOUTES les IRQ sont GATÉES par le SCU (SysIntMask/VmeIntMask) avant
    // d'atteindre le CPU — toujours actif comme `SCU_IsEnabled()` d'Hatari (= MegaSTE/TT).
    // Tout OS MegaSTE programme le SCU tôt au boot (TOS 2.06, EmuTOS 256K, diagnostic).
    if (g_bus && g_bus->machine == MachineType::MegaSte) {
        const bool scc5 = g_bus->scc && g_bus->scc->irqActive();  // SCC série niveau 5
        g_bus->scu.syncState(mfp6, scc5, g_vblPending, g_hblPending);  // état ← sources vivantes
        lvl = g_bus->scu.gatedLevel();                            // plus haut niveau autorisé
    } else {
        lvl = mfp6 ? 6 : g_vblPending ? 4 : g_hblPending ? 2 : 0;
    }
    if (g_moira) {
        if (commit) { g_moira->commitIpl(static_cast<moira::u8>(lvl)); g_desiredIpl = g_appliedIpl = lvl; }
        else if (g_iplDelay) {
            // cpuipldelay : enregistre le niveau désiré + l'horloge de changement ; ne propage à la
            // broche (POLL_IPL) que si le délai de 4 cyc est écoulé, sinon sync() le fera plus tard.
            if (lvl != g_desiredIpl) { g_desiredIpl = lvl; g_iplChgClock = g_moira->getClock(); }
            if (g_moira->getClock() - g_iplChgClock >= static_cast<int64_t>(g_iplDelayCyc) * g_cpuMul) {
                g_moira->setIPL(static_cast<moira::u8>(lvl)); g_appliedIpl = lvl;
            }
        }
        else { g_moira->setIPL(static_cast<moira::u8>(lvl)); g_appliedIpl = lvl; }
    }
}
}

CpuCore Cpu68k::parseCore(const std::string& s) {
    std::string l;
    for (char c : s) l += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    // L'ancien cœur Musashi a été retiré : on tolère la valeur historique pour ne pas
    // casser les vieux neost.cfg / scripts, mais on AVERTIT et on reste sur Moira.
    if (l == "musashi" || l == "uae")
        std::fprintf(stderr, "[cpu] cœur « %s » supprimé — NeoST utilise Moira (cycle-exact).\n", s.c_str());
    return CpuCore::Moira;
}

const char* Cpu68k::coreName(CpuCore) {
    return "moira";
}

Cpu68k::Cpu68k(Bus& bus, CpuCore core) : core_(core) {
    g_bus = &bus;
    initCore();
}

// (Ré)initialise le cœur Moira. Appelé par le constructeur ET par setCore()
// (reconfigure à chaud). Suppose qu'un éventuel ancien cœur a déjà été libéré.
void Cpu68k::initCore() {
    g_moira = new NeostMoira();         // backend cycle-exact (irqMode/USER, M68000)
}

// Conservé pour compat (reconfigure à chaud) : libère l'ancien cœur puis ré-init.
void Cpu68k::setCore(CpuCore core) {
    if (g_moira) { delete g_moira; g_moira = nullptr; }
    core_ = core;
    initCore();
}

void Cpu68k::setTracer(Tracer* t) {
    g_tracer = t;
    if (t) t->setCpu(this);    // le Tracer lit les registres via ce CPU
}

void Cpu68k::setScheduler(Scheduler* s) {
    g_sched = s;               // piloté depuis NeostMoira::sync (cf. en-tête hpp)
}

// Horloge BUS absolue live du cœur (cf. hpp). busOfClock intègre la bascule 8/16 MHz
// du Mega STE. Valide hors run comme en plein milieu d'une instruction.
int64_t Cpu68k::busClockNow() const {
    return busOfClock(static_cast<int64_t>(g_moira->getClock()));
}

void Cpu68k::reset() {
    g_bus->bootOverlay = true;
    g_inReset = true;                    // sync() n'avance pas l'ordonnanceur pendant le reset
    g_moira->reset();                    // lit SSP/PC via read16OnReset (overlay ROM)
    g_inReset = false;
    g_bus->bootOverlay = false;
}

int Cpu68k::run(int cycles) {
    inRun_ = true;
    struct RunGuard { bool& f; ~RunGuard() { f = false; } } guard{inRun_};   // hors run → delta intra-quantum = 0
    const moira::i64 c0 = g_moira->getClock();
    quantumStartClock_ = static_cast<int64_t>(c0);   // pour cyclesRunInQuantum()
    // Cible et résultat en cycles BUS (8 MHz). Le point de départ est FIGÉ ici :
    // une écriture $FF8E21 en plein quantum rebase la conversion (g_cpuBias),
    // mais celle-ci reste continue → les deltas restent exacts.
    quantumStartBus_ = busOfClock(c0);
    g_endSlice = false;                              // un éventuel résidu de préemption ne doit pas couper le 1er pas
    const int64_t targetBus = quantumStartBus_ + cycles;
    while (busOfClock(g_moira->getClock()) < targetBus) {
        g_inBusError = false;                        // nouvelle instruction → faute précédente retombée
        if (g_moira->isHalted()) { g_moira->setClock(cpuClockForBus(targetBus)); break; }  // double bus fault → CPU arrêté
        instrStartClock_ = static_cast<int64_t>(g_moira->getClock());   // repère « 1er accès » des wait states
        // Interception GEMDOS HD (port de OpCode_GemDos/Pexec/SysInit + CpuDoNOP
        // d'Hatari) : la cartouche système ($FA0000) place des opcodes « illégaux »
        // magiques (8=GEMDOS, 9=PEXEC, 10=SYSINIT). Quand le HD GEMDOS est actif et
        // que le PC est DANS la cartouche, on traite l'appel en C (lit/écrit les
        // registres + pose les codes condition du SR), puis on remplace l'opcode par
        // un NOP (0x4E71) : l'execute() ci-dessous le consomme, avançant PC et
        // prefetch comme une instruction d'un mot — exactement comme CpuDoNOP. Hors
        // cartouche, un vrai $0008 reste une instruction illégale normale.
        if (g_bus->gemdos) {
            const moira::u16 ird = g_moira->getIRD();
            if (ird >= 0x0008 && ird <= 0x000A) {
                const uint32_t pc0 = g_moira->getPC0() & 0x00FFFFFF;
                if (pc0 >= 0xFA0000 && pc0 < 0xFC0000 &&
                    g_bus->gemdos->handleOpcode(ird))
                    g_moira->setIRD(0x4E71);         // → exécuté comme NOP
            }
        }
        g_moira->execute();                          // une instruction (sync() dispatche les events échus)
        if (g_tracer) g_tracer->onInstruction(g_moira->getPC0());
        // Préemption (dormante dans le modèle piloté par sync : beginRun n'est plus
        // appelé) : conservée par sûreté si un chemin arme encore endSlice.
        if (g_endSlice) { g_endSlice = false; break; }
        // STOP : aucune instruction ne tournera tant qu'un événement ne change pas
        // l'IPL. Au lieu de simuler l'attente cycle par cycle (≈25× plus lent), on
        // saute au PROCHAIN événement armé (borné par la cible du bloc) et on le
        // DISPATCHE via syncTo : il peut lever l'IPL → l'execute() suivant sortira du
        // STOP au bon cycle. Sans événement avant la cible, on saute droit à la cible.
        // (≠ ancien modèle : on saute à l'EVENT, pas à la cible, sinon on zapperait
        // tous les events de la trame — la cible du bloc est maintenant la fin de trame.)
        if (g_moira->isStopped()) {
            const int64_t busNow = busOfClock(g_moira->getClock());
            if (busNow >= targetBus) break;
            const int64_t nd = g_sched ? g_sched->peekNextDue() : -1;
            const int64_t jumpTo = (nd > busNow && nd < targetBus) ? nd : targetBus;
            g_moira->setClock(cpuClockForBus(jumpTo));
            if (g_sched) g_sched->syncTo(jumpTo);    // dispatche l'event au saut (peut lever l'IPL)
        }
    }
    return static_cast<int>(busOfClock(g_moira->getClock()) - quantumStartBus_);
}

// Wait states de bus (cf. en-tête / Hatari M68000_SyncCpuBus). Appelé par le Shifter
// PENDANT l'exécution d'une instruction (depuis read8/write8 d'un registre aligné 4
// cycles) : on avance l'horloge du cœur, ce qui rallonge l'instruction en cours et
// décale tous les accès suivants (la contention de bus du vrai matériel).
void Cpu68k::addBusWaitCycles(int n) {
    if (n <= 0) return;
    // `n` est en cycles BUS (8 MHz) : à 16 MHz l'horloge du cœur compte des cycles
    // CPU, deux fois plus fins → ×g_cpuMul.
    g_moira->setClock(g_moira->getClock() + n * g_cpuMul);
}

// Wait states YM2149 PSG (port Hatari psg.c:PSG_WaitState). 4 cycles au PREMIER accès
// de l'instruction ; les accès suivants de la MÊME instruction n'ajoutent rien (le cas
// movem +4 cyc tous les 4 accès est omis : aucun logiciel réel n'accède au PSG via movem).
void Cpu68k::addPsgWaitCycles() {
    if (instrStartClock_ != psgPrevInstrClock_) {         // nouvelle instruction → 4 cyc
        psgPrevInstrClock_ = instrStartClock_;
        addBusWaitCycles(4);
    }
}

// Wait state MFP 68901 (port Hatari mfp.c : M68000_WaitState(4) sur CHAQUE handler de
// lecture/écriture de registre). 4 cycles à chaque accès, sans dédup par instruction.
void Cpu68k::addMfpWaitCycles() {
    addBusWaitCycles(4);
}

// Wait states ACIA 6850 (port Hatari acia.c:ACIA_AddWaitCycles). 6 cycles à chaque accès,
// plus la synchro sur l'E-Clock (1 MHz = CPU/10) UNIQUEMENT au premier accès de
// l'instruction : on patiente jusqu'au prochain multiple de 10 cycles (0..8 cyc, motif
// [0 8 6 4 2] ; port M68000_WaitEClock).
void Cpu68k::addAciaWaitCycles() {
    int cycles = 6;                                       // coût de base par accès
    if (instrStartClock_ != aciaPrevInstrClock_) {        // 1er accès ACIA de l'instruction
        aciaPrevInstrClock_ = instrStartClock_;
        // E-Clock = horloge BUS / 10 (1 MHz), indépendante du 8/16 MHz CPU MegaSTE
        // (Hatari M68000_WaitEClock travaille sur CyclesGlobalClockCounter).
        int toNextE = 10 - static_cast<int>(busOfClock(g_moira->getClock()) % 10);
        if (toNextE == 10) toNextE = 0;                   // déjà aligné sur l'E-Clock
        cycles += toNextE;
    }
    addBusWaitCycles(cycles);
}

// Cycles écoulés depuis le début du quantum run() courant (cf. en-tête).
int64_t Cpu68k::cyclesRunInQuantum() const {
    if (!inRun_) return 0;     // hors run : l'horloge sched.now() est déjà à jour
    // En cycles BUS (8 MHz), domaine de l'ordonnanceur — d'où la conversion 16 MHz.
    return busOfClock(static_cast<int64_t>(g_moira->getClock())) - quantumStartBus_;
}

void Cpu68k::endTimeslice() {
    g_endSlice = true;   // testé après l'instruction courante (cf. run)
}

// Bascule 8/16 MHz du Mega STE — cf. Cpu68k.hpp. Le rebasage de g_cpuBias garde
// l'horloge bus CONTINUE au cycle courant (la bascule arrive en plein quantum,
// pendant l'écriture $FF8E21) : busOfClock(c) garde la même valeur avant/après.
void Cpu68k::setMegaSteSpeed(bool sixteenMhz) {
    const int mul = sixteenMhz ? 2 : 1;
    if (mul == g_cpuMul) return;
    const int64_t c = static_cast<int64_t>(g_moira->getClock());
    const int64_t b = busOfClock(c);
    g_cpuMul  = mul;
    g_cpuBias = (mul == 2) ? (2 * b - c) : (b - c);
    std::fprintf(stderr, "[cpu] Mega STE : 68000 à %d MHz\n", sixteenMhz ? 16 : 8);
}

bool Cpu68k::megaSte16Mhz() const { return g_cpuMul == 2; }

bool Cpu68k::supervisor() const {
    return (g_moira->getSR() & 0x2000) != 0;
}

void Cpu68k::updateIpl() {
    neostUpdateIpl();
}

void Cpu68k::updateIplNow() {
    neostUpdateIpl(/*commit=*/true);
}

void Cpu68k::raiseVbl() {
    g_vblPending = true;
    neostUpdateIpl();
}

void Cpu68k::raiseHbl() {
    g_hblPending = true;
    neostUpdateIpl();
}

uint32_t Cpu68k::pc() const {
    return g_moira->getPC0();
}

uint32_t Cpu68k::reg(int idx) const {
    return idx < 8 ? g_moira->getD(idx) : g_moira->getA(idx - 8);
}

uint16_t Cpu68k::sr() const {
    return g_moira->getSR();
}

void Cpu68k::setReg(int idx, uint32_t v) {
    if (idx < 8) g_moira->setD(idx, v); else g_moira->setA(idx - 8, v);
}

void Cpu68k::setSr(uint16_t v) {
    g_moira->setSR(v);
}

uint32_t Cpu68k::usp() const {
    return g_moira->getUSP();
}

bool Cpu68k::triggerBusError(uint32_t addr, bool write) {
    return g_moira->faultOrHalt(addr, write);
}

int Cpu68k::disassemble(char* str, uint32_t addr) const {
    return g_moira->disassemble(str, addr);
}
