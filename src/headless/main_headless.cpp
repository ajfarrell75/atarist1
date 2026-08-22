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
#include <thread>

#include "core/Machine.hpp"
#include "util/HostPath.hpp"   // chemins hôte : UNE définition d'« absolu »
#include "gui/AppConfig.hpp"   // neost.cfg : UNE définition du rognage de ligne
#include "net/FujiHost.hpp"
#include "net/FujiHostReplay.hpp"
#include "net/MiniJson.hpp"
#include "net/NetBackend.hpp"
#include "net/SlirpBackend.hpp"
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
        "  --mono            monochrome monitor (high resolution)\n"
        "  --fpu             populate the Mega STE MC68881 socket ($FFFA40, functional\n"
        "                    emulation — absent by default: Hatari-faithful \"not found\")\n"
        "  --mem SIZE        ST-RAM: 256k, 512k (default), 1m, 2m, 4m\n"
        "  --walk-mouse      after boot, inject a mouse move + click (diagnostic)\n"
        "  --keys STR        after boot, type STR on the keyboard (e.g. diag menus)\n"
        "  --key-down N C    press key C at frame N and HOLD it (make code only)\n"
        "  --key-up N C      release key C at frame N (break code only)\n"
        "  --keys-at N STR   type STR from frame N on (repeatable): 4 frames per char,\n"
        "                    extended scancodes arrows <>[], Esc =, F1-F5 !@#$%%\n"
        "  --joy-at N VAL    set the port 1 joystick to VAL at frame N (same bits as --joy)\n"
        "  --joy-script N S  joystick script from frame N: U/D/L/R/F/. = one frame each\n"
        "  --mouse-at N S    mouse script from frame N: L/R/U/D = +/-8 px, 1/2 = left/\n"
        "                    right click, . = idle (one frame each)\n"
        "  --joy P1[,P0]     hold a joystick state (bits up$01 down$02 l$04 r$08 fire$80)\n"
        "  --disk FILE       mount an image in drive A (default disks/diskA.st)\n"
        "  --diskb FILE      mount an image in drive B (second drive)\n"
        "  --fastfdc         fast FDC (delays /10) — speeds up disk access\n"
        "  --loopback        \"plug in\" the RS232 and MIDI OUT->IN loopback connectors\n"
        "                    (diagnostic serial S / MIDI M tests; MIDI is unplugged by default)\n"
        "  --cart FILE       mount a cartridge ($FA0000): diagnostic Test Kit, etc.\n"
        "  --gemdos DIR      GEMDOS hard disk: map DIR onto C: (GEMDOS calls redirected\n"
        "                    to the host, Hatari-style; exclusive with --cart)\n"
        "  --printer FILE    Centronics printer: capture the printed bytes into FILE\n"
        "  --acsi IMG        ACSI hard disk image (target 0): TOS reads the partition\n"
        "                    table and mounts C:/D:… (alias --hd; port of hdc.c)\n"
        "  --fujinet         attach the virtual FujiNet on the ACSI bus (target 6;\n"
        "                    NeoST extension, see docs/EXTENSIONS.md)\n"
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
        "  --netusbee        NetUSBee on the cartridge port: NE2000 (as EtherNEC) +\n"
        "                    ISP1160 USB host controller (empty root hub; exclusive with --cart)\n"
        "  --slirp           REAL Internet for the NE2000 (--ethernec/--netusbee): user-mode\n"
        "                    NAT via libslirp. The ST gets 10.0.2.15/24, gateway 10.0.2.2,\n"
        "                    DNS 10.0.2.3 (DHCP served) — needs a TCP/IP stack on the ST side\n"
        "                    (STinG + ENEC.STX). --slirp-restricted = sandbox, no outbound\n"
        "  --ultrasatan      UltraSatan SD interface on the ACSI bus: 2 slots on targets\n"
        "                    N and N+1 (default 0-1), JOOKIE INQUIRY, RTC, 'US' commands\n"
        "  --ultrasatan-id N first ACSI ID of the UltraSatan (0-6, default 0)\n"
        "  --sd1 IMG, --sd2 IMG  raw image in UltraSatan slot 1 / slot 2 (--acsi IMG\n"
        "                    also lands in slot 1 when the UltraSatan sits on ID 0)\n"
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
        "  --usatan-selftest self-test of the UltraSatan (ACSI wire protocol: INQUIRY,\n"
        "                    ICD 'US' packets, RTC, empty slot) then exit\n"
        "  --netusbee-selftest self-test of the NetUSBee ISP1160 (cartridge-port wire\n"
        "                    protocol, chip ID, registers, ATL) + NE2000 coexistence, then exit\n"
        "  --slirp-selftest  self-test of the real-Internet backend (libslirp): ARP + DHCP\n"
        "                    through the emulated NE2000, no outbound traffic needed, then exit\n"
        "  --serial-dump F   write the raw RS-232 serial bytes into F (NEOST-TEST verdicts)\n"
        "  --from-cfg F      replay the GUI config (neost.cfg); later options override it\n"
        "  --dump-at N A L F raw dump of L bytes of RAM from $A (hex) after frame N → F\n"
        "  --screenshot PPM  dump the final framebuffer in PPM format\n"
        "  --shot-every N P  dump a PPM every N frames, named P00000.ppm, P00001.ppm...\n"
        "  --shot-from N     only start the --shot-every dumps at frame N\n"
        "  --version         print the build version and exit\n"
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
int fujiSelfTest(Machine& machine) {
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
    // CDB $60 COMPLET (10 octets) vers la cible 0, avec un octet device INVALIDE :
    // s'il était routé à tort vers FujiNet, execute() poserait NO_DEVICE — le test
    // est falsifiable. L'ancienne version n'envoyait que 2 octets : execute() ne
    // tournait jamais (déclenché à byteCount == 10), lastError restait OK dans
    // TOUS les builds, gating cassé compris — l'assertion ne validait rien.
    w16(0xFF8606, 0x0088);
    w16(0xFF8604, uint16_t((0u << 5) | 0x1F));              // cible 0, marqueur ICD
    w16(0xFF8606, 0x008A);
    for (uint8_t b : {uint8_t(0x60), uint8_t(0x00), uint8_t(0xEE), uint8_t('Z'),
                      uint8_t(0x00), uint8_t(0x00), uint8_t(0x00), uint8_t(0x00),
                      uint8_t(0x00), uint8_t(0x00)})
        w16(0xFF8604, b);
    // …cible vide (pas d'IRQ) ou peuplée : dans les deux cas, JAMAIS routé FujiNet.
    check(machine.fuji.lastError() == fn_err::OK, "vendor opcode gated to Fuji target");

    // --- 7. Cas limites du parseur JSON utilisé par N: --------------------------
    bool invalidRejected = true;
    for (const char* bad : {".", "+", "01", "1.", "1e", "true garbage", "{\"a\":1}tail",
                            "\"bad\\q\"", "\"bad\\uZZZZ\"", "\"raw\nline\""})
        invalidRejected = invalidRejected && !minijson::looksLikeJson(bad);
    std::string jsonValue;
    check(invalidRejected && minijson::looksLikeJson(" -12.5e+2 \n")
          && minijson::query("{\"a\":1.25e-2}", "/a", jsonValue)
          && jsonValue == "1.25e-2", "JSON strict numbers + full EOF");
#ifdef NEOST_WITH_NET
    neonet::Url parsed;
    const bool queryUrl = neonet::parseUrl("http://example.test?x=1", parsed)
                       && parsed.host == "example.test" && parsed.path == "/?x=1";
    const bool ipv6Url = neonet::parseUrl("http://[::1]:8080/x", parsed)
                      && parsed.host == "::1" && parsed.port == 8080 && parsed.path == "/x";
    check(queryUrl && ipv6Url && !neonet::parseUrl("http://host:abc/x", parsed)
          && !neonet::parseUrl("http://::1/x", parsed),
          "URL query/IPv6/port validation");
#endif

    // --- 8. Cas limites SCSI ACSI (sans passer par le backend FujiNet) ----------
    const fs::path acsiPath = dir / "acsi-count-probe.img";
    {
        std::ofstream f(acsiPath, std::ios::binary | std::ios::trunc);
        const char zero[512] = {};
        for (int i = 0; i < 300; ++i) f.write(zero, sizeof zero);
    }
    Acsi acsi;
    const bool mounted = acsi.mount(0, acsiPath.string());
    auto sendAcsi = [&](std::initializer_list<uint8_t> cdb) {
        acsi.selectTarget(0);
        for (uint8_t b : cdb) acsi.feedByte(b);
    };
    sendAcsi({0x08, 0, 0, 0, 0, 0});              // READ(6), count 0 == 256
    check(mounted && acsi.status() == 0 && acsi.dataLen() == 256 * 512,
          "ACSI READ(6) count 0 means 256");
    sendAcsi({0x0A, 0, 0, 0, 0, 0});              // WRITE(6), même codage spécial
    check(acsi.status() == 0 && acsi.dataLen() == 256 * 512 && acsi.isWrite(),
          "ACSI WRITE(6) count 0 means 256");
    sendAcsi({0x1A, 0, 0x04, 0, 4, 0});            // MODE SENSE(6), allocation 4
    check(acsi.status() == 0 && acsi.dataLen() == 4,
          "ACSI MODE SENSE allocation cap");
    acsi.selectTarget(0);
    acsi.feedByte(0x08); acsi.feedByte(0); acsi.feedByte(0); // CDB partiel
    acsi.reset();
    check(acsi.byteCount() == 0 && acsi.dataLen() == 0 && !acsi.isWrite(),
          "ACSI reset cancels command/data");
    acsi.unmountAll();
    fs::remove(acsiPath, ec);

    std::fprintf(stderr, "[fuji-selftest] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// =============================================================================
//  --enec-selftest — auto-test DÉTERMINISTE de la NE2000/EtherNEC au niveau FIL.
//  On pilote la carte EXACTEMENT comme le pilote ST : écritures registre par
//  fausses lectures ($FA0000 + reg*512 + data*2), lectures par $FB0000 + reg*512,
//  le tout à travers le VRAI plan mémoire (Bus). Backend en boucle locale : une
//  trame émise revient en réception → on la relit via Remote DMA. Aucune E/S
//  réseau. Cf. docs/EXTENSIONS.md § EtherNEC.
// =============================================================================
int enecSelfTest(Machine& machine) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* what) {
        std::fprintf(stderr, "[enec-selftest] %-34s %s\n", what, ok ? "OK" : "FAIL");
        (ok ? passed : failed)++;
    };

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
    // ⚠ UN port, QUATRE lectures : chaque rd(0x10) fait AVANCER le Remote DMA. Il faut
    // donc une lecture par instruction. « rd(0x10) | (rd(0x10) << 8) » ne convient PAS :
    // l'ordre d'évaluation des opérandes de « | » n'est pas séquencé (C++17), si bien que
    // les deux octets de longueur pouvaient être pris à l'envers — l'auto-test lisait
    // alors 0x2400 au lieu de 36 et TOMBAIT, sur le seul choix du compilateur. Constaté :
    // vert en Release, ROUGE sur la même source compilée avec sanitizers.
    const uint8_t rsr  = rd(0x10);
    const uint8_t next = rd(0x10);
    const uint8_t lenL = rd(0x10);
    const uint8_t lenH = rd(0x10);
    const uint16_t rlen = uint16_t(lenL | (lenH << 8));
    (void)next;
    check((rsr & 0x01) && rlen == sizeof frame + 4, "RX ring packet header (status/len)");

    std::fprintf(stderr, "[enec-selftest] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// =============================================================================
//  --usatan-selftest — auto-test DÉTERMINISTE de l'UltraSatan au niveau FIL : on
//  pilote $FF8604/06 comme l'outil US_CONF (marqueur ICD, paquet $20 'US…', un
//  secteur DMA) à travers le VRAI plan mémoire (Bus → Fdc → Acsi → UltraSatan),
//  plus les commandes SCSI que le TOS envoie au boot (INQUIRY, TEST UNIT READY,
//  READ CAPACITY, READ(6)) sur un slot vide puis sur un slot avec carte.
// =============================================================================
int usatanSelfTest(Machine& machine) {
    namespace fs = std::filesystem;
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* what) {
        std::fprintf(stderr, "[usatan-selftest] %-36s %s\n", what, ok ? "OK" : "FAIL");
        (ok ? passed : failed)++;
    };

    // Carte SD synthétique pour le slot 2 : 300 secteurs, motif reconnaissable.
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "neost-usatan-selftest";
    fs::create_directories(dir, ec);
    const fs::path sdPath = dir / "sd2.img";
    {
        std::ofstream f(sdPath, std::ios::binary | std::ios::trunc);
        std::vector<char> sec(512, 0);
        for (int s = 0; s < 300; ++s) {
            for (int i = 0; i < 512; ++i) sec[std::size_t(i)] = char((s * 7 + i) & 0xFF);
            f.write(sec.data(), 512);
        }
    }

    machine.enableUltraSatan(2);                            // IDs 2 et 3 (cas non trivial)
    const bool mounted = machine.fdc.mountAcsi(sdPath.string(), 3);
    UltraSatan::DateTime fixed; fixed.year = 2026; fixed.month = 8; fixed.day = 21;
    fixed.hour = 10; fixed.min = 30; fixed.sec = 15;
    machine.usatan.setDateTime(fixed);

    Bus& bus = machine.bus;
    constexpr uint32_t kDmaBuf = 0x8000;
    auto w16 = [&](uint32_t a, uint16_t v) { bus.write16(a, v); };
    auto setDmaAddr = [&](uint32_t addr) {
        w16(0xFF8608, uint16_t((addr >> 16) & 0xFF));
        w16(0xFF860A, uint16_t((addr >> 8) & 0xFF));
        w16(0xFF860C, uint16_t(addr & 0xFF));
    };
    // Séquence EXACTE de LongRW (outil US_CONF, dma.h) : 1er octet A1 bas, octets
    // suivants A1 haut, puis — AVANT le dernier octet — bascule du bit R/W (reset
    // DMA), compteur de secteurs = 1, dernier octet, MODE=0/$100 (transfert), et
    // lecture du statut en mode $8A/$18A. `pkt` = octets du paquet, marqueur
    // ICD compris pour les commandes de 10 octets ; le statut est RENVOYÉ.
    auto longRw = [&](int id, std::vector<uint8_t> pkt, bool write) -> uint8_t {
        setDmaAddr(kDmaBuf);
        const uint16_t wr = write ? 0x0100 : 0x0000;
        w16(0xFF8606, 0x0088);
        w16(0xFF8604, uint16_t((id << 5) | pkt[0]));
        w16(0xFF8606, 0x008A);
        for (std::size_t i = 1; i + 1 < pkt.size(); ++i) { w16(0xFF8604, pkt[i]); w16(0xFF8606, 0x008A); }
        w16(0xFF8606, uint16_t(0x0090 ^ 0x0100 ^ wr));      // bascule R/W → reset DMA
        w16(0xFF8606, uint16_t(0x0090 | wr));               // sélection compteur de secteurs
        w16(0xFF8604, 1);                                   // 1 secteur
        w16(0xFF8606, uint16_t(0x008A | wr));
        w16(0xFF8604, pkt.back());                          // dernier octet → la commande part
        w16(0xFF8606, wr);                                  // démarre le DMA
        w16(0xFF8606, uint16_t(0x008A | wr));               // endcmd : statut
        return uint8_t(bus.read16(0xFF8604) & 0xFF);
    };
    // Paquet 'US' (marqueur ICD, $20 'US', code, 3 paramètres). Un secteur DMA.
    auto sendUs = [&](int id, const char* code7, uint8_t p0, uint8_t p1, uint8_t p2, bool write) -> uint8_t {
        std::vector<uint8_t> pkt = {0x1F, 0x20, 'U', 'S', ' ', ' ', ' ', ' ', p0, p1, p2};
        for (int i = 0; i < 7 && code7[i]; ++i) pkt[std::size_t(4 + i)] = uint8_t(code7[i]);
        return longRw(id, pkt, write);
    };
    // Commande SCSI classe 0 (6 octets : opcode dans le 1er octet) ou classe 1
    // (10 octets derrière le marqueur ICD), comme le ferait un pilote.
    auto sendScsi = [&](int id, std::initializer_list<uint8_t> cdb, bool write = false) -> uint8_t {
        std::vector<uint8_t> pkt(cdb);
        if (pkt.size() > 6) pkt.insert(pkt.begin(), 0x1F);
        return longRw(id, pkt, write);
    };
    auto ramStr = [&](uint32_t at, const char* s) {
        for (std::size_t i = 0; s[i]; ++i) if (bus.ram[at + i] != uint8_t(s[i])) return false;
        return true;
    };

    // --- 1. INQUIRY des deux slots : « JOOKIE  UltraSatan », n° de slot, RMB -----
    uint8_t st = sendScsi(2, {0x12, 0, 0, 0, 44, 0});
    check(st == 0 && ramStr(kDmaBuf + 8, "JOOKIE  UltraSatan")
          && bus.ram[kDmaBuf + 27] == '1' && bus.ram[kDmaBuf + 1] == 0x80
          && ramStr(kDmaBuf + 32, "1.20") && ramStr(kDmaBuf + 36, "01/28/14"),
          "INQUIRY slot 1 (JOOKIE/UltraSatan/RMB/date)");
    st = sendScsi(3, {0x12, 0, 0, 0, 44, 0});
    check(st == 0 && ramStr(kDmaBuf + 8, "JOOKIE  UltraSatan")
          && bus.ram[kDmaBuf + 27] == '2', "INQUIRY slot 2 (slot digit)");

    // --- 2. Slot vide : TEST UNIT READY / READ(6) → NOT READY, medium not present -
    const bool turEmpty = sendScsi(2, {0x00, 0, 0, 0, 0, 0}) == 2;
    st = sendScsi(2, {0x03, 0, 0, 0, 22, 0});                  // REQUEST SENSE étendu
    check(turEmpty && st == 0 && (bus.ram[kDmaBuf + 2] & 0x0F) == 2
          && bus.ram[kDmaBuf + 12] == 0x3A, "empty slot: NOT READY / medium not present");
    check(sendScsi(2, {0x08, 0, 0, 0, 1, 0}) == 2, "empty slot: READ(6) fails");

    // --- 3. Slot avec carte : TEST UNIT READY, READ CAPACITY, READ(6) -------------
    const bool turOk = sendScsi(3, {0x00, 0, 0, 0, 0, 0}) == 0;
    st = sendScsi(3, {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0});       // READ CAPACITY (classe 1, ICD)
    const uint32_t last = (uint32_t(bus.ram[kDmaBuf]) << 24) | (uint32_t(bus.ram[kDmaBuf + 1]) << 16)
                        | (uint32_t(bus.ram[kDmaBuf + 2]) << 8) | bus.ram[kDmaBuf + 3];
    check(mounted && turOk && st == 0 && last == 299, "SD slot: TEST UNIT READY + READ CAPACITY");
    st = sendScsi(3, {0x08, 0, 0, 5, 1, 0});                   // READ(6) secteur 5
    bool pat = st == 0;
    for (int i = 0; pat && i < 512; ++i) pat = bus.ram[kDmaBuf + i] == uint8_t((5 * 7 + i) & 0xFF);
    check(pat, "SD slot: READ(6) sector contents");

    // --- 4. 'USCurntFW' : nom du firmware, un secteur --------------------------
    st = sendUs(2, "CurntFW", 0, 0, 0, false);
    check(st == 0 && ramStr(kDmaBuf, "UltraSatan v1.20"), "USCurntFW (firmware name)");

    // --- 5. 'USRdClRTC' : 'RTC' + {année-2000, mois, jour, h, min, s} ----------
    st = sendUs(3, "RdClRTC", 0, 0, 0, false);                 // répond sur les DEUX slots
    check(st == 0 && ramStr(kDmaBuf, "RTC") && bus.ram[kDmaBuf + 3] == 26
          && bus.ram[kDmaBuf + 4] == 8 && bus.ram[kDmaBuf + 5] == 21 && bus.ram[kDmaBuf + 6] == 10
          && bus.ram[kDmaBuf + 7] == 30 && bus.ram[kDmaBuf + 8] == 15, "USRdClRTC (deterministic clock)");

    // --- 6. 'USWrClRTC' : magie 'RTC' dans le paquet ET le secteur ---------------
    for (int i = 0; i < 512; ++i) bus.ram[kDmaBuf + i] = 0;
    bus.ram[kDmaBuf] = 'R'; bus.ram[kDmaBuf + 1] = 'T'; bus.ram[kDmaBuf + 2] = 'C';
    bus.ram[kDmaBuf + 3] = 30; bus.ram[kDmaBuf + 4] = 12; bus.ram[kDmaBuf + 5] = 31;
    bus.ram[kDmaBuf + 6] = 23; bus.ram[kDmaBuf + 7] = 59; bus.ram[kDmaBuf + 8] = 58;
    const bool wrOk = sendUs(2, "WrCl", 'R', 'T', 'C', true) == 0;
    st = sendUs(2, "RdClRTC", 0, 0, 0, false);
    check(wrOk && st == 0 && bus.ram[kDmaBuf + 3] == 30 && bus.ram[kDmaBuf + 4] == 12
          && bus.ram[kDmaBuf + 5] == 31 && bus.ram[kDmaBuf + 8] == 58, "USWrClRTC round-trip");
    check(sendUs(2, "WrCl", 0, 0, 0, true) == 2, "USWrClRTC without 'RTC' magic refused");

    // --- 7. Nom INQUIRY : 'USWrINQRN' puis INQUIRY + 'USRdINQRN' ----------------
    for (int i = 0; i < 512; ++i) bus.ram[kDmaBuf + i] = 0;
    std::memcpy(&bus.ram[kDmaBuf], "NeoST-SD  ", 10);
    const bool nmOk = sendUs(2, "WrINQRN", 0, 0, 0, true) == 0;
    st = sendScsi(3, {0x12, 0, 0, 0, 44, 0});
    const bool inqNew = st == 0 && ramStr(kDmaBuf + 16, "NeoST-SD");
    st = sendUs(3, "RdINQRN", 0, 0, 0, false);
    check(nmOk && inqNew && st == 0 && ramStr(kDmaBuf, "NeoST-SD"),
          "USWrINQRN/USRdINQRN + INQUIRY updated");
    for (int i = 0; i < 512; ++i) bus.ram[kDmaBuf + i] = 0;
    bus.ram[kDmaBuf] = 0xFF;                                   // $FF en tête = nom d'usine
    st = sendUs(2, "WrINQRN", 0, 0, 0, true);
    check(st == 0 && machine.usatan.inquiryName() == "UltraSatan", "USWrINQRN $FF restores default");

    // --- 8. Réglages : 'USWrSt' exige la magie $83 $03 $17, 'USRdSt' relit ------
    for (int i = 0; i < 512; ++i) bus.ram[kDmaBuf + i] = uint8_t(i);
    const bool noMagic = sendUs(2, "WrSt", 0, 0, 0, true) == 2;
    for (int i = 0; i < 512; ++i) bus.ram[kDmaBuf + i] = uint8_t(i);
    const bool stOk = sendUs(2, "WrSt", 0x83, 0x03, 0x17, true) == 0;
    st = sendUs(2, "RdSt", 0, 0, 0, false);
    check(noMagic && stOk && st == 0 && bus.ram[kDmaBuf + 5] == 5
          && bus.ram[kDmaBuf + 1] == 0 && bus.ram[kDmaBuf + 300] == 0, "USWrSt magic + USRdSt round-trip");

    // --- 9. Flash refusée, 'US' inconnu refusé, 'RdLog' vide ---------------------
    const bool fwRefused  = sendUs(2, "RdFW", 1, 0, 0, false) == 2;
    const bool unkRefused = sendUs(2, "Zzzz", 0, 0, 0, false) == 2;
    check(fwRefused && unkRefused && sendUs(2, "RdLog", 0, 0, 0, false) == 0,
          "RdFW/unknown refused, RdLog answers");

    // --- 10. Un paquet $20 'US' vers une cible NON UltraSatan reste rejeté ------
    machine.enableFujiNet(6);                                  // cible peuplée, non UltraSatan
    st = sendUs(6, "CurntFW", 0, 0, 0, false);
    check(st == 2 && !ramStr(kDmaBuf, "UltraSatan v1.20"), "'US' packet gated to UltraSatan targets");
    machine.disableFujiNet();

    machine.fdc.unmountAcsi();
    machine.disableUltraSatan();
    fs::remove(sdPath, ec);
    std::fprintf(stderr, "[usatan-selftest] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// =============================================================================
//  --netusbee-selftest — auto-test DÉTERMINISTE de l'ISP1160 du NetUSBee au niveau
//  FIL : séquences d'accès EXACTES du pilote FreeMiNT (isp116x.h — LSB latch,
//  MSB data/cmd, DATA_READ en mot 16 bits) à travers le VRAI plan mémoire, puis
//  coexistence avec la NE2000 (registres EtherNEC toujours décodés).
// =============================================================================
int netusbeeSelfTest(Machine& machine) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* what) {
        std::fprintf(stderr, "[netusbee-selftest] %-36s %s\n", what, ok ? "OK" : "FAIL");
        (ok ? passed : failed)++;
    };
    NetBackendLoop loop;
    machine.ne2000.setBackend(&loop);
    if (!machine.enableNetUsbee()) { std::fprintf(stderr, "[netusbee-selftest] enable failed\n"); return 1; }

    Bus& bus = machine.bus;
    // Primitives du pilote (isp116x.h), en LECTURES MOT comme raw_readw.
    auto rawReadw = [&](uint32_t a) -> uint16_t { return bus.read16(a); };
    auto writeAddr = [&](uint8_t reg) {
        (void)rawReadw(Isp1160::LSB_WRITE + (uint32_t(reg) << 1));
        (void)rawReadw(Isp1160::MSB_CMD_WRITE);
    };
    // isp116x_raw_write_data16 / raw_read_data16 — CE SONT les primitives des
    // accès registre du pilote NetUSBee (isp116x_read/write_reg16/32 n'utilisent
    // que les variantes raw) : LSB ← octet bas, MSB ← octet haut, lecture telle quelle.
    auto writeData16 = [&](uint16_t v) {
        (void)rawReadw(Isp1160::LSB_WRITE + (uint32_t(v & 0xFF) << 1));
        (void)rawReadw(Isp1160::MSB_DATA_WRITE + ((uint32_t(v) >> 8) << 1));
    };
    auto readData16 = [&]() -> uint16_t { return rawReadw(Isp1160::DATA_READ); };
    // raw_write_data32 / raw_read_data32 (registres 32 bits, sans swap).
    auto writeData32 = [&](uint32_t v) {
        (void)rawReadw(Isp1160::LSB_WRITE + ((v & 0xFF) << 1));
        (void)rawReadw(Isp1160::MSB_DATA_WRITE + (((v >> 8) & 0xFF) << 1));
        (void)rawReadw(Isp1160::LSB_WRITE + (((v >> 16) & 0xFF) << 1));
        (void)rawReadw(Isp1160::MSB_DATA_WRITE + (((v >> 24) & 0xFF) << 1));
    };
    auto readData32 = [&]() -> uint32_t {
        const uint32_t lo = rawReadw(Isp1160::DATA_READ);
        const uint32_t hi = rawReadw(Isp1160::DATA_READ);
        return lo | (hi << 16);
    };
    auto rd16 = [&](uint8_t reg) { writeAddr(reg); return readData16(); };
    auto wr16 = [&](uint8_t reg, uint16_t v) { writeAddr(uint8_t(reg | 0x80)); writeData16(v); };
    auto rd32 = [&](uint8_t reg) { writeAddr(reg); return readData32(); };
    auto wr32 = [&](uint8_t reg, uint32_t v) { writeAddr(uint8_t(reg | 0x80)); writeData32(v); };

    // --- 1. ID de puce (isp116x_check_id) ----------------------------------------
    check((rd16(Isp1160::HCCHIPID) & 0xFF00) == 0x6100, "chip ID $61xx (ISP1160)");
    // --- 2. Scratch 16 bits : round-trip avec la convention d'octets du pilote ----
    wr16(Isp1160::HCSCRATCH, 0xA55A);
    check(rd16(Isp1160::HCSCRATCH) == 0xA55A, "HcScratch 16-bit round-trip");
    wr16(Isp1160::HCSCRATCH, 0x1234);
    check(rd16(Isp1160::HCSCRATCH) == 0x1234, "HcScratch asymmetric value (byte order)");
    // --- 3. Reset logiciel : HCSWRES $F6 + HCR auto-effacé -----------------------
    wr16(Isp1160::HCSWRES, 0x00F6);
    wr32(Isp1160::HCCMDSTAT, 1);
    check((rd32(Isp1160::HCCMDSTAT) & 1) == 0 && rd16(Isp1160::HCSCRATCH) == 0x1234,
          "sw reset: HCR self-clears, scratch kept");
    // --- 4. Registres 32 bits : HcRhDescriptorA (NDP=2 figé), HcControl ----------
    wr32(Isp1160::HCRHDESCA, 0x19000000u | 0x200u | 0x1000u);
    const uint32_t desca = rd32(Isp1160::HCRHDESCA);
    check((desca & 0xFF) == 2 && (desca & 0x19001200u) == 0x19001200u, "HcRhDescriptorA 32-bit + NDP=2");
    wr32(Isp1160::HCCONTROL, 0x80);                            // USB_OPER
    check((rd32(Isp1160::HCCONTROL) & 0xC0) == 0x80, "HcControl operational");
    // --- 5. Hub racine vide : CCS=0 sur les deux ports, RHSC sur PRS ----------
    wr32(Isp1160::HCRHPORT1, 0x100);                           // SetPortPower
    const uint32_t p1 = rd32(Isp1160::HCRHPORT1);
    check((p1 & 1) == 0 && (p1 & 0x100) != 0 && (rd32(Isp1160::HCRHPORT2) & 1) == 0,
          "root hub: no device, port power");
    // --- 6. ATL : écrire un PTD, poll → ATL_DONE, CC = DeviceNotResponding -------
    wr16(Isp1160::HCATLBUFLEN, 16);
    writeAddr(uint8_t(Isp1160::HCATLPORT | 0x80));
    // PTD : w0 Active=1, w1 MPS=8, w2 total=8 (SETUP), w3 addr 0 ; puis 8 octets
    for (uint16_t w : {uint16_t(0x0800), uint16_t(0x0008), uint16_t(0x0008), uint16_t(0x0000),
                       uint16_t(0x0680), uint16_t(0x0100), uint16_t(0x0000), uint16_t(0x0008)})
        writeData16(w);
    const bool full = (rd16(Isp1160::HCBUFSTAT) & 0x04) != 0;
    machine.isp1160.poll();
    const uint16_t bs = rd16(Isp1160::HCBUFSTAT);
    writeAddr(Isp1160::HCATLPORT);
    const uint16_t w0 = rawReadw(Isp1160::DATA_READ);          // mot 0 du PTD
    check(full && (bs & 0x20) && ((w0 >> 12) & 0xF) == 5 && !(w0 & 0x800),
          "ATL: done after poll, CC=5, inactive");
    // --- 7. IRQ : OPR via HcInterrupt RHSC + HWCFG INT_ENABLE --------------------
    wr32(Isp1160::HCINTENB, 0x80000040u);                      // MIE | RHSC
    wr16(Isp1160::HCuPINTENB, 0x10);                           // OPR
    wr16(Isp1160::HCHWCFG, 0x0009);                            // DBWIDTH(1) | INT_ENABLE
    wr32(Isp1160::HCRHPORT1, 0x10);                            // SetPortReset sans device → CSC → RHSC
    check(machine.isp1160.irqAsserted() && (rd32(Isp1160::HCRHPORT1) & 0x10000),
          "IRQ line on RHSC (port reset w/o device = CSC)");
    // --- 8. NE2000 toujours décodée (registres EtherNEC, page 0) ------------------
    (void)bus.read8(Ne2000::WRITE_BASE + 0u * 512u + 0x21u * 2u);   // CR = page 0, stop
    (void)bus.read8(Ne2000::WRITE_BASE + 3u * 512u + 0x46u * 2u);   // BNRY = $46
    check(bus.read8(Ne2000::READ_BASE + 3u * 512u) == 0x46, "NE2000 BNRY round-trip in NetUSBee mode");
    // --- 9. Save-state round-trip de l'ISP1160 (registres + FIFO) ----------------
    {
        std::vector<uint8_t> blob;
        StateArchive out = StateArchive::saver(blob);
        machine.isp1160.serialize(out);
        Isp1160 copy;
        StateArchive in = StateArchive::loader(blob.data(), blob.size());
        copy.serialize(in);
        std::vector<uint8_t> blob2;
        StateArchive out2 = StateArchive::saver(blob2);
        copy.serialize(out2);
        check(!blob.empty() && in.ok() && blob == blob2, "ISP1160 serialize round-trip");
    }

    machine.disableNetUsbee();
    std::fprintf(stderr, "[netusbee-selftest] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// =============================================================================
//  --slirp-selftest — auto-test du backend Internet (libslirp) AU NIVEAU FIL.
//
//  On pilote la NE2000 exactement comme le pilote ST (écritures par fausses
//  lectures $FA0000+reg*512+data*2, lectures $FB0000+reg*512, Remote DMA), on
//  émet une trame construite à la main, et on vérifie que SLIRP répond — donc
//  que TOUT le chemin ST → carte → NAT → carte → ST fonctionne :
//
//    1. ARP « qui a 10.0.2.2 ? » → réponse ARP de la passerelle ;
//    2. DHCP DISCOVER → OFFER attribuant 10.0.2.15 (adresse, masque, routeur, DNS).
//
//  AUCUN accès à Internet n'est requis : les deux réponses viennent de SLIRP
//  lui-même (serveur ARP/DHCP internes) — l'auto-test est donc DÉTERMINISTE et
//  utilisable en CI hors ligne, contrairement à un vrai GET HTTP.
// =============================================================================
int slirpSelfTest(Machine& machine) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* what) {
        std::fprintf(stderr, "[slirp-selftest] %-40s %s\n", what, ok ? "OK" : "FAIL");
        (ok ? passed : failed)++;
    };
    if (!SlirpBackend::available()) {
        std::fprintf(stderr, "[slirp-selftest] libslirp absente de ce build — test sauté\n");
        return 0;                                    // pas un échec : dépendance optionnelle
    }
    SlirpBackend net;
    if (!net.open(false)) {
        std::fprintf(stderr, "[slirp-selftest] open: %s\n", net.lastError().c_str());
        return 1;
    }
    machine.ne2000.setBackend(&net);
    if (!machine.enableNetUsbee()) { std::fprintf(stderr, "[slirp-selftest] enable failed\n"); return 1; }

    Bus& bus = machine.bus;
    auto wr = [&](uint8_t reg, uint8_t data) {
        (void)bus.read8(Ne2000::WRITE_BASE + uint32_t(reg) * 512u + uint32_t(data) * 2u);
    };
    auto rd = [&](uint8_t reg) -> uint8_t { return bus.read8(Ne2000::READ_BASE + uint32_t(reg) * 512u); };

    const uint8_t mac[6] = {0x02, 0x4E, 0x53, 0x54, 0x00, 0x01};
    const uint8_t kTxPage = 0x40, kRxStart = 0x46, kRxStop = 0x80;
    wr(0x00, 0x21); wr(0x01, kRxStart); wr(0x02, kRxStop); wr(0x03, kRxStart);
    wr(0x00, 0x61);                                   // page 1 : MAC + CURR
    for (int i = 0; i < 6; ++i) wr(uint8_t(0x01 + i), mac[i]);
    wr(0x07, kRxStart);
    wr(0x00, 0x21);
    wr(0x0C, 0x04);                                   // RCR : accepte le broadcast

    // Émet `frame` par la carte, puis laisse SLIRP travailler quelques tours.
    auto transmit = [&](const std::vector<uint8_t>& frame) {
        const uint16_t addr = uint16_t(kTxPage) * 256u;
        wr(0x08, uint8_t(addr)); wr(0x09, uint8_t(addr >> 8));
        wr(0x0A, uint8_t(frame.size())); wr(0x0B, uint8_t(frame.size() >> 8));
        wr(0x00, 0x12);                               // Remote Write
        for (uint8_t b : frame) wr(0x10, b);
        wr(0x04, kTxPage);
        wr(0x05, uint8_t(frame.size())); wr(0x06, uint8_t(frame.size() >> 8));
        wr(0x00, 0x26);                               // TXP + STA
        for (int i = 0; i < 200; ++i) machine.ne2000.poll();
    };
    // Relit la trame déposée dans l'anneau à la page `page` (en-tête 4 octets).
    auto readRing = [&](uint8_t page, std::vector<uint8_t>& out) -> bool {
        const uint16_t hdr = uint16_t(page) * 256u;
        wr(0x08, uint8_t(hdr)); wr(0x09, uint8_t(hdr >> 8));
        wr(0x0A, 4); wr(0x0B, 0);
        wr(0x00, 0x0A);                               // Remote Read
        const uint8_t rsr = rd(0x10); const uint8_t next = rd(0x10);
        const uint8_t lo = rd(0x10);  const uint8_t hi = rd(0x10);
        (void)next;
        const int len = int(lo | (hi << 8)) - 4;      // l'en-tête compte dans la longueur
        if (!(rsr & 0x01) || len <= 0 || len > 1600) return false;
        wr(0x08, uint8_t(hdr + 4)); wr(0x09, uint8_t((hdr + 4) >> 8));
        wr(0x0A, uint8_t(len)); wr(0x0B, uint8_t(len >> 8));
        wr(0x00, 0x0A);
        out.clear();
        for (int i = 0; i < len; ++i) out.push_back(rd(0x10));
        return true;
    };

    // --- 1. ARP : « qui a 10.0.2.2 ? » ---------------------------------------
    std::vector<uint8_t> arp = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,                // dst broadcast
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],    // src
        0x08,0x06,                                    // EtherType ARP
        0x00,0x01, 0x08,0x00, 0x06,0x04, 0x00,0x01,   // Ethernet/IPv4, requête
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],    // expéditeur MAC
        10,0,2,15,                                    // expéditeur IP
        0,0,0,0,0,0,                                  // cible MAC (inconnue)
        10,0,2,2,                                     // cible IP = passerelle
    };
    arp.resize(60, 0);                                // padding Ethernet minimal
    transmit(arp);
    std::vector<uint8_t> reply;
    const bool gotArp = readRing(kRxStart, reply);
    const bool arpOk = gotArp && reply.size() >= 42
                    && reply[12] == 0x08 && reply[13] == 0x06        // ARP
                    && reply[20] == 0x00 && reply[21] == 0x02        // opcode = réponse
                    && reply[28] == 10 && reply[29] == 0 && reply[30] == 2 && reply[31] == 2;
    check(arpOk, "ARP: la passerelle 10.0.2.2 repond");

    // --- 2. DHCP DISCOVER → OFFER --------------------------------------------
    // Trame complète construite à la main : Ethernet + IPv4 + UDP + BOOTP/DHCP.
    std::vector<uint8_t> dhcp;
    auto put  = [&](std::initializer_list<uint8_t> b) { for (uint8_t x : b) dhcp.push_back(x); };
    auto put16 = [&](uint16_t v) { dhcp.push_back(uint8_t(v >> 8)); dhcp.push_back(uint8_t(v)); };
    put({0xFF,0xFF,0xFF,0xFF,0xFF,0xFF});
    put({mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]});
    put({0x08,0x00});                                  // IPv4
    const std::size_t ipOff = dhcp.size();
    put({0x45,0x00}); put16(0);                        // ver/IHL, TOS, longueur (patchée)
    put16(0); put16(0);                                // id, flags
    put({0x40,0x11}); put16(0);                        // TTL, UDP, somme (0 = tolérée)
    put({0,0,0,0});                                    // src 0.0.0.0
    put({255,255,255,255});                            // dst broadcast
    const std::size_t udpOff = dhcp.size();
    put16(68); put16(67); put16(0); put16(0);          // ports client/serveur, longueur, somme
    const std::size_t bootpOff = dhcp.size();
    put({0x01,0x01,0x06,0x00});                        // BOOTREQUEST, Ethernet, hlen 6
    put({0x12,0x34,0x56,0x78});                        // xid
    put16(0); put16(0);                                // secs, flags
    for (int i = 0; i < 16; ++i) dhcp.push_back(0);    // ciaddr/yiaddr/siaddr/giaddr
    put({mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]});
    for (int i = 0; i < 10; ++i) dhcp.push_back(0);    // reste de chaddr
    for (int i = 0; i < 192; ++i) dhcp.push_back(0);   // sname + file
    put({0x63,0x82,0x53,0x63});                        // cookie magique DHCP
    put({53,1,1});                                     // option 53 : DISCOVER
    put({55,3,1,3,6});                                 // option 55 : masque, routeur, DNS
    dhcp.push_back(0xFF);                              // fin des options
    // Longueurs : UDP puis IPv4 (le tout après le padding minimal).
    const uint16_t udpLen = uint16_t(dhcp.size() - udpOff);
    dhcp[udpOff + 4] = uint8_t(udpLen >> 8); dhcp[udpOff + 5] = uint8_t(udpLen);
    const uint16_t ipLen = uint16_t(dhcp.size() - ipOff);
    dhcp[ipOff + 2] = uint8_t(ipLen >> 8); dhcp[ipOff + 3] = uint8_t(ipLen);
    // Somme de contrôle IPv4 (obligatoire, SLIRP la vérifie).
    uint32_t sum = 0;
    for (std::size_t i = ipOff; i < ipOff + 20; i += 2)
        sum += (uint32_t(dhcp[i]) << 8) | dhcp[i + 1];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    const uint16_t ck = uint16_t(~sum);
    dhcp[ipOff + 10] = uint8_t(ck >> 8); dhcp[ipOff + 11] = uint8_t(ck);
    (void)bootpOff;
    transmit(dhcp);
    // La réponse arrive à la page suivante : l'ARP a consommé la première.
    const uint8_t nextPage = uint8_t(kRxStart + 1);
    std::vector<uint8_t> off;
    bool gotOffer = readRing(nextPage, off);
    if (!gotOffer) gotOffer = readRing(kRxStart, off);       // si l'ARP n'avait rien laissé
    // yiaddr (adresse offerte) est à l'octet 16 du BOOTP = 14 (Eth) + 20 (IP) + 8 (UDP) + 16.
    const std::size_t yi = 14 + 20 + 8 + 16;
    const bool dhcpOk = gotOffer && off.size() > yi + 4
                     && off[yi] == 10 && off[yi + 1] == 0 && off[yi + 2] == 2 && off[yi + 3] == 15;
    check(dhcpOk, "DHCP: OFFER attribue 10.0.2.15");

    // --- 3. Les compteurs voient passer le trafic ------------------------------
    check(net.txFrames() >= 2 && net.rxFrames() >= 1, "compteurs TX/RX du backend");

    // --- 4. SORTIE RÉELLE (opt-in) : requête DNS vers l'extérieur ---------------
    // Hors CI par défaut : les auto-tests du projet ne doivent JAMAIS dépendre du
    // réseau (règle de tools/run_all.py). NEOST_SLIRP_ONLINE=1 l'active pour
    // vérifier une vraie installation — c'est le seul contrôle qui prouve que les
    // paquets QUITTENT la machine, les points 1-2 étant servis par SLIRP lui-même.
    if (std::getenv("NEOST_SLIRP_ONLINE")) {
        // MAC de la passerelle, apprise de la réponse ARP (octets 22-27).
        uint8_t gw[6] = {0x52,0x55,0x0A,0x00,0x02,0x02};
        if (arpOk && reply.size() >= 28) std::memcpy(gw, reply.data() + 22, 6);
        std::vector<uint8_t> dns;
        auto d  = [&](std::initializer_list<uint8_t> b) { for (uint8_t x : b) dns.push_back(x); };
        auto d16 = [&](uint16_t v) { dns.push_back(uint8_t(v >> 8)); dns.push_back(uint8_t(v)); };
        d({gw[0],gw[1],gw[2],gw[3],gw[4],gw[5]});
        d({mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]});
        d({0x08,0x00});
        const std::size_t ip2 = dns.size();
        d({0x45,0x00}); d16(0); d16(0x1234); d16(0);
        d({0x40,0x11}); d16(0);
        // Cible du DNS : par défaut le relais interne de SLIRP (10.0.2.3), qui
        // réexpédie vers le résolveur de l'hôte. NEOST_SLIRP_DNS=a.b.c.d vise un
        // résolveur PUBLIC à la place — utile pour distinguer « le relais ne marche
        // pas » de « rien ne sort du tout ».
        uint8_t dnsIp[4] = {10, 0, 2, 3};
        if (const char* e = std::getenv("NEOST_SLIRP_DNS")) {
            unsigned a, b, c2, d2;
            if (std::sscanf(e, "%u.%u.%u.%u", &a, &b, &c2, &d2) == 4)
                { dnsIp[0] = uint8_t(a); dnsIp[1] = uint8_t(b); dnsIp[2] = uint8_t(c2); dnsIp[3] = uint8_t(d2); }
        }
        d({10,0,2,15}); d({dnsIp[0],dnsIp[1],dnsIp[2],dnsIp[3]});
        const std::size_t udp2 = dns.size();
        d16(5300); d16(53); d16(0); d16(0);
        const std::size_t q = dns.size();
        d16(0xBEEF); d16(0x0100); d16(1); d16(0); d16(0); d16(0);   // en-tête DNS
        for (const char* lbl : {"theoldnet", "com"}) {              // question, en labels
            dns.push_back(uint8_t(std::strlen(lbl)));
            for (const char* c = lbl; *c; ++c) dns.push_back(uint8_t(*c));
        }
        dns.push_back(0); d16(1); d16(1);                            // A, IN
        const uint16_t ul = uint16_t(dns.size() - udp2);
        dns[udp2 + 4] = uint8_t(ul >> 8); dns[udp2 + 5] = uint8_t(ul);
        const uint16_t il = uint16_t(dns.size() - ip2);
        dns[ip2 + 2] = uint8_t(il >> 8); dns[ip2 + 3] = uint8_t(il);
        uint32_t s2 = 0;
        for (std::size_t i = ip2; i < ip2 + 20; i += 2) s2 += (uint32_t(dns[i]) << 8) | dns[i + 1];
        while (s2 >> 16) s2 = (s2 & 0xFFFF) + (s2 >> 16);
        const uint16_t c2 = uint16_t(~s2);
        dns[ip2 + 10] = uint8_t(c2 >> 8); dns[ip2 + 11] = uint8_t(c2);
        (void)q;
        if (std::getenv("NEOST_SLIRP_TRACE")) {
            std::fprintf(stderr, "[slirp-selftest] trame DNS (%zu o) : ", dns.size());
            for (uint8_t b : dns) std::fprintf(stderr, "%02x", b);
            std::fprintf(stderr, "\n");
        }
        transmit(dns);
        // ⚠ Attendre du TEMPS RÉEL, pas des tours de boucle : un aller-retour DNS
        // prend des dizaines de MILLISECONDES, alors que 400 poll() non bloquants
        // s'exécutent en microsecondes — la première version concluait « pas de
        // réponse » avant même que le paquet n'ait quitté la machine.
        bool answered = false;
        for (int i = 0; i < 300 && !answered; ++i) {
            machine.ne2000.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));   // 3 s au plus
            // On balaie l'anneau : la réponse peut atterrir sur n'importe quelle page.
            for (uint8_t pg = kRxStart; pg < kRxStop && !answered; ++pg) {
                std::vector<uint8_t> r;
                if (!readRing(pg, r) || r.size() < 28) continue;
                // ⚠ SLIRP nous ARPe AVANT de livrer : il lui faut la MAC de 10.0.2.15.
                // Sur un vrai ST c'est la pile TCP/IP (STinG) qui répond ; ici c'est à
                // l'auto-test de le faire, sinon la réponse DNS n'est jamais remise et
                // le test conclut à tort « rien ne revient » (diagnostiqué le 2026-08-22).
                if (r[12] == 0x08 && r[13] == 0x06 && r.size() >= 42
                    && r[20] == 0x00 && r[21] == 0x01                       // requête ARP
                    && r[38] == 10 && r[39] == 0 && r[40] == 2 && r[41] == 15) {
                    std::vector<uint8_t> ar(r.begin() + 22, r.begin() + 28); // MAC du demandeur
                    std::vector<uint8_t> rep;
                    rep.insert(rep.end(), ar.begin(), ar.end());             // dst = demandeur
                    rep.insert(rep.end(), mac, mac + 6);                     // src = nous
                    for (uint8_t b : {0x08,0x06, 0x00,0x01, 0x08,0x00, 0x06,0x04, 0x00,0x02})
                        rep.push_back(b);                                    // ARP, RÉPONSE
                    rep.insert(rep.end(), mac, mac + 6);
                    for (uint8_t b : {10,0,2,15}) rep.push_back(b);          // nous
                    rep.insert(rep.end(), ar.begin(), ar.end());
                    rep.insert(rep.end(), r.begin() + 28, r.begin() + 32);   // IP du demandeur
                    rep.resize(60, 0);
                    transmit(rep);
                    continue;
                }
                if (r.size() < 14 + 20 + 8 + 12) continue;
                if (r[12] != 0x08 || r[13] != 0x00 || r[23] != 0x11) continue;  // IPv4/UDP
                const std::size_t h = 14 + 20 + 8;
                if (r[h] != 0xBE || r[h + 1] != 0xEF) continue;                 // notre xid
                answered = ((r[h + 6] << 8) | r[h + 7]) >= 1;                   // ancount
            }
        }
        check(answered, "SORTIE REELLE : DNS resout theoldnet.com");
    } else {
        std::fprintf(stderr, "[slirp-selftest] (sortie reelle non testee — NEOST_SLIRP_ONLINE=1 pour l'activer)\n");
    }

    machine.disableNetUsbee();
    net.close();
    std::fprintf(stderr, "[slirp-selftest] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// Dump du framebuffer décodé en PPM binaire (P6) — comparable visuellement.
bool writePpm(const char* path, const uint32_t* px, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    bool ok = true;
    for (int i = 0; i < w * h; ++i) {
        const uint32_t c = px[i];                 // ARGB8888
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((c >> 16) & 0xFF),
            static_cast<unsigned char>((c >> 8)  & 0xFF),
            static_cast<unsigned char>( c        & 0xFF) };
        if (std::fwrite(rgb, 1, 3, f) != 3) { ok = false; break; }
    }
    // fclose vérifié aussi : un disque plein peut n'échouer qu'au flush final —
    // une capture tronquée qui « réussit » finit diffée comme si c'était l'image.
    if (std::fclose(f) != 0) ok = false;
    return ok;
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
    bool        netusbeeFlag   = false;          // --netusbee : NE2000 + ISP1160 port cartouche
    bool        slirpFlag      = false;          // --slirp : NAT user-mode (Internet réel)
    bool        slirpRestricted = false;         // --slirp-restricted : bac à sable
    bool        ultrasatan     = false;          // --ultrasatan : interface SD sur le bus ACSI
    int         ultrasatanId   = 0;              // --ultrasatan-id N : première cible (0-6)
    std::string sd1Img, sd2Img;                  // --sd1/--sd2 IMG : images des slots SD
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
    bool        usatanSelfTestFlag = false;   // auto-test déterministe UltraSatan (fil ACSI)
    bool        netusbeeSelfTestFlag = false; // auto-test déterministe NetUSBee/ISP1160 (fil)
    bool        slirpSelfTestFlag = false;    // auto-test du backend Internet (ARP + DHCP)
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
        else if (!std::strcmp(a, "--netusbee"))        netusbeeFlag = true;
        else if (!std::strcmp(a, "--slirp"))           slirpFlag = true;
        else if (!std::strcmp(a, "--slirp-restricted")) { slirpFlag = true; slirpRestricted = true; }
        else if (!std::strcmp(a, "--ultrasatan"))      ultrasatan = true;
        else if (!std::strcmp(a, "--ultrasatan-id"))   { ultrasatan = true; ultrasatanId = std::atoi(next(a)); }
        else if (!std::strcmp(a, "--sd1"))             { ultrasatan = true; sd1Img = next(a); }
        else if (!std::strcmp(a, "--sd2"))             { ultrasatan = true; sd2Img = next(a); }
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
        else if (!std::strcmp(a, "--usatan-selftest")) usatanSelfTestFlag = true;
        else if (!std::strcmp(a, "--netusbee-selftest")) netusbeeSelfTestFlag = true;
        else if (!std::strcmp(a, "--slirp-selftest")) slirpSelfTestFlag = true;
        else if (!std::strcmp(a, "--shot-every"))  {
            shotEvery = std::atoi(next(a)); shotPrefix = next(a);
            if (shotEvery <= 0)             // le préfixe a été consommé pour rien : le dire
                std::fprintf(stderr, "[headless] --shot-every %d: no periodic capture "
                             "will be taken\n", shotEvery);
        }
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
            // P3 — pont GUI↔headless : rejoue la config exacte de neost.cfg — machine,
            // TOS, mem, cpu, mono, fastfdc, fpu, supports (disk, diskb, cart, gemdos,
            // acsi) ET réseau (fujinet*, modem, ethernec). La liste des clés relues est
            // celle de la boucle ci-dessous : la tenir à jour avec le lecteur du GUI.
            // Les options CLI placées APRÈS --from-cfg surchargent (le cfg sert de base).
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
            // ⚠ La règle « absolu » vient de util/HostPath : écrite ici à la main
            // (« s[0] == '/' »), elle traitait tout chemin Windows comme relatif et
            // reproduisait, dans --from-cfg, le défaut qui a tué le lecteur GEMDOS de
            // tous les paquets Windows (issue #37). Même remarque pour le séparateur :
            // un neost.cfg écrit sous Windows contient des « \\ ».
            const std::string cfgp = neost::hostpath::normalizeSeparators(p);
            const std::size_t slash = cfgp.find_last_of(neost::hostpath::SEP);
            const std::string cfgDir = (slash == std::string::npos) ? "" : cfgp.substr(0, slash);
            auto resolve = [&](std::string s) -> std::string {
                if (s.empty()) return s;
                s = neost::hostpath::normalizeSeparators(s);
                if (neost::hostpath::isAbsolute(s)) return s;
                while (s.rfind("./", 0) == 0) s = s.substr(2);     // ./ répétés
                if (s.rfind("../", 0) == 0)   s = s.substr(3);     // build → racine
                return neost::hostpath::join(cfgDir, s);
            };
            std::string ln;
            auto v = [](const std::string& s, std::size_t n) { return s.substr(n); };
            while (std::getline(cf, ln)) {
                // MÊME rognage que le lecteur du GUI (appconfig::trimConfigLine) et non
                // une recopie : ici ne tombait que le '\r', si bien qu'une espace de
                // queue — « machine=st » suivi d'un blanc — faisait retomber la clé sur
                // le défaut, exactement le défaut silencieux que le rognage GUI corrige.
                neost::appconfig::trimConfigLine(ln);
                if      (ln.rfind("rom=", 0) == 0)     { if (ln.size() > 4) romPath   = resolve(v(ln, 4)); }
                else if (ln.rfind("diskb=", 0) == 0)   { if (ln.size() > 6) diskBPath = resolve(v(ln, 6)); }
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
                // modem= / ethernec= : le GUI les écrit à côté des clés fujinet_*, toutes
                // relues ici — sauf ces deux-là. Rejouer une config réseau avec --from-cfg
                // démarrait donc SANS le modem ni la carte EtherNEC, sans un mot.
                else if (ln.rfind("modem=", 0) == 0)    modemFlag    = (v(ln, 6) == "1");
                else if (ln.rfind("ethernec=", 0) == 0) ethernecFlag = (v(ln, 9) == "1");
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
    if (fujiSelfTestFlag) return fujiSelfTest(machine);
    if (enecSelfTestFlag) return enecSelfTest(machine);
    if (usatanSelfTestFlag) return usatanSelfTest(machine);
    if (netusbeeSelfTestFlag) return netusbeeSelfTest(machine);
    if (slirpSelfTestFlag) return slirpSelfTest(machine);
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
    // UltraSatan (--ultrasatan…) : 2 slots SD sur les cibles N/N+1. Une image --acsi
    // déjà montée sur la cible 0 reste en place (= slot 1 quand N = 0).
    if (ultrasatan) {
        if (ultrasatanId < 0 || ultrasatanId > 6) {
            std::fprintf(stderr, "[headless] --ultrasatan-id must be 0-6\n");
            return 2;
        }
        machine.enableUltraSatan(ultrasatanId);
        if (!sd1Img.empty() && machine.fdc.mountAcsi(sd1Img, ultrasatanId))
            std::fprintf(stderr, "[headless] UltraSatan slot 1: %s\n", sd1Img.c_str());
        if (!sd2Img.empty() && machine.fdc.mountAcsi(sd2Img, ultrasatanId + 1))
            std::fprintf(stderr, "[headless] UltraSatan slot 2: %s\n", sd2Img.c_str());
        std::fprintf(stderr, "[headless] UltraSatan on ACSI targets %d-%d (%d partition(s))\n",
                     ultrasatanId, ultrasatanId + 1, machine.fdc.acsiPartitionCount());
    }
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
    SlirpBackend   slirpNet;
    if ((ethernecFlag || netusbeeFlag) && slirpFlag) {
        if (slirpNet.open(slirpRestricted)) {
            machine.ne2000.setBackend(&slirpNet);
        } else {
            std::fprintf(stderr, "[headless] --slirp: %s — repli sur la boucle locale\n",
                         slirpNet.lastError().c_str());
            slirpFlag = false;
        }
    }
    if (ethernecFlag || netusbeeFlag) {
        if (!slirpFlag) machine.ne2000.setBackend(&enecLoop);
        const bool ok = netusbeeFlag ? machine.enableNetUsbee() : machine.enableEtherNec();
        if (ok)
            std::fprintf(stderr, "[headless] %s on the cartridge port\n",
                         netusbeeFlag ? "NetUSBee (NE2000 + ISP1160 USB)" : "EtherNEC (NE2000)");
        else
            std::fprintf(stderr, "[headless] --%s refused: the cartridge port is in use\n",
                         netusbeeFlag ? "netusbee" : "ethernec");
    } else if (slirpFlag) {
        std::fprintf(stderr, "[headless] --slirp ignored: needs --ethernec or --netusbee\n");
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
    bool traceAttached = false;   // --trace-from réellement atteint ? (cf. garde de fin)
    for (int frame = 0; frame < frames; ++frame) {
        // Trace fenêtrée (--trace-from N) : branche le hook d'instruction à la trame N.
        if (traceFrom > 0 && frame == traceFrom && !tracePath.empty()) {
            machine.cpu.setTracer(&tracer);
            traceAttached = true;
        }
#ifdef NEOST_WITH_NET
        if (modem) modem->poll();   // pompe le TCP entrant vers la file RX du MFP
#endif
        if (machine.ne2000.enabled()) machine.ne2000.poll();   // trames RX → anneau
        if (machine.isp1160.enabled()) machine.isp1160.poll(); // trame USB (ATL → done)
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
    if (soundDump && dumpPcm.empty()) {
        // Zéro échantillon → AUCUN fichier écrit : le dire et échouer, sinon un WAV
        // périmé d'un run précédent serait ré-analysé comme s'il venait d'être produit.
        std::fprintf(stderr, "[headless] --sound-dump produced 0 samples — no WAV written\n");
        outFail = true;
    }
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
        if (loopback) { machine.mfp.setLoopback(true); machine.midi.setLoopback(true); }
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
    // Même garde pour --trace-from : trame jamais atteinte (>= --frames, ou sortie
    // anticipée sur --break/--until-pc) → le fichier de trace existe mais est VIDE,
    // et le diff oracle en aval croirait re-lire une trace fraîche.
    if (traceFrom > 0 && !tracePath.empty() && !traceAttached) {
        std::fprintf(stderr, "[headless] --trace-from frame %d never reached "
                     "(%d frames run) — trace is empty\n", traceFrom, frames);
        outFail = true;
    }

    tracer.close();
    return outFail ? 1 : 0;   // une sortie fichier a échoué → visible du runner
}
