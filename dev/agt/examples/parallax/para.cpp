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
	EntityType_OBJECT,
	
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
//	sincos table

#include "tables/sintab32.h"


// physical display size
#define SCREEN_XSIZE (320)
//#define SCREEN_YSIZE (240)

// virtual display size including h-scroll margin
#define VIEWPORT_XSIZE (SCREEN_XSIZE)
//#define VIEWPORT_YSIZE (SCREEN_YSIZE)


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
	/*max_scroll=*/8192,						// maximum scroll distance in pixels (reduce to save a little ram on tables)
	/*vscroll=*/VScrollMode_LOOPBACK,			// enable vscroll because each viewport uses y-offsets into the same map
	/*hscroll=*/HScrollMode_SCANWALK,			// enable full hscroll
	/*restore=*/RestoreMode_PAGERESTORE,		// restore from virtual page (verus save/restore for individual objects)
	/*update=*/UpdateMode_NORMAL,
	/*hwattr=*/PFH_Defaults,
	/*pfattr=*/PFA_Defaults,
	/*guardx=*/AGT_CONFIG_SYS_GUARDX,
	/*guardy=*/0								// removes guardband from screen splits = more efficient

> playfield_t;									// new name for our playfield definition

// define an arena (world) composed of 2 such playfields (double-buffering on STE)
typedef arena_template
<
	/*buffers=*/2,								// number of framebuffers (playfields) required
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
static s32 g_fscrollxdir = 0x00010000>>0; // 1.0 units in x	
static s32 g_fscrollydir = 0x00000000>>0; // 0.5 units in y	

// xlimit at which map stops and changes scroll direction
static s32 g_map_fxlimit = 0;
static s32 g_map_fylimit = 0;

// player viewport coordinates (local, control coordinates, relative to current screen position)
static s16 g_player_viewx = 160;
static s16 g_player_viewy = 100;

// player world coordinates (derived from local coords each frame, for interactions with world & other objects)
static s16 g_player_worldx = 0;
static s16 g_player_worldy = 0;

// clipping margin outside of the scrolling viewport, to accommodate the largest typical sprite before deletion etc.
static const s16 c_viewport_xmargin = playfield_t::c_guardx;
static const s16 c_viewport_ymargin = playfield_t::c_guardy;

// viewport (scroll) starting position in world map
static s16 g_viewport_worldx = c_viewport_xmargin; 
static s16 g_viewport_worldy = 0;

// pseudorandom number generator
mersenne_twister prng;

// ---------------------------------------------------------------------------------------------------------------------
// static pointers to significant entities (still these must be created the usual way)

// viewport entity is global and part of the entity drawing system

static entity_t *s_pe_viewport1 = nullptr;
static entity_t *s_pe_viewport2 = nullptr;
static entity_t *s_pe_viewport3 = nullptr;
static entity_t *s_pe_viewport4 = nullptr;

// ---------------------------------------------------------------------------------------------------------------------

// sprite assets

// sprite object
spritesheet object;

// playfield tile assets (x2 in dual-field mode)
tileset mytiles0;
tileset mytiles1;
tileset mytilesf0;
tileset mytilesf1;

// playfield map assets
worldmap mymap0;
worldmap mymapf;

drawcontext_t drawcontexts[4]; // one per display port

// ---------------------------------------------------------------------------------------------------------------------

static void init_viewport(entity_t *_pviewport, int _xoffset, int _yoffset, arena_t &_arena)
{
	EntitySelectViewport(_pviewport);

	// select the next workbuffer (..of two - we're using double buffering here)
	//_arena.select_buffer((_buffer_index & 1));

	// update the playfield coordinates for this draw pass
	_arena.reset
	(
		(_pviewport->rx-_xoffset)+c_viewport_xmargin, 
		(_pviewport->ry-_yoffset)
	);
}

static void update_viewport(entity_t *_pviewport, int _xoffset, int _yoffset, arena_t &_arena, int _buffer_index, int _viewbind)
{
	EntitySelectViewport(_pviewport);

	// select the next workbuffer (..of two - we're using double buffering here)
	_arena.select_buffer((_buffer_index & 1));

	// update the playfield coordinates for this draw pass
	_arena.setpos
	(
		(_pviewport->rx-_xoffset)+c_viewport_xmargin, 
		(_pviewport->ry-_yoffset)
	);

	// restore playfield from any previous drawing
	_arena.bgrestore();

	// ---------------------------------------------------------------------------------------------------------------------
	// select the working 50hz field (odd/even) from the currently active playfield buffer (back)
	// in single-field config, this is always 0
	// in dual-field FPS<=25hz config, its 0 or 1 for even/odd VBLs (4 buffers: front/back + even/odd)
	// in dual-field 50hz config, it's always 0, because front/back handles our even/odd fields

	s16 working_field = (_buffer_index & 1);

	// ---------------------------------------------------------------------------------------------------------------------
	// retrieve playfield [drawcontext] structure for sharing with low-level drawing functions
	// e.g.
	// - current framebuffer
	// - framebuffer scanline index (because screen size varies)
	// - dimensions
	// - vpage snapping offsets for loopback scrolling
	// - other devious details used to synchronize drawing with playfields
	// todo: this has not been properly adopted yet by some of the draw functions - changes are ongoing

	_arena.get_context(drawcontexts[_arena.viewbind_], working_field, VIEWPORT_XSIZE, _arena.display_height_);

	// ---------------------------------------------------------------------------------------------------------------------
	//	draw occlusion maps for objects using them

	//EntityOccludeVisibleXM(&drawcontext);

	// fill any background tiles required by this change in coordinates
	_arena.hybrid_fill(false);

	// ---------------------------------------------------------------------------------------------------------------------
	//	draw 'all the things'

//	EntitySelectViewport(s_pe_viewport3);
	EntityDrawVisible(&drawcontexts[_arena.viewbind_]);
		
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

static const int c_split0 = 0+16;
static const int c_split1 = 72+16;//64;
static const int c_split2 = 96+16;
static const int c_split3 = 184+16;//176;

s16 s_spr_view_x = 0;
s16 s_spr_view_y = 0;

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

	printf("[A]tari [G]ame [T]ools / dml 2020\n");
	printf("Parallax example (STE)\n");

	// ---------------------------------------------------------------------------------------------------------------------
	//	open scope before we create any interfaces (so we can expect them to be cleaned up before the scope is closed)
	// ---------------------------------------------------------------------------------------------------------------------
	{
		// ---------------------------------------------------------------------------------------------------------------------
		//	save machine state for a clean exit

		machinestate.configure();

		if (bSTE)
		{	printf("machine: STE\n"); }
		else
		{
			printf("machine: unknown...\n");
			printf("press space...\n");
			return -1;
		}

		object.load("object.ems");

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
		//	load map tiles

		printf("loading tiles...\n");
		mytiles0.load_cct("map0.cct");
		mytilesf0.load_cct("mapf0.cct");
		// only load 2nd tileset if interlacing/dual-field
		if (c_dualfield)
		{
			mytiles1.load_cct("map1.cct");
			mytilesf1.load_cct("mapf1.cct");
		}

#if defined(TIMING_RASTERS_MAIN)
		// save copy of BG colour for rasters
		s16 bgcol = mytiles0.getcol(0);
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map
		
		printf("loading map...\n");
		mymap0.load_ccm("map.ccm");
		mymapf.load_ccm("mapf.ccm");

		// get map tile dimensions (x16 -> in pixels) to help keep scroll inside the map limits
		int map_xsize = mymap0.getwidth() << 4;
		int map_ysize = mymap0.getheight() << 4;

		// start in middle of map (fixedpoint)
		//g_fscrollx = c_viewport_xmargin<<16;
		g_fscrollx = 512<<16;
		g_fscrolly = 0;//c_viewport_ymargin<<16;

		// max scroll is map size minus screen dimensions (plus safety margin)
		g_map_fxlimit = (map_xsize-VIEWPORT_XSIZE-c_scroll_safety_margin)<<16;
		g_map_fylimit = (map_ysize-272-c_scroll_safety_margin)<<16;


		// ---------------------------------------------------------------------------------------------------------------------
		//	create the double-buffered 'world', set the viewport dimensions and connect it to the shifter interface

		printf("creating world...\n");
		arena_t arena_view_1(shift, SCREEN_XSIZE, c_split1-c_split0/*72+16*/);
		arena_t arena_view_2(shift, SCREEN_XSIZE, c_split2-c_split1/*(96-72)*/);
		arena_t arena_view_3(shift, SCREEN_XSIZE, c_split3-c_split2/*(184-96)*/);
		arena_t arena_view_4(shift, SCREEN_XSIZE, 256-c_split3/*(272-184)*/);

		arena_view_1.bind_displayport(0);
		arena_view_2.bind_displayport(1);
		arena_view_3.bind_displayport(2);
		arena_view_4.bind_displayport(3);

		arena_view_4.set_viewport_offset(0, 32);

		// ---------------------------------------------------------------------------------------------------------------------
		//	associate map & tiles with the world buffers.
		//	Q: why do we bother to do this manually? 
		//	A: because in dual-field mode each display field can have a different (dithered) tile library
	
		printf("assigning world map...\n");
		arena_view_1.setmap(&mymap0);
		arena_view_2.setmap(&mymap0);
		arena_view_3.setmap(&mymap0);
		arena_view_4.setmap(&mymapf);

		pprintf("assigning world tiles...\n");
		for (int b = arena_t::c_num_buffers_-1; b >= 0; b--)
		{
			arena_view_1.select_buffer(b);
			arena_view_2.select_buffer(b);
			arena_view_3.select_buffer(b);
			arena_view_4.select_buffer(b);
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
				{
					//myworld.settiles(&mytiles1, &mytiles1);		// odd buffers
					arena_view_1.settiles(&mytiles1, &mytiles1);
					arena_view_2.settiles(&mytiles1, &mytiles1);
					arena_view_3.settiles(&mytiles1, &mytiles1);
					arena_view_4.settiles(&mytilesf1, &mytilesf1);
				}
				else
				{
					arena_view_1.settiles(&mytiles0, &mytiles0);
					arena_view_2.settiles(&mytiles0, &mytiles0);
					arena_view_3.settiles(&mytiles0, &mytiles0);
					arena_view_4.settiles(&mytilesf0, &mytilesf0);
				}
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
				arena_view_1.settiles(&mytiles0, &mytiles1);
				arena_view_2.settiles(&mytiles0, &mytiles1);
				arena_view_3.settiles(&mytiles0, &mytiles1);
				arena_view_4.settiles(&mytilesf0, &mytilesf1);
			}
		}

		// ---------------------------------------------------------------------------------------------------------------------
		//	load sprite gfx assets
		// ---------------------------------------------------------------------------------------------------------------------
		//	note: using EMSPR sprite format to keep memory load sensible with 8x4 rotation x animation frames

		// sprite
		//object.load("object.ems");

		// ---------------------------------------------------------------------------------------------------------------------
		//	tie gfx assets into entity type dictionary, so we don't need to do it every time we spawn an object

		// note: asset binding could be done automatically via the dictionary but its useful to be able to override sizes etc.
		// and its better not to hide asset assignment steps in a system with simple memory management.

		EntityDefAsset_Sprite(EntityType_OBJECT, &object);

		// ---------------------------------------------------------------------------------------------------------------------

		printf("ready... (press key)\n");
		Crawcin();

		// ---------------------------------------------------------------------------------------------------------------------
		//	now claim control over machine resources - display, timers, input etc.

#if !defined(AGT_DISABLE_DISPLAYSERVICE)
		machinestate.claim();
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	install input service for keyboard, joysticks, mouse
		//	note: joysticks/pads not yet tied - will be fixed soon

#if !defined(AGT_DISABLE_DISPLAYSERVICE)
		AGT_InstallInputService();
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	initialize display, configure single- or dual-field graphics mode, load palettes for phys & log shiftfields
		// ---------------------------------------------------------------------------------------------------------------------
		// note: there are 2 shiftfields by default, even if there are more than 2 playfield buffers. The shiftfield contains
		// all HW state associated with a single frame, including the framebuffer address, palette, scroll/linewid and other
		// relevant state. They must all be committed at exactly the same time by the displayservice.
		// The logical shifstate is always the one to be written by the game loop because it will not reach the HW registers 
		// until the framebuffers are exchanged.

		nickel_op_t nickel_commands[] =
		{
			{ NE_PortSelect,	0+16,	NickelPort0	},
			{ NE_PortSelect,	72+16,  NickelPort1	},
			{ NE_PortSelect,	96+16,	NickelPort2	},
			{ NE_PortSelect,	184+16,	NickelPort3	},
			{ NE_Stop,			240+16 }
		};

		shift.load_nickel(nickel_commands, 2);

		printf("init shifter...\n");
		shift.set_fieldmode(/*dualfield=*/(c_dualfield && !c_singlevbl));
		shift.init(Nickel);

		// on F030 we can just open the top/bottom border through Videl
		if (bFalcon030)
		{
			reg16(ffff82a8) -= 40;
			reg16(ffff82aa) += 40;
		}

		printf("set palette...\n");	
		for (s16 v = 0; v < 4; v++)
		{
			for (s16 c = 0; c < 16; ++c)
			{
				// logic (next)
				shift.setcolour(0, 0, c, mytiles0.getcol(c), v);
				shift.setcolour(0, 1, c, mytiles1.getcol(c), v);	// only needed in dual-field mode

				// physic (current)
				shift.setcolour(1, 0, c, mytiles0.getcol(c), v);
				shift.setcolour(1, 1, c, mytiles1.getcol(c), v);	// only needed in dual-field mode
			}
		}

		// ---------------------------------------------------------------------------------------------------------------------
		//	init world, set viewport initial position

		// convert fixedpoint to screen coords
		g_viewport_worldx = g_fscrollx>>16;
		//g_viewport_worldy = 0;//g_fscrolly>>16;

		printf("init world viewport...\n");
		arena_view_1.init(g_viewport_worldx, c_split0+32);
		arena_view_2.init(g_viewport_worldx, c_split1+32);
		arena_view_3.init(g_viewport_worldx, c_split2+32);
		arena_view_4.init(g_viewport_worldx, c_split3+0);

		// =====================================================================================================================

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

		arena_view_1.bgrestore_reset();
		arena_view_2.bgrestore_reset();
		arena_view_3.bgrestore_reset();
		arena_view_4.bgrestore_reset();

		// ---------------------------------------------------------------------------------------------------------------------
		//	reset the scrolling playfield

		// player coords within viewport
		g_player_viewx = 160;
		g_player_viewy = 100;

		// viewport coords within world - centre of map
		g_fscrollx = 512<<16;//(map_xsize<<(16-1)) - (160<<16);
//		g_fscrolly = 0;//(map_ysize<<(16-1)) - (120<<16);

		// convert fixedpoint to screen coords
		g_viewport_worldx = g_fscrollx>>16;
		//g_viewport_worldy = 0;//g_fscrolly>>16;


		// ---------------------------------------------------------------------------------------------------------------------
		//	create some significant 'static' entities related to the player

		// create the viewport so we can track its relationship with everything else
		s_pe_viewport1 = EntitySpawn(EntityType_VIEWPORT, 0, 0);
		s_pe_viewport2 = EntitySpawn(EntityType_VIEWPORT, 0, 0);
		s_pe_viewport3 = EntitySpawn(EntityType_VIEWPORT, 0, 0);
		s_pe_viewport4 = EntitySpawn(EntityType_VIEWPORT, 0, 0);

		// ---------------------------------------------------------------------------------------------------------------------
		//	initialise (paint) all viewports with inital state, using correct parallax offsets
		// ---------------------------------------------------------------------------------------------------------------------
		{
			s_spr_view_x = g_viewport_worldx+100;
			int view4_offset = 80;

			s_pe_viewport1->rx = s_spr_view_x-c_viewport_xmargin;
			s_pe_viewport1->ry = c_split0+32;

			s_pe_viewport2->rx = s_spr_view_x-c_viewport_xmargin;
			s_pe_viewport2->ry = c_split1+32;

			s_pe_viewport3->rx = s_spr_view_x-c_viewport_xmargin;
			s_pe_viewport3->ry = c_split2+32;

			s_pe_viewport4->rx = s_spr_view_x-c_viewport_xmargin;
			s_pe_viewport4->ry = c_split3+32;

			// parallax offsets, relative to viewport 3 (base scroll rate = 1px/vbl)
			s16 px1 = s_pe_viewport3->rx - ((s_spr_view_x>>2)-c_viewport_xmargin);
			s16 px2 = s_pe_viewport3->rx - ((s_spr_view_x>>1)-c_viewport_xmargin);
			s16 px3 = 0;
			s16 px4 = s_pe_viewport3->rx - (((((s_spr_view_x+view4_offset)*7)/6)-view4_offset)-c_viewport_xmargin);

			// sprite drawing offset compensation for playfield offsets
			arena_view_1.set_viewport_offset(px1, 0);
			arena_view_2.set_viewport_offset(px2, 0);
			arena_view_3.set_viewport_offset(px3, 0);
			arena_view_4.set_viewport_offset(px4, 32);

			// update views, applying parallax offsets to each playfield
			init_viewport(s_pe_viewport1, px1, 0,  arena_view_1);
			init_viewport(s_pe_viewport2, px2, 0,  arena_view_2);
			init_viewport(s_pe_viewport3, px3, 0,  arena_view_3);
			init_viewport(s_pe_viewport4, px4, 32, arena_view_4);
		}

//		arena_view_1.reset(g_viewport_worldx, c_split0+32);
//		arena_view_2.reset(g_viewport_worldx, c_split1+32);
//		arena_view_3.reset(g_viewport_worldx, c_split2+32);
//		arena_view_4.reset(g_viewport_worldx, c_split3+0);

		// ---------------------------------------------------------------------------------------------------------------------

		entity_t *pobj;
		
		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 12;
		pobj->vy = 7;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 11;
		pobj->vy = 27;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 15;
		pobj->vy = 21;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 23;
		pobj->vy = 19;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 17;
		pobj->vy = 17;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 14;
		pobj->vy = 16;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 9;
		pobj->vy = 18;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 24;
		pobj->vy = 23;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 21;
		pobj->vy = 11;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 10;
		pobj->vy = 15;

		pobj = EntitySpawn(EntityType_OBJECT, 0, 0);
		pobj->vx = 12;
		pobj->vy = 20;

		// =====================================================================================================================
		//	must link any pending entities before starting mainloop (viewport is an entity and viewport scans rely on it)

		EntityExecuteAllLinks();

		// =====================================================================================================================
		//	mainloop
		// =====================================================================================================================
	
mainloop:
			EntitySelectViewport(s_pe_viewport1);

			// ---------------------------------------------------------------------------------------------------------------------
			//	convert fixedpoint coordinates to playfield coordinates
			g_viewport_worldx = g_fscrollx>>16;
			//g_viewport_worldy = 0;//g_fscrolly>>16;

			// ---------------------------------------------------------------------------------------------------------------------
			//	wait for the current VBL period to elapse
			//	note: this simplistic approach will cause a hard drop to 25hz if time runs out but we're aiming for 50hz anyway

			TIMING_MAIN(bgcol);	

			//s16 field_index = g_vbl;
			//while (g_vbl == field_index) { }
			s16 field_index = g_vbl;
			static s16 s_vbl_counter = g_vbl;
			shift.wait_vbl_minbound(s_vbl_counter, 1);


			TIMING_MAIN(0x077);

			// ---------------------------------------------------------------------------------------------------------------------
			// update the viewport first

			s_spr_view_x = g_viewport_worldx+100;//s_pe_viewport1->rx;
			int view4_offset = 80;

			s_pe_viewport1->rx = s_spr_view_x-c_viewport_xmargin;//(s_spr_view_x>>2)-c_viewport_xmargin;
			s_pe_viewport1->ry = c_split0+32;

			s_pe_viewport2->rx = s_spr_view_x-c_viewport_xmargin;//(s_spr_view_x>>1)-c_viewport_xmargin;
			s_pe_viewport2->ry = c_split1+32;

			s_pe_viewport3->rx = s_spr_view_x-c_viewport_xmargin;//(s_spr_view_x>>0)-c_viewport_xmargin;
			s_pe_viewport3->ry = c_split2+32;

			s_pe_viewport4->rx = s_spr_view_x-c_viewport_xmargin;//(((s_spr_view_x+offset)*7)/6)-offset-c_viewport_xmargin;
			s_pe_viewport4->ry = c_split3+32;

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

			//EntitySelectViewport(s_pe_viewport1);
			//EntityInteractVisible(VIEWPORT_XSIZE, c_viewport_xmargin, VIEWPORT_YSIZE, c_viewport_ymargin);

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

			// parallax offsets, relative to viewport 3 (base scroll rate = 1px/vbl)
			s16 px1 = s_pe_viewport3->rx - ((s_spr_view_x>>2)-c_viewport_xmargin);
			s16 px2 = s_pe_viewport3->rx - ((s_spr_view_x>>1)-c_viewport_xmargin);
			s16 px3 = 0;
			s16 px4 = s_pe_viewport3->rx - (((((s_spr_view_x+view4_offset)*7)/6)-view4_offset)-c_viewport_xmargin);

			// sprite drawing offset compensation for playfield offsets
			arena_view_1.set_viewport_offset(px1, 0);
			arena_view_2.set_viewport_offset(px2, 0);
			arena_view_3.set_viewport_offset(px3, 0);
			arena_view_4.set_viewport_offset(px4, 32);

			// update views, applying parallax offsets to each playfield
			update_viewport(s_pe_viewport1, px1, 0,  arena_view_1, buffer_index, 0);
			update_viewport(s_pe_viewport2, px2, 0,  arena_view_2, buffer_index, 1);
			update_viewport(s_pe_viewport3, px3, 0,  arena_view_3, buffer_index, 2);
			update_viewport(s_pe_viewport4, px4, 32, arena_view_4, buffer_index, 3);
				
			// present updated buffers to display ports
			arena_view_1.activate_buffer();
			arena_view_2.activate_buffer();
			arena_view_3.activate_buffer();
			arena_view_4.activate_buffer();

			// ---------------------------------------------------------------------------------------------------------------------
			//	advance workbuffer (i.e. double buffering of playfields)
			buffer_index++;

			TIMING_MAIN(0x505);	

			// ---------------------------------------------------------------------------------------------------------------------
			// advance the multiway scroll until we hit the end of the map, then turn around!
			// note: this *should*  be done by a viewport tick, with appropriate tick priority
			// where the viewport entity is using proper isometric coords too.

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
			if ((g_fscrollx >= g_map_fxlimit/2))
			{
				// bounce off the right side of the map
				g_fscrollxdir = -g_fscrollxdir;
				g_fscrollx += g_fscrollxdir << 1;
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

void object_fntick(entity_t *_pself)
{
	entity_t& self = *_pself;

	// fetch x,y position offset from sintable
	s32 x = sintab32[self.wx & 8191];
	s32 y = sintab32[self.wy & 8191];

	// advance positions in sintable
	self.wx += self.vx;
	self.wy += self.vy;

	// assign position, relative to viewport
	self.rx = s_spr_view_x + 160 - self.ox + (x >> 24);
	self.ry = s_spr_view_y + (128+32) - self.oy + (y >> 24);
}

// =====================================================================================================================
//	collision reaction functions
// =====================================================================================================================


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

	// object
	{ &object_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		0,0,			30,0,0,0,			EntityDraw_EMSPRQ,0,		EntityFlag_PLAYER|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },

};

//----------------------------------------------------------------------------------------------------------------------
//	Music fixup callback: opportunity to set the instruments and multiplex routing for specific songs
//----------------------------------------------------------------------------------------------------------------------

#if defined(ENABLE_AGT_AUDIO)
void AGT_MusicFixupCallback(const char* const _name, remap_ft &remap_func)
{
}
#endif // defined(ENABLE_AGT_AUDIO)
