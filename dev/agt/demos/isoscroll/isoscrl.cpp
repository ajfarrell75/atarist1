//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	example: simulated isometric scrolling
//----------------------------------------------------------------------------------------------------------------------
//	controls: none
//----------------------------------------------------------------------------------------------------------------------
//	notes:
//		- entities use extended 3D coordinates (wx,wy,wz) (vx,vy,vz)
//		  which are translated into 2D playfield coords (rx,ry) during each tick
//		- the map is still drawn from typical 16x16 tiles (not isometric tiles!)
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
	EntityType_VILLAGER,
	
	// end!
	EntityType_MAX_
};


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
	/*max_scroll=*/4096,						// maximum scroll distance in pixels (reduce to save a little ram on tables)
	/*vscroll=*/VScrollMode_LOOPBACK,			// enable full vscroll
	/*hscroll=*/HScrollMode_SCANWALK,			// enable full hscroll
	/*restore=*/RestoreMode_PAGERESTORE,		// restore from virtual page (verus save/restore for individual objects)
	/*update=*/UpdateMode_NORMAL,
	/*hardware=*/PFH_Defaults,
	/*attributes=*/PFA_UpdateFullTiles			// use normal tile repainting, not direction-speculative. safely supports (x,y)==(0,0)

> playfield_t;									// new name for our playfield definition

// define an arena (world) composed of 2 such playfields (double-buffering on STE)
typedef arena_template
<
	/*buffers=*/4,								// number of framebuffers (playfields) required
	/*singleframe=*/c_singlevbl ? true : false,	// optimise dualfield to half the framebuffer count, when locked @ 50hz
	/*dualfield=*/true,							// enable dualfield support at compiletime, otherwise on/off flag will be ignored
	/*playfield_type=*/playfield_t				// playfield config/definition to use for framebuffers
> arena_t;	

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

// viewport 3D world coordinates
static s32 g_fworldx = 100 << 16;
static s32 g_fworldy = 480 << 16;
static s32 g_fworldz = 0;

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
entity_t *s_pe_viewport = NULL;

drawcontext_t drawcontext;

// ---------------------------------------------------------------------------------------------------------------------

// sprite assets

// villager
spritesheet villager_asset;

// playfield tile assets (x2 in dual-field mode)
tileset mytiles0;
tileset mytiles1;

// playfield map assets
worldmap mymap0;

// ---------------------------------------------------------------------------------------------------------------------
// convert 3D world (wx,wy,wz) position to 2:1 isometric 2D screen (x,y) position

static agt_inline void Iso2DFrom3D(const s32 _wx, const s32 _wy, const s32 _wz, s32 &r_x, s32 &r_y)
{
	r_x = (_wy + _wx);
	r_y = ((_wy - _wx) - _wz) >> 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// convert 2D isometric (x,y) screen position to 3D world (wx,wy,wz) position
// note: assumes ground height (wz = 0)

static agt_inline void Iso3DFrom2D(const s32 _x, const s32 _y, s32 &r_wx, s32 &r_wy, s32 &r_wz)
{
	s32 yy = _y + _y;
	r_wx = (_x - yy) >> 1;
	r_wy = yy - r_wx;
	r_wz = 0;
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
	printf("Isometric scroll (STE)\n");

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

		mytiles0.load_cct("isoscrl0.cct");
		// only load 2nd tileset if interlacing/dual-field
		if (c_dualfield)
			mytiles1.load_cct("isoscrl1.cct");

#if defined(TIMING_RASTERS_MAIN)
		// save copy of BG colour for rasters
		s16 bgcol = mytiles0.getcol(0);
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map
		
		printf("loading map...\n");
		mymap0.load_ccm("isoscrl.ccm");

		// get map tile dimensions (x16 -> in pixels) to help keep scroll inside the map limits
		int map_xsize = mymap0.getwidth() << 4;
		int map_ysize = mymap0.getheight() << 4;

		// max scroll is map size minus screen dimensions (plus safety margin)
		g_map_fxlimit = (map_xsize-VIEWPORT_XSIZE-c_scroll_safety_margin)<<16;
		g_map_fylimit = (map_ysize-VIEWPORT_YSIZE-0)<<16;

		// find the 3D world coords representing the middle of the 2D screenspace map
		Iso3DFrom2D
		(
			(g_map_fxlimit>>1), (g_map_fylimit>>1), 
			g_fworldx, g_fworldy, g_fworldz
		);

		// ---------------------------------------------------------------------------------------------------------------------
		//	create the double-buffered 'world', set the viewport dimensions and connect it to the shifter interface

		printf("creating world...\n");
		arena_t myworld(shift, SCREEN_XSIZE, SCREEN_YSIZE);

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
		//	note: using EMXSPR sprite format

		// villager sprite
		villager_asset.load("villager.emx");

		// ---------------------------------------------------------------------------------------------------------------------
		//	tie gfx assets into entity type dictionary, so we don't need to do it every time we spawn an object

		// note: asset binding could be done automatically via the dictionary but its useful to be able to override sizes etc.
		// and its better not to hide asset assignment steps in a system with simple memory management.

		EntityDefAsset_Sprite(EntityType_VILLAGER, &villager_asset);

		// ---------------------------------------------------------------------------------------------------------------------
		//	init world, set viewport initial position


		// convert 3D world to 2D iso coords (all fixedpoint)
		Iso2DFrom3D(g_fworldx,g_fworldy,g_fworldz, g_fscrollx,g_fscrolly);

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
		//	reset the player & scrolling playfield

		// player coords within viewport
		g_player_viewx = 160;
		g_player_viewy = 100;

		// convert 3D world to 2D iso coords (all fixedpoint)
		Iso2DFrom3D(g_fworldx,g_fworldy,g_fworldz, g_fscrollx,g_fscrolly);

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

		// ---------------------------------------------------------------------------------------------------------------------
		//	fill the map with bouncy villagers!

		for (int e = 0; e < 40; e++)
		{
			// make the entity first
			// note: the first tick of the entity will incur a spike cost as
			// the real coordinates are applied and the scene is sorted-in. this
			// happens because we spawned at 0,0 2D and forced a big change in 
			// position later. an isometric-specific spawn would be better here.
			entity_t *pent = EntitySpawn(EntityType_VILLAGER, 0, 0);

			s32 vx, vy, x, y, z;

			// they're all on the groud, to start with at least
			z = 0;

			// generate tons of random 3D coordinates and keep only those 
			// with a 3D->2D translation that lands safely inside the viewport
			// ...so so lazy!
			do
			{
				x = prng.genS32();
				y = prng.genS32();

				// convert 3D to isometric 2D screen coordinates and test the result
//				vx = (x + y);
//				vy = ((y - x) - z) >> 1;

				Iso2DFrom3D(x,y,z, vx,vy);

				// flashy colours to show we're busy
				reg16(FFFF8240) += 0x315;
			}
			while ((vx < 0) || (vx > g_map_fxlimit) || 
				   (vy < 0) || (vy > g_map_fylimit));

			// this coord is visible, so keep it
			pent->fwx = x;
			pent->fwy = y;
			pent->fwz = z;
			pent->fvz = 0;

			// give the entity a nonzero direction vector on one axis only
			// so it can walk around the grid. we'll rotate his vector by 90'
			// at intervals so it walks in a square
			if ((prng.genS32() >> 3) & 1)
				pent->fvx = xsign(prng.genS32()) << 15;
			else
				pent->fvy = xsign(prng.genS32()) << 15;

			Iso2DFrom3D(pent->fwx,pent->fwy,pent->fwz, pent->frx,pent->fry);

		}

		// =====================================================================================================================
		//	must link any pending entities before starting mainloop (viewport is an entity and viewport scans rely on it)

		EntityExecuteAllLinks();

		// =====================================================================================================================
		//	mainloop
		// =====================================================================================================================
	
mainloop:

			// ---------------------------------------------------------------------------------------------------------------------
			// convert viewport 3D world to 2D iso coords (all fixedpoint)

			Iso2DFrom3D(g_fworldx,g_fworldy,g_fworldz, g_fscrollx,g_fscrolly);

			// ---------------------------------------------------------------------------------------------------------------------
			//	convert fixedpoint coordinates to playfield coordinates
			g_viewport_worldx = g_fscrollx>>16;
			g_viewport_worldy = g_fscrolly>>16;

			// ---------------------------------------------------------------------------------------------------------------------
			//	wait for the current VBL period to elapse
			//	note: this simplistic approach will cause a hard drop to 25hz if time runs out but we're aiming for 50hz anyway

			TIMING_MAIN(bgcol);

			// wait at least 1vbl but not necessarily more (allow frameslip)
			static s16 s_vbl_counter = g_vbl;
			shift.wait_vbl_minbound(s_vbl_counter, 1);

			s16 field_index = g_vbl;
			//while (g_vbl == field_index) { }


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

//			EntityInteractVisible(VIEWPORT_XSIZE, c_viewport_xmargin, VIEWPORT_YSIZE, c_viewport_ymargin);

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
			myworld.select_buffer((buffer_index & 3));

			// update the playfield coordinates for this draw pass
			myworld.setpos(g_viewport_worldx, g_viewport_worldy);

			// restore background from any previous drawing
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

			// advance the world scroll until we hit the end of the map, then turn around!
			// note: this *should*  be done by a viewport tick, with appropriate tick priority
			// where the viewport entity is using proper isometric coords too.

			{
				g_fworldx += g_fscrollxdir;
				g_fworldy += g_fscrollydir;

				// we're scrolling in 3D, but we need to test the 2D map bounds - so we'll convert into 2D coords to test
				s32 ftestx, ftesty;
				Iso2DFrom3D(g_fworldx,g_fworldy,g_fworldz, ftestx,ftesty);

				if (ftestx < ((s32)c_scroll_safety_margin<<16))
				{
					// bounce off the left side of the map
					g_fscrollxdir = -g_fscrollxdir;
					g_fworldx += g_fscrollxdir << 2;

					// rotate world scroll by 90'
					s32 tmp = g_fscrollxdir;
					g_fscrollxdir = -g_fscrollydir;
					g_fscrollydir = tmp;
				}
				else
				if (ftestx >= g_map_fxlimit)
				{
					// bounce off the right side of the map
					g_fscrollxdir = -g_fscrollxdir;
					g_fworldx += g_fscrollxdir << 2;

					// rotate world scroll by 90'
					s32 tmp = g_fscrollxdir;
					g_fscrollxdir = -g_fscrollydir;
					g_fscrollydir = tmp;
				}

				if (ftesty < ((s32)0<<16))
				{
					// bounce off the top side of the map
					g_fscrollydir = -g_fscrollydir;
					g_fworldy += g_fscrollydir << 2;

					// rotate world scroll by 90'
					s32 tmp = g_fscrollxdir;
					g_fscrollxdir = -g_fscrollydir;
					g_fscrollydir = tmp;
				}
				else
				if (ftesty >= g_map_fylimit)
				{
					// bounce off the bottom side of the map
					g_fscrollydir = -g_fscrollydir;
					g_fworldy += g_fscrollydir << 2;

					// rotate world scroll by 90'
					s32 tmp = g_fscrollxdir;
					g_fscrollxdir = -g_fscrollydir;
					g_fscrollydir = tmp;
				}
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
// tick: villager

static const s8 animations_for_directions[] =
{
	//LL
	2, 3, 4, 3, 2, 1, 0, 1,
	//LR
	7, 8, 9, 8, 7, 6, 5, 6,
	//UR
	12,13,14,13,12,11,10,11,
	//UL
	17,18,19,18,17,16,15,16,
};

void villager_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// sleep when offscreen
	u16 spx = self.rx - (g_viewport_worldx-32);
	u16 spy = self.ry - (g_viewport_worldy-32);
	if ((spx > (VIEWPORT_XSIZE+32)) || (spy > (VIEWPORT_YSIZE+64)))
		return;

	s16 s = (self.id + self.counter++);

	if ((s & 1) == 0)
	{
		s16 walk = (s >> 1) & 7;

	//	\ 0,-1  1,0 a=3
	//	/ 1,0  2,1  a=2
	//	\ 0,1  1,2  a=1
	//	/ -1,0  0,1 a=0
		s16 angle = tsign16(self.fvx>>2)+1;
		if (self.vy < 0) 
			angle = 3;

		self.frame = animations_for_directions[(angle << 3) + walk];

		// cycle of 127 with id-offset trigger
		if ((s & 127) == 0)
		{
			// rotate direction by 90'
			{
				s32 vx = self.fvx;
				s32 vy = self.fvy;
				self.fvx = -vy;
				self.fvy = vx;
			}

			// jump every 64'th frame (but each entity is async with others)
			if ((s & 255) == 0)
				self.fvz = 0x00030000;
		}
	}
	
	// move on z axis (height)
	self.fwz += self.fvz;
	if (self.fwz > 0)
	{
		// fall back under gravity
		self.fvz -= 0x00004000;
	}
	else
	{
		// hit ground & stop
		self.fwz = 0;
		self.fvz = 0;
	}

	// move on iso grid axis
	self.fwx += self.fvx;
	self.fwy += self.fvy;

	// convert 3D to isometric 2D screen coordinates
	Iso2DFrom3D(self.fwx,self.fwy,self.fwz, self.frx,self.fry);
//	self.frx = (self.fwx + self.fwy);
//	self.fry = ((self.fwy - self.fwx) - self.fwz) >> 1;
}

// =====================================================================================================================
//	collision reaction functions
// =====================================================================================================================

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: player vs other

void villager_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;
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

	// villager objects...

	// villager
	//	summary:
	//	- bouncy, run, bouncy!
	{ &villager_fntick,&villager_fncol,0,0,		{0},{0},{0},{0},0,0,0,0,		0,0,			30,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_PLAYER|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },

};

//----------------------------------------------------------------------------------------------------------------------
//	Music fixup callback: opportunity to set the instruments and multiplex routing for specific songs
//----------------------------------------------------------------------------------------------------------------------

#if defined(ENABLE_AGT_AUDIO)
void AGT_MusicFixupCallback(const char* const _name, remap_ft &remap_func)
{
}
#endif // defined(ENABLE_AGT_AUDIO)
