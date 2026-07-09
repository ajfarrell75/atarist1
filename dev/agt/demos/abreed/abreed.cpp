//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	example: fixedpoint multi-way scroll Alien Breed mockup
//----------------------------------------------------------------------------------------------------------------------
//	controls: none
//----------------------------------------------------------------------------------------------------------------------
//	notes:
//		- aim to maintain ~40 monsters on the map at once
//		- aim to interact all monsters with walls and each other
//		- aim to maintain steady 50fps 
//		- aim to avoid bitplane trickery, using 4-bitplanes/16-colours all the way
//		- aim to reproduce some look/feel of original game
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

#include <mint/sysbind.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "agtsys/common_cpp.h"

// basic memory interface
#include "agtsys/ealloc.h"
#include "agtsys/compress.h"

// machine stuff
#include "agtsys/system.h"

// display stuff
#include "agtsys/shifter.h"

// keyboard, joysticks & mouse
#include "agtsys/input.h"

// background stuff
#include "agtsys/tileset.h"
#include "agtsys/worldmap.h"
#include "agtsys/playfield.h"
#include "agtsys/arena.h"

// sprite stuff
#include "agtsys/spritesheet.h"
#include "agtsys/slabsheet.h"
#include "agtsys/spritelib.h"

// entity/gameobject interface
#include "agtsys/entity.h"

// audio
#if defined(ENABLE_AGT_AUDIO)
#include "agtsys/sound/sound.h"
#include "agtsys/sound/music.h"
#endif

// titlescreen via pcs
//#include "agtsys/pcs.h"

// random source
#include "agtsys/rnd.h"

// ---------------------------------------------------------------------------------------------------------------------7
//	Game Entity IDs
// ---------------------------------------------------------------------------------------------------------------------7
//	Notes:
//		- add new entities to this index
//		- keep entries matched 1:1 with the entity dictionary table! (entity_dictionary[])
// ---------------------------------------------------------------------------------------------------------------------7

enum EntityType : int
{
	// special viewport entity, tracks visible world
	EntityType_VIEWPORT,

	// player
	EntityType_ALIEN,
	
	// end!
	EntityType_MAX_
};

// ---------------------------------------------------------------------------------------------------------------------
//	atan2 translation table
//	converts 4:4-bit signed dx:dy pairs into 16 sprite rotation frames (0-15)
// ---------------------------------------------------------------------------------------------------------------------

#include "tables/atan2_16frames.h"

// ---------------------------------------------------------------------------------------------------------------------
//	directionvector table
//	converts 32 angles into dx:dy vector pairs
// ---------------------------------------------------------------------------------------------------------------------

#include "tables/fdirs_32angles.h"

// ---------------------------------------------------------------------------------------------------------------------


// physical display size
#define SCREEN_XSIZE (320)
#define SCREEN_YSIZE (240)

// virtual display size including h-scroll margin
#define VIEWPORT_XSIZE (SCREEN_XSIZE)
#define VIEWPORT_YSIZE (SCREEN_YSIZE)

// ---------------------------------------------------------------------------------------------------------------------
//	configure our scrolling playfield system

// enable this for dualfield/interlaced rendering (requires 2x playfield graphics)
// otherwise use normal rendering
static const s16 c_dualfield = 1;
// this tweak optimizes dualfield display for 50hz, otherwise set 0 for a 25hz-or-less refresh
// note: at 50hz we need only one buffer for even field, one for odd. at 25hz or below we need 
// to store independent odd/even fields for each front/back buffer so the 50hz interlace can 
// be maintained.
static const s16 c_singlevbl = 1;


// define a playfield config for multiway scrolling using STE HW HSCROLL
typedef playfield_template
<
	/*tilesize=*/4,								// fixed at 2^4 for 16x16 bg tiles
	/*max_scroll=*/1920,						// maximum scroll distance in pixels (reduce to save a little ram on tables)
	/*vscroll=*/VScrollMode_LOOPBACK,			// enable full vscroll
	/*hscroll=*/HScrollMode_SCANWALK,			// enable full hscroll
	/*restore=*/RestoreMode_PAGERESTORE			// restore from virtual page (verus save/restore for individual objects)

> playfield_t;									// new name for our playfield definition

// define an arena (world) composed of 2 such playfields (double-buffering on STE)
typedef arena_template
<
	/*buffers=*/4,								// number of framebuffers (playfields) required
	/*singleframe=*/c_singlevbl ? true : false,	// optimise dualfield to half the framebuffer count, when locked @ 50hz
	/*dualfield=*/true,							// enable dualfield support at compiletime, otherwise on/off flag will be ignored
	/*playfield_type=*/playfield_t				// playfield config/definition to use for framebuffers
> arena_t;										// new name for our playfield arena definition


// ---------------------------------------------------------------------------------------------------------------------
//	machine state interface (setup/shutdown)

machine machinestate;

// ---------------------------------------------------------------------------------------------------------------------
//	MiNTlib builds need need to define a local superstack. otherwise, AGT does it during CRT startup.
// ---------------------------------------------------------------------------------------------------------------------

S_SUPER_SSP(AGT_CONFIG_STACK);

AGT_MAIN;

// ---------------------------------------------------------------------------------------------------------------------

// in 8-way scrolling mode we need to keep a 16-pixel (1-tile) safety margin between the viewport and the map edges
// otherwise the tile update speculation algorithm will cause draw glitches on scroll direction changes. it renders
// slightly ahead of the scroll and won't wrap on the left or top edges. this safety margin avoids any issues.
static const int c_scroll_safety_margin = 16;

// initial scroll & direction (16:16 fixed point)
static s32 g_fscrollx = 0;
static s32 g_fscrolly = 0;
static s32 g_fscrollxdir = 0x00010000>>1; // 1.0 units in x	
static s32 g_fscrollydir = 0x00000000>>1; // 0.5 units in y	

// xlimit at which map stops and changes scroll direction
static s32 g_map_fxlimit = 0;
static s32 g_map_fylimit = 0;

// player viewport coordinates (local, control coordinates, relative to current screen position)
static s16 g_player_viewx = 160;
static s16 g_player_viewy = 100;

// player world coordinates (derived from local coords each frame, for interactions with world & other objects)
static s16 g_player_worldx = 0;
static s16 g_player_worldy = 0;

// viewport (scroll) starting position in world map
static s16 g_viewport_worldx = c_scroll_safety_margin; 
static s16 g_viewport_worldy = 0;

// clipping margin outside of the scrolling viewport, to accommodate the largest typical sprite before deletion etc.
static const s16 c_viewport_xmargin = playfield_t::c_guardx;
static const s16 c_viewport_ymargin = playfield_t::c_guardy;

// pseudorandom number generator
mersenne_twister prng;

// ---------------------------------------------------------------------------------------------------------------------
// static pointers to significant entities (still these must be created the usual way)

// viewport entity is global and part of the entity drawing system
static entity_t *s_pe_viewport = nullptr;

static drawcontext_t drawcontext;

// ---------------------------------------------------------------------------------------------------------------------

// sprite assets

// alien monster
spritesheet alien;

// playfield tile assets (x2 in dual-field mode)
tileset mytiles0;
tileset mytiles1;

// playfield map assets
worldmap mymap0;

// ---------------------------------------------------------------------------------------------------------------------
// indices of tiles we know are solid - monsters can't pass
// ---------------------------------------------------------------------------------------------------------------------
// these can be found in several ways. the best way is to define them via a map editor, however the method
// used here was to look at 'feedback' images from agtcut [abreed_tags.tga] while feeding in a CSV text file
// listing the solid tile indices. these get highlighted in [abreed_tags.tga] the next time the tiles are cut.
// after a few rounds its possible to enumerate all the solid tiles by hand, in about 15 minutes.
// once a final list has been built, the CSV contents can be put in a table here.
//
// CAUTION: while this method is fairly easy to use and doesn't take long, it is not stable. if the input map
// image is changed, the tile order can change and the solid tile indices will end up wrong. the errors will
// show in the [abreed_tags.tga] image on the next BG cut.
//
// see other demos for example of solid tiles defined via map editor

s16 s_solidtiles[] =
{
	0x00,
	0x01,
	0x02,
	0x03,
	0x04,
	0x05,
	0x06,
	0x07,
	0x08,
	0x09,
	0x0a,
	0x0b,
	0x0c,
	0x0d,
	0x0e,
	0x0f,
	0x10,
	0x11,
	0x12,
	0x13,
	0x14,
	0x1c,
	0x1e,
	0x20,
	0x22,
	0x25,
	0x26,
	0x27,
	0x2f,
	0x34,
	0x37,
	0x58,
	0x68,
	0x69,
	0x6a,
	0x7a,
	0x9a,
	0xe8,
	0xe9,
	0xea,
	0x136,
	0x14b,
	0x14f,
	0x150,
	0x151,
	0x152,
	0x153,
	0x158,
	0x159,
	0x15b,
	0x15c,
	0x160,
	0x162,
	0x163,
	0x164,
	0x168,
	0x16e,
	0x16e,
	0x18b,
	0x18d,
	0x18e,
	0x18f,
	0x190,
	0x192,
	0x193,
	0x194,
	0x19a,
	0x19b,
	0x19c,
	0x19d,
	0x19e,
	0x1b3,
	0x1b4,
	0x1b5,
	0x1b6,
	0x1b8,
	0x1b9,
	0x1d0,
	0x1d1,
	0x1d2,

	-1 // end of table
};

static u8 s_solidindex[512];

static s32 *s_pmap = 0;
static s16 s_mapw = 0;

static s16 *s_pmaplineindex = 0;

static u8 s_psolidmap[(1920*1536)>>(4+4)];	// space for 8bit solid tile queries
static u8* s_solidmaplines[1536>>4];

// ---------------------------------------------------------------------------------------------------------------------
// plain, non-accelerated version of solid tile test for 32x32 shape

static inline u8 is_solidarea32_base(s16 _x, s16 _y)
{
	s32 *parea = &s_pmap[(s_pmaplineindex[(_y >> 4)]) + (_x >> 4)];

	u8 solid = 0;

	solid |= s_solidindex[parea[0] >> 7];
	solid |= s_solidindex[parea[1] >> 7];
	solid |= s_solidindex[parea[2] >> 7]; parea += s_mapw;
	solid |= s_solidindex[parea[0] >> 7];
	solid |= s_solidindex[parea[1] >> 7];
	solid |= s_solidindex[parea[2] >> 7]; parea += s_mapw;
	solid |= s_solidindex[parea[0] >> 7];
	solid |= s_solidindex[parea[1] >> 7];
	solid |= s_solidindex[parea[2] >> 7];

	return (solid);
}

// ---------------------------------------------------------------------------------------------------------------------
// returns (!=0) if 32x32 shape at (x,y) contacts a solid tile

static __attribute__((always_inline)) u8 is_solidarea32(s16 _x, s16 _y)
{
	u8* parea = s_solidmaplines[_y >> 4] + (_x >> 4);
	return (*parea);
}

// ---------------------------------------------------------------------------------------------------------------------
// bake an accelerated map of solid tile contact results for all (x,y) tile positions for a 32x32 shape

void prepare_solidmap(s32 *_pmap, s16 _mw, s16 _mh)
{
	// mark 1 byte per recognized solid tile
	s32 *pmap = _pmap;
	for (s16 y = 0; y < _mh; y++)
	{
		s32 *pmapline = pmap; pmap += _mw;

		s_solidmaplines[y] = &s_psolidmap[(y * s_mapw)];

		for (s16 x = 0; x < _mw; x++)
		{
			s32 tileidx = pmapline[x] >> 7;

			if (s_solidindex[tileidx])
				s_psolidmap[(y * s_mapw) + x] = 1;
			else
				s_psolidmap[(y * s_mapw) + x] = 0;
		}
	}

	// combine 3x3 tile tests into 1 by OR'ing the samples together, 
	// so the AI only need check 1 byte instead of a grid of 9, every frame.
	// note: this optimization is only good for a 32x32 sprite. for a mixture
	// of sizes, use a bitfield instead and mask the test for each size.

	u8 *psmap = s_psolidmap;
	for (s16 y = 0; y < (_mh-2); y++)
	{
		u8 *pline0 = psmap; psmap += s_mapw;
		u8 *pline1 = psmap;
		u8 *pline2 = pline1 + s_mapw;

		for (s16 x = 0; x < (_mw-2); x++)
		{
			u8 s = pline0[x+0] | pline0[x+1] | pline0[x+2] |
				   pline1[x+0] | pline1[x+1] | pline1[x+2] |
				   pline2[x+0] | pline2[x+1] | pline2[x+2];

			pline0[x] = s;
		}
	}
}

// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
// =====================================================================================================================
//	program start
// =====================================================================================================================

int AGT_EntryPoint()
{
	// ---------------------------------------------------------------------------------------------------------------------
	//	locate the end of the program to help track memory usage, when USE_MEMTRACE is enabled

	g_tracked_allocation_ += (int)(&end);
#if defined(USE_MEMTRACE)
	printf("ealloc: [%d] initial committed\n", g_tracked_allocation_);
#endif

	// ---------------------------------------------------------------------------------------------------------------------
	//	welcome message

	printf("[A]tari [G]ame [T]ools / dml 2017\n");
	printf("Alien Breed mockup (STE)\n");

	// ---------------------------------------------------------------------------------------------------------------------
	//	open scope before we create any interfaces (so we can expect them to be cleaned up before the scope is closed)
	// ---------------------------------------------------------------------------------------------------------------------
	{
		// ---------------------------------------------------------------------------------------------------------------------
		//	save machine state for a clean exit

		machinestate.configure();

		if (bFalcon030)
		{	printf("machine: Falcon0x0\n"); }
		else
		if (bMegaSTE)
		{	printf("machine: MegaSTE\n"); }
		else
		if (bSTE)
		{	printf("machine: STE\n"); }
		else
		if (bSTF)
		{
			printf("machine: STF -> not supported. exiting...\n");
			printf("press space...\n");
			Crawcin();
			return -1;
		}
		else
		{
			printf("machine: unknown, so assuming STE (?!)...\n");
			bSTE = true;
		}

		// ---------------------------------------------------------------------------------------------------------------------

		machinestate.save();

		// ---------------------------------------------------------------------------------------------------------------------
		//	shifter (video) init
	
		// interface to display services
		shifter_ste shift;

		// pulls any machine-specific metrics out of the display service & save state before any drawing systems are used
		shift.configure();
		shift.save();

		// ---------------------------------------------------------------------------------------------------------------------
		// allocate 64k of reusable scratch heap to reduce fragmentation on loading, esp. for packed assets

		void * scratch_area = ealloc(65536);
		M_InitScratch(scratch_area, 65536);

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map tiles

		printf("loading tiles...\n");
		mytiles0.load_cct("abreed0.cct");
		// only load 2nd tileset if interlacing/dual-field
		if (c_dualfield)
			mytiles1.load_cct("abreed1.cct");

#if defined(TIMING_RASTERS_MAIN)
		// save copy of BG colour for rasters
		s16 bgcol = mytiles0.getcol(0);
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map
		
		printf("loading map...\n");
		mymap0.load_ccm("abreed.ccm");

		// map array
		s_pmap = mymap0.get();
		// map x size
		s_mapw = mymap0.getwidth();

		// get map tile dimensions (x16 -> in pixels) to help keep scroll inside the map limits
		int map_xsize = mymap0.getwidth() << 4;
		int map_ysize = mymap0.getheight() << 4;

		// start in middle of map (fixedpoint)
		g_fscrollx = map_xsize<<(16-1);
		g_fscrolly = map_ysize<<(16-1);

		// max scroll is map size minus screen dimensions (plus safety margin)
		g_map_fxlimit = (map_xsize-VIEWPORT_XSIZE-c_scroll_safety_margin)<<16;
		g_map_fylimit = (map_ysize-VIEWPORT_YSIZE-0)<<16;


		// ---------------------------------------------------------------------------------------------------------------------
		//	create the double-buffered 'world', set the viewport dimensions and connect it to the shifter interface

		printf("creating world...\n");
		arena_t myworld(shift, VIEWPORT_XSIZE, VIEWPORT_YSIZE);

		// ---------------------------------------------------------------------------------------------------------------------
		//	associate map & tiles with the world buffers.
		//	Q: why do we bother to do this manually? 
		//	A: because in dual-field mode each display field can have a different (dithered) tile library
	
		printf("assigning world map...\n");
		myworld.setmap(&mymap0);

		pprintf("assigning world tiles...\n");
		for (int b = myworld.c_num_buffers_-1; b >= 0; b--)
		{
			myworld.select_buffer(b);
			pprintf("assigning buffer %d world tileset...\n", b);

			// what kind of dual-field drawing do we want?
			if (c_dualfield && c_singlevbl)
			{
				// we're going to ensure <1vbl drawing, so we can simply assign
				// different tiles (+maps) to each buffer but display using single fields
				// pros: 
				// - requires only 2 playfields, versus 4 required for <=2vbl drawing
				// - only updating (hidden) one per VBL, versus 2 for <=2vbl drawing
				// cons:
				// - effect breaks down if we trip over 1vbl because log/phys exchange
				//   rate directly affects the interlacing.

				// assign different tiles to each buffer, but same tiles to both fields
				if (b & 1)
					myworld.settiles(&mytiles1, &mytiles1);		// odd buffers
				else
					myworld.settiles(&mytiles0, &mytiles0);		// even buffers
			}
			else
			{
				// either we're not interlacing, or we can't ensure <1vbl drawing, so
				// we must assign different tiles (+maps) to each *field* for each buffer
				// and display using the dual-field mechanism on the VBL itself.
				//
				// (for <=25hz interlacing)
				// pros: 
				// - tolerates <= 2vbl refresh because two fields are available for
				//   interlacing at all times, even if the mainloop stops.
				// cons:
				// - requires storage for 4 playfields - 2 for log, 2 for phys
				// - 2 fields need written for the backbuffer/log on each draw pass 
				//   (for BG updates only - not sprites). slightly slower.

				// assign different tiles to each field, but same for both buffers
				myworld.settiles(&mytiles0, &mytiles1);
			}
		}

		// ---------------------------------------------------------------------------------------------------------------------
		//	load sprite gfx assets
		// ---------------------------------------------------------------------------------------------------------------------
		//	note: using EMSPR sprite format to keep memory load sensible with 8x4 rotation x animation frames

		// alien monster
		alien.load("monster.ems");

		// ---------------------------------------------------------------------------------------------------------------------
		// loading done, can stop using scratch area for unpacking etc.
		// ---------------------------------------------------------------------------------------------------------------------

		M_ResetScratch();		// stop using scratch
		efree(scratch_area);	// free the buffer assigned to scratch

		// ---------------------------------------------------------------------------------------------------------------------
		//	tie gfx assets into entity type dictionary, so we don't need to do it every time we spawn an object

		// note: asset binding could be done automatically via the dictionary but its useful to be able to override sizes etc.
		// and its better not to hide asset assignment steps in a system with simple memory management.

		EntityDefAsset_Sprite(EntityType_ALIEN, &alien);

		// ---------------------------------------------------------------------------------------------------------------------
		//	init world, set viewport initial position

		// convert fixedpoint to screen coords
		g_viewport_worldx = g_fscrollx>>16;
		g_viewport_worldy = g_fscrolly>>16;

		printf("init world viewport...\n");
		myworld.init(g_viewport_worldx, g_viewport_worldy);

		// provides quick access to tilemap rows
		s_pmaplineindex = myworld.buffers_[0].fields[0].pmapcontext_->mul_map_x_;

		// generate solid tile map (0 or 1) from solid tile index list
		qmemset(s_solidindex, 0, sizeof(s_solidindex));
		{
			s32 i = 0;

			s16 ti = s_solidtiles[i++];
			while (ti >= 0)
			{
				s_solidindex[ti] = 1;
				ti = s_solidtiles[i++];
			};
		}

		// create a quick testing mask for solid tiles
		prepare_solidmap(mymap0.get(), mymap0.getwidth(), mymap0.getheight());

		// ---------------------------------------------------------------------------------------------------------------------

		printf("ready... (press key)\n");
		Crawcin();

		// ---------------------------------------------------------------------------------------------------------------------
		//	now claim control over machine resources - display, timers, input etc.

		machinestate.claim();

		// ---------------------------------------------------------------------------------------------------------------------
		//	install input service for keyboard, joysticks, mouse
		//	note: joysticks/pads not yet tied - will be fixed soon

		AGT_InstallInputService();

		// ---------------------------------------------------------------------------------------------------------------------
		//	initialize display, configure single- or dual-field graphics mode, load palettes for phys & log shiftfields
		// ---------------------------------------------------------------------------------------------------------------------
		// note: there are 2 shiftfields by default, even if there are more than 2 playfield buffers. The shiftfield contains
		// all HW state associated with a single frame, including the framebuffer address, palette, scroll/linewid and other
		// relevant state. They must all be committed at exactly the same time by the displayservice.
		// The logical shifstate is always the one to be written by the game loop because it will not reach the HW registers 
		// until the framebuffers are exchanged.

		printf("init shifter...\n");
		shift.set_fieldmode(/*dualfield=*/(c_dualfield && !c_singlevbl));
		shift.init();

		// on F030 we can just open the top/bottom border through Videl
		if (bFalcon030)
		{
			reg16(ffff82a8) -= 40;
			reg16(ffff82aa) += 40;
		}

		printf("set palette...\n");	
		for (s16 c = 0; c < 16; ++c)
		{
			// logic (next)
			shift.setcolour(0, 0, c, mytiles0.getcol(c));
			shift.setcolour(0, 1, c, mytiles1.getcol(c));	// only needed in dual-field mode

			// physic (current)
			shift.setcolour(1, 0, c, mytiles0.getcol(c));
			shift.setcolour(1, 1, c, mytiles1.getcol(c));	// only needed in dual-field mode
		}

		s16 buffer_index = 0;

		// =====================================================================================================================
		//	game restart point
		// =====================================================================================================================

restart_game:


		// ---------------------------------------------------------------------------------------------------------------------
		//	initialize entity manager - clears entity chain, populates freelist etc.

		EntitySetup(EntityType_MAX_);

		// ---------------------------------------------------------------------------------------------------------------------
		//	reset the sprite clearing stacks so we don't erase bogus rectangles when we restart the game

		myworld.bgrestore_reset();

		// ---------------------------------------------------------------------------------------------------------------------
		//	reset the scrolling playfield

		// player coords within viewport
		g_player_viewx = 160;
		g_player_viewy = 100;

		// viewport coords within world - centre of map
		g_fscrollx = (map_xsize<<(16-1)) - (160<<16);
		g_fscrolly = (map_ysize<<(16-1)) - (120<<16);

		// convert fixedpoint to screen coords
		g_viewport_worldx = g_fscrollx>>16;
		g_viewport_worldy = g_fscrolly>>16;

		// must perform a complete tile-refill on all buffers if the player just died
		// todo: ugly jump - should fade/blank screen first, but it's only an example..
		myworld.reset(g_viewport_worldx, g_viewport_worldy);

		// ---------------------------------------------------------------------------------------------------------------------
		//	create some significant 'static' entities related to the player

		// create the viewport so we can track its relationship with everything else
		s_pe_viewport = EntitySpawn(EntityType_VIEWPORT, 0, 0);

		// select as active viewport
		EntitySelectViewport(s_pe_viewport);

		// ---------------------------------------------------------------------------------------------------------------------
		//	fill the map with alien monsters

		for (int e = 0; e < 45; e++)
		{
			// repeatedly generate random 2D coordinates and keep only those 
			// with a position that lands safely inside the non-solid map tiles
			// ...so so lazy!

			s32 fx, fy;
			s16 x, y;
			do
			{
				fx = prng.genU32() & 0x07FFFFFF;
				fy = prng.genU32() & 0x07FFFFFF;

				x = fx >> 16;
				y = fy >> 16;

				if ((x >= 800) && (x < (800+432)))
				{
					// rectangles for loading bay areas - we don't want all the monsters spawning mostly
					// in there just because there are fewer solid tests due to less densely populated walls
					// so we just exclude samples in these boxes. the monsters can still wander in there by
					// following corridors.

					//800,176 800+432,176+256
					//800,592, 800+432,592+256
					//800,1008, 800+432,1008+256

					if ((y >= 176) && (y < (176+256)))
					{
						// x,y outside of map causes the test to fail & retry another spawnpoint
						x = 0;
						y = 0;
					}
					if ((y >= 592) && (y < (592+256)))
					{
						// x,y outside of map causes the test to fail & retry another spawnpoint
						x = 0;
						y = 0;
					}
					if ((y >= 1008) && (y < (1008+256)))
					{
						// x,y outside of map causes the test to fail & retry another spawnpoint
						x = 0;
						y = 0;
					}
				}

				reg16(FFFF8240) += 0x315;
			}
			while ((x < c_scroll_safety_margin) || (fx > g_map_fxlimit) || 
				   (y < 0) || (fy > g_map_fylimit) ||
				   is_solidarea32_base(x, y)
			);

			// place the entity on the map
			entity_t *pent = EntitySpawn(EntityType_ALIEN, x, y);

			if (!pent)
				break;

			// load with random direction vector
			const s32 *pang = &s_fdirs_32ang[(prng.genS32() & 31) << 1];
			pent->fvx = pang[0];
			pent->fvy = pang[1];
		}

		// =====================================================================================================================
		//	must link any pending entities before starting mainloop (viewport is an entity and viewport scans rely on it)

		EntityExecuteAllLinks();

		// =====================================================================================================================
		//	mainloop
		// =====================================================================================================================
	
mainloop:

			// ---------------------------------------------------------------------------------------------------------------------
			//	convert fixedpoint coordinates to playfield coordinates
			g_viewport_worldx = g_fscrollx>>16;
			g_viewport_worldy = g_fscrolly>>16;

			// ---------------------------------------------------------------------------------------------------------------------
			//	wait for the current VBL period to elapse
			//	note: this simplistic approach will cause a hard drop to 25hz if time runs out but we're aiming for 50hz anyway

			TIMING_MAIN(bgcol);	

			s16 field_index = g_vbl;
			static s16 s_vbl_counter = g_vbl;
			shift.wait_vbl_minbound(s_vbl_counter, 1);

			TIMING_MAIN(0x077);

			// ---------------------------------------------------------------------------------------------------------------------
			// update the viewport first

			s_pe_viewport->rx = g_viewport_worldx - c_viewport_xmargin;
			s_pe_viewport->ry = g_viewport_worldy - c_viewport_ymargin;

			// ---------------------------------------------------------------------------------------------------------------------
			//	tick all currently linked objects
			// ---------------------------------------------------------------------------------------------------------------------
			//	this causes the user defined entity::fntick() function of each entity to be called
			//	note: tick order is not guaranteed unless entity::tick_priority is used to specify an order. this incurs a small cost.

			EntityTickAll();

			TIMING_MAIN(0x777);

			// ---------------------------------------------------------------------------------------------------------------------
			//	perform object-object interactions (collision processing)
			// ---------------------------------------------------------------------------------------------------------------------
			//	this causes user-defined entity::fncollide() function of an entity to be called if the following is satisfied:
			//	- entity hitbox overlaps with another
			//	- entities involved are at least partially visible
			//	- entity has a valid entity::fncollide() function assigned to it
			//	- interaction flags of overlapping entities allow an interaction. specifically:
			//		(self::f_self & other::f_interactswith) != 0
			//		i.e. the f_interactswith bits cause the collide function of *another* entity to be called on a match
			// ---------------------------------------------------------------------------------------------------------------------
			//	the interaction is reciprocal, so both entities will perform the same test on each other with their own flags.
			//	a particular interaction order is not guaranteed although it will be stable.

			EntityInteractVisible(VIEWPORT_XSIZE, c_viewport_xmargin, VIEWPORT_YSIZE, c_viewport_ymargin);

			TIMING_MAIN(0x707);	

			// ---------------------------------------------------------------------------------------------------------------------
			//	link any new entities created during [tick] pass. ensures they get drawn in their spawned states *before* next tick

			EntityExecuteAllLinks();

			// ---------------------------------------------------------------------------------------------------------------------
			//	physically remove any entities that were queued for killing during the AI tick pass
			//	Q: why necessary? 
			//	A: because AIs killing themselves or other AIs during the tick pass places a valid-test burden on any AI
			//	   which refers to another, possibly dead entity via pointers and we don't want that burden scattered 
			//	   around all the AI code. it's less complex and more efficient to delay killing until after AIs are processed.

			EntityExecuteAllRemoves();

			TIMING_MAIN(0x770);

			// ---------------------------------------------------------------------------------------------------------------------
			//	update the background & scroll

			// select the next workbuffer (..of two - we're using double buffering here)
			myworld.select_buffer((buffer_index & 1));

			// update the playfield coordinates for this draw pass
			myworld.setpos(g_viewport_worldx, g_viewport_worldy);

			// restore playfield from any previous drawing
			myworld.bgrestore();

			// fill any background tiles required by this change in coordinates
			myworld.hybrid_fill(false);

			TIMING_MAIN(0x373);	

			// ---------------------------------------------------------------------------------------------------------------------
			// select the working 50hz field (odd/even) from the currently active playfield buffer (back)
			// in single-field config, this is always 0
			// in dual-field FPS<=25hz config, its 0 or 1 for even/odd VBLs (4 buffers: front/back + even/odd)
			// in dual-field 50hz config, it's always 0, because front/back handles our even/odd fields

			s16 working_field = (buffer_index & 1);

			// ---------------------------------------------------------------------------------------------------------------------
			// retrieve playfield [drawcontext] structure for sharing with low-level drawing functions
			// e.g.
			// - current framebuffer
			// - framebuffer scanline index (because screen size varies)
			// - dimensions
			// - vpage snapping offsets for loopback scrolling
			// - other devious details used to synchronize drawing with playfields
			// todo: this has not been properly adopted yet by some of the draw functions - changes are ongoing

			myworld.get_context(drawcontext, working_field, VIEWPORT_XSIZE, VIEWPORT_YSIZE);

			TIMING_MAIN(0x753);	

			// ---------------------------------------------------------------------------------------------------------------------
			//	draw 'all the things'

			EntityDrawVisible(&drawcontext);

			TIMING_MAIN(0x0f0);	
				
			// ---------------------------------------------------------------------------------------------------------------------
			//	activate the new workbuffer as the next shifter state so it will be presented on the next VBLank

			myworld.activate_buffer();

			// ---------------------------------------------------------------------------------------------------------------------
			//	advance workbuffer (i.e. double buffering of playfields)
			buffer_index++;

			TIMING_MAIN(0x505);	

			// ---------------------------------------------------------------------------------------------------------------------
			// advance the multiway scroll until we hit the end of the map, then turn around!
			// note: this *should*  be done by a viewport tick, with appropriate tick priority
			// where the viewport entity is using proper isometric coords too.

			// countdown to random direction change
			static s16 ccc = 1024; // static, so it's initialised only once on first use!
			if (ccc-- < 0)
			{
				ccc = 1024;

				// apply random direction change
				const s32 *pang = &s_fdirs_32ang[((prng.genS32()>>7) & 31) << 1];
				g_fscrollxdir = pang[0] >> 1;
				g_fscrollydir = pang[1] >> 1;
			}

			// advance scroll
			g_fscrollx += g_fscrollxdir;
			g_fscrolly += g_fscrollydir;

			if ((g_fscrollx < ((s32)c_scroll_safety_margin<<16)))
			{
				// bounce off the left side of the map
				g_fscrollxdir = -g_fscrollxdir;
				g_fscrollx += g_fscrollxdir << 1;
			}
			else
			if ((g_fscrollx >= g_map_fxlimit))
			{
				// bounce off the right side of the map
				g_fscrollxdir = -g_fscrollxdir;
				g_fscrollx += g_fscrollxdir << 1;
			}

			if ((g_fscrolly < (128<<16)))
			{
				// bounce off the left side of the map
				g_fscrollydir = -g_fscrollydir;
				g_fscrolly += g_fscrollydir << 1;
			}
			else
			if ((g_fscrolly >= g_map_fylimit-(128<<16)))
			{
				// bounce off the right side of the map
				g_fscrollydir = -g_fscrollydir;
				g_fscrolly += g_fscrollydir << 1;
			}

			// ---------------------------------------------------------------------------------------------------------------------
			//	any remaining CPU time here can be put to good use

		goto mainloop;

		// =====================================================================================================================
		//	end mainloop
		// =====================================================================================================================

		// ---------------------------------------------------------------------------------------------------------------------
		//	restore primary display state

		shift.restore();
	}


	// ---------------------------------------------------------------------------------------------------------------------
	//	restore full machine state

	machinestate.restore();

	// ---------------------------------------------------------------------------------------------------------------------
	// back to TOS

	pprintf("terminate...\n");

	return 0;
}




// =====================================================================================================================
//	entity tick functions
// =====================================================================================================================


// ---------------------------------------------------------------------------------------------------------------------
// tick: alien

static s16 s_gcount = 0;

// walk animation frame sequence (bouncing between 0 and 3)
static u8 s_anim[16] = 
{
	1,2,3,2, 1,0,1,2, 3,2,1,0, 1,2,3,2
};

void alien_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// sleep when offscreen
	// todo: good use case for a vis-trigger entity flag
	u16 spx = self.rx - (g_viewport_worldx-32);
	u16 spy = self.ry - (g_viewport_worldy-32);
	if ((spx > (VIEWPORT_XSIZE+32)) || (spy > (VIEWPORT_YSIZE+32)))
		return;


	// cycle of 64 with id-offset trigger
	s16 s = (self.id + self.counter++);

	// animate continuously

	// use slowed-down (divided) counter to set animation rate
	s16 af = (self.counter>>3) & 15;

	// animation frame is upper 2 bits (4 animations)
	// rotation frame  is lower 3 bits (8 rotations)
	// combine to index 8x4 sprite grid
	self.frame = (s_anim[af] << 3) | (self.frame & 0x0007);

	self.fwx = self.frx;
	self.fwy = self.fry;

	// move
	self.frx += self.fvx;
	self.fry += self.fvy;

	{
		s16 dirchanged = 0;

		// upon contact with anything, reset to last good position then change direction
		u8 wall = 0;
		while ((wall = is_solidarea32(self.rx, self.ry)) | (self.wz > 0))
		{
			// reset to last good position
			self.frx = self.fwx;
			self.fry = self.fwy;

			// prioritise wall contacts over entity contacts 
			if (wall)
			{
				if (s & 6) // 2 bits = zero 1 out of 4 
				{
					// 75% chance of a 90' turn
					s32 t = self.fvx;
					self.fvx = -self.fvy;
					self.fvy = t;
				}
				else
				{
					// 25% chance of a random direction change
					const s32 *pang = &s_fdirs_32ang[((s_gcount += 5) & 31) << 1];
					self.fvx = pang[0] >> 0;
					self.fvy = pang[1] >> 0;
				}
			}
			else
			{
				// for entity contacts, send entities in opposite directions for quick separation
				self.fvx = -self.fvx;
				self.fvy = -self.fvy;
			}

			// padding counter prevents re-collision with other monsters for a short period
			// to reduce incidence of fist-fights - keeps the monsters from getting stuck
			// this is mainly needed because these entities are spawned randomly instead of being
			// carefully placed in an editor - so they may overlap significantly on first tick. 
			// they need an opportunity to put distance between each other.
			if (self.wz > 0)
				self.wz = -64;

			// try to move again
			self.frx += self.fvx;
			self.fry += self.fvy;

			// keep trying until we're not hitting walls
			// (yes, this is not a smart monster AI)
			dirchanged++;
		}

		// assuming no collision, occasionally change direction anyway
		if (!dirchanged && ((s & 127) == 0))
		{
			// turn
			const s32 *pang = &s_fdirs_32ang[((s_gcount += 5) & 31) << 1];
			self.fvx = pang[0] >> 0;
			self.fvy = pang[1] >> 0;
			dirchanged++;
		}

		// we're done turning, so we can re-evaluate our rotation sprite frame
		if (dirchanged)
		{
			// construct 4:4-bit rotation code from velocity vector
			s16 atancode = ((((self.fvx>>14) & 0xf) << 4) | ((self.fvy>>14) & 0xf));

			// todo: (+4 & 15) can be baked into table but only if all sprites use the same frame mapping
			self.frame = (self.frame & 0xFFF8) | ((6-((s_atan2_16frames[atancode]) >> 1)) & 7);
		}

		// padding counter prevents re-collision with other monsters for a short period
		// to reduce incidence of fist-fights - keeps the monsters moving
		if (self.wz < 0)
			self.wz++;

	}

}

// =====================================================================================================================
//	collision reaction functions
// =====================================================================================================================

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: player vs other

void alien_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// mild hint to change direction
	if (self.wz >= 0)
		self.wz = 1;
}

// =====================================================================================================================
// sample entity dictionary. 
// =====================================================================================================================
// note: there are better ways to do this, but the example is meant to be simple to follow more than anything.
//
// this dictionary contains only the 'mostly static' fields from an entity. 'mostly' in the sense that an entity can
// still modify all the fields it wants after it has been created. there are some entity fields however which are used 
// by the engine and do not make sense to pre-define in the dictionary. it also saves time to avoid copying those when 
// spawning a new entity instance.
//
// the process of spawning a new entity should be as simple as possible - copy all the static fields from the dictionary
// over the same fields in the entity instance. internal/dynamic fields are left to the engine.
//
// fields in {0} braces are unions - usually a scalar value which has BOTH 16bit integer AND 32bit fixedpoint reps at
// the same address. here, the initialized value is the integer part (but this can be changed by reordering the union).

entitydef_t entity_dictionary[EntityType_MAX_] =
{
	// [functions]								[coords]						[control]		[tick state]		[rendering]				[classification properties]
	// tick,collide,asset,hasset				px/y,vx/y,sx/y,ox/y				tickpri,drflag	h,d,frame,counter	drawtype,drawlayer		f_self,f_interactswith	

	// engine objects...

	// viewport																					// viewport has no tick
	// Q: why? A: because it's a rectangle and we can determine what is inside it quickly if it is also an entity which moves through the world
	{ 0,0,0,0,									{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,0,		EntityFlag_VIEWPORT|EntityFlag_SPATIAL,	0 },

	// skeleton objects...

	// alien monster
	//	summary:
	//	- run around in 8 directions, avoiding solid walls and each other
	{ &alien_fntick,&alien_fncol,0,0,			{0},{0},{0},{0},0,0,0,0,		0,0,			30,0,0,0,				EntityDraw_EMSPRQ,0,		EntityFlag_PLAYER|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },

};

//----------------------------------------------------------------------------------------------------------------------
//	Music fixup callback: opportunity to set the instruments and multiplex routing for specific songs
//----------------------------------------------------------------------------------------------------------------------

#if defined(ENABLE_AGT_AUDIO)
void AGT_MusicFixupCallback(const char* const _name, remap_ft &remap_func)
{
}
#endif // defined(ENABLE_AGT_AUDIO)
