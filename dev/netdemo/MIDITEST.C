/* ==========================================================================
 *  MIDITEST.C — banc de l'anneau MIDI reseau NeoST.
 *
 *  Envoie 10 octets sur MIDI OUT (ACIA $FFFC04/06) puis lit ce qui revient sur
 *  MIDI IN, et journalise les octets recus sur le port serie RS-232 (capture
 *  hote). Avec --midi-net pointant sur un echo UDP, chaque octet emis revient :
 *  c'est le trajet OUT -> reseau -> IN d'un anneau MIDI (MIDI Maze).
 *
 *  Acces MATERIEL direct (Supexec) : pas de vecteur MIDI GEMDOS.
 *
 *  (c) 2026 VERHILLE Arnaud — projet NeoST.
 * ========================================================================== */
#include <tos.h>

#define ACIA_CTRL (*(volatile unsigned char *)0xFFFFFC04UL)
#define ACIA_DATA (*(volatile unsigned char *)0xFFFFFC06UL)
#define UART_DATA (*(volatile unsigned char *)0xFFFFFA2FUL)   /* MFP USART (serie) */

static void midi_super(void)
{
    short i;
    long  spins;
    short got = 0;

    ACIA_CTRL = 0x03;              /* master reset */
    ACIA_CTRL = 0x95;              /* /16, 8N1, RIE (recepteur + IRQ) */

    /* Phase 1 : emet les 10 octets sur MIDI OUT (partent sur l'anneau UDP). */
    for (i = 0; i < 10; i++) {
        while (!(ACIA_CTRL & 0x02))   /* TDRE : emetteur pret */
            ;
        ACIA_DATA = (unsigned char)('A' + i);
    }

    /* Phase 2 : draine MIDI IN. En headless, l'echo UDP a besoin de temps mur ;
     * le poll de l'anneau tourne ENTRE les trames, donc on tourne longtemps
     * (des millions de tours = des dizaines de trames) en journalisant chaque
     * octet revenu sur la serie. On s'arrete apres les 10 attendus ou au bout. */
    for (spins = 0; spins < 20000000L && got < 10; spins++) {
        if (ACIA_CTRL & 0x01) {
            UART_DATA = ACIA_DATA;    /* octet recu de l'anneau -> serie (capture) */
            got++;
        }
    }
}

int main(void)
{
    Cconws("MIDITEST - MIDI ring bench\r\n");
    Supexec((long (*)())midi_super);
    Cconws("Done.\r\n");
    return 0;
}
