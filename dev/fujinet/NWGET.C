/* ==========================================================================
 *  NWGET.C — telecharge une URL via le FujiNet virtuel de NeoST.
 *
 *      NWGET.TOS [URL [FICHIER_DEST]]
 *
 *  Sans argument : demo autonome — recupere HTTP://example.test/hello.txt
 *  (present dans les fixtures de rejeu tests/fixtures/fuji), affiche le
 *  contenu et l'ecrit dans NWGET.OUT (verifiable par empreinte cote hote).
 *
 *  L'interface et les messages sont en ANGLAIS (règle du projet).
 *
 *  (c) 2026 VERHILLE Arnaud — projet NeoST.
 * ========================================================================== */
#include <tos.h>
#include <string.h>
#include <stdio.h>

#include "fujinet.h"

static void puts_st(const char *s) { Cconws(s); }

int main(int argc, char **argv)
{
    static unsigned char chunk[2048];
    const char *url  = "HTTP://example.test/hello.txt";
    const char *dest = "NWGET.OUT";
    char spec[300];
    struct fn_status st;
    long total = 0;
    short fh, wifi, quiet_polls;
    char line[80];

    if (argc >= 2) url  = argv[1];
    if (argc >= 3) dest = argv[2];

    puts_st("NWGET - FujiNet HTTP fetch for NeoST\r\n");

    if (fn_init(6) != FN_ERR_OK) {
        puts_st("No FujiNet device on ACSI target 6.\r\n"
                "Run NeoST with --fujinet (see docs/FUJINET.md).\r\n");
        Cconin();
        return 1;
    }
    wifi = fn_wifi_status();
    sprintf(line, "WiFi status: %d %s\r\n", wifi,
            wifi == 3 ? "(connected)" : "(offline)");
    puts_st(line);

    strcpy(spec, "N1:");
    strncat(spec, url, sizeof(spec) - 4);
    sprintf(line, "GET %s\r\n", spec);
    puts_st(line);

    if (network_open(spec, OPEN_MODE_READ, 0) != FN_ERR_OK) {
        puts_st("Open failed (offline? bad URL?).\r\n");
        Cconin();
        return 1;
    }

    fh = (short)Fcreate(dest, 0);
    if (fh < 0) {
        puts_st("Cannot create the output file.\r\n");
        network_close(spec);
        Cconin();
        return 1;
    }

    /* Boucle de transfert : Status -> Read(avail) -> Fwrite, jusqu'a la fin
     * du flux (avail 0 et connexion retombee). quiet_polls borne l'attente
     * du corps HTTP (rempli en arriere-plan cote NeoST). */
    quiet_polls = 0;
    for (;;) {
        unsigned short n;
        if (network_status(spec, &st) != FN_ERR_OK) {
            puts_st("Status failed.\r\n");
            break;
        }
        if (st.avail == 0) {
            if (!st.connected || ++quiet_polls > 500)
                break;                        /* fin de flux (ou ~10 s muets) */
            continue;
        }
        quiet_polls = 0;
        n = st.avail > sizeof(chunk) ? (unsigned short)sizeof(chunk) : st.avail;
        if (network_read(spec, chunk, n) < 0) {
            puts_st("Read failed.\r\n");
            break;
        }
        Fwrite(fh, (long)n, chunk);
        total += n;
        sprintf(line, "\r%ld byte(s)", total);
        puts_st(line);
    }
    Fclose(fh);
    network_close(spec);

    sprintf(line, "\r\nSaved %ld byte(s) to %s\r\n", total, dest);
    puts_st(line);
    if (argc < 2) {
        puts_st("(demo mode - press a key)\r\n");
        Cconin();
    }
    return total > 0 ? 0 : 1;
}
