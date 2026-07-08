/*************************************************************************************
 * cube3d.c — cube vectoriel 3D en rotation (fil de fer), étalon GODLIB/NeoST.
 *
 * Low-res 320x200 4 plans, double buffer GODLIB (Screen_Init/Screen_Update),
 * rotation 3 axes en virgule fixe 2.14 (table de sinus 256 entrées embarquée),
 * projection perspective, tracé Bresenham maison directement dans le plan 0 du
 * buffer logique (Graphic_4BP_DrawLine est exporté mais jamais implémenté dans
 * GRF_4_S.S). ESPACE pour quitter.
 *************************************************************************************/

#include <GODLIB/GEMDOS/GEMDOS.H>
#include <GODLIB/GRAPHIC/GRAPHIC.H>
#include <GODLIB/IKBD/IKBD.H>
#include <GODLIB/PLATFORM/PLATFORM.H>
#include <GODLIB/SCREEN/SCREEN.H>
#include <GODLIB/SYSTEM/SYSTEM.H>
#include <GODLIB/VBL/VBL.H>
#include <GODLIB/VIDEO/VIDEO.H>

/* ------------------------------------------------------------------ constantes */

#define dCUBE_HALF   40      /* demi-arete du cube                              */
#define dCUBE_ZOFF   256     /* recul camera (>> demi-diagonale ~70)            */
#define dCUBE_FOCAL  256     /* focale de projection                            */

/* sinus 2.14 : 16384 = 1.0, periode 256 */
static const S16 gSin[256] =
{
         0,    402,    804,   1205,   1606,   2006,   2404,   2801,
      3196,   3590,   3981,   4370,   4756,   5139,   5520,   5897,
      6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765,
      9102,   9434,   9760,  10080,  10394,  10702,  11003,  11297,
     11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395,
     13623,  13842,  14053,  14256,  14449,  14635,  14811,  14978,
     15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986,
     16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379,
     16384,  16379,  16364,  16340,  16305,  16261,  16207,  16143,
     16069,  15986,  15893,  15791,  15679,  15557,  15426,  15286,
     15137,  14978,  14811,  14635,  14449,  14256,  14053,  13842,
     13623,  13395,  13160,  12916,  12665,  12406,  12140,  11866,
     11585,  11297,  11003,  10702,  10394,  10080,   9760,   9434,
      9102,   8765,   8423,   8076,   7723,   7366,   7005,   6639,
      6270,   5897,   5520,   5139,   4756,   4370,   3981,   3590,
      3196,   2801,   2404,   2006,   1606,   1205,    804,    402,
         0,   -402,   -804,  -1205,  -1606,  -2006,  -2404,  -2801,
     -3196,  -3590,  -3981,  -4370,  -4756,  -5139,  -5520,  -5897,
     -6270,  -6639,  -7005,  -7366,  -7723,  -8076,  -8423,  -8765,
     -9102,  -9434,  -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
    -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986,
    -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143,
    -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842,
    -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080,  -9760,  -9434,
     -9102,  -8765,  -8423,  -8076,  -7723,  -7366,  -7005,  -6639,
     -6270,  -5897,  -5520,  -5139,  -4756,  -4370,  -3981,  -3590,
     -3196,  -2801,  -2404,  -2006,  -1606,  -1205,   -804,   -402
};

#define mSIN( a )  gSin[ (U8)(a) ]
#define mCOS( a )  gSin[ (U8)((a) + 64) ]
#define mMUL( s, v )  ((S16)(((S32)(s) * (S32)(v)) >> 14))

/* 8 sommets, 12 aretes */
static const S16 gVerts[8][3] =
{
    { -dCUBE_HALF, -dCUBE_HALF, -dCUBE_HALF },
    {  dCUBE_HALF, -dCUBE_HALF, -dCUBE_HALF },
    {  dCUBE_HALF,  dCUBE_HALF, -dCUBE_HALF },
    { -dCUBE_HALF,  dCUBE_HALF, -dCUBE_HALF },
    { -dCUBE_HALF, -dCUBE_HALF,  dCUBE_HALF },
    {  dCUBE_HALF, -dCUBE_HALF,  dCUBE_HALF },
    {  dCUBE_HALF,  dCUBE_HALF,  dCUBE_HALF },
    { -dCUBE_HALF,  dCUBE_HALF,  dCUBE_HALF },
};

static const U8 gEdges[12][2] =
{
    {0,1},{1,2},{2,3},{3,0},      /* face avant  */
    {4,5},{5,6},{6,7},{7,4},      /* face arriere */
    {0,4},{1,5},{2,6},{3,7},      /* liaisons     */
};

static U16 gMyPalette[16] =
{
    0x000, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF,
    0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF
};

/* ------------------------------------------------------- tracé plan 0 (couleur 1) */

/* pixel (aX,aY) dans le plan 0 du buffer logique — pas de clipping : la
 * projection garde le cube largement dans 320x200 */
static void plot(U16 *apVRAM, S16 aX, S16 aY)
{
    apVRAM[ aY * 80 + ((aX >> 4) << 2) ] |= (U16)(0x8000u >> (aX & 15));
}

static void line(U16 *apVRAM, S16 aX0, S16 aY0, S16 aX1, S16 aY1)
{
    S16 lDx = (S16)(aX1 - aX0);
    S16 lDy = (S16)(aY1 - aY0);
    S16 lSx = 1, lSy = 1, lErr;

    if (lDx < 0) { lDx = (S16)-lDx; lSx = -1; }
    if (lDy < 0) { lDy = (S16)-lDy; lSy = -1; }
    lErr = (S16)(lDx - lDy);

    for (;;)
    {
        plot(apVRAM, aX0, aY0);
        if (aX0 == aX1 && aY0 == aY1) break;
        {
            S16 l2 = (S16)(lErr << 1);
            if (l2 > -lDy) { lErr = (S16)(lErr - lDy); aX0 = (S16)(aX0 + lSx); }
            if (l2 <  lDx) { lErr = (S16)(lErr + lDx); aY0 = (S16)(aY0 + lSy); }
        }
    }
}

/* --------------------------------------------------------------------- rendu 3D */

static void cube_render(U8 aA, U8 aB, U8 aC)
{
    S16  lPx[8], lPy[8];
    S16  lSinA = mSIN(aA), lCosA = mCOS(aA);
    S16  lSinB = mSIN(aB), lCosB = mCOS(aB);
    S16  lSinC = mSIN(aC), lCosC = mCOS(aC);
    U16 *lpVRAM = (U16 *)gScreenLogicGraphic.mpVRAM;
    U16  i;

    for (i = 0; i < 8; i++)
    {
        S16 lX = gVerts[i][0], lY = gVerts[i][1], lZ = gVerts[i][2];
        S16 lT;

        /* rotation Z puis X puis Y */
        lT = (S16)(mMUL(lCosA, lX) - mMUL(lSinA, lY));
        lY = (S16)(mMUL(lSinA, lX) + mMUL(lCosA, lY));
        lX = lT;

        lT = (S16)(mMUL(lCosB, lY) - mMUL(lSinB, lZ));
        lZ = (S16)(mMUL(lSinB, lY) + mMUL(lCosB, lZ));
        lY = lT;

        lT = (S16)(mMUL(lCosC, lX) + mMUL(lSinC, lZ));
        lZ = (S16)(mMUL(lCosC, lZ) - mMUL(lSinC, lX));
        lX = lT;

        /* projection perspective */
        lPx[i] = (S16)(160 + ((S32)lX * dCUBE_FOCAL) / (lZ + dCUBE_ZOFF));
        lPy[i] = (S16)(100 + ((S32)lY * dCUBE_FOCAL) / (lZ + dCUBE_ZOFF));
    }

    for (i = 0; i < 12; i++)
    {
        line(lpVRAM, lPx[gEdges[i][0]], lPy[gEdges[i][0]],
                     lPx[gEdges[i][1]], lPy[gEdges[i][1]]);
    }
}

/* ------------------------------------------------------------------------- main */

int main(void)
{
    U8 lA = 0, lB = 0, lC = 0;

    GemDos_Super(0);
    Platform_Init();

    Screen_Init(320, 200, eGRAPHIC_COLOURMODE_4PLANE, eSCREEN_SCROLL_NONE);
    Video_SetPalST(&gMyPalette[0]);

    while (!IKBD_GetKeyStatus(eIKBDSCAN_SPACE))
    {
        Screen_Logic_ClearScreen();
        cube_render(lA, lB, lC);
        lA = (U8)(lA + 2);
        lB = (U8)(lB + 3);
        lC = (U8)(lC + 1);
        Screen_Update();
        IKBD_Update();
    }

    Screen_DeInit();
    Platform_DeInit();

    return 0;
}
