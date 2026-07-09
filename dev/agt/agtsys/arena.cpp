//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// 	world arena: creates world instance from multiple playfield buffers each with separate state
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"
#include "system.h"
#include "shifter.h"
#include "tileset.h"
#include "worldmap.h"
#include "playfield.h"
#include "arena.h"

extern "C" 
{
	u16 * volatile g_rst_pfb;
	s32 * volatile g_rst_pflineidx;
	volatile s32 g_rst_vpagebytes;
	volatile s16 g_rst_vpageh;
	volatile restorestate_t * volatile g_rst_state;
};

// ---------------------------------------------------------------------------------------------------------------------
