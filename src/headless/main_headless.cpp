// =============================================================================
//  main_headless.cpp — NeoST sans interface : exécution déterministe + traces.
//
//  But : produire des journaux d'exécution très précis (trace d'instructions
//  façon MAME, registres, interruptions) pour diff avec une trace MAME, et
//  pouvoir tourner en CI / sans serveur graphique. Aucune dépendance GL/GLFW.
//
//  Exemples :
//    neost-headless --frames 50 --trace trace.txt
//    neost-headless --frames 50 --trace trace.txt --regs --irq
//    neost-headless --until-pc FC0030 --trace -        (trace vers stdout)
//    neost-headless --frames 50 --screenshot screen.ppm
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <fstream>

#include "core/Machine.hpp"
#include "net/FujiHost.hpp"
#include "net/FujiHostReplay.hpp"
#include "net/NetBackend.hpp"
#ifdef NEOST_WITH_NET
#include "net/FujiHostLive.hpp"
#include "net/HayesModem.hpp"
#include "net/MidiRing.hpp"
#endif
#include "core/Tracer.hpp"
#include "core/Symbols.hpp"
#include "core/AudioMix.hpp"   // chaîne de mixage partagée (--sound-dump)

namespace {
void usage() {
    std::printf(
        "Usage: neost-headless [options] [rom]\n"
        "  --frames N        number of frames to run (default 200, ~4 s of ST time)\n"
        "  --sound-dump F    WAV audio dump (48 kHz stereo s16): YM2149 + STE DMA + LMC,\n"
        "                    same chain as the GUI (--frames loop only)\n"
        "  --trace FILE      write the instruction trace ('-' = stdout)\n"
        "  --trace-from N    only enable the trace from frame N onwards\n"
        "  --regs            append the register state to every instruction\n"
        "  --irq             also trace the interrupts taken\n"
        "  --until-pc HEX    stop as soon as PC reaches this address (hex)\n"
        "  --break HEX       PC breakpoint (instruction-exact, repeatable): stops BEFORE\n"
        "                    executing the instruction, dumps the registers, exits\n"
        "  --symbols FILE    symbol table (nm-style .sym OR a TOS $601A executable)\n"
        "  --symbols-base HEX  relocation base added to the symbols of a TOS executable\n"
        "  --break-sym NAME  breakpoint on a symbol (requires --symbols; repeatable)\n"
        "  --watch HEX       memory watchpoint (read/write access; break-after; repeatable)\n"
        "  --save-state FILE write the full state (save-state) at the end of --frames\n"
        "  --load-state FILE restore a state BEFORE running (same machine config required)\n"
        "  --save-state-test DETERMINISM self-test: run --frames → save → load → re-run,\n"
        "                    the re-serialized state AND the screen must match, then exits\n"
        "  --cpu CORE        68000 core: moira (the only one, cycle-exact)\n"
        "  --machine TYPE    profile: st, megast, ste (default), megaste\n"
        "  --fpu             populate the Mega STE MC68881 socket ($FFFA40, functional\n"
        "                    emulation — absent by default: Hatari-faithful \"not found\")\n"
        "  --mem SIZE        ST-RAM: 256k, 512k (default), 1m, 2m, 4m\n"
        "  --walk-mouse      after boot, inject a mouse move + click (diagnostic)\n"
        "  --keys STR        after boot, type STR on the keyboard (e.g. diag menus)\n"
        "  --key-down N C    make ONLY of character C at frame N (key HELD)\n"
        "  --key-up N C      break ONLY of character C at frame N\n"
        "  --joy P1[,P0]     hold a joystick state (bits up$01 down$02 l$04 r$08 fire$80)\n"
        "  --disk FILE       mount an image in drive A (default disks/diskA.st)\n"
        "  --diskb FILE      mount an image in drive B (second drive)\n"
        "  --fastfdc         fast FDC (delays /10) — speeds up disk access\n"
        "  --loopback        \"plug in\" the RS232 loopback connector (serial S test)\n"
        "  --cart FILE       mount a cartridge ($FA0000): diagnostic Test Kit, etc.\n"
        "  --gemdos DIR      GEMDOS hard disk: map DIR onto C: (GEMDOS calls redirected\n"
        "                    to the host, Hatari-style; exclusive with --cart)\n"
        "  --printer FILE    Centronics printer: capture the printed bytes into FILE\n"
        "  --acsi IMG        ACSI hard disk image (target 0): TOS reads the partition\n"
        "                    table and mounts C:/D:… (alias --hd; port of hdc.c)\n"
        "  --fujinet         attach the virtual FujiNet on the ACSI bus (target 6;\n"
        "                    NeoST extension, see docs/FUJINET.md)\n"
        "  --fujinet-target N  ACSI target of the FujiNet device (0-7, default 6)\n"
        "  --fujinet-host URL  put URL in host slot 0; if it points to a disk image\n"
        "                    (.st/.msa/.dim/.stx or raw HD), download and mount it\n"
        "  --fujinet-replay DIR  deterministic backend: replay fixtures from DIR\n"
        "                    (no network I/O — used by the test suite)\n"
        "  --fujinet-offline no network backend (device present, WiFi down)\n"
        "  --modem           Hayes modem on the MFP USART: AT commands bridge the\n"
        "                    serial port to real TCP (ATDT host:port -> CONNECT)\n"
        "  --ethernec        NE2000/EtherNEC on the cartridge port (loopback backend;\n"
        "                    for STinG/MiNTnet drivers — exclusive with --cart)\n"
        "  --midi-net H:P[:L]  MIDI ring over UDP (MIDI Maze online): send MIDI OUT to\n"
        "                    peer H:P, receive MIDI IN on local port L (default 6820)\n"
        "  --glue-selftest   self-test of the Glue machine (borders) then exit\n"
        "  --spec512-selftest self-test of the Spectrum 512 re-render (palette/pixel) then exit\n"
        "  --bus-selftest    self-test of the bus error model (whitelist) then exit\n"
        "  --mfp-selftest    self-test of the MFP (GPIP/edges/Timer B) then exit\n"
        "  --msa-selftest    self-test of the .msa re-encoding (round-trip) then exit\n"
        "  --fuji-selftest   self-test of the virtual FujiNet (ACSI wire protocol,\n"
        "                    deterministic replay backend) then exit\n"
        "  --enec-selftest   self-test of the NE2000/EtherNEC (cartridge-port wire\n"
        "                    protocol, loopback backend) then exit\n"
        "  --serial-dump F   write the raw RS-232 serial bytes into F (NEOST-TEST verdicts)\n"
        "  --from-cfg F      replay the GUI config (neost.cfg); later options override it\n"
        "  --dump-at N A L F raw dump of L bytes of RAM from $A (hex) after frame N → F\n"
        "  --screenshot PPM  dump the final framebuffer in PPM format\n"
        "  rom               TOS image (default roms/etos192us.img)\n");
}

// =============================================================================
//  --fuji-selftest — auto-test DÉTERMINISTE du FujiNet virtuel, au niveau FIL :
//  on pilote les registres DMA $FF8604/06 exactement comme le ferait un pilote
//  ST (marqueur ICD, CDB $60 de 10 octets, phases DMA), à travers le VRAI plan
//  mémoire (Bus → Fdc → Acsi → FujiDevice → backend de rejeu). Les fixtures
//  sont auto-générées dans un dossier temporaire : aucune E/S réseau, aucun
//  fichier du dépôt requis — rejouable par tools/run_all.py --tier fast.
// =============================================================================
int fujiSelfTest() {
    namespace fs = std::filesystem;
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* what) {
        std::fprintf(stderr, "[fuji-selftest] %-34s %s\n", what, ok ? "OK" : "FAIL");
        (ok ? passed : failed)++;
    };

    // --- Fixtures auto-générées ------------------------------------------------
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "neost-fuji-selftest";
    fs::create_directories(dir, ec);
    const std::string hello = "Hello from NeoST FujiNet!\n";
    const std::string json  = "{\"name\":\"neost\",\"values\":[10,20,30]}";
    { std::ofstream f(dir / "HTTP___test_hello.txt", std::ios::binary); f << hello; }
    { std::ofstream f(dir / "HTTP___test_data.json", std::ios::binary); f << json; }

    Machine machine;                     // 512 Ko, STE — pas de TOS : on pilote le MMIO
    FujiHostReplay host(dir.string());
    machine.fuji.setHost(&host);
    machine.enableFujiNet(6);

    Bus& bus = machine.bus;
    constexpr uint32_t kDmaBuf = 0x8000;                    // tampon DMA dans la ST-RAM
    auto w16 = [&](uint32_t a, uint16_t v) { bus.write16(a, v); };
    auto setDmaAddr = [&](uint32_t addr) {
        w16(0xFF8608, uint16_t((addr >> 16) & 0xFF));
        w16(0xFF860A, uint16_t((addr >> 8) & 0xFF));
        w16(0xFF860C, uint16_t(addr & 0xFF));
    };
    auto acsiStatus = [&]() -> uint8_t {
        w16(0xFF8606, 0x008A);                              // CSACSI | A0 (statut)
        return uint8_t(bus.read16(0xFF8604) & 0xFF);
    };
    // Émet un CDB FujiNet complet (marqueur ICD cible 6 + 10 octets) puis déclenche
    // la phase DMA (write=false : device→ST ; write=true : ST→device).
    auto sendFujiCdb = [&](uint8_t dev, uint8_t cmd, uint8_t aux1, uint8_t aux2,
                           uint8_t dirByte, uint16_t len, bool write) {
        setDmaAddr(kDmaBuf);
        w16(0xFF8606, 0x0088);                              // CSACSI, A1 bas → 1er octet
        w16(0xFF8604, uint16_t(0x00C0 | 0x1F));             // (6<<5)|$1F : cible 6, marqueur ICD
        w16(0xFF8606, 0x008A);                              // A1 haut → octets suivants
        const uint8_t cdb[10] = {0x60, 0x00, dev, cmd, aux1, aux2, dirByte,
                                 uint8_t(len >> 8), uint8_t(len & 0xFF), 0x00};
        for (uint8_t b : cdb) w16(0xFF8604, b);
        w16(0xFF8606, write ? 0x0100 : 0x0000);             // 0xC0 → 0 : transfert DMA
    };
    // Dépose un payload ST→device dans la RAM (lu par le DMA).
    auto putPayload = [&](const std::string& s) {
        for (std::size_t i = 0; i < s.size() && kDmaBuf + i < bus.ram.size(); ++i)
            bus.ram[kDmaBuf + i] = uint8_t(s[i]);
        bus.ram[kDmaBuf + s.size()] = 0;
    };

    // --- 1. Statut WiFi (device $70, $FA, dir=1) -------------------------------
    sendFujiCdb(0x70, 0xFA, 0, 0, 1, 1, false);
    check(acsiStatus() == 0 && bus.ram[kDmaBuf] == 3, "wifi status ($70/$FA)");

    // --- 2. Horloge (device $70, $D2) — date FIXE du backend de rejeu ----------
    sendFujiCdb(0x70, 0xD2, 0, 0, 1, 7, false);
    check(acsiStatus() == 0 && bus.ram[kDmaBuf] == 0x07 && bus.ram[kDmaBuf + 1] == 0xC1
          && bus.ram[kDmaBuf + 2] == 6, "clock ($70/$D2, deterministic)");

    // --- 3. Open + Status + Read sur le canal N1: ------------------------------
    putPayload("N1:HTTP://test/hello.txt");
    sendFujiCdb(0x71, 'O', 4, 0, 2, 24, true);
    check(acsiStatus() == 0, "N1: open (payload DMA write)");

    sendFujiCdb(0x71, 'S', 0, 0, 1, 4, false);
    const int avail = bus.ram[kDmaBuf] | (bus.ram[kDmaBuf + 1] << 8);
    check(acsiStatus() == 0 && avail == (int)hello.size() && bus.ram[kDmaBuf + 2] == 1,
          "N1: status (avail/connected)");

    sendFujiCdb(0x71, 'R', 0, 0, 1, uint16_t(hello.size()), false);
    bool same = acsiStatus() == 0;
    for (std::size_t i = 0; same && i < hello.size(); ++i)
        same = bus.ram[kDmaBuf + i] == uint8_t(hello[i]);
    check(same, "N1: read (contents)");

    // Relire alors que le canal est vide → erreur propre (contrat FujiNet).
    sendFujiCdb(0x71, 'R', 0, 0, 1, 16, false);
    check(acsiStatus() == 2, "N1: read past EOF fails cleanly");
    sendFujiCdb(0x71, 'C', 0, 0, 0, 0, false);
    check(acsiStatus() == 0, "N1: close");

    // --- 4. JSON déporté (parse + query) sur N2: -------------------------------
    putPayload("N2:HTTP://test/data.json");
    sendFujiCdb(0x72, 'O', 4, 0, 2, 25, true);
    sendFujiCdb(0x72, 'P', 0, 0, 0, 0, false);
    check(acsiStatus() == 0, "N2: JSON parse");
    putPayload("/values/2");
    sendFujiCdb(0x72, 'Q', 0, 0, 2, 10, true);
    sendFujiCdb(0x72, 'R', 0, 0, 1, 2, false);
    check(acsiStatus() == 0 && bus.ram[kDmaBuf] == '3' && bus.ram[kDmaBuf + 1] == '0',
          "N2: JSON query (/values/2 = 30)");

    // --- 5. Les commandes SCSI standard marchent toujours sur la cible 6 -------
    setDmaAddr(kDmaBuf);
    w16(0xFF8606, 0x0088);
    w16(0xFF8604, uint16_t((6u << 5) | 0x12));              // INQUIRY (classe 0)
    w16(0xFF8606, 0x008A);
    for (uint8_t b : {uint8_t(0), uint8_t(0), uint8_t(0), uint8_t(36), uint8_t(0)})
        w16(0xFF8604, b);
    w16(0xFF8606, 0x0000);
    check(acsiStatus() == 0 && bus.ram[kDmaBuf + 8] == 'N' && bus.ram[kDmaBuf + 9] == 'e',
          "target 6: standard INQUIRY intact");

    // --- 6. L'opcode $60 reste REJETÉ sur une cible non-FujiNet ----------------
    machine.fdc.mountAcsi("disks/etalons/selftest_hd.img", 0);   // peut échouer : absent
    w16(0xFF8606, 0x0088);
    w16(0xFF8604, uint16_t((0u << 5) | 0x1F));              // cible 0, marqueur ICD
    w16(0xFF8606, 0x008A);
    w16(0xFF8604, 0x0060);                                  // opcode vendeur…
    // …cible vide (pas d'IRQ) ou peuplée : dans les deux cas, JAMAIS routé FujiNet.
    check(machine.fuji.lastError() == fn_err::OK, "vendor opcode gated to Fuji target");

    std::fprintf(stderr, "[fuji-selftest] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// =============================================================================
//  --enec-selftest — auto-test DÉTERMINISTE de la NE2000/EtherNEC au niveau FIL.
//  On pilote la carte EXACTEMENT comme le pilote ST : écritures registre par
//  fausses lectures ($FA0000 + reg*512 + data*2), lectures par $FB0000 + reg*512,
//  le tout à travers le VRAI plan mémoire (Bus). Backend en boucle locale : une
//  trame émise revient en réception → on la relit via Remote DMA. Aucune E/S
//  réseau. Cf. docs/FUJINET.md § EtherNEC.
// =============================================================================
int enecSelfTest() {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* what) {
        std::fprintf(stderr, "[enec-selftest] %-34s %s\n", what, ok ? "OK" : "FAIL");
        (ok ? passed : failed)++;
    };

    Machine machine;                 // 512 Ko STE, pas de TOS : on pilote la carte au fil
    NetBackendLoop loop;
    machine.ne2000.setBackend(&loop);
    if (!machine.enableEtherNec()) { std::fprintf(stderr, "[enec-selftest] enable failed\n"); return 1; }

    Bus& bus = machine.bus;
    // Accès EtherNEC : tout est une LECTURE dans la fenêtre cartouche.
    auto wr = [&](uint8_t reg, uint8_t data) {
        (void)bus.read8(Ne2000::WRITE_BASE + uint32_t(reg) * 512u + uint32_t(data) * 2u);
    };
    auto rd = [&](uint8_t reg) -> uint8_t {
        return bus.read8(Ne2000::READ_BASE + uint32_t(reg) * 512u);
    };

    // Configuration standard de l'anneau : TX en pages 0x40-0x45, anneau RX
    // 0x46-0x80. (PSTART/PSTOP sont WRITE-ONLY sur le DP8390 — on ne les relit
    // pas ; le décodage registre est prouvé par le round-trip BNRY et la MAC.)
    const uint8_t kTxPage = 0x40, kRxStart = 0x46, kRxStop = 0x80;
    wr(0x00, 0x21);                  // CR : page 0, stop
    wr(0x01, kRxStart);              // PSTART
    wr(0x02, kRxStop);               // PSTOP
    wr(0x03, kRxStart);              // BNRY (dernière page LUE)

    // --- 1. Décodage registre : BNRY est lisible (round-trip) -----------------
    check(rd(0x03) == kRxStart, "register decode (BNRY round-trip)");

    // --- 2. MAC + CURR en page 1 ----------------------------------------------
    const uint8_t mac[6] = {0x02, 0x4E, 0x53, 0x54, 0x12, 0x34};
    wr(0x00, 0x61);                  // CR : page 1
    for (int i = 0; i < 6; ++i) wr(uint8_t(0x01 + i), mac[i]);
    wr(0x07, kRxStart);              // CURR = 1re page d'écriture de l'anneau
    bool macOk = true;
    for (int i = 0; i < 6; ++i) macOk = macOk && rd(uint8_t(0x01 + i)) == mac[i];
    check(macOk && rd(0x07) == kRxStart, "MAC + CURR in page 1");
    wr(0x00, 0x21);                  // retour page 0

    // --- 3. Remote DMA : écrire une trame dans la RAM NIC, la relire ----------
    // Trame Ethernet : dst=broadcast, src=MAC, type=0x0800, payload court.
    uint8_t frame[32];
    memset(frame, 0xFF, 6);          // dst broadcast
    memcpy(frame + 6, mac, 6);       // src
    frame[12] = 0x08; frame[13] = 0x00;
    for (int i = 14; i < 32; ++i) frame[i] = uint8_t(0xA0 + i);
    const uint16_t txaddr = uint16_t(kTxPage) * 256u;
    wr(0x08, uint8_t(txaddr)); wr(0x09, uint8_t(txaddr >> 8));   // RSAR
    wr(0x0A, uint8_t(sizeof frame)); wr(0x0B, 0);               // RBCR
    wr(0x00, 0x12);                  // CR : Remote Write (RD1) + STA
    for (uint8_t b : frame) wr(0x10, b);
    // Relecture par Remote Read.
    wr(0x08, uint8_t(txaddr)); wr(0x09, uint8_t(txaddr >> 8));
    wr(0x0A, uint8_t(sizeof frame)); wr(0x0B, 0);
    wr(0x00, 0x0A);                  // CR : Remote Read (RD0) + STA
    bool dmaOk = true;
    for (uint8_t b : frame) dmaOk = dmaOk && rd(0x10) == b;
    check(dmaOk, "remote DMA read-back (RAM NIC)");

    // --- 4. Transmission → backend boucle → réception dans l'anneau -----------
    wr(0x04, kTxPage);                        // TPSR = page de départ TX
    wr(0x05, uint8_t(sizeof frame)); wr(0x06, 0);   // TBCR
    wr(0x0C, 0x04);                           // RCR : accepte broadcast (AB)
    wr(0x00, 0x26);                           // CR : TXP + STA (page 0)
    machine.ne2000.poll();                    // la trame émise revient en réception
    const uint8_t isr = rd(0x07);
    check((isr & 0x02) && (isr & 0x01), "TX done + RX into ring (ISR PTX|PRX)");

    // --- 5. Lecture de l'en-tête de la trame reçue (page CURR init = kRxStart) -
    const uint16_t hdrAddr = uint16_t(kRxStart) * 256u;
    wr(0x08, uint8_t(hdrAddr)); wr(0x09, uint8_t(hdrAddr >> 8));
    wr(0x0A, 4); wr(0x0B, 0);
    wr(0x00, 0x0A);                           // Remote Read
    const uint8_t rsr = rd(0x10), next = rd(0x10);
    const uint16_t rlen = uint16_t(rd(0x10) | (rd(0x10) << 8));
    (void)next;
    check((rsr & 0x01) && rlen == sizeof frame + 4, "RX ring packet header (status/len)");

    std::fprintf(stderr, "[enec-selftest] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// Dump du framebuffer décodé en PPM binaire (P6) — comparable visuellement.
bool writePpm(const char* path, const uint32_t* px, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        const uint32_t c = px[i];                 // ARGB8888
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((c >> 16) & 0xFF),
            static_cast<unsigned char>((c >> 8)  & 0xFF),
            static_cast<unsigned char>( c        & 0xFF) };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

uint8_t stScancode(char c) {
    switch (c) {
        case '1': return 0x02; case '2': return 0x03;
        case '3': return 0x04; case '4': return 0x05;
        case '5': return 0x06; case '6': return 0x07;
        case '7': return 0x08; case '8': return 0x09;
        case '9': return 0x0A; case '0': return 0x0B;
        case '\n': case '\r': return 0x1C;
        case 'a': case 'A': return 0x1E;
        case 's': case 'S': return 0x1F;
        case 'd': case 'D': return 0x20;
        case 'f': case 'F': return 0x21;
        case 'g': case 'G': return 0x22;
        case 'h': case 'H': return 0x23;
        case 'j': case 'J': return 0x24;
        case 'k': case 'K': return 0x25;
        case 'l': case 'L': return 0x26;
        case 'z': case 'Z': return 0x2C;
        case 'x': case 'X': return 0x2D;
        case 'c': case 'C': return 0x2E;
        case 'v': case 'V': return 0x2F;
        case 'b': case 'B': return 0x30;
        case 'n': case 'N': return 0x31;
        case 'm': case 'M': return 0x32;
        case ' ': return 0x39;
        // Touches spéciales pour piloter des menus (scancodes ST) : flèches, Esc,
        // F1-F3, Tab, Backspace, Delete. Conventions ASCII libres choisies ici.
        case '<': return 0x48;   // flèche HAUT
        case '>': return 0x50;   // flèche BAS
        case '[': return 0x4B;   // flèche GAUCHE
        case ']': return 0x4D;   // flèche DROITE
        case '=': return 0x01;   // Esc
        case '!': return 0x3B;   // F1
        case '@': return 0x3C;   // F2
        case '#': return 0x3D;   // F3
        case '$': return 0x3E;   // F4
        case '%': return 0x3F;   // F5
        case '\t': return 0x0F;  // Tab
        case '^': return 0x0E;   // Backspace
        case '~': return 0x53;   // Delete
        case 'q': case 'Q': return 0x10;
        case 'w': case 'W': return 0x11;
        case 'e': case 'E': return 0x12;
        case 'r': case 'R': return 0x13;
        case 't': case 'T': return 0x14;
        case 'y': case 'Y': return 0x15;
        case 'u': case 'U': return 0x16;
        case 'i': case 'I': return 0x17;
        case 'o': case 'O': return 0x18;
        case 'p': case 'P': return 0x19;
        default: return 0x00;
    }
}
} // namespace

int main(int argc, char** argv) {
    int         frames     = 200;
    std::string tracePath;
    int         traceFrom  = 0;       // --trace-from N : n'active la trace qu'à la trame N
                                      // (fenêtrer un diff oracle sur une scène tardive — menu
                                      // de démo — sans traîner des Go de boot)
    std::string shotPath;
    std::string diskPath   = "disks/diskA.st";
    std::string diskBPath;                       // lecteur B (optionnel, --diskb)
    bool        fastFdc    = false;   // FDC rapide (--fastfdc) : délais commande/transfert ÷10
    std::string romPath    = "roms/etos192us.img";
    std::string cartPath;
    std::string printerPath;                     // --printer FILE : capture Centronics (port parallèle)
    std::string gemdosDir;                       // --gemdos DIR : disque dur GEMDOS (dossier hôte)
    std::string acsiImg;                         // --acsi IMG : image disque dur ACSI (cible 0)
    bool        fujinet       = false;           // --fujinet : FujiNet virtuel sur le bus ACSI
    int         fujinetTarget = 6;               // --fujinet-target N (défaut 6)
    std::string fujinetHost;                     // --fujinet-host URL (slot 0 + auto-montage)
    std::string fujinetReplay;                   // --fujinet-replay DIR (backend déterministe)
    bool        fujinetOffline = false;          // --fujinet-offline : backend nul
    bool        modemFlag      = false;          // --modem : modem Hayes sur l'USART
    bool        ethernecFlag   = false;          // --ethernec : NE2000 port cartouche
    std::string midiNetPeer;                     // --midi-net host:port[:listen] : anneau MIDI UDP
    int         midiNetListen  = 6820;           // port d'écoute par défaut
    std::string soundDumpPath;                   // --sound-dump F : WAV 48 kHz de la boucle --frames
    std::string serialDumpPath;                  // --serial-dump F : octets série RS-232 bruts (verdicts)
    bool        outFail    = false;   // une SORTIE fichier a échoué → exit ≠ 0 (jamais silencieux)
    bool        regs       = false;
    bool        irq        = false;
    bool        haveUntil  = false;
    uint32_t    untilPc    = 0;
    std::vector<uint32_t> breakAddrs;            // --break HEX : breakpoints PC (répétable)
    std::vector<uint32_t> watchAddrs;            // --watch HEX : watchpoints mémoire (répétable)
    bool        saveStateTest = false;            // --save-state-test : run N → save → modif → load → re-save == save
    std::string saveStatePath;                    // --save-state FILE : écrit l'état à la fin de la boucle
    std::string loadStatePath;                    // --load-state FILE : restaure l'état AVANT de tourner
    std::vector<std::string> breakSyms;          // --break-sym NAME : breakpoints par symbole
    std::string symbolsPath;                     // --symbols FILE (.sym nm-style ou exécutable TOS)
    uint32_t    symBase = 0;                      // --symbols-base HEX (relocation d'un exécutable TOS)
    SymbolTable symbols;
    bool        walkMouse  = false;
    std::string keys;                 // touches à injecter après le boot (ex. "Z\n")
    bool        haveJoy    = false;   // --joy : maintient un état joystick pendant le run
    uint8_t     joy0Hold   = 0, joy1Hold = 0;  // bits ST (haut$01 bas$02 gauche$04 droite$08 feu$80)
    bool        loopback   = false;   // « branche » le connecteur de bouclage RS232 (test S)
    bool        machineMono = false;
    bool        glueSelfTest = false; // auto-test déterministe de la machine Glue (bordures)
    bool        spec512SelfTest = false; // auto-test déterministe du re-rendu Spectrum 512
    bool        busSelfTest  = false;  // auto-test déterministe du modèle de bus error
    bool        mfpSelfTest  = false;  // auto-test déterministe du MFP (GPIP/fronts/Timer B)
    bool        msaSelfTest  = false;  // auto-test déterministe du ré-encodage .msa
    bool        fujiSelfTestFlag = false; // auto-test déterministe du FujiNet (protocole fil)
    bool        enecSelfTestFlag = false; // auto-test déterministe NE2000/EtherNEC (fil)
    int         shotEvery   = 0;      // --shot-every N : dump une capture toutes les N trames
    std::string shotPrefix;           // --shot-every PREFIX : préfixe des captures périodiques
    int         shotFrom    = 0;      // --shot-from N : ne capture qu'à partir de la trame N
    // Injections DATÉES dans la boucle principale (≠ --keys/--joy qui agissent après/avant) :
    // indispensables pour piloter un menu de démo (intro → menu → déplacement) tout en
    // gardant --shot-every actif (calibration d'étalons, diagnostic scrolling).
    // --keys-at N STR : tape STR à partir de la trame N. RÉPÉTABLE : plusieurs
    // occurrences = plusieurs frappes datées (menus en cascade — cracktro D-BUG
    // « Y/N » PUIS « press any key », etc.).
    std::vector<std::pair<int, std::string>> keysAtList;
    // --key-down N C / --key-up N C : make SEUL à la trame N (resp. break seul) —
    // reproduit une touche TENUE comme en GUI (≠ --keys-at qui pulse make/break
    // toutes les 4 trames). C = caractère de la table stScancode ('[' ']' '<' '>'…).
    // RÉPÉTABLES (paires down/up successives).
    std::vector<std::pair<int, char>> keyDownList, keyUpList;
    int         joyAtFrame  = -1;     // --joy-at N P1 : pose l'état joystick port 1 à la trame N
    uint8_t     joyAt1      = 0;
    // --mouse-at N "SCRIPT" : pilote la souris (mode REL) à partir de la trame N pour
    // naviguer un menu souris (ex. Vroom). Un token = une trame ; L/R/U/D = déplacement
    // (±8 px), '1' = clic gauche, '2' = clic droit (appui+relâche sur 2 trames), '.' = idle.
    int         mouseAtFrame = -1;
    std::string mouseAt;
    // --joy-script N "SCRIPT" : pose l'état joystick port 1 trame par trame à partir de N.
    // Tokens : U/D/L/R = direction, F = feu, '.' = neutre. Permet de PULSER (presser puis
    // relâcher) le feu et de bouger une sélection dans un menu joystick (ex. Vroom).
    int         joyScrFrame = -1;
    std::string joyScr;
    // --dump-at N ADDR LEN FILE : dump brut de LEN octets de RAM à partir d'ADDR
    // (hex) après la trame N — diff de buffers contre l'oracle Hatari (débogueur
    // « m addr len »). Lectures via bus.read8 (RAM : sans effet de bord).
    int         dumpAtFrame = -1;
    bool        dumpDone    = false;   // le --dump-at a-t-il RÉELLEMENT eu lieu ? (cf. fin de boucle)
    uint32_t    dumpAddr = 0, dumpLen = 0;
    std::string dumpPath;
    CpuCore     cpuCore    = CpuCore::Moira;   // seul cœur disponible (cycle-exact)
    MachineType machType   = MachineType::Ste;
    std::size_t ramBytes   = 512u * 1024u;
    bool        fpuPresent = false;     // --fpu : MC68881 Mega STE (cf. Fpu.hpp)

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s expects an argument\n", opt); std::exit(2); }
            return argv[++i];
        };
        if (!std::strcmp(a, "--version")) {       // identité de build
#ifdef NEOST_VERSION
            std::printf("neost-headless %s\n", NEOST_VERSION);
#else
            std::printf("neost-headless (unknown version)\n");
#endif
            return 0;
        }
        // Clamp ≥ 0 : une valeur négative signée se propagerait aux calculs de
        // tailles size_t (--sound-dump réserve frames × 48 kHz) → allocation géante.
        if      (!std::strcmp(a, "--frames"))     frames    = std::max(0, std::atoi(next(a)));
        else if (!std::strcmp(a, "--sound-dump")) soundDumpPath = next(a);
        else if (!std::strcmp(a, "--serial-dump")) serialDumpPath = next(a);
        else if (!std::strcmp(a, "--trace"))      tracePath = next(a);
        else if (!std::strcmp(a, "--trace-from")) traceFrom = std::atoi(next(a));
        else if (!std::strcmp(a, "--regs"))       regs      = true;
        else if (!std::strcmp(a, "--irq"))        irq       = true;
        else if (!std::strcmp(a, "--screenshot")) shotPath  = next(a);
        else if (!std::strcmp(a, "--disk"))       diskPath  = next(a);
        else if (!std::strcmp(a, "--diskb"))      diskBPath = next(a);
        else if (!std::strcmp(a, "--fastfdc"))    fastFdc   = true;
        else if (!std::strcmp(a, "--cart"))       cartPath  = next(a);
        else if (!std::strcmp(a, "--gemdos"))     gemdosDir = next(a);
        else if (!std::strcmp(a, "--printer"))    printerPath = next(a);
        else if (!std::strcmp(a, "--acsi") || !std::strcmp(a, "--hd")) acsiImg = next(a);
        else if (!std::strcmp(a, "--fujinet"))         fujinet = true;
        else if (!std::strcmp(a, "--fujinet-target"))  { fujinet = true; fujinetTarget = std::atoi(next(a)); }
        else if (!std::strcmp(a, "--fujinet-host"))    { fujinet = true; fujinetHost = next(a); }
        else if (!std::strcmp(a, "--fujinet-replay"))  { fujinet = true; fujinetReplay = next(a); }
        else if (!std::strcmp(a, "--fujinet-offline")) { fujinet = true; fujinetOffline = true; }
        else if (!std::strcmp(a, "--modem"))           modemFlag = true;
        else if (!std::strcmp(a, "--ethernec"))        ethernecFlag = true;
        else if (!std::strcmp(a, "--midi-net")) {
            // "host:port" ou "host:port:listen" (le port d'écoute par défaut est 6820).
            std::string s = next(a);
            const auto p1 = s.find(':');
            const auto p2 = (p1 == std::string::npos) ? std::string::npos : s.find(':', p1 + 1);
            if (p2 != std::string::npos) { midiNetListen = std::atoi(s.c_str() + p2 + 1); s = s.substr(0, p2); }
            midiNetPeer = s;
        }
        else if (!std::strcmp(a, "--walk-mouse")) walkMouse = true;
        else if (!std::strcmp(a, "--keys"))       keys      = next(a);
        else if (!std::strcmp(a, "--joy")) {      // état joystick maintenu : "P1" ou "P1,P0"
            const char* s = next(a);
            joy1Hold = (uint8_t)std::strtoul(s, nullptr, 0);   // port 1 (jeux) en premier
            const char* comma = std::strchr(s, ',');
            joy0Hold = comma ? (uint8_t)std::strtoul(comma + 1, nullptr, 0) : 0;  // port 0 optionnel
            haveJoy = true;
        }
        else if (!std::strcmp(a, "--loopback"))   loopback  = true;
        else if (!std::strcmp(a, "--mono"))       machineMono = true;
        else if (!std::strcmp(a, "--glue-selftest")) glueSelfTest = true;
        else if (!std::strcmp(a, "--spec512-selftest")) spec512SelfTest = true;
        else if (!std::strcmp(a, "--bus-selftest")) busSelfTest = true;
        else if (!std::strcmp(a, "--mfp-selftest")) mfpSelfTest = true;
        else if (!std::strcmp(a, "--msa-selftest")) msaSelfTest = true;
        else if (!std::strcmp(a, "--fuji-selftest")) fujiSelfTestFlag = true;
        else if (!std::strcmp(a, "--enec-selftest")) enecSelfTestFlag = true;
        else if (!std::strcmp(a, "--shot-every"))  { shotEvery = std::atoi(next(a)); shotPrefix = next(a); }
        else if (!std::strcmp(a, "--shot-from"))   shotFrom = std::atoi(next(a));
        else if (!std::strcmp(a, "--keys-at"))     { const int f = std::atoi(next(a)); keysAtList.emplace_back(f, next(a)); }
        else if (!std::strcmp(a, "--key-down"))    { const int f = std::atoi(next(a)); keyDownList.emplace_back(f, next(a)[0]); }
        else if (!std::strcmp(a, "--key-up"))      { const int f = std::atoi(next(a)); keyUpList.emplace_back(f, next(a)[0]); }
        else if (!std::strcmp(a, "--joy-at"))      { joyAtFrame = std::atoi(next(a)); joyAt1 = (uint8_t)std::strtoul(next(a), nullptr, 0); }
        else if (!std::strcmp(a, "--mouse-at"))    { mouseAtFrame = std::atoi(next(a)); mouseAt = next(a); }
        else if (!std::strcmp(a, "--joy-script"))  { joyScrFrame = std::atoi(next(a)); joyScr = next(a); }
        else if (!std::strcmp(a, "--dump-at"))     { dumpAtFrame = std::atoi(next(a));
                                                     dumpAddr = (uint32_t)std::strtoul(next(a), nullptr, 16);
                                                     dumpLen  = (uint32_t)std::strtoul(next(a), nullptr, 0);
                                                     dumpPath = next(a); }
        else if (!std::strcmp(a, "--from-cfg")) {
            // P3 — pont GUI↔headless : rejoue la config exacte de neost.cfg (machine,
            // TOS, mem, cpu, disque, cartouche, mono, fastfdc, fpu, gemdos, acsi). Les
            // options CLI placées APRÈS --from-cfg surchargent (le cfg sert de base).
            const char* p = next(a);
            std::ifstream cf(p);
            // peek() en plus de l'ouverture : sous Linux, ouvrir un RÉPERTOIRE réussit,
            // et la boucle de lecture qui suit tournait alors à vide — NeoST annonçait
            // « config reprise de … » puis démarrait sur ses valeurs par défaut.
            if (!cf || cf.peek() == std::ifstream::traits_type::eof()) {
                std::fprintf(stderr, "[headless] --from-cfg: %s not found or unreadable\n", p);
                return 2;
            }
            // Les chemins de neost.cfg sont relatifs à exeDir (= <racine>/build) : le GUI
            // les écrit préfixés « ./../ » (build → racine). On résout relativement au
            // DOSSIER du cfg après avoir collapsé ce préfixe, pour retomber sur la racine.
            const std::string cfgp = p;
            const std::size_t slash = cfgp.find_last_of('/');
            const std::string cfgDir = (slash == std::string::npos) ? "" : cfgp.substr(0, slash);
            auto resolve = [&](std::string s) -> std::string {
                if (s.empty() || s[0] == '/') return s;
                while (s.rfind("./", 0) == 0) s = s.substr(2);     // ./ répétés
                if (s.rfind("../", 0) == 0)   s = s.substr(3);     // build → racine
                return cfgDir.empty() ? s : cfgDir + "/" + s;
            };
            std::string ln;
            auto v = [](const std::string& s, std::size_t n) { return s.substr(n); };
            while (std::getline(cf, ln)) {
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                if      (ln.rfind("rom=", 0) == 0)     { if (ln.size() > 4) romPath   = resolve(v(ln, 4)); }
                else if (ln.rfind("disk=", 0) == 0)    { if (ln.size() > 5) diskPath  = resolve(v(ln, 5)); }
                else if (ln.rfind("cart=", 0) == 0)    { if (ln.size() > 5) cartPath  = resolve(v(ln, 5)); }
                else if (ln.rfind("gemdos=", 0) == 0)  { if (ln.size() > 7) gemdosDir = resolve(v(ln, 7)); }
                else if (ln.rfind("acsi=", 0) == 0)    { if (ln.size() > 5) acsiImg   = resolve(v(ln, 5)); }
                else if (ln.rfind("fujinet=", 0) == 0) fujinet = (v(ln, 8) == "1");
                else if (ln.rfind("fujinet_target=", 0) == 0) fujinetTarget = std::atoi(v(ln, 15).c_str());
                else if (ln.rfind("fujinet_host=", 0) == 0) { if (ln.size() > 13) fujinetHost = v(ln, 13); }
                // Clé écrite par le GUI : liste de slots séparés par '|', slot 0 en tête.
                else if (ln.rfind("fujinet_hosts=", 0) == 0) {
                    if (ln.size() > 14) fujinetHost = v(ln, 14).substr(0, v(ln, 14).find('|'));
                }
                else if (ln.rfind("machine=", 0) == 0) machType   = parseMachine(v(ln, 8).c_str());
                else if (ln.rfind("mem=", 0) == 0)     ramBytes   = parseRamBytes(v(ln, 4).c_str());
                else if (ln.rfind("cpu=", 0) == 0)     cpuCore    = Cpu68k::parseCore(v(ln, 4).c_str());
                else if (ln.rfind("mono=", 0) == 0)    machineMono = (v(ln, 5) == "1");
                else if (ln.rfind("fastfdc=", 0) == 0) fastFdc    = (v(ln, 8) == "1");
                else if (ln.rfind("fpu=", 0) == 0)     fpuPresent = (v(ln, 4) == "1");
            }
            std::fprintf(stderr, "[headless] config taken from %s\n", p);
        }
        else if (!std::strcmp(a, "--cpu"))        cpuCore   = Cpu68k::parseCore(next(a));
        else if (!std::strcmp(a, "--machine"))    machType  = parseMachine(next(a));
        else if (!std::strcmp(a, "--fpu"))        fpuPresent = true;
        else if (!std::strcmp(a, "--mem"))        ramBytes  = parseRamBytes(next(a));
        else if (!std::strcmp(a, "--until-pc"))   { untilPc = (uint32_t)std::strtoul(next(a), nullptr, 16); haveUntil = true; }
        else if (!std::strcmp(a, "--break"))      breakAddrs.push_back((uint32_t)std::strtoul(next(a), nullptr, 16));
        else if (!std::strcmp(a, "--watch"))      watchAddrs.push_back((uint32_t)std::strtoul(next(a), nullptr, 16));
        else if (!std::strcmp(a, "--save-state-test")) saveStateTest = true;
        else if (!std::strcmp(a, "--save-state")) saveStatePath = next(a);
        else if (!std::strcmp(a, "--load-state")) loadStatePath = next(a);
        else if (!std::strcmp(a, "--break-sym"))  breakSyms.emplace_back(next(a));
        else if (!std::strcmp(a, "--symbols"))    symbolsPath = next(a);
        else if (!std::strcmp(a, "--symbols-base")) symBase = (uint32_t)std::strtoul(next(a), nullptr, 16);
        else if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) { usage(); return 0; }
        else if (a[0] == '-')                     { std::fprintf(stderr, "unknown option: %s\n", a); usage(); return 2; }
        else                                      romPath   = a;
    }

    // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
    machType = Machine::adjustMachineForTos(machType, romPath);
    Machine machine(ramBytes, cpuCore, machType);
    if (fpuPresent) {
        if (machType == MachineType::MegaSte) machine.bus.setFpuPresent(true);
        else std::fprintf(stderr, "[headless] --fpu ignored: the 68881 socket only exists "
                                  "on the Mega STE (--machine megaste)\n");
    }
    // Auto-test de la machine Glue (bordures) : pas besoin de ROM/boot, on teste
    // directement la logique du Shifter contre les valeurs documentées d'Hatari.
    if (glueSelfTest) return machine.shifter.glueSelfTest() ? 0 : 1;
    if (spec512SelfTest) return machine.shifter.spec512SelfTest() ? 0 : 1;
    if (busSelfTest) return machine.bus.busSelfTest() ? 0 : 1;
    if (mfpSelfTest) return machine.mfp.mfpSelfTest() ? 0 : 1;
    if (msaSelfTest) return machine.fdc.msaSelfTest() ? 0 : 1;
    if (fujiSelfTestFlag) return fujiSelfTest();
    if (enecSelfTestFlag) return enecSelfTest();
    std::fprintf(stderr, "[headless] CPU core: %s | machine: %s | RAM: %s\n",
                 Cpu68k::coreName(machine.cpu.core()), machineName(machType), ramLabel(ramBytes));
    if (!machine.loadTos(romPath)) {
        std::fprintf(stderr, "[headless] cannot load %s\n", romPath.c_str());
        return 1;
    }
    machine.loadDisk(diskPath);   // lecteur A (optionnel)
    if (!diskBPath.empty()) machine.loadDiskB(diskBPath);   // lecteur B (optionnel)
    machine.fdc.setFastFdc(fastFdc);   // FDC rapide (--fastfdc) : accès disque ÷10
    // Disque dur GEMDOS (--gemdos) : installe la cartouche système à $FA0000 →
    // exclusif avec une cartouche externe (--cart), comme Hatari.
    if (!gemdosDir.empty()) {
        if (!cartPath.empty())
            std::fprintf(stderr, "[headless] --cart ignored: incompatible with --gemdos\n");
        machine.gemdos.setDirectory(gemdosDir);
    } else if (!cartPath.empty()) {
        machine.loadCart(cartPath);   // cartouche $FA0000 (optionnelle)
    }
    // Imprimante Centronics (--printer FILE) : capture les octets imprimés dans FILE.
    if (!printerPath.empty()) {
        if (machine.setPrinterFile(printerPath))
            std::printf("[headless] Centronics printer → %s\n", printerPath.c_str());
        else
            std::fprintf(stderr, "[headless] cannot open %s for the printer\n", printerPath.c_str());
    }
    // Disque dur ACSI (--acsi/--hd) : le TOS détecte le périphérique, lit la table de
    // partitions et monte les partitions FAT (C:, D:…). Indépendant du GEMDOS HD.
    if (!acsiImg.empty() && machine.fdc.mountAcsi(acsiImg))
        std::fprintf(stderr, "[headless] ACSI: %d partition(s) detected\n",
                     machine.fdc.acsiPartitionCount());
    // FujiNet virtuel (--fujinet…) : cible ACSI dédiée + backend hôte. Le backend
    // vit ici (frontend) — le cœur ne voit que l'interface FujiHost.
    std::unique_ptr<FujiHost> fujiHost;
    if (fujinet) {
        if (fujinetTarget < 0 || fujinetTarget > 7) {
            std::fprintf(stderr, "[headless] --fujinet-target must be 0-7\n");
            return 2;
        }
        if (!fujinetReplay.empty())
            fujiHost = std::make_unique<FujiHostReplay>(fujinetReplay);
        else if (fujinetOffline)
            fujiHost = std::make_unique<FujiHostNull>();
        else {
#ifdef NEOST_WITH_NET
            fujiHost = std::make_unique<FujiHostLive>();
#else
            std::fprintf(stderr, "[headless] this build has no network backend "
                                 "(NEOST_WITH_NET=OFF) — FujiNet is offline\n");
            fujiHost = std::make_unique<FujiHostNull>();
#endif
        }
        machine.fuji.setHost(fujiHost.get());
        machine.enableFujiNet(fujinetTarget);
        std::fprintf(stderr, "[headless] FujiNet backend: %s\n", fujiHost->name());
        if (!fujinetHost.empty()) {
            if (machine.fuji.mountRemote(fujinetHost))
                std::fprintf(stderr, "[headless] FujiNet: %s mounted\n", fujinetHost.c_str());
            else {
                machine.fuji.setHostSlot(0, fujinetHost);
                std::fprintf(stderr, "[headless] FujiNet: %s in host slot 0 (not a "
                                     "mountable image)\n", fujinetHost.c_str());
            }
        }
    }
    machine.mfp.setColorMonitor(!machineMono);   // --mono → moniteur mono (haute rés)

    // Capture du port série (RS-232) : les ROMs de diagnostic y impriment leur
    // rapport. On l'affiche sur stderr en fin d'exécution.
    std::string serialOut;
    machine.mfp.setSerialSink([&serialOut](uint8_t b) { serialOut.push_back(char(b)); });
#ifdef NEOST_WITH_NET
    // Modem Hayes (--modem) : commandes AT sur l'USART → pont TCP réel. Le sink
    // série CHAÎNE la capture de verdicts (inchangée) et le modem.
    std::unique_ptr<HayesModem> modem;
    if (modemFlag) {
        modem = std::make_unique<HayesModem>(machine.mfp);
        HayesModem* m = modem.get();
        machine.mfp.setSerialSink([&serialOut, m](uint8_t b) {
            serialOut.push_back(char(b));
            m->onTx(b);
        });
        std::fprintf(stderr, "[headless] Hayes modem on RS-232 (ATDT host:port)\n");
    }
#else
    if (modemFlag)
        std::fprintf(stderr, "[headless] --modem ignored: no network backend in this build\n");
#endif
    // EtherNEC (--ethernec) : NE2000 sur le port cartouche, backend boucle locale
    // (aucune E/S réseau). Exclusif d'une cartouche montée.
    NetBackendLoop enecLoop;
    if (ethernecFlag) {
        machine.ne2000.setBackend(&enecLoop);
        if (machine.enableEtherNec())
            std::fprintf(stderr, "[headless] EtherNEC (NE2000) on the cartridge port\n");
        else
            std::fprintf(stderr, "[headless] --ethernec refused: the cartridge port is in use\n");
    }
    // Anneau MIDI réseau (--midi-net) : MIDI OUT → UDP → pair aval ; datagrammes
    // de l'amont → MIDI IN. Débranche le bouclage interne de l'ACIA MIDI.
#ifdef NEOST_WITH_NET
    std::unique_ptr<MidiRing> midiRing;
    if (!midiNetPeer.empty()) {
        midiRing = std::make_unique<MidiRing>();
        if (midiRing->open(midiNetPeer, midiNetListen)) {
            MidiRing* r = midiRing.get();
            machine.midi.setMidiSink([r](uint8_t b) { r->sendByte(b); });
            std::fprintf(stderr, "[headless] MIDI ring: OUT->%s, IN<-udp:%d\n",
                         midiNetPeer.c_str(), midiNetListen);
        } else {
            std::fprintf(stderr, "[headless] --midi-net: cannot open the UDP ring\n");
            midiRing.reset();
        }
    }
#else
    if (!midiNetPeer.empty())
        std::fprintf(stderr, "[headless] --midi-net ignored: no network backend in this build\n");
#endif

    Tracer tracer;
    if (!tracePath.empty()) {
        if (!tracer.open(tracePath)) {
            std::fprintf(stderr, "[headless] cannot open the trace %s\n", tracePath.c_str());
            return 1;
        }
        tracer.setLogRegs(regs);
        tracer.setLogInterrupts(irq);
        if (traceFrom <= 0)
            machine.cpu.setTracer(&tracer);    // active le hook d'instruction
        // --trace-from N > 0 : le hook n'est branché qu'à la trame N (boucle principale).
    }

    // Mode déterministe absolu : l'horloge RTC Mega ST(E) doit être constante.
    // Sinon EmuTOS STE affiche l'heure système réelle sur le bureau, ce qui casse
    // la comparaison pixel au pixel (test `etos_ste_boot`).
    machine.rtc.setDateTime(Rtc::DateTime{0, 0, 12, 1, 1, 1, 26}); // 1er jan 2026, 12:00:00
    // Même chose pour l'horloge IKBD (commande $1C) : EmuTOS STE/ST affiche la
    // date/heure du bureau depuis l'IKBD, pas la RTC — figée pour le déterminisme.
    machine.ikbd.setClock(26, 1, 1, 12, 0, 0);

    machine.reset();

    // Joystick maintenu (--joy) : pose l'état hôte sur l'IKBD (lu aux interrogations
    // $16 et au report auto $14). Constant pour tout le run — utile pour piloter un
    // jeu (« tient le feu/une direction ») ou valider le chemin de report joystick.
    if (haveJoy) {
        machine.ikbd.setJoystick(joy0Hold, joy1Hold);
        machine.bus.stePads.setJoystick(joy0Hold, joy1Hold);   // joypads STE ($FF9200/02)
        std::fprintf(stderr, "[headless] joystick held: port1=$%02X port0=$%02X\n",
                     joy1Hold, joy0Hold);
    }

    // Dump audio (--sound-dump) : LA chaîne de mixage du projet (core/AudioMix.cpp,
    // partagée avec le GUI et le frontend web) — YM2149 horodaté (modèle push) +
    // DMA STE + gains/tonalité LMC1992 — mais débit EXACT (frameCycles × 48 kHz /
    // CPU_HZ, report fractionnaire) sans asservissement d'anneau (pas de
    // périphérique). Couvre la boucle --frames.
    constexpr uint32_t kDumpRate = 48000;
    std::vector<int16_t> dumpPcm;                 // stéréo entrelacé s16
    neost::FrameMixBuffers dumpBuf;
    double dumpCarry = 0.0;
    const bool soundDump = !soundDumpPath.empty();
    if (soundDump) {
        machine.psg.setCycleClock([&machine] { return machine.frameRelCycle(); });
        machine.dmasnd.setCycleClock([&machine] { return machine.frameRelCycle(); });
        // Plafonné : reserve() n'est qu'une optimisation, et --frames n'est borné que
        // par le bas — « --frames 90000000 --sound-dump » demandait 320 Go et mourait
        // sur std::bad_alloc (SIGABRT + core) avant d'émuler la moindre trame.
        dumpPcm.reserve(std::min<std::size_t>(std::size_t(frames) * kDumpRate / 50 * 2,
                                              64u << 20));
    }
    auto dumpFrame = [&]() {
        static constexpr double CPU_HZ = 8021248.0;
        const int64_t fc = machine.frameCycles();
        dumpCarry += double(fc) * kDumpRate / CPU_HZ;
        const int n = int(dumpCarry);
        dumpCarry -= n;
        if (n <= 0) return;
        float* st = neost::mixEmulatedFrame(machine.psg, &machine.dmasnd,
                                            machineHasDmaSound(machine.bus.machine),
                                            uint32_t(n), kDumpRate, fc, dumpBuf);
        if (!st) return;
        for (int i = 0; i < 2 * n; ++i) {         // clamp → s16 (comme l'anneau GUI)
            float s = st[i];
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            dumpPcm.push_back(int16_t(std::lround(s * 32767.0f)));
        }
    };

    // Exécution déterministe : nombre fixe de trames (pas de Date/random/sleep).
    // Note : --until-pc s'évalue par trame (granularité d'une trame), suffisant
    // pour borner une capture autour d'un point d'intérêt.
    // Symboles (débogueur) : charge la table puis résout les breakpoints par nom.
    if (!symbolsPath.empty()) {
        if (symbols.load(symbolsPath, symBase))
            std::fprintf(stderr, "[headless] symbols: %zu loaded from %s\n",
                         symbols.count(), symbolsPath.c_str());
        else
            std::fprintf(stderr, "[headless] symbols: failed to load %s\n", symbolsPath.c_str());
    }
    // Save-state — test de DÉTERMINISME (le vrai) : run N trames → save A → passe DIRECTE
    // (200 trames, capture état+écran) → load(A) → RE-JOUE 200 trames → capture état+écran.
    // Si la restauration est complète, l'état re-sérialisé ET l'écran sont byte-identiques.
    // Une divergence d'état affiche le 1ᵉʳ offset qui diffère → localise le champ oublié
    // (l'ordre de sérialisation est connu : Machine::serializeState).
    if (saveStateTest) {
        auto screenHash = [&]() -> uint64_t {   // FNV-1a 64 bits sur le framebuffer
            const uint8_t* p = reinterpret_cast<const uint8_t*>(machine.shifter.pixels());
            const size_t n = size_t(machine.shifter.width()) * machine.shifter.height() * 4;
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
            return h;
        };
        const int runLen = 200;
        for (int i = 0; i < frames; ++i) machine.runFrame();       // → point de sauvegarde
        std::vector<uint8_t> A; machine.saveState(A);
        for (int i = 0; i < runLen; ++i) machine.runFrame();       // passe DIRECTE
        std::vector<uint8_t> stD; machine.saveState(stD);
        const uint64_t hD = screenHash();
        if (!machine.loadState(A.data(), A.size())) {
            std::fprintf(stderr, "[save-state-det] FAIL: loadState failed\n");
            return 1;
        }
        for (int i = 0; i < runLen; ++i) machine.runFrame();       // RE-JOUE depuis A
        std::vector<uint8_t> stR; machine.saveState(stR);
        const uint64_t hR = screenHash();
        const bool stateEq = (stD == stR), screenEq = (hD == hR);
        if (!stateEq) {
            // Démarre APRÈS l'en-tête (17 o) : le CRC du payload (offset 13) diverge
            // dès que le payload diverge et masquerait l'offset du champ fautif.
            size_t off = 17; const size_t m = std::min(stD.size(), stR.size());
            while (off < m && stD[off] == stR[off]) ++off;
            std::fprintf(stderr, "[save-state-det] STATE DIVERGENCE @ offset %zu / %zu "
                         "(dir[%zu]=%02X res=%02X)\n", off, stD.size(), off,
                         off < m ? stD[off] : 0, off < m ? stR[off] : 0);
        }
        std::fprintf(stderr, "[save-state-det] screen %s | re-serialized state %s\n",
                     screenEq ? "OK (identical)" : "DIFF", stateEq ? "OK (identical)" : "DIFF");
        return (stateEq && screenEq) ? 0 : 1;
    }
    for (uint32_t a : breakAddrs) machine.cpu.setBreakpoint(a);   // débogueur : breakpoints PC
    for (uint32_t a : watchAddrs) machine.cpu.setWatchpoint(a);   // débogueur : watchpoints mémoire
    for (const std::string& s : breakSyms) {
        uint32_t a = 0;
        if (symbols.lookup(s, a)) { machine.cpu.setBreakpoint(a);
            std::fprintf(stderr, "[headless] symbol breakpoint '%s' → $%06X\n", s.c_str(), a); }
        else std::fprintf(stderr, "[headless] unknown symbol: '%s'\n", s.c_str());
    }
    if (!loadStatePath.empty()) {   // restaure un état AVANT de tourner (config machine identique requise)
        // Un échec DOIT teinter le code de sortie, comme toutes les autres E/S fichier :
        // sinon un runner d'oracle « restaurer l'état → tourner N trames → diffier »
        // repart d'un boot à froid et rend un vert silencieux sur la mauvaise scène.
        if (!machine.loadStateFile(loadStatePath)) {
            std::fprintf(stderr, "[headless] FAILED to restore state %s\n", loadStatePath.c_str());
            outFail = true;
        } else {
            std::fprintf(stderr, "[headless] state restored from %s\n", loadStatePath.c_str());
            // --joy est posé AVANT ce point (après reset) mais l'état restauré
            // rétablit hostJoy_/stePads sauvegardés (généralement neutres) : sans
            // cette repose, « --load-state titre.state --joy 0x80 » n'appuyait
            // jamais le feu, silencieusement.
            if (haveJoy) {
                machine.ikbd.setJoystick(joy0Hold, joy1Hold);
                machine.bus.stePads.setJoystick(joy0Hold, joy1Hold);
                std::fprintf(stderr, "[headless] joystick re-applied after restore: port1=$%02X port0=$%02X\n",
                             joy1Hold, joy0Hold);
            }
        }
    }
    for (int frame = 0; frame < frames; ++frame) {
        // Trace fenêtrée (--trace-from N) : branche le hook d'instruction à la trame N.
        if (traceFrom > 0 && frame == traceFrom && !tracePath.empty())
            machine.cpu.setTracer(&tracer);
#ifdef NEOST_WITH_NET
        if (modem) modem->poll();   // pompe le TCP entrant vers la file RX du MFP
#endif
        if (machine.ne2000.enabled()) machine.ne2000.poll();   // trames RX → anneau
#ifdef NEOST_WITH_NET
        if (midiRing) midiRing->poll([&](uint8_t b) {
            if (!machine.midi.rxCanAccept()) return false;
            machine.midi.receiveExternal(b);
            return true;
        });
#endif
        // Injections datées (--keys-at / --joy-at) : pilotage d'un menu de démo en
        // PLEINE boucle (l'intro Cuddly attend espace ; le robot du menu, le stick),
        // sans perdre --shot-every. Une touche = make à +0, break à +2, 4 trames/char.
        // Touche tenue (--key-down / --key-up) : make ou break isolé à la trame dite.
        for (const auto& [kf, kc] : keyDownList)
            if (frame == kf) { const uint8_t sc = stScancode(kc);
                               if (sc) { machine.ikbd.keyEvent(sc, true);  machine.cpu.updateIpl(); } }
        for (const auto& [kf, kc] : keyUpList)
            if (frame == kf) { const uint8_t sc = stScancode(kc);
                               if (sc) { machine.ikbd.keyEvent(sc, false); machine.cpu.updateIpl(); } }
        for (const auto& [kf, ks] : keysAtList) {
            if (frame < kf) continue;
            const int rel = frame - kf;
            const int idx = rel / 4;
            if (idx < (int)ks.size()) {
                const uint8_t sc = stScancode(ks[idx]);
                if (sc) {
                    if      (rel % 4 == 0) { machine.ikbd.keyEvent(sc, true);  machine.cpu.updateIpl(); }
                    else if (rel % 4 == 2) { machine.ikbd.keyEvent(sc, false); machine.cpu.updateIpl(); }
                }
            }
        }
        if (dumpAtFrame >= 0 && frame == dumpAtFrame && dumpLen) {
            dumpDone = true;
            std::FILE* df = std::fopen(dumpPath.c_str(), "wb");
            if (df) {
                for (uint32_t k = 0; k < dumpLen; ++k) {
                    const uint8_t b = machine.bus.read8((dumpAddr + k) & 0xFFFFFFu);
                    std::fwrite(&b, 1, 1, df);
                }
                std::fclose(df);
                std::fprintf(stderr, "[headless] RAM dump frame %d: $%06X+%u → %s\n",
                             frame, dumpAddr, dumpLen, dumpPath.c_str());
            } else {
                std::fprintf(stderr, "[headless] FAILED to open RAM dump %s\n", dumpPath.c_str());
                outFail = true;
            }
        }
        if (joyAtFrame >= 0 && frame == joyAtFrame) {
            machine.ikbd.setJoystick(0, joyAt1);
            machine.bus.stePads.setJoystick(0, joyAt1);   // joypads STE ($FF9200/02)
            std::fprintf(stderr, "[headless] joystick applied at frame %d: port1=$%02X\n", frame, joyAt1);
        }
        // Script souris daté (--mouse-at) : 1 token = 1 trame. Pilote un menu souris.
        if (mouseAtFrame >= 0 && frame >= mouseAtFrame) {
            const int idx = frame - mouseAtFrame;
            if (idx < (int)mouseAt.size()) {
                static bool mClickL = false, mClickR = false;
                const char t = mouseAt[idx];
                int dx = 0, dy = 0; bool l = false, r = false;
                switch (t) {
                    case 'L': dx = -8; break;
                    case 'R': dx =  8; break;
                    case 'U': dy = -8; break;
                    case 'D': dy =  8; break;
                    case '1': l = true; mClickL = true; break;   // clic gauche : appui
                    case '2': r = true; mClickR = true; break;   // clic droit : appui
                    case '3': l = r = true; mClickL = mClickR = true; break;  // les deux (ex. nitro Super Hang-On)
                    default: break;                              // '.' = idle
                }
                // Maintien d'un clic : si la trame précédente était un appui et celle-ci
                // ne l'est pas, on relâche (paquet bouton=0) pour finir le clic.
                if (t != '1' && t != '3' && mClickL) { l = false; mClickL = false; }
                if (t != '2' && t != '3' && mClickR) { r = false; mClickR = false; }
                machine.ikbd.mouseEvent(dx, dy, l, r);
                machine.cpu.updateIpl();
            }
        }
        // Script joystick daté (--joy-script) : 1 token = 1 trame. Pulse feu / déplace
        // une sélection dans un menu joystick (ex. menu Vroom atteint au feu).
        if (joyScrFrame >= 0 && frame >= joyScrFrame) {
            const int idx = frame - joyScrFrame;
            if (idx < (int)joyScr.size()) {
                uint8_t st = 0;
                switch (joyScr[idx]) {
                    case 'U': st = 0x01; break;
                    case 'D': st = 0x02; break;
                    case 'L': st = 0x04; break;
                    case 'R': st = 0x08; break;
                    case 'F': st = 0x80; break;
                    default:  st = 0x00; break;    // '.' = neutre
                }
                machine.ikbd.setJoystick(0, st);
                machine.bus.stePads.setJoystick(0, st);   // joypads STE ($FF9200/02)
                machine.cpu.updateIpl();
            }
        }
        machine.runFrame();
        if (machine.cpu.breakpointHit()) {
            const uint32_t bpa = machine.cpu.breakpointHitAddr();
            const bool     isW = machine.cpu.breakpointHitIsWatch();
            const uint32_t pc  = machine.cpu.pc();
            char dis[256]; machine.cpu.disassemble(dis, pc);   // toujours l'instruction au PC
            uint32_t off = 0;
            const std::string sym = symbols.nameFor(bpa, &off);
            char label[128] = "";
            if (!sym.empty()) std::snprintf(label, sizeof label, " <%s+%u>", sym.c_str(), off);
            if (isW)   // break-after : bpa = adresse DONNÉE accédée, PC = instruction suivante
                std::fprintf(stderr, "[headless] WATCH access $%06X%s (frame %d) \xe2\x80\x94 PC=$%06X: %s\n",
                             bpa, label, frame, pc, dis);
            else
                std::fprintf(stderr, "[headless] BREAK $%06X%s (frame %d): %s\n", bpa, label, frame, dis);
            std::fprintf(stderr, "  PC=%06X SR=%04X\n", pc, machine.cpu.sr());
            for (int r = 0; r < 8; ++r) std::fprintf(stderr, "  D%d=%08X A%d=%08X\n",
                                                     r, machine.cpu.reg(r), r, machine.cpu.reg(8 + r));
            break;
        }
        if (soundDump) dumpFrame();
        if (shotEvery > 0 && frame >= shotFrom && (frame % shotEvery) == 0) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s%05d.ppm", shotPrefix.c_str(), frame);
            if (!writePpm(path, machine.shifter.pixels(),
                          machine.shifter.width(), machine.shifter.height())) {
                std::fprintf(stderr, "[headless] FAILED periodic screenshot %s\n", path);
                outFail = true;
            }
        }
        if (haveUntil && machine.cpu.pc() == untilPc) {
            std::fprintf(stderr, "[headless] PC=$%06X reached at frame %d\n", untilPc, frame);
            break;
        }
    }

    if (!saveStatePath.empty()) {   // sauvegarde l'état à la fin de la boucle
        const bool ok = machine.saveStateFile(saveStatePath);
        std::fprintf(stderr, ok ? "[headless] state saved \xe2\x86\x92 %s\n"
                                : "[headless] FAILED to save state %s\n", saveStatePath.c_str());
        if (!ok) outFail = true;
    }

    // Écriture du WAV (--sound-dump) : PCM 16 bits stéréo 48 kHz, en-tête RIFF canonique.
    if (soundDump && !dumpPcm.empty()) {
        std::FILE* wf = std::fopen(soundDumpPath.c_str(), "wb");
        if (wf) {
            const uint32_t dataLen = uint32_t(dumpPcm.size() * 2);
            auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, wf); };
            auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, wf); };
            std::fwrite("RIFF", 4, 1, wf); w32(36 + dataLen); std::fwrite("WAVE", 4, 1, wf);
            std::fwrite("fmt ", 4, 1, wf); w32(16); w16(1); w16(2);
            w32(kDumpRate); w32(kDumpRate * 4); w16(4); w16(16);
            std::fwrite("data", 4, 1, wf); w32(dataLen);
            std::fwrite(dumpPcm.data(), 2, dumpPcm.size(), wf);
            std::fclose(wf);
            std::fprintf(stderr, "[headless] audio dump → %s (%.1f s at %u Hz)\n",
                         soundDumpPath.c_str(), double(dumpPcm.size() / 2) / kDumpRate, kDumpRate);
        } else {
            std::fprintf(stderr, "[headless] FAILED to open audio dump %s\n", soundDumpPath.c_str());
            outFail = true;
        }
    }

    // Diagnostic souris : après boot, on déplace le pointeur en diagonale et on
    // clique au milieu du parcours, pour voir si le curseur GEM apparaît/bouge.
    if (walkMouse) {
        auto idle   = [&](int frames) { for (int i = 0; i < frames; ++i) machine.runFrame(); };
        auto packet = [&](int dx, int dy, bool l) {
            machine.ikbd.mouseEvent(dx, dy, l, false);
            machine.cpu.updateIpl();
            machine.runFrame();
        };
        // CLIC-GLISSÉ : prendre l'icône Disque A (haut-gauche) et la traîner au centre.
        for (int i = 0; i < 58; ++i) packet(-5, -3, false);  // 1) aller sur Disque A
        idle(5);
        packet(0, 0, true);                                  // 2) appui (bouton bas)
        idle(3);
        for (int i = 0; i < 45; ++i) packet(4, 4, true);     // 3) glisser bouton TENU
        idle(3);
        packet(0, 0, false);                                 // 4) relâcher
        idle(40);
        std::fprintf(stderr, "[headless] sequence: click-drag from Disk A to the centre\n");
    }

    // Injection de touches (pilotage des menus de diagnostic). Table de scancodes
    // ST (jeu « PC/AT » du clavier ST) pour A-Z, 0-9 et Entrée ; on envoie make
    // puis break, avec quelques trames de battement, puis on laisse tourner.
    if (!keys.empty()) {
        auto idle = [&](int n) { for (int i = 0; i < n; ++i) machine.runFrame(); };
        for (char c : keys) {
            const uint8_t sc = stScancode(c);
            if (!sc) continue;
            machine.ikbd.keyEvent(sc, true);  machine.cpu.updateIpl(); idle(2);
            machine.ikbd.keyEvent(sc, false); machine.cpu.updateIpl(); idle(2);
        }
        // « Branche » le connecteur de bouclage RS232 APRÈS la navigation clavier :
        // s'il était branché plus tôt, l'écho du rapport série imprimé en console au
        // boot reviendrait en réception et serait lu comme entrée terminal → le test
        // clavier échouerait. Le technicien le branche juste avant de lancer le test S.
        if (loopback) machine.mfp.setLoopback(true);
        idle(frames);   // laisse les tests déclenchés s'exécuter
        std::fprintf(stderr, "[headless] keys injected: \"%s\"\n", keys.c_str());
    }

    std::fprintf(stderr, "[headless] %llu instructions traced\n",
                 (unsigned long long)tracer.instructionCount());
    // Métrique précision cycle : pire retard d'IRQ timer MFP + préemptions du
    // timeslice CPU (cf. Scheduler). Retard faible = quantum « sous la ligne ».
    std::fprintf(stderr, "[headless] timer IRQ max lateness = %lld cyc | preemptions = %ld\n",
                 (long long)machine.sched.timerMaxLate, machine.sched.preemptions);
    std::fprintf(stderr, "[headless] video: %dx%d @ %d Hz\n",
                 machine.shifter.width(), machine.shifter.height(), machine.shifter.refreshHz());

    if (!shotPath.empty()) {
        if (writePpm(shotPath.c_str(), machine.shifter.pixels(),
                     machine.shifter.width(), machine.shifter.height()))
            std::fprintf(stderr, "[headless] screenshot → %s (%dx%d)\n",
                         shotPath.c_str(), machine.shifter.width(), machine.shifter.height());
        else {
            std::fprintf(stderr, "[headless] FAILED screenshot %s\n", shotPath.c_str());
            outFail = true;
        }
    }

    // --disasm ADDR,LEN : désassemble LEN octets à partir de ADDR (hexa) via Moira.
    if (const char* da = std::getenv("NEOST_DISASM")) {
        uint32_t addr = 0, len = 0;
        std::sscanf(da, "%x,%x", &addr, &len);
        char buf[256];
        uint32_t pc = addr;
        while (pc < addr + len) {
            int n = machine.cpu.disassemble(buf, pc);
            std::fprintf(stderr, "%06X: %s\n", pc, buf);
            pc += n > 0 ? (uint32_t)n : 2u;
        }
    }

    if (!serialOut.empty())
        std::fprintf(stderr, "[headless] RS-232 serial port (%zu bytes):\n%s\n",
                     serialOut.size(), serialOut.c_str());
    // --serial-dump FILE : écrit les octets série bruts dans FILE (capture propre pour
    // les runners de verdict, ex. tools/run_selftests.py qui y cherche NEOST-TEST: … PASS).
    if (!serialDumpPath.empty()) {
        if (FILE* sf = std::fopen(serialDumpPath.c_str(), "wb")) {
            std::fwrite(serialOut.data(), 1, serialOut.size(), sf);
            std::fclose(sf);
        } else {
            std::fprintf(stderr, "[headless] cannot write the serial dump %s\n",
                         serialDumpPath.c_str());
            outFail = true;
        }
    }

    // --dump-at demandé mais jamais atteint (N >= --frames, ou boucle sortie plus tôt
    // sur --break/--until-pc, ou LEN=0) : sans ce contrôle, aucun fichier n'était écrit,
    // rien n'était dit, et le runner diffiait un dump PÉRIMÉ en croyant l'avoir refait.
    if (dumpAtFrame >= 0 && !dumpDone) {
        std::fprintf(stderr, "[headless] --dump-at frame %d never reached (LEN=%u) — "
                     "no dump written\n", dumpAtFrame, dumpLen);
        outFail = true;
    }

    tracer.close();
    return outFail ? 1 : 0;   // une sortie fichier a échoué → visible du runner
}
