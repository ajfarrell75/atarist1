//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	SPRITE drawing interfaces:
//	- Full clipping
//	- Optional auto-clearing mechanism
//	- All methods include FISR/NFSR read optimizations
//	- Compatible with sprite files from AGTCut tool
//----------------------------------------------------------------------------------------------------------------------
//	Realtime-shifted sprites:
//	- IMSPR/EMSPR/EMXSPR methods for BLiTTER
//	- ISSPR method (SVMC/SFMC) for STF
// ---------------------------------------------------------------------------------------------------------------------
//	Preshifted sprites:
//	- EMSPR,EMXSPR methods for BLiTTER (preshifted mask)
//	- IPSPR method for STF (todo)
// ---------------------------------------------------------------------------------------------------------------------
//	Generated sprites:
//	- EMXSPR method for BLiTTER
//	- IGSPR method for STF (todo)
// ---------------------------------------------------------------------------------------------------------------------

#ifndef spritelib_h_
#define spritelib_h_

// =====================================================================================================================
// 	access to sprite library
// =====================================================================================================================

#include "drawcontext.h"

// ---------------------------------------------------------------------------------------------------------------------
// clipped, masked, 4-bitplane sprite functions (for direct calls)

extern "C" int AGT_STF_ISSprDrawClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);

extern "C" int AGT_BLiT_IMSprDrawClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);
extern "C" int AGT_BLiT_EMSprDrawClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);
extern "C" int AGT_BLiT_EMXSprDrawClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);

// ---------------------------------------------------------------------------------------------------------------------
// ubclipped, masked, 4-bitplane sprite functions (for direct calls)

extern "C" int AGT_STF_ISSprDrawUnClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);

extern "C" int AGT_BLiT_IMSprDrawUnClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);
extern "C" int AGT_BLiT_EMSprDrawUnClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);
extern "C" int AGT_BLiT_EMXSprDrawUnClipped(drawcontext_t* _pctx, spritesheet::sps_header *_psps, int _frame, int _x, int _y);

extern "C" int AGT_BLiT_SlabDraw(slabsheet::sls_header *_psls, u16 *_pfb, int _frame, int _x, int _y);
extern "C" int AGT_BLiT_SlabSetupOnce();

extern "C" int AGT_BLiT_EMSprDrawString(char* string, spritesheet::sps_header *_psps, u16 *_pfb, s32 *_plinetable, int _x, int _y);

// ---------------------------------------------------------------------------------------------------------------------
// signal to playfield engine that a rectangle has been drawn/destroyed and needs repainted with BG
// note: in [PageRestore] mode, this is not order sensitive. repainting is achieved from an offscreen copy
// however in [SaveRestore] mode, the repainting order takes place in reverse order, and is order sensitive
// since the current BG state is saved at the moment the signal is sent.

extern "C" int AGT_MarkDirtyRectangle(u16 *_pfb, int _x, int _y, int _xs, int _ys);

extern "C" int AGT_SlabDirtyRegion(slabsheet::sls_header *_psls, u16 *_pfb, int _frame, int _x, int _y);

// ---------------------------------------------------------------------------------------------------------------------
// clipped, software-scrolled, fixed-shift, masking, 4-bitplane sprite function
// notes: 
//	- precise clipping of T,R,B edges, providing sprite does not cross L+R or T+B edges together
//  - L edge uses safety clipping only - the FISR (force initial source read) case has 
//	  been removed for speed and to save RAM 
//	- preferred path for most wide or tall sprites
extern "C" int AGT_STF_SprClipOpt4BPL(spritesheet::sps_header *_psps, u16 *_pfb, int _frame, int _x, int _y);

// ---------------------------------------------------------------------------------------------------------------------
// unclipped, software-scrolled, fixed-shift, masking, 4-bitplane sprite function
// notes: 
//  - no clipping at all - be careful! use as fast path only.
//	- special path for any sprites which don't approach window edges (e.g. bullets, player fx)
extern "C" int AGT_STF_SprNoClipOpt4BPL(spritesheet::sps_header *_psps, u16 *_pfb, int _frame, int _x, int _y);

// ---------------------------------------------------------------------------------------------------------------------
// init sprite clearing stacks - called one time only before using sprite system

extern "C" int AGT_PlayfieldRestoreInit();

// ---------------------------------------------------------------------------------------------------------------------
// reset sprite clearing stacks - called before first frame to be drawn in each new scene/level

extern "C" int AGT_PlayfieldRestoreReset();

// ---------------------------------------------------------------------------------------------------------------------
// restore sprite footprints using Blitter/CPU (STE)

extern "C" int AGT_BLiT_PlayfieldRestore();
extern "C" int AGT_CPU_PlayfieldRestore();

// ---------------------------------------------------------------------------------------------------------------------
// restore sprite footprints using CPU only (STF)

extern "C" int AGT_STF_PlayfieldRestore();

// ---------------------------------------------------------------------------------------------------------------------
// initialise Blitter for [I]nterleaved [M]asked [S]prites - moderate performance, low memory overhead
// notes:
// - called once before a series of manually drawn IMSPR sprites
// - automatically handled from EntityDraw system - only needed when drawing objects directly via manual calls

extern "C" int AGT_BLiT_IMSprInit();

// ---------------------------------------------------------------------------------------------------------------------
// initialise Blitter for [E]nd [M]asked (+E[X]tended)[S]prites - high performance, moderate or high memory overhead
// notes:
// - called once before a series of manually drawn EMSPR/EMXSPR sprites
// - automatically handled from EntityDraw system - only needed when drawing objects directly via manual calls

extern "C" int AGT_BLiT_EMSprInit();
extern "C" int AGT_BLiT_EMXSprInit();

// ---------------------------------------------------------------------------------------------------------------------
// sprite clipping window (world coordinates)

extern "C" s16 g_clipwin_gpp_x1;
extern "C" s16 g_clipwin_gpp_y1p;
extern "C" s16 g_clipwin_gpp_x2;
extern "C" s16 g_clipwin_gpp_y2p;
extern "C" s16 g_clipwin_gpp_x1p;
extern "C" s16 g_clipwin_gpp_x2p;

// ---------------------------------------------------------------------------------------------------------------------
// some internal stuff: todo: clean this up

//extern "C" u16 *g_frb;
//extern "C" s16 *g_pflinemod;
//extern "C" s32 *g_pflineidx;
//extern "C" s32 g_pfvpagebytes;
//extern "C" s16 g_pfvpageh;
//extern "C" s16 g_pfviewy;

// ---------------------------------------------------------------------------------------------------------------------

#endif // spritelib_h_
