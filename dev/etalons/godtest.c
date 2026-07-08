/*************************************************************************************
 * godtest.c — GODLIB self-test / proof that the Reservoir Gods library builds into a
 * FUNCTIONAL 68000 binary through the NeoST toolchain (dev/etalons/build.sh).
 *
 * Console only (VT52). Exercises pure-logic GODLIB modules with KNOWN inputs and
 * checks the results, so correctness is verifiable at a glance (PASS / FAIL):
 *   - STRING : String_StrLen / StrCpy / StrCat / StrCmpi
 *   - RANDOM : Random_Get (deterministic LCG: s = s*69069 + 41)
 *
 * This deliberately avoids the video/screen/blitter stack (machine-specific, still
 * WIP for the ported samples) and focuses on proving the C<->asm ABI and the library
 * logic are sound end to end.
 *************************************************************************************/

#include <tos.h>
#include <GODLIB/STRING/STRING.H>
#include <GODLIB/RANDOM/RANDOM.H>

static U16 gPass = 0;
static U16 gFail = 0;

static void report(const char *name, U16 ok)
{
    Cconws(ok ? " PASS  " : " FAIL  ");
    Cconws(name);
    Cconws("\r\n");
    if (ok) gPass++; else gFail++;
}

int main(void)
{
    char buf[64];

    Cconws("\033E\033f");
    Cconws("======================================\r\n");
    Cconws("  GODLIB self-test (Reservoir Gods)\r\n");
    Cconws("  built by the NeoST 68000 toolchain\r\n");
    Cconws("======================================\r\n\r\n");

    /* -- STRING -------------------------------------------------------------- */
    report("String_StrLen(\"NeoST\") == 5",
           String_StrLen("NeoST") == 5);

    String_StrCpy(buf, "Neo");
    report("String_StrCpy -> \"Neo\"",
           String_StrLen(buf) == 3 && buf[0] == 'N' && buf[2] == 'o');

    String_StrCat(buf, "Neo", "ST");
    report("String_StrCat(\"Neo\",\"ST\") -> \"NeoST\"",
           String_StrCmpi(buf, "NeoST") == 0);

    report("String_StrCmpi is case-insensitive",
           String_StrCmpi("NeoST", "neost") == 0 &&
           String_StrCmpi("NeoST", "atari") != 0);

    /* -- RANDOM (deterministic LCG) ----------------------------------------- */
    {
        U32 a = Random_Get();
        U32 b = Random_Get();
        U32 expect = a * 69069L + 41L;   /* the exact recurrence GODLIB uses */
        report("Random_Get follows s*69069+41",
               b == expect);
    }

    /* -- verdict ------------------------------------------------------------- */
    Cconws("\r\n--------------------------------------\r\n");
    if (gFail == 0)
        Cconws(" ALL TESTS PASSED - GODLIB is alive!\r\n");
    else
        Cconws(" SOME TESTS FAILED\r\n");
    Cconws("======================================\r\n");
    Cconws(" Press any key...");

    Cconin();
    Cconws("\033e");
    return 0;
}
