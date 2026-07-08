/*************************************************************************************
 * gfxtest.c — étalon de la pile graphique GODLIB (couches mémoire, sans mode écran).
 *
 * Exerce chaque étage de la pile avec des entrées CONNUES et vérifie les octets
 * produits, PASS/FAIL lisible en console VT52 :
 *   - SYSTEM  : détection VDO / BLT / TOS (affichées pour diagnostic)
 *   - MEMORY  : Memory_Clear (asm MEMORY_S.S) sur motif $AA
 *   - GRAPHIC : GraphicCanvas_Init (offsets de lignes 4 plans)
 *   - GRF_4   : DrawBox CPU aligné 16 px, non aligné (masques bords), ClearScreen
 *   - GRF_B4  : mêmes tests via blitter si présent (Graphic_SetBlitterEnable)
 *
 * Ne touche NI la résolution NI le VBL : tout se joue dans un buffer alloué,
 * l'écran reste sur la console → sûr sur ST comme STE, sortie lisible.
 *************************************************************************************/

#include <GODLIB/GEMDOS/GEMDOS.H>
#include <GODLIB/GRAPHIC/GRAPHIC.H>
#include <GODLIB/MEMORY/MEMORY.H>
#include <GODLIB/SYSTEM/SYSTEM.H>

static U16 gPass = 0;
static U16 gFail = 0;

static void report(const char *name, U16 ok)
{
    GemDos_Cconws(ok ? " PASS  " : " FAIL  ");
    GemDos_Cconws(name);
    GemDos_Cconws("\r\n");
    if (ok) gPass++; else gFail++;
}

static void print_hex16(U16 v)
{
    static const char digits[] = "0123456789ABCDEF";
    char buf[5];
    buf[0] = digits[(v >> 12) & 15];
    buf[1] = digits[(v >> 8) & 15];
    buf[2] = digits[(v >> 4) & 15];
    buf[3] = digits[v & 15];
    buf[4] = 0;
    GemDos_Cconws(buf);
}

/* mot du plan aPlane au groupe 16 px aGroup de la ligne aY (écran 320 px, 4 plans) */
static U16 peek_plane(const U16 *apVRAM, U16 aY, U16 aGroup, U16 aPlane)
{
    return apVRAM[aY * 80u + aGroup * 4u + aPlane];
}

static U16 box_all_zero(const U16 *apVRAM)
{
    U32 i;
    for (i = 0; i < 16000UL; i++)
    {
        if (apVRAM[i]) return 0;
    }
    return 1;
}

/* Vérifie une boîte pleine couleur 1 : plan 0 porte les masques, plans 1-3 vides. */
static U16 check_box(const U16 *apVRAM, U16 aX, U16 aY, U16 aW, U16 aH)
{
    U16 lY, lG, lP;
    U16 lX1 = (U16)(aX + aW); /* exclusif */

    for (lY = 0; lY < 200; lY++)
    {
        for (lG = 0; lG < 20; lG++)
        {
            U16 lExpect = 0;
            U16 lBit;
            for (lBit = 0; lBit < 16; lBit++)
            {
                U16 lPx = (U16)(lG * 16 + lBit);
                if (lY >= aY && lY < (U16)(aY + aH) && lPx >= aX && lPx < lX1)
                    lExpect |= (U16)(0x8000u >> lBit);
            }
            if (peek_plane(apVRAM, lY, lG, 0) != lExpect)
            {
                GemDos_Cconws("   plan0 y=");
                print_hex16(lY);
                GemDos_Cconws(" g=");
                print_hex16(lG);
                GemDos_Cconws(" lu=");
                print_hex16(peek_plane(apVRAM, lY, lG, 0));
                GemDos_Cconws(" attendu=");
                print_hex16(lExpect);
                GemDos_Cconws("\r\n");
                return 0;
            }
            for (lP = 1; lP < 4; lP++)
            {
                if (peek_plane(apVRAM, lY, lG, lP) != 0)
                {
                    GemDos_Cconws("   plan");
                    print_hex16(lP);
                    GemDos_Cconws(" non vide y=");
                    print_hex16(lY);
                    GemDos_Cconws("\r\n");
                    return 0;
                }
            }
        }
    }
    return 1;
}

static sGraphicCanvas gCanvas;

static void run_drawbox_suite(const char *aTag, U16 *apVRAM)
{
    sGraphicRect lRect;

    /* boîte alignée 16 px : x=16 w=32 -> plan 0 = $FFFF sur groupes 1-2 */
    Memory_Clear(32000UL, apVRAM);
    lRect.mX = 16; lRect.mY = 2; lRect.mWidth = 32; lRect.mHeight = 3;
    GraphicCanvas_DrawBox((&gCanvas), (&lRect), 1);
    report(aTag[0] == 'C' ? "DrawBox CPU aligne 16px" : "DrawBox BLT aligne 16px",
           check_box(apVRAM, 16, 2, 32, 3));

    /* boîte du sample BOX : x=10 w=200 (bords partiels des deux cotes) */
    Memory_Clear(32000UL, apVRAM);
    lRect.mX = 10; lRect.mY = 80; lRect.mWidth = 200; lRect.mHeight = 60;
    GraphicCanvas_DrawBox((&gCanvas), (&lRect), 1);
    report(aTag[0] == 'C' ? "DrawBox CPU x=10 w=200 (BOX)" : "DrawBox BLT x=10 w=200 (BOX)",
           check_box(apVRAM, 10, 80, 200, 60));

    /* boîte etroite dans un seul groupe : x=3 w=5 */
    Memory_Clear(32000UL, apVRAM);
    lRect.mX = 3; lRect.mY = 0; lRect.mWidth = 5; lRect.mHeight = 1;
    GraphicCanvas_DrawBox((&gCanvas), (&lRect), 1);
    report(aTag[0] == 'C' ? "DrawBox CPU x=3 w=5 (1 groupe)" : "DrawBox BLT x=3 w=5 (1 groupe)",
           check_box(apVRAM, 3, 0, 5, 1));

    /* ClearScreen efface tout */
    GraphicCanvas_ClearScreen((&gCanvas));
    report(aTag[0] == 'C' ? "ClearScreen CPU" : "ClearScreen BLT",
           box_all_zero(apVRAM));

}

int main(void)
{
    U16 *lpVRAM;
    U8   lMem[32];
    U16  i, lOk;

    GemDos_Cconws("\033E\033f");
    GemDos_Cconws("======================================\r\n");
    GemDos_Cconws("  GODLIB gfx stack test (in-memory)\r\n");
    GemDos_Cconws("======================================\r\n\r\n");

    GemDos_Super(0);
    System_Init();
    Graphic_Init();

    GemDos_Cconws(" VDO=");
    print_hex16((U16)System_GetVDO());
    GemDos_Cconws(" BLT=");
    print_hex16((U16)System_GetBLT());
    GemDos_Cconws(" TOS=");
    print_hex16(System_GetTosVersion());
    GemDos_Cconws("\r\n\r\n");

    /* -- MEMORY : Memory_Clear (asm) ----------------------------------------- */
    for (i = 0; i < 32; i++) lMem[i] = 0xAA;
    Memory_Clear(24UL, lMem);
    lOk = 1;
    for (i = 0; i < 24; i++) if (lMem[i]) lOk = 0;
    for (i = 24; i < 32; i++) if (lMem[i] != 0xAA) lOk = 0;
    report("Memory_Clear 24 octets (asm)", lOk);

    /* -- GRAPHIC : canvas 320x200 4 plans ------------------------------------ */
    lpVRAM = (U16 *)Memory_Calloc(32000UL);
    if (!lpVRAM)
    {
        GemDos_Cconws(" FAIL  Memory_Calloc 32000\r\n");
        GemDos_Cconin();
        return 1;
    }
    GraphicCanvas_Init(&gCanvas, eGRAPHIC_COLOURMODE_4PLANE, 320, 200);
    GraphicCanvas_SetpVRAM((&gCanvas), lpVRAM);
    report("LineOffsets[1] == 160", gCanvas.mLineOffsets[1] == 160UL);
    report("LineOffsets[2] == 320", gCanvas.mLineOffsets[2] == 320UL);
    report("mpFuncs installes", gCanvas.mpFuncs != 0 && gCanvas.mpFuncs->DrawBox != 0);

    /* -- GRF_4 : rendu CPU ---------------------------------------------------- */
    Graphic_SetBlitterEnable(0);
    run_drawbox_suite("CPU", lpVRAM);

    /* -- GRF_B4 : rendu blitter si la machine en a un ------------------------- */
    if (System_GetBLT() == BLT_BLITTER)
    {
        Graphic_SetBlitterEnable(1);
        if (Graphic_GetBlitterEnable())
        {
            run_drawbox_suite("BLT", lpVRAM);
        }
        else
        {
            GemDos_Cconws(" SKIP  blitter refuse par Graphic_SetBlitterEnable\r\n");
        }
        Graphic_SetBlitterEnable(0);
    }
    else
    {
        GemDos_Cconws(" SKIP  pas de blitter (tests BLT ignores)\r\n");
    }

    /* -- verdict ------------------------------------------------------------- */
    GemDos_Cconws("\r\n--------------------------------------\r\n");
    if (gFail == 0)
        GemDos_Cconws(" ALL TESTS PASSED - gfx stack OK!\r\n");
    else
        GemDos_Cconws(" SOME TESTS FAILED\r\n");
    GemDos_Cconws("======================================\r\n");
    GemDos_Cconws(" Press any key...");

    GemDos_Cconin();
    GemDos_Cconws("\033e");
    return 0;
}
