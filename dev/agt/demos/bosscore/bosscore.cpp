//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	example: use of EMH for end-of-level boss
//----------------------------------------------------------------------------------------------------------------------
//	controls:
//		- WSAD keys to move around
//		- RSHFT key to shoot
//----------------------------------------------------------------------------------------------------------------------
//	notes:
//----------------------------------------------------------------------------------------------------------------------

// enable this to switch to older SLAB method: uses a lot less memory, slower drawing.
//#define BOSS_USE_SLAB_VS_EMX

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

// ---------------------------------------------------------------------------------------------------------------------7

//#include "tables/sintab32.h"

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
	EntityType_PLAYER,

	// other
	EntityType_BOSS,
	EntityType_BOSSEYE,
	EntityType_BOSSPULP,
	EntityType_BOSSSHIELD,
	EntityType_BOSSBEAM,
	EntityType_BOSSBALL,

	// various weapons
	EntityType_BULLET,						// bullet
	
	// end!
	EntityType_MAX_
};

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
static const s16 c_singlevbl = 0;


// define a playfield config for horizontal-only scrolling using STE HW HSCROLL
typedef playfield_template
<
	/*tilesize=*/4,								// 2^4 for 16x16 bg tiles
	/*max_scroll=*/2880,						// maximum scroll distance in pixels (reduce to save a little ram on tables)
	/*vscroll=*/VScrollMode_NONE,				// disable vscroll
	/*hscroll=*/HScrollMode_SCANWALK,			// enable full hscroll
	/*restore=*/RestoreMode_PAGERESTORE			// restore from virtual page (verus save/restore for individual objects)

> playfield_t;									// new name for our playfield definition

// define an arena (world) composed of 2 such playfields (double-buffering on STE)
typedef arena_template
<
	/*buffers=*/2,								// number of framebuffers (playfields) required
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
static s32 g_fscrollx = c_scroll_safety_margin<<16;
static s32 g_fscrolly = 0;
static s32 g_fscrollxdir = 0x00010000>>1; // 1.0 units in x	

// xlimit at which map stops and changes scroll direction
static s32 g_map_fxlimit = 0;
static s32 g_map_fylimit = 0;

// fraction bits in tilt-position/tilt-speed, for gradual tilt animation as ship moves up/down
static const s16 c_player_tilt_fracbits = 2;

// player craft tilt angle. for visual effect only.
static s16 g_player_tilt = 0;

// player viewport coordinates (local, control coordinates, relative to current screen position)
static s16 g_player_viewx = 160+16;
static s16 g_player_viewy = 100;

// player world coordinates (created from local coords, for interactions with world & other objects)
static s16 g_player_worldx = 0;
static s16 g_player_worldy = 0;

// complete hack to spawn different entities at regular intervals. this is not typical of a real game.
static s16 g_hack_countdown = 0;

// viewport (scroll) starting position in world map
static s16 g_viewport_worldx = 32;
static s16 g_viewport_worldy = 0;

// some margin outside of the scrolling viewport, to accommodate the largest typical sprite before deletion etc.
static const s16 c_viewport_xmargin = playfield_t::c_guardx;
static const s16 c_viewport_ymargin = playfield_t::c_guardy;

// artificial countdown after player dies, to allow time after death animation
static s16 g_deathdelay = 0;

// always-incrementing counter, used for powerup drops etc. caution: increments per use, so irregular intervals.
static s16 g_sequence = 0;

// cheap PRNG random source, only changes once per frame
static s16 g_frame_prng = 0;

// ---------------------------------------------------------------------------------------------------------------------
// static pointers to significant entities (still these must be created the usual way)

// viewport entity is global and part of the entity drawing system
entity_t *s_pe_viewport = NULL;

entity_t *g_pe_boss = NULL;

// useful to have direct access to player-related entities 
static entity_t *g_pe_player = NULL;

static drawcontext_t drawcontext;

// ---------------------------------------------------------------------------------------------------------------------

// sprite assets

// player
spritesheet player_asset;
spritesheet bullet_asset;

#if defined(BOSS_USE_SLAB_VS_EMX)
slabsheet bossdraw_asset;
#else
spritesheet bossdraw_asset;
#endif
slabsheet bossclear_asset;
spritesheet bossbeam_asset;
spritesheet bosseye_asset;
spritesheet bosspulp_asset;
spritesheet bossball_asset;

// playfield tile assets (x2 in dual-field mode)
tileset mytiles0;
tileset mytiles1;

// playfield map assets
worldmap mymap0;

int map_xsize;
int map_ysize;

s16 buffer_index = 0;
arena_t *g_pworld = 0;
static s16 s_bgcol = 0;

extern "C" void menu_vblservice()
{
mainloop:

			// ---------------------------------------------------------------------------------------------------------------------
			//	convert fixedpoint coordinates to playfield coordinates
			g_viewport_worldx = g_fscrollx>>16;
			g_viewport_worldy = g_fscrolly>>16;

			// ---------------------------------------------------------------------------------------------------------------------
			// no need to wait for vbl - we're using a vbl-based foreground thread

			s16 field_index = g_vbl;

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
			g_pworld->select_buffer((buffer_index & 1));

			// update the playfield coordinates for this draw pass
			g_pworld->setpos(g_viewport_worldx, g_viewport_worldy);

			// restore playfield from any previous drawing
			g_pworld->bgrestore();

			// fill any background tiles required by this change in coordinates
			g_pworld->hybrid_fill(false);

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

			g_pworld->get_context(drawcontext, 0, VIEWPORT_XSIZE, VIEWPORT_YSIZE);

			TIMING_MAIN(0x753);	


			// ---------------------------------------------------------------------------------------------------------------------
			//	now we're ready to draw stuff for the next frame

			// animation time (todo: this is part of the 'options' bodge below, otherwise it would be stored in the entity)
			g_frame_prng += 19723;
			g_frame_prng *= (s16)28691;

			// ---------------------------------------------------------------------------------------------------------------------
			//	draw 'all the things'
			//	todo: should be using drawcontext instead of passing all those args...

			EntityDrawVisible(&drawcontext);

			TIMING_MAIN(0x0f0);	
				
			// ---------------------------------------------------------------------------------------------------------------------
			//	activate the new workbuffer as the next shifter state so it will be presented on the next VBLank

			g_pworld->activate_buffer();

			// ---------------------------------------------------------------------------------------------------------------------
			//	advance workbuffer (i.e. double buffering of playfields)
			buffer_index++;

			TIMING_MAIN(0x505);	

			// ---------------------------------------------------------------------------------------------------------------------
			// advance the horizontal scroll until we hit the end of the map, then turn around!
			// note: this *should*  be done by a viewport tick, with appropriate tick priority
			// where the viewport entity is using proper isometric coords too.

			// advance scroll
			g_fscrollx += g_fscrollxdir;
//			g_fscrolly += g_fscrollydir;

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


			TIMING_MAIN(s_bgcol);	

			// ---------------------------------------------------------------------------------------------------------------------
			//	any remaining CPU time here can be put to good use
}

// =====================================================================================================================
//	program start
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
	printf("Boss Core (STE)\n");

	// ---------------------------------------------------------------------------------------------------------------------
	//	using BLiTTER slabs requires a one-time precalc during startup
	//	IMORTANT: required for both .sls slabs and .slr clearing primitives, even if used as a custom clear for EMXUR!

	AGT_BLiT_SlabSetupOnce();

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

		pprintf("loading tiles...\n");
		mytiles0.load_cct("bosscore.cct");
		// only load 2nd tileset if interlacing/dual-field
		//if (c_dualfield)
		//	mytiles1.load_cct("bosscor1.cct");

#if defined(TIMING_RASTERS_MAIN)
		// save copy of BG colour for rasters
		s_bgcol = mytiles0.getcol(0);
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map
		
		pprintf("loading map...\n");
		mymap0.load_ccm("bosscore.ccm");

		// get map tile dimensions (x16 -> in pixels) to help guide the demo around the map limits
		map_xsize = mymap0.getwidth() << 4;
		map_ysize = mymap0.getheight() << 4;

		// start at left of map
		g_fscrollx = c_scroll_safety_margin<<16;
		g_fscrolly = 0;

		// max scroll is map size minus screen dimensions (plus safety margin)
		g_map_fxlimit = (map_xsize-VIEWPORT_XSIZE-c_scroll_safety_margin)<<16;
		g_map_fylimit = 0;


		// ---------------------------------------------------------------------------------------------------------------------
		//	create the double-buffered 'world', set the viewport dimensions and connect it to the shifter interface

		pprintf("creating world...\n");
		arena_t myworld(shift, SCREEN_XSIZE, SCREEN_YSIZE);

		g_pworld = &myworld;

		// ---------------------------------------------------------------------------------------------------------------------
		//	associate map & tiles with the world buffers.
		//	Q: why do we bother to do this manually? 
		//	A: because in dual-field mode each display field can have a different (dithered) tile library
	
		pprintf("assigning world map...\n");
		myworld.setmap(&mymap0);

		pprintf("assigning world tiles...\n");
		for (int b = myworld.c_num_buffers_-1; b >= 0; b--)
		{
			myworld.select_buffer(b);
			pprintf("assigning buffer %d world tileset...\n", b);

			// what kind of dual-field drawing do we want?
/*			if (c_dualfield && c_singlevbl)
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
*/			{
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
		//	note: using EMXSPR sprite format for speed

		// player craft & engines, multiples, other fx
		player_asset.load("vicviper.emx");

		// boss
	#if defined(BOSS_USE_SLAB_VS_EMX)
		bossdraw_asset.load_sls("bosscore.sls");
	#else
		bossdraw_asset.load("bosscore.emx");
	#endif
		bossclear_asset.load_sls("bosscore.slr");

		// boss parts
		bosseye_asset.load("bosseye.emx");
		bosspulp_asset.load("bosspulp.emx");

		// boss weapons
		bossbeam_asset.load("beam.emx");
		bossball_asset.load("balls.emx");

		// weapons
		bullet_asset.load("bullet.emx");

		// ---------------------------------------------------------------------------------------------------------------------
		//	tie gfx assets into entity type dictionary, so we don't need to do it every time we spawn an object

		// note: asset binding could be done automatically via the dictionary but its useful to be able to override sizes etc.
		// and its better not to hide asset assignment steps in a system with simple memory management.

		EntityDefAsset_Sprite(EntityType_PLAYER, &player_asset);
		EntityDefAsset_Sprite(EntityType_BULLET, &bullet_asset);

#if defined(BOSS_USE_SLAB_VS_EMX)
		sls_pair boss_assetpair;
		boss_assetpair.assign(bossdraw_asset.get(), bossclear_asset.get());
		EntityDefAsset_Slab(EntityType_BOSS, &boss_assetpair);
#else
		sps_pair boss_assetpair;
		boss_assetpair.assign(bossdraw_asset.get(), bossclear_asset.get());
		EntityDefAsset_SpriteProxy(EntityType_BOSS, &boss_assetpair);
#endif
		EntityDefAsset_Sprite(EntityType_BOSSBEAM, &bossbeam_asset);
		EntityDefAsset_Sprite(EntityType_BOSSBALL, &bossball_asset);
		EntityDefAsset_Sprite(EntityType_BOSSEYE, &bosseye_asset);
		EntityDefAsset_Sprite(EntityType_BOSSPULP, &bosspulp_asset);

		// reset rectangle origin for player weapons (default is sprite centre - we want left edge)
		entity_dictionary[EntityType_BULLET].ox = 0;
		entity_dictionary[EntityType_BULLET].oy = 0;

		entity_dictionary[EntityType_BOSSBEAM].ox = 15;

		// ---------------------------------------------------------------------------------------------------------------------
		//	init world, set viewport initial position

		// convert fixedpoint to screen coords
		g_viewport_worldx = g_fscrollx>>16;
		g_viewport_worldy = g_fscrolly>>16;

		pprintf("init world viewport...\n");
		myworld.init(g_viewport_worldx, g_viewport_worldy);

//		while (1) { }

		// ---------------------------------------------------------------------------------------------------------------------

		pprintf("ready... (press key)\n");
		Crawcin();

		// ---------------------------------------------------------------------------------------------------------------------
		//	now claim control over machine resources - display, timers, input etc.

		machinestate.claim();

		// ---------------------------------------------------------------------------------------------------------------------
		//	install input service for keyboard, joysticks, mouse
		//	note: joysticks/pads not yet tied - will be fixed soon

		AGT_InstallInputService();

		// ---------------------------------------------------------------------------------------------------------------------
		//	install sound services

#if defined(ENABLE_AGT_AUDIO)
		AGT_SoundOpen();
#endif // defined(ENABLE_AGT_AUDIO)

		// ---------------------------------------------------------------------------------------------------------------------
		//	initialize display, configure single- or dual-field graphics mode, load palettes for phys & log shiftfields
		// ---------------------------------------------------------------------------------------------------------------------
		// note: there are 2 shiftfields by default, even if there are more than 2 playfield buffers. The shiftfield contains
		// all HW state associated with a single frame, including the framebuffer address, palette, scroll/linewid and other
		// relevant state. They must all be committed at exactly the same time by the displayservice.
		// The logical shifstate is always the one to be written by the game loop because it will not reach the HW registers 
		// until the framebuffers are exchanged.

		pprintf("init shifter...\n");
		shift.set_fieldmode(/*dualfield=*/(c_dualfield && !c_singlevbl));
		shift.init();

		if (bFalcon030)
		{
			reg16(ffff82a8) -= 40;
			reg16(ffff82aa) += 40;
		}

		pprintf("set palette...\n");	
		for (s16 c = 0; c < 16; ++c)
		{
			// logic (next)
			shift.setcolour(0, 0, c, mytiles0.getcol(c));
			shift.setcolour(0, 1, c, mytiles1.getcol(c));	// only needed in dual-field mode

			// physic (current)
			shift.setcolour(1, 0, c, mytiles0.getcol(c));
			shift.setcolour(1, 1, c, mytiles1.getcol(c));	// only needed in dual-field mode
		}


		// =====================================================================================================================
		//	game restart point
		// =====================================================================================================================

#if defined(ENABLE_AGT_AUDIO)
//		AGT_MusicLoad("lfpoison.bmz");
//		AGT_MusicLoad("gra2boss.bmu");
		AGT_MusicLoad("gra2boss.bmz");
		AGT_MusicStart(1);
#endif // defined(ENABLE_AGT_AUDIO)

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

		// start at left of map
		g_fscrollx = c_scroll_safety_margin<<16;
		g_fscrolly = 0;

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

		// create the player entity
		g_pe_player = EntitySpawn(EntityType_PLAYER, 0, 0);

		// create the boss
		g_pe_boss = EntitySpawn(EntityType_BOSS, 100, -100);

		g_pe_boss->wx = VIEWPORT_XSIZE-80;
		g_pe_boss->wy = -96;
		g_pe_boss->wz = 0;

		// =====================================================================================================================
		//	must link any pending entities before starting mainloop (viewport is an entity and viewport scans rely on it)

		EntityExecuteAllLinks();

		// =====================================================================================================================
		//	mainloop
		// =====================================================================================================================

		// place mainloop on AGT's VBL handler - alternative to running a normal mainloop!
		// benefit here is that loading/depacking and so on can be done here on the 'background thread'
		reg32(a0) = (int)&menu_vblservice;

		while (1)
		{
			// spin forever
		}

		// =====================================================================================================================
		//	end mainloop
		// =====================================================================================================================

		AGT_RemoveInputService();
#if defined(ENABLE_AGT_AUDIO)
		AGT_SoundClose();
#endif

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
// tick: player (input, plus options/multiples which follow)

void play_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// ---------------------------------------------------------------------------------------------------------------------
	// handle player input using scancode-based live key states
	//	- this does not require polling any hardware and does not disturb the machine state
	//	- it is easy and efficient, providing we don't have too many keys to check (if so, prefer an event handler)
	//	- using event handler would avoid missed keys but its not really neccessary for a fixed-framerate game

	bool snake = false;

	if (key_states[ScanCode_W])
	{
		// down!
		g_player_viewy -= 2;

		// moving down, so tilt upwards
		g_player_tilt -= 1;
		if (g_player_tilt < (0<<c_player_tilt_fracbits))
			g_player_tilt = (0<<c_player_tilt_fracbits);

		// we're moving, so snake those multiples through the historybuffer
		snake = true;
	}
	else 
	if (key_states[ScanCode_S])
	{
		// up!
		g_player_viewy += 2;

		// moving down, so tilt downwards
		g_player_tilt += 1;
		if (g_player_tilt > (9<<c_player_tilt_fracbits)-1)
			g_player_tilt = (9<<c_player_tilt_fracbits)-1;

		// we're moving, so snake those multiples through the historybuffer
		snake = true;
	}
	else
	{
		// if not moving up or down, drift tilt animation back to centre frame #4
		g_player_tilt -= tsign16(g_player_tilt-(4<<c_player_tilt_fracbits)-(1<<(c_player_tilt_fracbits-1)));

		// ...and we don't snake the multiples. leave them in current position.
	}

	// set animation frame from tilt
	self.frame = g_player_tilt >> c_player_tilt_fracbits;

	// left and right is simple - doesn't affect animation yet (later will add engines)
	if (key_states[ScanCode_A])
	{
		g_player_viewx -= 3;
		snake = true;
	}
	else 
	if (key_states[ScanCode_D])
	{
		g_player_viewx += 3;
		snake = true;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// player world coordinates = (local) screen coords + (scroll) viewport world coords
	// this is needed for correct interactions with other objects in the world - especially those which don't follow scroll

	g_player_worldx = g_player_viewx + g_viewport_worldx;
	g_player_worldy = g_player_viewy + g_viewport_worldy;

	// ---------------------------------------------------------------------------------------------------------------------
	// load entity coordinates to allow interactions between player craft and other objects
	// note: also need to load this *before* spawning weapon shots from player entity or they will lag..

	self.rx = g_player_worldx;
	self.ry = g_player_worldy;

	// ---------------------------------------------------------------------------------------------------------------------
	// fire control

	if (key_states[ScanCode_LSHIFT] || key_states[ScanCode_RSHIFT])
	{
		static s16 pewcounter = 0;

		{
			if ((pewcounter++ & 0x7) == 0)
			{
				// todo: player shots could be processed as multiple [0] with the others being [1...n]
				EntitySpawnRel
				(
					EntityType_BULLET, 
					g_pe_player, 
					8,0
				);

				//for (s16 o = 0; o < g_num_options; ++o)
				//{
				//	EntitySpawnRel
				//	(
				//		EntityType_BULLET, 
				//		g_pe_options[o], 
				//		8,0
				//	);
				//}
			}
		}
	}
}

void bullet_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move in the initiated direction (well, in this demo it's always be towards the right but lets pretend its generic)
	self.rx += self.vx;
	// sneaky! when the player moves up/down, the lasers follow on the y axis instead of lagging behind like bullets, 
	// because the laser is a child entity of the player and can watch its parent's state...
	self.ry = self.pparent->ry + self.pparent->oy;

	// terminate offscreen. careful to use world coordinates.
	// todo: can precalculate left/right limits once per frame and save precious cycles
	if ((self.rx >= (g_viewport_worldx + VIEWPORT_XSIZE)) /*|| (self.rx <= (g_viewport_worldx - 32))*/)
		EntityRemove(_pself);
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: boss

// some forward-declarations, so tick functions can refer to each other
void boss_state1_eyeopen_fntick(entity_t *_pself);
void boss_state2_attackA_fntick(entity_t *_pself);
//
void boss_state3_moveA_fntick(entity_t *_pself);
void boss_state4_attackB_fntick(entity_t *_pself);
void boss_state5_moveB_fntick(entity_t *_pself);
void boss_state6_attackC_fntick(entity_t *_pself);
void boss_state7_sweep_fntick(entity_t *_pself);

// some (x,y) offsets for sub-parts of the boss

// eye position (relative to center)
static const s16 c_bosseye_xoff = 7;
static const s16 c_bosseye_yoff = 0;

// ppulp position (relative to center)
static const s16 c_bosspulp_xoff = 36-24;
static const s16 c_bosspulp_yoff = 44;

// upper shielding position (relative to topleft)
static const s16 c_bossshieldU_xoff = 11;
static const s16 c_bossshieldU_yoff = 16;

// lower shielding position (relative to topleft)
static const s16 c_bossshieldL_xoff = 11;
static const s16 c_bossshieldL_yoff = 52;

void boss_state0_enter_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// not in berzerk mode to begin with
	self.vy = 0;

	if (self.counter == 0)
	{
		self.counter++;

		// first, create eye in middle of boss
		// this eye entity will remain connected to its parent (boss) via entity_t::pparent
		EntitySpawnRel
		(
			EntityType_BOSSEYE, 
			&self, 
			c_bosseye_xoff,c_bosseye_yoff
		);

	}
	// strobe mode: 0x8000 = flash/blink mode, 0x0010 = reset flash counter to >=16 every tick but leave lower bits to count down
	// this allows continuous blinking, otherwise we can only blink for 0xfe frames countdown, maximum (approx 4 seconds)
	// 0x80ff == permanent invisibility, no countdown
	self.draw_hitflash |= 0x8010;


	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;

	// drift onto play area until we get to the middle
	self.wy += 1;
	// then switch tick behaviour to start attack patterns
	if (self.wy >= (VIEWPORT_YSIZE/2))
	{
		self.fntick = &boss_state1_eyeopen_fntick;
		// reset counter
		self.counter = 0;
	}
}

void boss_state1_eyeopen_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	switch (self.counter++)
	{
	case 0:
		{
			entity_t *pnew;

			self.draw_hitflash = 0;

			pnew = EntitySpawnFrom
			(
				EntityType_BOSSPULP, 
				&self
			);
			// use general purpose vars to store x/y offset from boss
			pnew->wx = c_bosspulp_xoff;
			pnew->wy = c_bosspulp_yoff;

			// also create invisible shielding either side of eye channel

			pnew = EntitySpawnFrom
			(
				EntityType_BOSSSHIELD, 
				&self
			);
			// use general purpose vars to store x/y offset from boss
			pnew->wx = c_bossshieldU_xoff;
			pnew->wy = c_bossshieldU_yoff;

			pnew = EntitySpawnFrom
			(
				EntityType_BOSSSHIELD, 
				&self
			);
			// use general purpose vars to store x/y offset from boss
			pnew->wx = c_bossshieldL_xoff;
			pnew->wy = c_bossshieldL_yoff;
		}
		break;

	case 32:
		// later, switch to attack mode A & reset counter
		self.fntick = &boss_state2_attackA_fntick;
		self.counter = 0;
		break;

	default:
		// until then, wait around
		break;
	};

	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;
}

void boss_state2_attackA_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	switch (self.counter++)
	{
	case 32:
		// after a while, shoot 4 lasers
		{
			// gun placements:
			// boss size (144,96)
			// boss origin (72,48)
			// A: 56,2
			// B: 5,18
			// C: 5,77
			// D: 56,93

			// shoot 4 lasers from different positions relative to boss origin (center)
			// A
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				56-72,2-48
			);
			// B
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				5-72,18-48
			);
			// C
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				5-72,77-48
			);
			// D
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				56-72,93-48
			);
		}
		break;

	case 128:
		// later, switch to defensive mode
		self.fntick = &boss_state3_moveA_fntick;
		self.counter = 0;
		break;

	default:
		// at other times, just wait around
		break;
	}

	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;
}

void boss_state3_moveA_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// counter used for sub-states within this state
	switch (self.counter)
	{
	case 0:
		// move downwards until crossing low border, then advance to next sub-state
		self.wy += 1;
		if (self.wy >= (VIEWPORT_YSIZE-24))
			self.counter++;

		// berzerk spray towards the left
		if (self.vy)
		if ((--self.wz & 63) == 0)
		{
			for (s16 b = 0; b < 7; b++)
			{
				entity_t *pnew = EntitySpawnFrom
				(
					EntityType_BOSSBALL,
					&self
				);

				// sweep through 180' of direction table (left directions)
				s16 a = (((b & 15) - 11) & 31) << 1;

				// assign velocity
				pnew->fvx = s_fdirs_32ang[a + 0] << 3;
				pnew->fvy = s_fdirs_32ang[a + 1] << 3;
				// pseudorandom starting anim-frame
				pnew->frame = pnew->id & 7;
			}
		}

		break;
	case 1:
		// move upwards until low-middle of screen
		self.wy -= 1;
		if (self.wy <= (VIEWPORT_YSIZE-64))
			self.counter++;

		// berzerk spray towards the left
		if (self.vy)
		if ((--self.wz & 63) == 0)
		{
			for (s16 b = 0; b < 7; b++)
			{
				entity_t *pnew = EntitySpawnFrom
				(
					EntityType_BOSSBALL,
					&self
				);

				// sweep through 180' of direction table (left directions)
				s16 a = (((b & 15) - 11) & 31) << 1;

				// assign velocity
				pnew->fvx = s_fdirs_32ang[a + 0] << 2;
				pnew->fvy = s_fdirs_32ang[a + 1] << 2;
				// pseudorandom starting anim-frame
				pnew->frame = pnew->id & 7;
			}
		}

		break;
	case 2:
		// ...then switch to attack pattern B
		self.fntick = &boss_state4_attackB_fntick;
		self.counter = 0;
		break;
	}

	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;
}

void boss_state4_attackB_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	switch (self.counter++)
	{
	case 32:
		// after a while, shoot 4 lasers
		{
			// gun placements:
			// boss size (144,96)
			// boss origin (72,48)
			// A: 56,2
			// B: 5,18
			// C: 5,77
			// D: 56,93

			// shoot 4 lasers from different positions relative to boss origin (center)
			// A
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				56-72,2-48
			);
			// B
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				5-72,18-48
			);
			// C
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				5-72,77-48
			);
			// D
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				56-72,93-48
			);
		}
		break;

	case 128:
		// later, switch to defensive mode
		self.fntick = &boss_state5_moveB_fntick;
		self.counter = 0;
		break;

	default:
		// at other times, just wait around
		break;
	}

	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;
}

void boss_state5_moveB_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// counter used for sub-states within this state
	switch (self.counter)
	{
	case 0:
		// move upwards until crossing top border, then advance to next sub-state
		self.wy -= 1;
		if (self.wy <= (0+24))
			self.counter++;

		// berzerk spray towards the left
		if (self.vy)
		if ((--self.wz & 63) == 0)
		{
			for (s16 b = 0; b < 7; b++)
			{
				entity_t *pnew = EntitySpawnFrom
				(
					EntityType_BOSSBALL,
					&self
				);

				// sweep through 180' of direction table (left directions)
				s16 a = (((b & 15) - 11) & 31) << 1;

				// assign velocity
				pnew->fvx = s_fdirs_32ang[a + 0] << 2;
				pnew->fvy = s_fdirs_32ang[a + 1] << 3;
				// pseudorandom starting anim-frame
				pnew->frame = pnew->id & 7;
			}
		}

		break;
	case 1:
		// move downwards until upper-middle of screen
		self.wy += 1;
		if (self.wy >= (0+64))
			self.counter++;

		// berzerk spray towards the left
		if (self.vy)
		if ((--self.wz & 63) == 0)
		{
			for (s16 b = 0; b < 7; b++)
			{
				entity_t *pnew = EntitySpawnFrom
				(
					EntityType_BOSSBALL,
					&self
				);

				// sweep through 180' of direction table (left directions)
				s16 a = (((b & 15) - 11) & 31) << 1;

				// assign velocity
				pnew->fvx = s_fdirs_32ang[a + 0] << 3;
				pnew->fvy = s_fdirs_32ang[a + 1] << 2;
				// pseudorandom starting anim-frame
				pnew->frame = pnew->id & 7;
			}
		}

		break;
	case 2:
		// ...then switch to attack pattern C
		self.fntick = &boss_state6_attackC_fntick;
		self.counter = 0;
		break;
	}

	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;
}

void boss_state6_attackC_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	switch (self.counter++)
	{
	case 32:
		// after a while, shoot 4 lasers
		{
			// gun placements:
			// boss size (144,96)
			// boss origin (72,48)
			// A: 56,2
			// B: 5,18
			// C: 5,77
			// D: 56,93

			// shoot 4 lasers from different positions relative to boss origin (center)
			// A
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				56-72,2-48
			);
			// B
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				5-72,18-48
			);
			// C
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				5-72,77-48
			);
			// D
			EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				56-72,93-48
			);
		}
		break;

	case 128:
		// later, switch to defensive mode
		self.fntick = &boss_state7_sweep_fntick;
		self.counter = 0;
		break;

	default:
		// at other times, just wait around
		break;
	}

	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;
}

void boss_state7_sweep_fntick(entity_t *_pself)
{
	s16 selector;
	entity_t *pnew = NULL;

	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// counter used for sub-states within this state
	switch (self.counter)
	{
	case 0:
		// move upwards
		self.wy -= 1;
		if (self.wy <= (0+50))
		{
			self.counter++;
			// reset spare counter for next sub-state
			self.wz = 0;
		}
		break;
	case 1:
		// dash leftwards, spraying balls
		{
			self.wx -= 2;
			if (self.wx <= 80)
				self.counter++;

			if (--self.wz < 0)
			{
				self.wz = 8;

				pnew = EntitySpawnFrom
				(
					EntityType_BOSSBALL,
					&self
				);

				// sweep through 180' of direction table (downward directions)
				s16 a = (((self.vz++ & 15) - 8) & 31) << 1;

				// assign velocity
				pnew->fvx = s_fdirs_32ang[a + 0] << 2;
				pnew->fvy = s_fdirs_32ang[a + 1] << 2;
			}
		}
		break;
	case 2:
		// move downwards
		self.wy += 1;
		if (self.wy >= (VIEWPORT_YSIZE-50))
		{
			self.counter++;
			// reset spare counter for next sub-state
			self.wz = 0;
		}

		// randomly fire beams from one of 4 placements
		selector = self.wz++ & 7;

		if (selector == 0)
		{
			// pick one random placement to shoot from (of 4)
			// note: could be done with a table of positions instead of 4 specific calls but hey...
			s16 rear_gun_index = ((g_frame_prng >> 9) & 7) << 1;

			static const s8 rear_gun_positions[8*2] = 
			{
				95-72,3-48,
				131-72,14-48,
				142-72,31-48,
				142-72,65-48,
				131-72,81-48,
				95-72,92-48,
				142-72,31-48,
				142-72,65-48
			};

			s16 rear_gun_x = rear_gun_positions[rear_gun_index + 0];
			s16 rear_gun_y = rear_gun_positions[rear_gun_index + 1];

			pnew = EntitySpawnRel
			(
				EntityType_BOSSBEAM,
				&self,
				rear_gun_x,rear_gun_y
			);

			// rear beams move quickly to the right (default is left)
			pnew->vx = 6;
		}
		break;
	case 3:
		// move right, spraying balls
		{
			self.wx += 2;
			if (self.wx >= (VIEWPORT_XSIZE-80))
				self.counter++;

			if (--self.wz < 0)
			{
				self.wz = 8;

				pnew = EntitySpawnFrom
				(
					EntityType_BOSSBALL,
					&self
				);

				// sweep through 180' of direction table (upward directions)
				s16 a = (((self.vz++ & 15) + 8) & 31) << 1;

				// assign velocity
				pnew->fvx = s_fdirs_32ang[a + 0] << 2;
				pnew->fvy = s_fdirs_32ang[a + 1] << 2;
			}
		}

		break;
	case 4:
		// ...then switch to attack pattern C
		self.fntick = &boss_state3_moveA_fntick;
		self.counter = 0;
		break;
	}

	// generate world coords from viewport-relative coords
	self.rx = g_viewport_worldx + self.wx - self.ox;
	self.ry = g_viewport_worldy + self.wy - self.oy;
}

void bossbeam_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move in the initiated direction (well, in this demo it's always be towards the right but lets pretend its generic)
	self.rx += self.vx;

	// terminate offscreen. careful to use world coordinates.
	// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
	u16 spx = self.rx - (g_viewport_worldx-30);
	if (spx > (VIEWPORT_XSIZE+60))
		EntityRemove(_pself);
}

void bossball_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move in the initiated direction 
	// since this is aimed at fine angles, we apply fixedpoint velocity (fvx,fvy)
	self.frx += self.fvx;
	self.fry += self.fvy;

	// there are lots of these and numbers cost - so we try to minimise the amount of 
	// code executed on the majority of visits by enclosing the animation and visibility
	// test with a decrementing counter, triggering on -1
	if (--self.counter < 0)
	{
		// reset counter
		self.counter = 1;

		// advance animation (backwards, for quick -ve test on wrap)
		// note: there are 9 frames, so 0...8
		if (--self.frame < 0)
			self.frame = 9-1;

		// terminate offscreen. careful to use world coordinates.
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-5);
		u16 spy = self.ry - (g_viewport_worldy-5);
		if ((spx > (VIEWPORT_XSIZE+10)) || (spy > (VIEWPORT_YSIZE+10)))
			EntityRemove(_pself);
	}
}

static const int c_eye_stroberate = 2;

void bosseye_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;
	// eye can't exist without boss, so we don't never to check the pointer destination is valid!
	entity_t &parent = *self.pparent;

	if (parent.vy)
	{
		// advance animation frame
		self.frame = self.counter++ >> c_eye_stroberate;
		// wrap at 6 frames
		if (self.counter >= (6 << c_eye_stroberate))
			self.counter = 0;
	}

	// attached to boss - follows boss
	// entities are defined as rectangles, with graphics offset by a centering origin (ox,oy)
	// so to update the eye position correctly we need to account for both (ox,oy) origins 
	// plus any additional constant offset we want to include from the boss center
	self.rx = ((parent.rx + parent.ox) - self.ox) + c_bosseye_xoff;
	self.ry = ((parent.ry + parent.oy) - self.oy) + c_bosseye_yoff;
}

void bosspulp_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;
	// eye can't exist without boss, so we don't never to check the pointer destination is valid!
	entity_t &parent = *self.pparent;

	// attached to boss - follows boss
	// we calculate our updated position each tick using general purpose wx,wy as offsets
	// note: object relative to boss rectangle top-left so it's easy
	self.rx = parent.rx + self.wx;
	self.ry = parent.ry + self.wy;
}

void bossshield_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;
	// eye can't exist without boss, so we don't never to check the pointer destination is valid!
	entity_t &parent = *self.pparent;

	// attached to boss - follows boss, but there is more than one of these each with its
	// own offset (stored in wx,wy) so we calculate our updated position each tick using these
	// note: shield hitboxes are relative to boss rectangle top-left so it's easy
	self.rx = parent.rx + self.wx;
	self.ry = parent.ry + self.wy;
}

// =====================================================================================================================
//	collision reaction functions
// =====================================================================================================================

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: player vs other

void play_fncol(entity_t *_pself, entity_t *_pother)
{
	return;

	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// deduct damage caused by other
	self.health -= _pother->damage;

	// if damage is excessive, die in flames
	if (self.health <= 0)
	{
		// clear all properties except tick/2D, stops all interactions but continues to animate & draw
		self.f_self &= EntityFlag_TICKED|EntityFlag_SPATIAL;
		self.f_interactswith = 0;

		// change tick function to simply animate then kill itself...
//		self.fntick = &player_death_fntick;

		// make the object crash in a gravity well (initial y velocity = 0, but will increment)
		self.vx = 1;
		self.vy = 0;
	}
}

void bullet_fncol(entity_t *_pself, entity_t *_pother)
{
	// laser makes puff/spark when it strikes something
	// todo: this looks like a pink splat, meant for shooting amoeba sprites - should be changed to actual spark anim
	// removed for now - bit too expensive to produce lots of these with current sprite routines
	//entity_t * pespark = EntitySpawnRel
	//(
	//	EntityType_WSPARK, 
	//	_pself, 
	//	16, 0	// spawn roughly half way along the laser, to save time figuring out exact contact
	//);

	// laser beam reacts to all interactions by just getting absorbed!
	EntityRemove(_pself);
}

void bosseye_fncol(entity_t *_pself, entity_t *_pother)
{
	// when the eye takes damage, it causes the boss to strobe (hitflash)
	// this is done by accessing the eye's parent link (i.e. the boss)
	_pself->pparent->draw_hitflash += 3;

	// put boss into berzerk mode
	_pself->pparent->vy = 1;
}

void bosspulp_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	_pself->draw_hitflash += 3;

	// when the pulp takes damage, it loses a segment each time the health
	// counter hits zero, then resets its health. counter is incremented
	// for each segment. when counter hits 6, all segments are gone and
	// object deletes itself.

	self.health -= _pother->damage;

	if (self.health < 0)
	{
		// lose a segment, shift one segment to the right
		self.frame++;
		self.wx += 5;

		// reset health, count segments
		self.health = 50;
		self.counter++;
		if (self.counter == 6)
			EntityRemove(_pself);
	}

	// this is done by accessing the eye's parent link (i.e. the boss)
	//_pself->pparent->draw_hitflash += 3;
}

void boss_fncol(entity_t *_pself, entity_t *_pother)
{
	// absorbs bullets, otherwise does nothing
//	_pself->draw_hitflash += 3;
}

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: powerup vs player

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
// the same address. here, the initialized value is the integer part but this can be changed by reordering the union.

entitydef_t entity_dictionary[EntityType_MAX_] =
{
	// [functions]								[coords]						[priorities]	[tick state]		[rendering]				[classification properties]
	// tick,collide,asset,hasset				px/y,vx/y,sx/y,ox/y				tickpri,drawpri	h,d,frame,counter	drawtype,drawlayer		f_self,f_interactswith	

	// engine objects...

	// viewport																					// viewport has no tick
	// Q: why? A: because it's a rectangle and we can determine what is inside it quickly if it is also an entity which moves through the world
	{ 0,0,0,0,									{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,0,		EntityFlag_VIEWPORT|EntityFlag_SPATIAL,	0 },

	// player objects...

	// player																					// tick player first...
	//	- move and shoot to select tracks
	{ &play_fntick,&play_fncol,0,0,				{0},{0},{0},{0},0,0,0,0,		0,0,			30,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_PLAYER|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },

	// boss
	//	- moves around slowly, takes no damage, shoots huge guns with crappy aim
#if defined(BOSS_USE_SLAB_VS_EMX)
	{ &boss_state0_enter_fntick,0,0,0,			{0},{0},{0},{0},0,0,0,0,		0,0,			30,0,0,0,			EntityDraw_SLAB,0,		EntityFlag_INERT|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },
#else
	{ &boss_state0_enter_fntick,0,0,0,			{0},{0},{0},{0},0,0,0,0,		0,0,			30,0,0,0,			EntityDraw_EMXSPRUR,0,	EntityFlag_INERT|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },
#endif
	// boss eye
	//	- attached to boss, takes damage, kills boss when health hits zero
	{ &bosseye_fntick,bosseye_fncol,0,0,		{0},{0},{0},{0},0,0,0,0,		1,0,			100,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_BULLET },
	{ &bosspulp_fntick,bosspulp_fncol,0,0,		{0},{0},{0},{0},30,0,0,0,		1,0,			100,0,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_BULLET },
	// boss shielding
	//	- hitbox attached to boss either side of eye channel, absorbs bullets, except in channel area
	{ &bossshield_fntick,0,0,0,					{0},{0},{0},{0},110,28,0,0,		1,0,			0,0,0,0,			EntityDraw_NONE,0,		EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_BULLET },

	// boss laser
	{ &bossbeam_fntick,0,0,0,					{0},{0},{-4},{0},0,0,0,0,		2,0,			0,12,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_NMEBULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },
	// boss ball weapon
	{ &bossball_fntick,0,0,0,					{0},{0},{0},{0},0,0,0,0,		2,0,			0,12,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_NMEBULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },

	// bullet weapon																			// ...then everything else
	{ &bullet_fntick,&bullet_fncol,0,0,			{0},{0},{10},{0},0,0,0,0,		2,0,			0,15,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_BULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_NME },
};

//----------------------------------------------------------------------------------------------------------------------
//	Music fixup callback: opportunity to set the instruments and multiplex routing for specific songs
//----------------------------------------------------------------------------------------------------------------------

#if defined(ENABLE_AGT_AUDIO)
void AGT_MusicFixupCallback(const char* const _name, remap_ft &remap_func)
{
	printf("fixup [%s]\n", _name);

	// todo: crude - use a better callback filter
	if (strncmp(_name, "lfpoison",8) == 0)
	{
		printf("preparing [%s]\n", _name);

		//drum: 0056 -> [036-1][kick drum 2]
		//drum: 0144 -> [039-1][snare drum 1]
		//drum: 0136 -> [041-1][snare drum 2]
		//drum: 0224 -> [043-1][closed high hat exc1]
		//drum: 0024 -> [047-1][open high hat exc1]
		//instrument: 0528 -> [038-1][Slap Bass 2]
		//instrument: 0408 -> [049-1][String Ensemble 1]
		//instrument: 0148 -> [053-1][Choir Aahs]
		//instrument: 0056 -> [082-1][Lead 2 (sawtooth)]

		for (s16 c = 0; c < 16; c++)
			g_bmm_channel_octave_map[c]	= 12*-1;

		// shift bass up
		g_bmm_channel_octave_map[1-1]		= -12*0;
		g_bmm_channel_octave_map[2-1]		= -12*0;
		g_bmm_channel_octave_map[3-1]		= 12*1;
		g_bmm_channel_octave_map[4-1]		= 12*0;

		g_bmm_channel_octave_map[6-1]		= 12*1;

		// remap selected instruments (default: YMI_Piano)
		g_instrument_map[gmi_SlapBass2]	= YMI_FretPhaseTri;

		// manual YM routing for 16 midi channels
		static const s8 routing[16] = 
		{ 
			1,1,2,2,	8,0,8,8,	8,(-1),8,8,		8,8,8,8
		};

		set_channel_routing(routing);
	}
	else
	if (strncmp(_name, "gra2boss",8) == 0)
	{
		printf("preparing [%s]\n", _name);

		for (s16 c = 0; c < 16; c++)
			g_bmm_channel_octave_map[c]	= 12*0;

		g_bmm_channel_octave_map[1-1]		= 12*-1;

		g_bmm_channel_octave_map[3-1]		= 12*0;

		// remap selected instruments (default: YMI_Piano)
		g_instrument_map[gmi_Lead1_square]	= YMI_FretPhaseTri;

		// manual YM routing for 16 midi channels
		static const s8 routing[16] = 
		{ 
			0,1,2,8,	8,8,8,8,	8,(-1),8,8,		8,8,8,8
		};

		set_channel_routing(routing);
	}

}
#endif // defined(ENABLE_AGT_AUDIO)
