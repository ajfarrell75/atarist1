//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	example: 8x8 2-layer tilemap with tile animation on both layers
//----------------------------------------------------------------------------------------------------------------------
//	controls: none
//----------------------------------------------------------------------------------------------------------------------
//	notes:
//		- same as map8x8 example, but with 2 playfield layers
//		- involves loading 2 maps and 2 tilesets into same playfield arena, 
//		  the top layer being masked (cut with -layer option)
//		- dual-layer playfield enabled with PFA_DualLayer attribute (must assign both map/tile layers!)
//		- tile animation enabled with PFA_TileAnimation attribute
//		- different animation edits/updates applied to each layer (& overlapping same position)
//		- see notes for examples/map8x8 for more details on 8x8 tilemaps
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
static const s16 c_dualfield = 0;
// this tweak optimizes dualfield display for 50hz, otherwise set 0 for a 25hz-or-less refresh
// note: at 50hz we need only one buffer for even field, one for odd. at 25hz or below we need 
// to store independent odd/even fields for each front/back buffer so the 50hz interlace can 
// be maintained.
static const s16 c_singlevbl = 1;

// define a playfield config for multiway scrolling using STE HW HSCROLL
typedef playfield_template
<
	/*tilesize=*/3,								// 2^3 for 8x8 bg tiles
	/*max_scroll=*/1920,						// maximum scroll distance in pixels (reduce to save a little ram on tables)
	/*vscroll=*/VScrollMode_LOOPBACK,			// enable full vscroll
	/*hscroll=*/HScrollMode_SCANWALK,			// enable full hscroll
	/*restore=*/RestoreMode_PAGERESTORE,		// restore from virtual page (verus save/restore for individual objects)
	/*updatemode=*/UpdateMode_NORMAL,			// tile update method (normal=full speculative-margin on STE, partial/vertical on STF)
	/*hwattr=*/PlayfieldHardware				// hardware configuration
	(
		PFH_STE|PFH_Blitter
	),
	/*pfattr=*/PlayfieldAttributes				// enable specific platfield features
	(
		PFA_DualLayer |
		PFA_TileAnimation
	),
	/*guardx=*/32,								// x guardband size
	/*guardy=*/32,								// y guardband size
	/*mapaddr=*/s32								// type required for highest map address (big 8x8 maps require 32bit)

> playfield_t;									// new name for our playfield definition

// define an arena (world) composed of 2 such playfields (double-buffering on STE)
typedef arena_template
<
	/*buffers=*/2,								// number of framebuffers (playfields) required
	/*singleframe=*/c_singlevbl ? true : false,	// optimise dualfield to half the framebuffer count, when locked @ 50hz
	/*dualfield=*/false,						// enable dualfield support at compiletime, otherwise on/off flag will be ignored
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

// in 8-way scrolling mode we need to keep a 16 1-tile safety margin between the viewport and the map edges
// otherwise the tile update speculation algorithm will cause draw glitches on scroll direction changes. it renders
// slightly ahead of the scroll and won't wrap on the left or top edges. this safety margin avoids any issues.
static const int c_scroll_safety_margin = 8;

static const s16 c_direction_change_timer = 1024;

// initial scroll & direction (16:16 fixed point)
static s32 g_fscrollx = 0;
static s32 g_fscrolly = 0;
static s32 g_fscrollxdir = 0x00010000>>1; // 1.0 units in x	
static s32 g_fscrollydir = 0x00010000>>1; // 0.5 units in y	

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
static entity_t *s_pe_viewport = NULL;

static drawcontext_t drawcontext;

// ---------------------------------------------------------------------------------------------------------------------

// sprite assets

// playfield tile assets (x2 in dual-field mode)
tileset mytiles;
tileset myolaytiles;

// playfield map assets (x2 in dual-field mode)
worldmap mymap;
worldmap myolaymap;

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
	printf("8x8 2layer map with scroll & map anim\n");

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
		//	load map tiles

		printf("loading tiles...\n");
		mytiles.load_cct("map.cct");
		myolaytiles.load_cct("mapo.cct");

#if defined(TIMING_RASTERS_MAIN)
		// save copy of BG colour for rasters
		s16 bgcol = mytiles.getcol(0);
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	load maps
		
		printf("loading map...\n");
		mymap.load_ccm("map.ccm");
		myolaymap.load_ccm("mapo.ccm");

		// get map tile dimensions (x16 -> in pixels) to help keep scroll inside the map limits
		int map_xtiles = mymap.getwidth();
		int map_ytiles = mymap.getheight();
		int map_xsize = map_xtiles << playfield_t::c_tilestep_;
		int map_ysize = map_ytiles << playfield_t::c_tilestep_;

		// start in middle of map (fixedpoint)
		//g_fscrollx = map_xsize<<(16-1);
		//g_fscrolly = map_ysize<<(16-1);
		g_fscrollx = c_scroll_safety_margin<<16;
		g_fscrolly = 0<<16;

		// max scroll is map size minus screen dimensions (plus safety margin)
		g_map_fxlimit = ((map_xsize/2)-VIEWPORT_XSIZE-c_scroll_safety_margin)<<16;
		g_map_fylimit = ((map_ysize/2)-VIEWPORT_YSIZE-0)<<16;


		// ---------------------------------------------------------------------------------------------------------------------
		//	create the double-buffered 'world', set the viewport dimensions and connect it to the shifter interface

		printf("creating world...\n");
		arena_t myworld(shift, SCREEN_XSIZE, SCREEN_YSIZE);

		// ---------------------------------------------------------------------------------------------------------------------
		//	associate map & tiles with the world buffers.
		//	Q: why do we bother to do this manually? 
		//	A: because in dual-field mode each display field can have a different (dithered) tile library
	
		printf("assigning world map...\n");
		myworld.setmap(&mymap);
		myworld.setoverlaymap(&myolaymap);

		pprintf("assigning world tiles...\n");
		for (int b = myworld.c_num_buffers_-1; b >= 0; b--)
		{
			myworld.select_buffer(b);
			pprintf("assigning buffer %d world tileset...\n", b);

			myworld.settiles(&mytiles, &mytiles);
			myworld.setoverlaytiles(&myolaytiles, &myolaytiles);
		}

		// ---------------------------------------------------------------------------------------------------------------------
		//	init world, set viewport initial position

		// convert fixedpoint to screen coords
		g_viewport_worldx = g_fscrollx>>16;
		g_viewport_worldy = g_fscrolly>>16;

		printf("init world viewport...\n");
		myworld.init(g_viewport_worldx, g_viewport_worldy);

		// ---------------------------------------------------------------------------------------------------------------------

		printf("ready... (press key)\n");
		Crawcin();

		// ---------------------------------------------------------------------------------------------------------------------
		//	now claim control over machine resources - display, timers, input etc.

		machinestate.claim();

		// ---------------------------------------------------------------------------------------------------------------------
		//	install input service for keyboard, joysticks, mouse
		//	note: joysticks/pads not yet tied - will be fixed soon

		//AGT_InstallInputService();

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
			shift.setcolour(0, 0, c, mytiles.getcol(c));
			shift.setcolour(0, 1, c, mytiles.getcol(c));	// only needed in dual-field mode

			// physic (current)
			shift.setcolour(1, 0, c, mytiles.getcol(c));
			shift.setcolour(1, 1, c, mytiles.getcol(c));	// only needed in dual-field mode
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
		//g_fscrollx = c_scroll_safety_margin<<16;//(map_xsize<<(16-1)) - (160<<16);
		//g_fscrolly = c_scroll_safety_margin<<16;//(map_ysize<<(16-1)) - (120<<16);

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

		// select this as the active viewport
		EntitySelectViewport(s_pe_viewport);

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
			while (g_vbl == field_index) { }


			TIMING_MAIN(0x077);

			// ---------------------------------------------------------------------------------------------------------------------
			// manually update the viewport first

			g_pe_viewport->rx = g_viewport_worldx - c_viewport_xmargin;
			g_pe_viewport->ry = g_viewport_worldy - c_viewport_ymargin;
	

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
			// retrieve playfield [drawcontext] structure for sharing with low-level drawing functions
			// e.g.
			// - current framebuffer
			// - framebuffer scanline index (because screen size varies)
			// - dimensions
			// - vpage snapping offsets for loopback scrolling
			// - other devious details used to synchronize drawing with playfields
			// todo: this has not been properly adopted yet by some of the draw functions - changes are ongoing

			myworld.get_context(drawcontext, 0, VIEWPORT_XSIZE, VIEWPORT_YSIZE);

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
			// animate some 8x8 tiles in the map
			{
				static s32 n = 0;

				// tile indices 
				// (pre-shifted into map format for 8x8 tilesize @ 4x8 = 32bytes per tile)
				// note: the shift constant '5' can be found more properly using 'playfield_multiscroll_ste::c_tileshift_b_'
				static s32 newtiles_layer1[] = 
				{ 
					0<<5, 1<<5, 2<<5, 3<<5, 4<<5, 5<<5, 6<<5, 7<<5, 
					8<<5, 9<<5, 10<<5, 11<<5, 12<<5, 13<<5, 14<<5, 15<<5, 
					16<<5, 17<<5, 18<<5, 19<<5, 20<<5, 21<<5, 22<<5, 23<<5, 24<<5,
					25<<5, 26<<5, 27<<5, 28<<5, 29<5, 30<<5, 31<<5, 32<<5, 33<<5,
				};

				// note: 2nd layer requires index preshift of (c_tileshift_b_ + 1) due to stored mask
				static s32 newtiles_layer2[] = 
				{ 
					0<<6, 1<<6, 2<<6, 3<<6, 4<<6, 5<<6, 6<<6, 7<<6, 
					8<<6, 9<<6, 10<<6, 11<<6, 12<<6, 13<<6, 14<<6, 15<<6, 
					16<<6, 17<<6, 18<<6, 19<<6, 20<<6, 21<<6, 22<<6, 23<<6, 24<<6,
					25<<6, 26<<6, 27<<6, 28<<6, 29<<6, 30<<6, 31<<6, 32<<6, 33<<6,
				};

				// update irregular block of tiles
				// note: prefer multimod_row/column/rect vs individual tile edits for speed!
				// cycle through tile array with offset 0-7
				s16 nanim = n & 7; 
				n++;
				// animate tiles in centre of map
				s16 xp = map_xtiles >> 2;
				s16 yp = map_ytiles >> 2;

				// edit some rows in the base layer - stagger them just for fun
				s16 yp2 = yp + 8;
				for (s16 y = yp; y < yp2; y++)
					myworld.multimod_row<PFLayer_Base>(y, y, 8, &newtiles_layer1[nanim]);

				// edit a moving rectangle on the top layer
				myworld.multimod_rect<PFLayer_Overlay>(yp+4, (yp-4+(n>>3&15)), 4, 4, &newtiles_layer2[0]);
			}

			TIMING_MAIN(606);	

			// ---------------------------------------------------------------------------------------------------------------------
			// advance the multiway scroll until we hit the end of the map, then turn around!
			// note: this *should*  be done by a viewport tick, with appropriate tick priority
			// where the viewport entity is using proper isometric coords too.

			// countdown to random direction change
			static s16 ccc = c_direction_change_timer; // static, so it's initialised only once on first use!
			if (ccc-- < 0)
			{
				ccc = c_direction_change_timer;

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

			if ((g_fscrolly < ((s32)0<<16)))
			{
				// bounce off the left side of the map
				g_fscrollydir = -g_fscrollydir;
				g_fscrolly += g_fscrollydir << 1;
			}
			else
			if ((g_fscrolly >= g_map_fylimit))
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
	// tick,collide,asset,hidden				px/y,vx/y,sx/y,ox/y				tickpri,drflag	h,d,frame,counter	drawtype,drawlayer		f_self,f_interactswith	

	// engine objects...

	// viewport																					// viewport has no tick
	// Q: why? A: because it's a rectangle and we can determine what is inside it quickly if it is also an entity which moves through the world
	{ 0,0,0,0,									{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,0,		EntityFlag_VIEWPORT|EntityFlag_SPATIAL,	0 },

};

//----------------------------------------------------------------------------------------------------------------------
