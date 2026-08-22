/* ==========================================================================
 *  MODMTEST.C — banc du modem Hayes NeoST via le BIOS TOS (AUX = RS-232).
 *
 *  Dialogue : ATI (identification), puis ATDT 127.0.0.1:8792 (pont TCP vers
 *  un serveur local du banc), lit la banniere, raccroche (+++ ATH).
 *  Tout ce qui est recu est journalise dans MODM.OUT (verifie cote hote).
 *
 *  (c) 2026 VERHILLE Arnaud — projet NeoST.
 * ========================================================================== */
#include <tos.h>
#include <string.h>

static short fh;
static long  total = 0;

static void aux_puts(const char *s)
{
    while (*s)
        Bconout(1, *s++);
}

/* Draine AUX vers MODM.OUT jusqu'a `idle_max` scrutations muettes. */
static void drain(long idle_max)
{
    long idle = 0;
    char c;
    while (idle < idle_max) {
        if (Bconstat(1)) {
            c = (char)Bconin(1);
            Fwrite(fh, 1L, &c);
            total++;
            idle = 0;
        } else {
            idle++;
        }
    }
}

int main(void)
{
    Cconws("MODMTEST - Hayes modem bench\r\n");
    fh = (short)Fcreate("MODM.OUT", 0);
    if (fh < 0)
        return 1;

    aux_puts("ATI\r");
    drain(60000L);

    aux_puts("ATDT127.0.0.1:8792\r");
    drain(400000L);                 /* connexion + banniere du serveur */

    aux_puts("+++");
    drain(60000L);
    aux_puts("ATH\r");
    drain(60000L);

    Fclose(fh);
    Cconws("Done.\r\n");
    return total > 0 ? 0 : 1;
}
