//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	example: 16x16 2-layer tilemap with tile animation on 2nd layer
//			 
//----------------------------------------------------------------------------------------------------------------------
//	controls: none
//----------------------------------------------------------------------------------------------------------------------
//	notes:
//		- map scrolls diagonally, trees grow slowly on 2nd tilemap
//		- tree animations are stored on map assets with transparent tilesets
//		- tilesets can be shared between map assets (multi-cut)
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
	
	// tree proxy
	EntityType_TREE,

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

// define a playfield config for horizontal-only scrolling using STE HW HSCROLL
typedef playfield_template
<
	/*tilesize=*/4,								// 2^4 for 16x16 bg tiles
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
		PFA_DualLayer|PFA_TileAnimation
	)
> playfield_t;		

// define an arena (world) composed of 2 such playfields (double-buffering on STE)
typedef arena_template
<
	/*buffers=*/2,								// number of framebuffers (playfields) required
	/*singleframe=*/c_singlevbl ? true : false,	// optimise dualfield to half the framebuffer count, when locked @ 50hz
	/*dualfield=*/c_dualfield,					// enable dualfield support at compiletime, otherwise on/off flag will be ignored
	/*playfield_type=*/playfield_t				// playfield config/definition to use for framebuffers
> arena_t;										// new name for our playfield arena definition

arena_t *g_pworld = nullptr;

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
static const int c_scroll_safety_margin = 16;//16;

static const s16 c_direction_change_timer = 1024;

// initial scroll & direction (16:16 fixed point)
static s32 g_fscrollx = 0;
static s32 g_fscrolly = 0;
static s32 g_fscrollxdir = 0;//0x00010000>>2; // 1.0 units in x	
static s32 g_fscrollydir = 0x00010000>>1; // 1.0 units in y	

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
static s16 g_viewport_worldy = 0;//c_scroll_safety_margin;

// clipping margin outside of the scrolling viewport, to accommodate the largest typical sprite before deletion etc.
static const s16 c_viewport_xmargin = playfield_t::c_guardx;
static const s16 c_viewport_ymargin = playfield_t::c_guardy;

// pseudorandom number generator
mersenne_twister prng;

// ---------------------------------------------------------------------------------------------------------------------
// static pointers to significant entities (still these must be created the usual way)

// viewport entity is global and part of the entity drawing system
static entity_t *s_pe_viewport = NULL;

drawcontext_t drawcontext;

// ---------------------------------------------------------------------------------------------------------------------

// playfield tile assets (x2 in dual-field mode)
tileset world_layer1_tiles0;
tileset world_layer1_tiles1;
tileset world_layer2_tiles0;
tileset world_layer2_tiles1;

// playfield map assets (x2 in dual-field mode)
worldmap world_layer1_map;
worldmap world_layer2_map;
worldmap tree_anim_map;

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
	printf("16x16 2-layer map with tile animation\n");

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
		world_layer1_tiles0.load_cct("map0.cct");
		world_layer1_tiles1.load_cct("map1.cct");
		world_layer2_tiles0.load_cct("mapo0.cct");
		world_layer2_tiles1.load_cct("mapo1.cct");

		// ---------------------------------------------------------------------------------------------------------------------
		//	load maps
		
		printf("loading maps...\n");

		// layer1 (main background) map loaded from disk
		world_layer1_map.load_ccm("map.ccm");
		// layer2 (transparent overlay) map created as empty tilemap 
		world_layer2_map.create(world_layer1_map.getwidth(), world_layer1_map.getheight(), 4, true, 0);

		// tree (animation library) map loaded from disk
		tree_anim_map.load_ccm("mapo.ccm");

		// get map tile dimensions (x16 -> in pixels) to help keep scroll inside the map limits
		int map_xtiles = world_layer1_map.getwidth();
		int map_ytiles = world_layer1_map.getheight();
		int map_xsize = map_xtiles << playfield_t::c_tilestep_;
		int map_ysize = map_ytiles << playfield_t::c_tilestep_;

		// scroll starting point (fixedpoint)
		g_fscrollx = 16<<16;
		g_fscrolly = 0;//c_scroll_safety_margin<<16;

		// max scroll is map size minus screen dimensions (plus safety margin)
		g_map_fxlimit = ((map_xsize/1)-VIEWPORT_XSIZE/*-c_scroll_safety_margin*/)<<16;//256<<16;//192<<16;
		g_map_fylimit = ((map_ysize/1)-VIEWPORT_YSIZE/*-c_scroll_safety_margin*/)<<16;

		// ---------------------------------------------------------------------------------------------------------------------
		//	create the double-buffered 'world', set the viewport dimensions and connect it to the shifter interface

		printf("creating world...\n");
		arena_t myworld(shift, SCREEN_XSIZE, SCREEN_YSIZE);
		g_pworld = &myworld;

		// ---------------------------------------------------------------------------------------------------------------------
		//	associate map & tiles with the world buffers.
		//	Q: why do we bother to do this manually? 
		//	A: because in dual-field mode each display field can have a different (dithered) tile library
	
		printf("assigning world map...\n");
		myworld.setmap(&world_layer1_map);
		myworld.setoverlaymap(&world_layer2_map);

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
				{
					myworld.settiles(&world_layer1_tiles1, &world_layer1_tiles1);		// odd buffers
					myworld.setoverlaytiles(&world_layer2_tiles1, &world_layer2_tiles1);
				}
				else
				{
					myworld.settiles(&world_layer1_tiles0, &world_layer1_tiles0);		// even buffers
					myworld.setoverlaytiles(&world_layer2_tiles0, &world_layer2_tiles0);
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
				myworld.settiles(&world_layer1_tiles0, &world_layer1_tiles1);
				myworld.setoverlaytiles(&world_layer2_tiles0, &world_layer2_tiles1);
			}
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
			shift.setcolour(0, 0, c, world_layer1_tiles0.getcol(c));
			shift.setcolour(0, 1, c, world_layer1_tiles1.getcol(c));	// only needed in dual-field mode

			// physic (current)
			shift.setcolour(1, 0, c, world_layer1_tiles0.getcol(c));
			shift.setcolour(1, 1, c, world_layer1_tiles1.getcol(c));	// only needed in dual-field mode
		}

		s16 buffer_index = 0;

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

		// convert fixedpoint to screen coords
		g_viewport_worldx = g_fscrollx>>16;
		g_viewport_worldy = g_fscrolly>>16;

		myworld.reset(g_viewport_worldx, g_viewport_worldy);

		// ---------------------------------------------------------------------------------------------------------------------
		// create the viewport so we can track its relationship with everything else

		s_pe_viewport = EntitySpawn(EntityType_VIEWPORT, 0, 0);

		// select this as the active viewport
		EntitySelectViewport(s_pe_viewport);

		// ---------------------------------------------------------------------------------------------------------------------
		// create 'growing' trees, managed as map edits to 2nd playfield layer by invisible entities
		// tick function for each tree will copy a small map rectangle from

		entity_t *pe;

		pe = EntitySpawn(EntityType_TREE, 80 &-16, 48 &-16);
		pe = EntitySpawn(EntityType_TREE, 32 &-16, 190 &-16);
		pe = EntitySpawn(EntityType_TREE, 256 &-16, 156 &-16);
		pe = EntitySpawn(EntityType_TREE, 270 &-16, 360 &-16); 
		pe = EntitySpawn(EntityType_TREE, 112 &-16, 460 &-16); 
		pe = EntitySpawn(EntityType_TREE, 128 &-16, 512 &-16);
		pe = EntitySpawn(EntityType_TREE, 183 &-16, 533 &-16);

		pe = EntitySpawn(EntityType_TREE, 56 &-16, 580 &-16);
		pe = EntitySpawn(EntityType_TREE, 56 &-16, 790 &-16);

		pe = EntitySpawn(EntityType_TREE, 368 + 48, 736);
		pe = EntitySpawn(EntityType_TREE, 368 + 96, 736);
		pe = EntitySpawn(EntityType_TREE, 368, 736 + 48);
		pe = EntitySpawn(EntityType_TREE, 368 + 48, 736 + 48);
		pe = EntitySpawn(EntityType_TREE, 368 + 96, 736 + 48);

		pe = EntitySpawn(EntityType_TREE, 128 + 48, 936 & -16);

		pe = EntitySpawn(EntityType_TREE, 48, 1142);
		pe = EntitySpawn(EntityType_TREE, 108, 1097);
		pe = EntitySpawn(EntityType_TREE, 146, 1136);

		pe = EntitySpawn(EntityType_TREE, 275, 1166);
		pe = EntitySpawn(EntityType_TREE, 324, 1133);
		pe = EntitySpawn(EntityType_TREE, 312, 1253);
		pe = EntitySpawn(EntityType_TREE, 408, 1175);
		pe = EntitySpawn(EntityType_TREE, 448, 1120);

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

			shift.wait_vbl();

			s16 field_index = g_vbl;

			// ---------------------------------------------------------------------------------------------------------------------
			// manually update the viewport first

			s_pe_viewport->rx = g_viewport_worldx - c_viewport_xmargin;
			s_pe_viewport->ry = g_viewport_worldy - c_viewport_ymargin;
	

			// ---------------------------------------------------------------------------------------------------------------------
			//	tick all currently linked objects

			EntityTickAll();

			// ---------------------------------------------------------------------------------------------------------------------
			//	perform object-object interactions (collision processing)

			//EntityInteractVisible(VIEWPORT_XSIZE, c_viewport_xmargin, VIEWPORT_YSIZE, c_viewport_ymargin);

			// ---------------------------------------------------------------------------------------------------------------------
			//	link any new entities created during [tick] pass. ensures they get drawn in their spawned states *before* next tick

			EntityExecuteAllLinks();

			// ---------------------------------------------------------------------------------------------------------------------
			//	physically remove any entities that were queued for killing during the AI tick pass

			EntityExecuteAllRemoves();

			// ---------------------------------------------------------------------------------------------------------------------
			//	update the background & scroll

			// select the next workbuffer (..of two - we're using double buffering here)
			myworld.select_buffer((buffer_index & 1));

			// ---------------------------------------------------------------------------------------------------------------------
			// select the working 50hz field (odd/even) from the currently active playfield buffer (back)
			// in single-field config, this is always 0
			// in dual-field FPS<=25hz config, its 0 or 1 for even/odd VBLs (4 buffers: front/back + even/odd)
			// in dual-field 50hz config, it's always 0, because front/back handles our even/odd fields

			s16 working_field = (buffer_index & 1);


			// restore playfield from any previous drawing
			myworld.bgrestore();

			// update the playfield coordinates for this draw pass
			myworld.setpos(g_viewport_worldx, g_viewport_worldy);

			// fill any background tiles required by this change in coordinates
			myworld.hybrid_fill(false);

			// ---------------------------------------------------------------------------------------------------------------------
			// retrieve playfield [drawcontext] structure for sharing with low-level drawing functions

			myworld.get_context(drawcontext, working_field, VIEWPORT_XSIZE, VIEWPORT_YSIZE);

			// ---------------------------------------------------------------------------------------------------------------------
			//	draw 'all the things'

			//EntityDrawVisible(&drawcontext);
				
			// ---------------------------------------------------------------------------------------------------------------------
			//	activate the new workbuffer as the next shifter state so it will be presented on the next VBLank

			myworld.activate_buffer();

			// ---------------------------------------------------------------------------------------------------------------------
			//	advance workbuffer (i.e. double buffering of playfields)

			buffer_index++;

			// ---------------------------------------------------------------------------------------------------------------------
			// update scroll coords

			static s16 s_delaycount = 1024;
			if (--s_delaycount == 0)
				g_fscrollxdir = 0x00010000 >> 2;

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

			if ((g_fscrolly < 0/*((s32)c_scroll_safety_margin<<16)*/))
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

// source (tilemap) coords for upper-left of each animation frame, per tree type

int s_tree1[8][2] =
{
//	{ srcx, srcy }
	{ 0, 0 + 3 },
	{ 0, 3 + 3 },
	{ 0, 6 + 3 },
	{ 0, 9 + 3 },
	{ 0,12 + 3 },
	{ 0, 9 + 3 },
	{ 0, 6 + 3 },
	{ 0, 3 + 3 },
};

int s_tree2[8][2] =
{
//	{ srcx, srcy }
	{ 3, 0 + 3 },
	{ 3, 3 + 3 },
	{ 3, 6 + 3 },
	{ 3, 9 + 3 },
	{ 3,12 + 3 },
	{ 3, 9 + 3 },
	{ 3, 6 + 3 },
	{ 3, 3 + 3 },
};

int s_tree3[8][2] =
{
//	{ srcx, srcy }
	{ 6, 0 + 3 },
	{ 6, 3 + 3 },
	{ 6, 6 + 3 },
	{ 6, 9 + 3 },
	{ 6,12 + 3 },
	{ 6, 9 + 3 },
	{ 6, 6 + 3 },
	{ 6, 3 + 3 },
};

int s_tree4[8][2] =
{
//	{ srcx, srcy }
	{ 9, 0 + 3 },
	{ 9, 3 + 3 },
	{ 9, 6 + 3 },
	{ 9, 9 + 3 },
	{ 9,12 + 3 },
	{ 9, 9 + 3 },
	{ 9, 6 + 3 },
	{ 9, 3 + 3 },
};

int s_tree5[12][2] =
{
//	{ srcx, srcy }
	{ 0,12 + 3 },
	{ 3,12 + 3 },
	{ 6,12 + 3 },
	{ 9,12 + 3 },
	{ 6,15 + 3 },
	{ 3,15 + 3 },
	{ 0,15 + 3 },
	{ 3,15 + 3 },
	{ 6,15 + 3 },
	{ 9,12 + 3 },
	{ 6,12 + 3 },
	{ 3,12 + 3 },
};

struct treetypes_s
{
	int frames;			// number of frames in this animation
	int (*sequence)[2]; // pointer to frame tile positions (x,y tile offsets into map)
};

// library of different tree type animations
treetypes_s s_trees[] =
{
//	{ framecount, framesequence }
	{ 8, s_tree1 },	// pad it out to 8 types but only 5 distinct ones
	{ 8, s_tree2 },
	{ 8, s_tree3 },
	{ 8, s_tree4 },
	{12, s_tree5 },
	{ 8, s_tree1 },
	{ 8, s_tree3 },
	{ 8, s_tree5 },
};

// a shared tick function for tree background objects
void tree_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// process animation every 64'th frame
	if (--self.counter <= 0)
	{
		if (self.counter < 0)
		{
			// on first trigger (transition from 0 -> -1) we set up a random period
			// so the trees don't all animate on the same synchronized frame
			// they will regularly update every 256 frames but initially wait up to 512 frames before anything happens
			self.counter = prng.genU32() & 0x1FF;
		}
		else
			self.counter = 256;  // on subsequent triggers, fixed period for each tree.
								// they are already offset from each other by up to 512 frames!

		int treetype = self.id & (8-1); // use entity's ID to select a treetype

		// limit updates to entities near or inside viewport
		if (self.rx >= g_viewport_worldx-256 && self.rx < g_viewport_worldx + VIEWPORT_XSIZE+256 &&
			self.ry >= g_viewport_worldy-256 && self.ry < g_viewport_worldy + VIEWPORT_YSIZE+256)
		{
			// get animation sequence for this treetype
			int (*sequence)[2] = s_trees[treetype].sequence;

			// map source coords for this animation frame
			int sx = sequence[self.frame][0];
			int sy = sequence[self.frame][1];

			// copy map rectangle from loaded map asset to active playfield
			g_pworld->multimod_copy<PFLayer_Overlay>
			(
				/*sx=*/sx,
				/*sy=*/sy,
				/*dx=*/self.rx >> 4,
				/*dy=*/self.ry >> 4,
				/*w=*/3,
				/*h=*/3,
				/*source=*/tree_anim_map
			);
		}

		self.frame++;
		if (self.frame >= s_trees[treetype].frames) // loop animation
			self.frame = 0;
	}
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
	// tick,collide,asset,hidden				px/y,vx/y,sx/y,ox/y				tickpri,drflag	h,d,frame,counter	drawtype,drawlayer		f_self,f_interactswith	

	// engine objects...

	// viewport																					// viewport has no tick
	// Q: why? A: because it's a rectangle and we can determine what is inside it quickly if it is also an entity which moves through the world
	{ 0,0,0,0,									{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,0,		EntityFlag_VIEWPORT|EntityFlag_SPATIAL,	0 },

	{ &tree_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,0,		EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },
};

//----------------------------------------------------------------------------------------------------------------------
