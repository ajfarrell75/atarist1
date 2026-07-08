/*************************************************************************************
 * hello.c — NeoST "showcase" page, launched from a GEMDOS hard disk.
 *
 * Built by the 68000 toolchain (dev/etalons/build.sh, vc/vbcc); dropped into
 * gemdos/etalon/HELLO.TOS. Doubles as a "it works" fixture (running a .TOS from a
 * GEMDOS folder on the desktop) AND as a presentation of the project.
 *
 * Plain VT52 BIOS console (no GEM). 40-column frame, exactly 25 rows so it never
 * scrolls -> readable in low-res colour as well as high-res mono. ASCII only.
 *************************************************************************************/

#include <tos.h>

/* Write one line followed by a carriage return. */
static void line(const char *s)
{
    Cconws(s);
    Cconws("\r\n");
}

int main(void)
{
    Cconws("\033E");                 /* VT52: clear screen + cursor home */
    Cconws("\033f");                 /* hide cursor */

    line("======================================");
    line("        \033p  N e o S T  \033q");     /* title in reverse video */
    line("======================================");
    line("");
    line(" An Atari ST emulator: a hands-on,");
    line(" \"hack box\" learning machine. This");
    line(" very text runs INSIDE the emulator");
    line(" itself, from a GEMDOS hard disk.");
    line("");
    line(" -- What it wants to be ------------");
    line(" A machine you can OPEN and grasp.");
    line(" Not a black box: every chip, cycle");
    line(" and interrupt laid bare, to learn.");
    line("");
    line(" -- Writing an ST emulator ---------");
    line(" Replay a 68000 cycle by cycle, the");
    line(" Shifter pixel by pixel, the MFP,");
    line(" blitter, YM-2149 + DMA sound... and");
    line(" check AGAINST real silicon (Hatari,");
    line(" MAME) until every frame matches.");
    line(" Accuracy measured, never guessed.");
    line("");
    line(" C++17 . Moira 68000 . GLFW + OpenGL");
    line("======================================");
    Cconws(" Press any key...");

    Cconin();                        /* wait for a key */
    Cconws("\033e");                 /* show cursor again */
    return 0;
}
