//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	example: horizontally scrolling shmup
//----------------------------------------------------------------------------------------------------------------------
//	controls:
//		- WSAD keys to move around
//		- RSHFT key to shoot
//		- ? key to lock multiples in formation
//		- @ key to recall multiples to ship
//----------------------------------------------------------------------------------------------------------------------
//	notes:
//		- aim to implement more or less all game functionality in a single sourcefile with minimum of hacks
//		- aim to maintain steady 50fps 
//		- aim to do most things procedurally (i.e. with minimum use of precalc) to help demonstrate basic AI coding
//		- aim to avoid bitplane trickery, using 4-bitplanes/16-colours all the way
//		- aim to use more or less common/reusable code for all effects (i.e. common sprite & background code etc.)
//----------------------------------------------------------------------------------------------------------------------

#define ENABLE_PLAYER_COLLISIONS (1)
#define ENABLE_PLAYER_RANDOMKILL (0)

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
#include "agtsys/pcs.h"

// ---------------------------------------------------------------------------------------------------------------------

// 8bit normalization table for AI
#include "tables/normtab.h"

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

	// various weapons
	EntityType_OPTION,						// trails behind player
	EntityType_LASER,						// single lazer beamz
	 EntityType_DLASER,						// double lazer beamz
	 EntityType_PLASER,						// pulse/ripple lazer beamz
	EntityType_BULLET,						// bullet
	EntityType_MISSILE,						// missile
	EntityType_SMARTBOMB,					// smartbomb death-slab

	// special fx
	EntityType_WSPARK,						// weapon spark on contacts
	EntityType_DFLAME,						// death flames
	
	// powerups
	EntityType_POWERUP1,					// powerup cell
	EntityType_POWERUP2,					// smartbomb cell

	// special enemy triggers
	EntityType_WAVESPAWN,					// spawn wave of something, then disappear

	// enemy weapons
	EntityType_NMEPELLET,
	EntityType_NMELASER,

	// enemies
	EntityType_NMEWALKER,					// walks along bottom of screen
	EntityType_NMESPINNY,					// z-spinning 3-winged disc in waves
	EntityType_NMEFLY,						// 360-seeking, annoying insect-like thing
	EntityType_NMEFLY2,						// 360-seeking, divebombing insect thingy
	EntityType_NMEAMOEBA,					// wobbly amoeba enemy
	EntityType_NMEQUAD,						// materializing quad-sphere
	EntityType_NMESQUARE,					// spinning square bomb
	EntityType_NMEMINE,						// mine obstacle
	EntityType_NMETIE,						// laser-wielding ship
	EntityType_NMEJELLY,					// jelly blob obstacle
	EntityType_NMEDRONE,					// advances in straight line, leaves behind diamonds
	EntityType_NMECYLON,					// advances in wobbly waves

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
	/*max_scroll=*/5120,						// maximum scroll distance in pixels (reduce to save a little ram on tables)
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

// some globals and bodge variables - some of this should be moved inside a player entity

// max number of multiples trailing player craft
static const s16 c_max_multiples = 4;

// number of multiples currently active
static s16 g_num_multiples = 0;

// type of laser currently active, otherwise bullets + missiles
static s16 g_lasertype = 0;

// initial scroll direction = positive/right
static s16 g_scrolldir = 1;	

// xlimit at which map stops and changes scroll direction
static s16 g_map_xlimit = 0;

// set the time-separation/shift between trailing multiples, in terms of 2^n frames 
static const s16 c_option_separation = 4; // set time delay to 16 frames, * 4 options (+1 for player) = 80 slots total
static const s16 c_player_position_historypairs = (c_max_multiples+1) << c_option_separation; //80;
static const s16 c_player_position_historysize = c_player_position_historypairs*2;

// storage buffer for multiples coord history
static s16 g_player_position_history[c_player_position_historysize];

// fraction bits in tilt-position/tilt-speed, for gradual tilt animation as ship moves up/down
static const s16 c_player_tilt_fracbits = 2;

// player craft tilt angle. for visual effect only, as craft moves up/down.
static s16 g_player_tilt = 0<<c_player_tilt_fracbits;

// player viewport coordinates (local, control coordinates, relative to current screen position)
static s16 g_player_viewx = 160;
static s16 g_player_viewy = 100;

// player world coordinates (derived from local coords each frame, for interactions with world & other objects)
static s16 g_player_worldx = 0;
static s16 g_player_worldy = 0;

// COMPLETE HACK to spawn different entities at regular intervals. this is *not* typical of a real game.
static s16 g_spawn_countdown = 0;
// wave type counter, advances type of entity produced in next wave
static s16 g_spawn_wavetype = 0;

// note: maintain safety margin of 2 tiles in all *active* scroll directions because speculative tile drawing
// occurs slightly in advance of the scroll and a safety margin saves us the need to wrap/tile the map address.
// (e.g. strange glitches can be seen in 8way mode when scroll direction changes too near the left edge of the map)
static const s16 c_scroll_safety_margin = 32;

// viewport (scroll) starting position in world map
static s16 g_viewport_worldx = c_scroll_safety_margin; 
static s16 g_viewport_worldy = 0;

// clipping margin outside of the scrolling viewport, to accommodate the largest typical sprite before deletion etc.
static const s16 c_viewport_xmargin = playfield_t::c_guardx;
static const s16 c_viewport_ymargin = playfield_t::c_guardy;

// artificial countdown after player dies, to allow time after death animation
static s16 g_deathdelay = 0;

// always-incrementing counter, used for powerup drops etc. caution: increments per use, so irregular intervals.
static s16 g_sequence = 0;

// always-incrementing counter, used for some animations
static s16 g_animtime = 0;

// cheap PRNG random source, only changes once per frame
static s16 g_frame_prng = 0;

// ---------------------------------------------------------------------------------------------------------------------
// static pointers to significant entities (still these must be created the usual way)

// viewport entity is global and part of the entity drawing system
static entity_t *s_pe_viewport = NULL;

static drawcontext_t drawcontext;

// useful to have direct/global access to player-related entities 
static entity_t *g_pe_player = NULL;
static entity_t *g_pe_options[c_max_multiples] = { NULL, NULL, NULL, NULL };

// ---------------------------------------------------------------------------------------------------------------------

// sprite assets

// player
spritesheet player;
spritesheet options;
spritesheet flames;
spritesheet laser;
spritesheet dlaser;
spritesheet plaser;
spritesheet bullet;
spritesheet missile;
spritesheet sparks;
spritesheet powerup;

// enemy sprites
spritesheet nme_pellets;
spritesheet nme_beam;
spritesheet nme_amoeba;
spritesheet nme_spinny;
spritesheet nme_square;
spritesheet nme_drone;
spritesheet nme_cylon;
spritesheet nme_bee;
spritesheet nme_fly;
spritesheet nme_fly2;
spritesheet nme_tie;
spritesheet nme_quad;
spritesheet nme_diamond;
spritesheet nme_blobs;
spritesheet nme_jelly;
spritesheet nme_walker;

// playfield tile assets (x2 in dual-field mode)
tileset mytiles0;
tileset mytiles1;

// playfield map assets
worldmap mymap0;

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

	//printf("mperc_last_: %d\n", mperc_last_);
	//printf("midi_mperc_start_: %d\n", midi_mperc_start_);

	// ---------------------------------------------------------------------------------------------------------------------
	//	locate the end of the program to help track memory usage, when USE_MEMTRACE is enabled

	g_tracked_allocation_ += (int)(&end);
#if defined(USE_MEMTRACE)
	printf("ealloc: [%d] initial committed\n", g_tracked_allocation_);
#endif

	// ---------------------------------------------------------------------------------------------------------------------
	//	welcome message

	printf("[A]tari [G]ame [T]ools / dml 2017\n");
	printf("Horizontal SHMUP demo (STE)\n");

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
		mytiles0.load_cct("demo10.cct");
		// only load 2nd tileset if interlacing/dual-field
		if (c_dualfield)
			mytiles1.load_cct("demo11.cct");

#if defined(TIMING_RASTERS_MAIN)
		// save copy of BG colour for rasters
		s16 bgcol = mytiles0.getcol(0);
#endif

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map
		
		printf("loading map...\n");
		mymap0.load_ccm("demo1.ccm");

		// get map tile dimensions (x16 -> in pixels) to help keep scroll inside the map limits
		int map_xsize = mymap0.getwidth() << 4;
		int map_ysize = mymap0.getheight() << 4;

		// max scroll is map size minus screen dimensions (plus safety margin)
		g_map_xlimit = map_xsize-VIEWPORT_XSIZE-c_scroll_safety_margin;

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

		// player craft & engines, multiples, other fx
		player.load("vicviper.emx");
		options.load("options.emx");
		flames.load("flames.emx");

		// weapon
		laser.load("lasers.emx");
		dlaser.load("dlasers.emx");
		plaser.load("plasers.emx");
		sparks.load("sparks.emx");
		bullet.load("bullet.emx");
		missile.load("missile.emx");
		powerup.load("powerup.emx");

		nme_pellets.load("pellets.emx");
		nme_beam.load("beam.emx");
		// enemy craft
		nme_amoeba.load("amoeba.emx");
		nme_spinny.load("spinny.emx");
		nme_square.load("square.emx");
		nme_drone.load("drone.emx");
		nme_cylon.load("cylon.emx");
		nme_bee.load("bee.emx");
		nme_fly.load("fly.emx");
		nme_fly2.load("fly2.emx");
		nme_tie.load("tie.emx");
		nme_quad.load("quad.emx");
		nme_diamond.load("diamonds.emx");
		nme_blobs.load("blobs.emx");
		nme_jelly.load("jelly.emx");
		nme_walker.load("walker.emx");

		// ---------------------------------------------------------------------------------------------------------------------
		//	tie gfx assets into entity type dictionary, so we don't need to do it every time we spawn an object

		// note: asset binding could be done automatically via the dictionary but its useful to be able to override 
		// hitbox sizes, origins etc. and its better not to hide asset assignment steps in a system with simple
		// memory management as it is now.

		EntityDefAsset_Sprite(EntityType_PLAYER, &player);
		EntityDefAsset_Sprite(EntityType_OPTION, &options);
		EntityDefAsset_Sprite(EntityType_LASER, &laser);
		EntityDefAsset_Sprite(EntityType_DLASER, &dlaser);
		EntityDefAsset_Sprite(EntityType_PLASER, &plaser);
		EntityDefAsset_Sprite(EntityType_BULLET, &bullet);
		EntityDefAsset_Sprite(EntityType_MISSILE, &missile);
		EntityDefAsset_Sprite(EntityType_WSPARK, &sparks);
		EntityDefAsset_Sprite(EntityType_DFLAME, &flames);
		EntityDefAsset_Sprite(EntityType_POWERUP1, &powerup);
		EntityDefAsset_Sprite(EntityType_POWERUP2, &powerup);

		EntityDefAsset_Sprite(EntityType_NMEPELLET, &nme_pellets);
		EntityDefAsset_Sprite(EntityType_NMELASER, &nme_beam);
		EntityDefAsset_Sprite(EntityType_NMEAMOEBA, &nme_amoeba);
		EntityDefAsset_Sprite(EntityType_NMESPINNY, &nme_spinny);
		EntityDefAsset_Sprite(EntityType_NMESQUARE, &nme_square);
		EntityDefAsset_Sprite(EntityType_NMEFLY, &nme_fly);
		EntityDefAsset_Sprite(EntityType_NMEFLY2, &nme_fly2);
		EntityDefAsset_Sprite(EntityType_NMEQUAD, &nme_quad);
		EntityDefAsset_Sprite(EntityType_NMEMINE, &nme_diamond);
		EntityDefAsset_Sprite(EntityType_NMETIE, &nme_tie);
		EntityDefAsset_Sprite(EntityType_NMEJELLY, &nme_jelly);
		EntityDefAsset_Sprite(EntityType_NMEDRONE, &nme_drone);
		EntityDefAsset_Sprite(EntityType_NMECYLON, &nme_cylon);
		EntityDefAsset_Sprite(EntityType_NMEWALKER, &nme_walker);

		// reset rectangle origin for player weapons (default is sprite centre - we want left edge)
		entity_dictionary[EntityType_LASER].ox = 0;
		entity_dictionary[EntityType_DLASER].ox = 0;
		entity_dictionary[EntityType_PLASER].ox = 0;
//		entity_dictionary[EntityType_LASER].oy = 0;		// leave vertical centering alone
		entity_dictionary[EntityType_BULLET].ox = 0;
		entity_dictionary[EntityType_BULLET].oy = 0;
		entity_dictionary[EntityType_NMELASER].ox = 28;
//		entity_dictionary[EntityType_NMELASER].oy = 0;

		// ---------------------------------------------------------------------------------------------------------------------
		//	init world, set viewport initial position

		printf("init world viewport...\n");
		myworld.init(g_viewport_worldx, g_viewport_worldy);

		// ---------------------------------------------------------------------------------------------------------------------

		printf("ready... (press key)\n");
		Crawcin();

		// ---------------------------------------------------------------------------------------------------------------------
		//	now claim control over machine resources - display, timers, input etc.

		machinestate.claim();

		// display the PCS title images
		// note: this gets skipped if the assets are missing
		// note: temporarily removed for debugging purposes

		//STE_ShowPCS416("info.pcd", 3, 580, 192);
		//STE_ShowPCS416("ggposter.pcd", 3, 464, 0);

		// ---------------------------------------------------------------------------------------------------------------------
		//	install input service for keyboard, joysticks, mouse
		//	note: joysticks/pads not yet tied - will be fixed soon

		AGT_InstallInputService();


#if defined(ENABLE_AGT_AUDIO)
		AGT_SoundOpen();
#endif

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

#if defined(ENABLE_AGT_AUDIO)
		{
			AGT_MusicStop();
			AGT_MusicLoad("salslash.bmu");
			AGT_MusicStart(1);
		}
#endif

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

		// viewport coords within world
		g_viewport_worldx = c_scroll_safety_margin;
		g_viewport_worldy = 0;

		// reset game variables
		g_num_multiples = 0;
		g_lasertype = 0;
		g_spawn_wavetype = 0;

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

		// create 4x options entities
		for (s16 o = 0; o < c_max_multiples; o++)
			g_pe_options[o] = EntitySpawnFrom(EntityType_OPTION, g_pe_player);

		// reset player history (multiples)
		s16* pout = g_player_position_history;
		for (s16 p = c_player_position_historypairs-1; p >= 0; p--)
		{
			*(pout++) = g_player_viewx;
			*(pout++) = g_player_viewy;
		}

		// =====================================================================================================================
		//	must link any pending entities before starting mainloop (viewport is an entity and viewport scans rely on it)

		EntityExecuteAllLinks();

		// =====================================================================================================================
		//	mainloop
		// =====================================================================================================================
	

mainloop:

			// ---------------------------------------------------------------------------------------------------------------------
			//	simple hack to reset game state when the player gets killed

			if (g_deathdelay)
			{
				// if death delay counter is running, restart game when counter has elapsed
				if (g_deathdelay-- == 1)
					goto restart_game;
			}
			else
			{
				// otherwise just check for killed player and start the counter running
				// note: potentially buggy. the player entity won't last long before its state gets recycled by some
				// other entity needing a slot in the manager, so test and transfer the player death state to a counter ASAP
				// todo: safer to just start the counter in the AI itself
				if (g_pe_player->f_self & EntityFlags_KILL)
					g_deathdelay = 40;
			}

			// ---------------------------------------------------------------------------------------------------------------------
			//	wait for the current VBL period to elapse
			//	note: this simplistic approach will cause a hard drop to 25hz if time runs out but we're aiming for 50hz anyway

			TIMING_MAIN(bgcol);

			s16 field_index = g_vbl;
			while (g_vbl == field_index) { }


			TIMING_MAIN(0x077);

			// ---------------------------------------------------------------------------------------------------------------------
			//	hack-slash-bodge: every so often, spawn an enemy near the player

			if (--g_spawn_countdown <= 0)
			{
				// spawn every ~200 frames
				g_spawn_countdown = 192;

				// spawn at left or right side of screen, depending on scroll direction right now
				// note: shouldn't spawn so far out that entities kill themselves due to being significantly offscreen
				s16 offset = 320+128;
				if (g_scrolldir < 0)
					offset = 320+64;

				// psuedo-random y position for enemy 'wave'
				s16 spawn_yoff = ((g_player_worldx * 11823) & 0x7f) - 64;

				// sequence through different kinds of enemy 'wave'
				switch (g_spawn_wavetype)
				{
				default:
				case 0:
					{
						// spawn the enemy and let it go about its business
						entity_t *pnme = EntitySpawn
						(
							EntityType_WAVESPAWN,
							g_viewport_worldx + offset, 
							spawn_yoff + (VIEWPORT_YSIZE>>1)
						);
						if (pnme)
						{
							pnme->frame = EntityType_NMESPINNY;
							pnme->damage = 16-1;
							if (spawn_yoff >= 0)
								pnme->vy = -1;
							else
								pnme->vy = 1;
						}
					}
					break;
				case 1:
					{
						// spawn the enemy and let it go about its business
						entity_t *pnme = EntitySpawn
						(
							EntityType_WAVESPAWN,
							g_viewport_worldx + offset, 
							spawn_yoff + (VIEWPORT_YSIZE>>1)
						);
						if (pnme)
						{
							pnme->frame = EntityType_NMECYLON;
							pnme->damage = 16-1;
							pnme->counter = 128;
							if (spawn_yoff >= 0)
								pnme->fvy = -0x8000;
							else
								pnme->fvy = 0x8000;
						}
					}
					break;
				// spawn a few different enemies at once:
				// - jelly blob
				// - wave of fly/bee-like things
				// - walker
				case 2:
					{
						EntitySpawn
						(
							EntityType_NMEJELLY,
							g_viewport_worldx + 320, 
							spawn_yoff + (VIEWPORT_YSIZE>>1)
						);

						// spawn the enemy and let it go about its business
						entity_t *pnme = EntitySpawn
						(
							EntityType_WAVESPAWN,
							g_viewport_worldx + offset, 
							spawn_yoff + (VIEWPORT_YSIZE>>1)
						);
						if (pnme)
						{
							pnme->frame = EntityType_NMEFLY;
							pnme->damage = 16-1;
							if (spawn_yoff >= 0)
								pnme->vy = -1;
							else
								pnme->vy = 1;
						}

						EntitySpawn(EntityType_NMEWALKER, g_viewport_worldx + 320, 208);
					}
					break;
				case 3:
					{
						// spawn the enemy and let it go about its business
						entity_t *pnme = EntitySpawn
						(
							EntityType_WAVESPAWN,
							g_viewport_worldx + offset, 
							spawn_yoff + (VIEWPORT_YSIZE>>1)
						);
						if (pnme)
						{
							pnme->frame = EntityType_NMETIE;
							pnme->damage = 32-1;
							if (spawn_yoff >= 0)
								pnme->vy = -1;
							else
								pnme->vy = 1;
						}
					}
					break;
				// spawn 'quad' objects in a circle around the player
				case 4:
					{
						//EntitySpawn
						//(
						//	EntityType_NMEAMOEBA,
						//	g_viewport_worldx + 320, 
						//	spawn_yoff + (SCREEN_LINES>>1) + 32
						//);

						//EntitySpawn
						//(
						//	EntityType_NMEAMOEBA,
						//	g_viewport_worldx + 320, 
						//	spawn_yoff + (SCREEN_LINES>>1) - 32
						//);

						// spawn NMEQUAD
						s16 r = ((g_frame_prng & 3) + 5) << 1;
						for (s16 q = 0, a = 0; q < 6; q++, a += r)
						{
							s16 i = (a & 63);
							s32 xo = s_fdirs_32ang[i+0] >> 10;
							s32 yo = s_fdirs_32ang[i+1] >> 10;

							entity_t *pnme = EntitySpawnRel
							(
								EntityType_NMEQUAD,
								s_pe_viewport,
								xo + SCREEN_XSIZE, 
								yo + 120
							);
						}

					}
					break;
				// spawn some flying craft and two walkers
				case 5:
					{
						// spawn the enemy and let it go about its business
						entity_t *pnme = EntitySpawn
						(
							EntityType_WAVESPAWN,
							g_viewport_worldx + offset, 
							spawn_yoff + (VIEWPORT_YSIZE>>1)
						);
						if (pnme)
						{
							pnme->frame = EntityType_NMEFLY2;
							pnme->damage = 16-1;
							if (spawn_yoff >= 0)
								pnme->vy = -1;
							else
								pnme->vy = 1;
						}

						EntitySpawn(EntityType_NMEWALKER, g_viewport_worldx + SCREEN_XSIZE+16, 208);

						EntitySpawn(EntityType_NMEWALKER, g_viewport_worldx + SCREEN_XSIZE+64, 208);
					}
					break;
				case 6:
					{
						// spawn the enemy and let it go about its business
						entity_t *pnme = EntitySpawn
						(
							EntityType_WAVESPAWN,
							g_viewport_worldx + offset, 
							spawn_yoff + (VIEWPORT_YSIZE>>1)
						);
						if (pnme)
						{
							pnme->frame = EntityType_NMEDRONE;
							pnme->damage = 32-1;
							if (spawn_yoff >= 0)
								pnme->vy = -1;
							else
								pnme->vy = 1;
						}
					}
					break;
				case 7:
					{
						if ((g_pe_player->rx & 2) == 0)
						{
							entity_t *pnme = EntitySpawn
							(
								EntityType_WAVESPAWN,
								g_viewport_worldx + SCREEN_XSIZE, 
								8
							);
							if (pnme)
							{
								pnme->frame = EntityType_NMESQUARE;
								pnme->damage = 32-1;
							}
						}
						else
						{
							entity_t *pnme = EntitySpawn
							(
								EntityType_WAVESPAWN,
								g_viewport_worldx + 320, 
								240-8
							);
							if (pnme)
							{
								pnme->frame = EntityType_NMESQUARE;
								pnme->damage = 32-1;
							}
						}
					}
					break;
				// spawn a collection of randomly spaced jelly blobs
				case 8:
					{
						static const s16 offsets[] = 
						{
							 54,  86,  21,  30, 100,  19, 144, 161, 170,  56, 148, 248,  51, 209, 211,  47, 
							173,  26,  68, 238,  24, 117, 106, 194,  67, 223,  43, 198, 182, 211, 181, 137 
						};

						s16 i = 0;
						for (s16 c = 0; c < 3; c++)
						{
							//s16 yoff = 0;
							//if (spawn_yoff >= 0)
							//	yoff = -(c<<4);
							//else
							//	yoff = (c<<4);

							s16 xoff = (offsets[i++] - 128) >> 2;
							s16 yoff = (offsets[i++] - 128) >> 1;

							EntitySpawn
							(
								EntityType_NMEJELLY,
								g_viewport_worldx + 320 + xoff, 
								spawn_yoff + (VIEWPORT_YSIZE>>1) + yoff
							);
						}

					}
					break;
				};

				g_spawn_wavetype++;
				if (g_spawn_wavetype == 9)
					g_spawn_wavetype = 0;
			}


			TIMING_MAIN(0x077);	

			// ---------------------------------------------------------------------------------------------------------------------
			// update the viewport first.

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

			EntityInteractVisible(320, c_viewport_xmargin, 240, c_viewport_ymargin);

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
			// animation time (todo: this is part of the 'options' bodge below, otherwise it would be stored in the entity)

			g_frame_prng += 19723;
			g_frame_prng *= (s16)28691;

			g_animtime += 37;

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
			// advance the horizontal scroll until we hit the end of the map, then turn around!
			// note: this should probably be done by a viewport fntick(), with appropriate tick priority

			g_viewport_worldx += g_scrolldir;

			if (g_viewport_worldx < c_scroll_safety_margin)
			{
				// bounce off the left side of the map
				g_scrolldir = -g_scrolldir;
				g_viewport_worldx += g_scrolldir;
				g_viewport_worldx += g_scrolldir;
			}
			else
			if (g_viewport_worldx >= g_map_xlimit)
			{
				// bounce off the right side of the map
				g_scrolldir = -g_scrolldir;
				g_viewport_worldx += g_scrolldir;
				g_viewport_worldx += g_scrolldir;
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

// holding down a key locks player multiples in formation, otherwise they 'snake' after the player
bool g_lockmultiples = false;

// ---------------------------------------------------------------------------------------------------------------------
// tick: player (input, plus options/multiples which follow)

void play_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// record change in position, if any
	s16 deltax = 0;
	s16 deltay = 0;

	// ---------------------------------------------------------------------------------------------------------------------
	// handle player input using scancode-based live key states
	//	- this does not require polling any hardware and does not disturb the machine state
	//	- it is easy and efficient, providing we don't have too many keys to check (if so, prefer an event handler)
	//	- using event handler would avoid missed keys but its not really neccessary for a 50hz fixed-framerate game

	// will become true if player moves, forcing multiples to follow
	bool snake = false;

	if (key_states[ScanCode_W])
	{
		// down!
		g_player_viewy -= 2;
		deltay = -2;

		// moving down, so tilt animation upwards
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
		deltay = 2;

		// moving down, so tilt animation downwards
		g_player_tilt += 1;
		if (g_player_tilt > (9<<c_player_tilt_fracbits)-1)
			g_player_tilt = (9<<c_player_tilt_fracbits)-1;

		// we're moving, so snake those multiples through the historybuffer
		snake = true;
	}
	else
	{
		// if not moving up or down, drift tilt animation back to centre frame #4
		// note: tsign() quickly returns +1, 0, -1 depending on sign of expression!
		g_player_tilt -= tsign16(g_player_tilt-(4<<c_player_tilt_fracbits)-(1<<(c_player_tilt_fracbits-1)));

		// ...and we don't snake the multiples. leave them in current position.
	}

	// set animation frame from tilt
	self.frame = g_player_tilt >> c_player_tilt_fracbits;

	// left and right is simple - doesn't affect animation yet (later will add engines)

	g_lockmultiples = false;

	if (key_states[ScanCode_A])
	{
		if (g_player_viewx >= 0)
		{
			// move left
			g_player_viewx -= 3;
			deltax = -3;
			snake = true;
		}
	}
	else 
	if (key_states[ScanCode_D])
	{
		// move right
		g_player_viewx += 3;
		deltax = 3;
		snake = true;
	}
	else 
	if (key_states[ScanCode_AT])
	{
		// '@' key forces multiples to snake after player
		snake = true;
	}

	// '/' key locks multiples in formation, overriding snake
	g_lockmultiples = key_states[ScanCode_FSLASH];

	// ---------------------------------------------------------------------------------------------------------------------
	// player world coordinates = (local) screen coords + (scroll) viewport world coords
	// this is needed for correct interactions with other objects in the world - especially those which don't follow scroll

	g_player_worldx = g_player_viewx + g_viewport_worldx;
	g_player_worldy = g_player_viewy + g_viewport_worldy;

	// ---------------------------------------------------------------------------------------------------------------------
	// 'snake' player positions through historybuffer to drive 'options'/'multiples'. easy peasy.
	// todo: this is a bit slow. C is crap at innerloops. do this kind of thing in asm.

	if (snake)
	{
		if (g_lockmultiples)
		{
			// retain multiples formation - move them all in unison with player (apply deltax,delray)
			s16* pout = &g_player_position_history[c_player_position_historysize];
			for (s16 p = c_player_position_historypairs-1; p >= 0; p--)
			{
				*(--pout) += deltay;	// y
				*(--pout) += deltax;	// x
			}
		}
		else
		{
			// propagate multiples' positions through history buffer

			// careful to do this in reverse because we're moving memory forwards and input / output 
			// buffers overlap
			// todo: provide a decent version of qmemmov for overlapped transfers

			// [X:0......63]
			// [Y:0......63]

			s16* pout = &g_player_position_history[c_player_position_historysize];
			s16* pin  = &g_player_position_history[c_player_position_historysize-2];

			for (s16 p = c_player_position_historypairs-1-1; p >= 0; p--)
			{
				*(--pout) = *(--pin);	// y
				*(--pout) = *(--pin);	// x
			}

			// feed the buffer head with current player position
			*(--pout) = g_player_viewy;
			*(--pout) = g_player_viewx;
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// find options in worldspace
	// note: this is about updating hitbox top-left origin, not sprite centre. the sprite is drawn offset from the hitbox.
	// however we do need to take both offsets into account since the multiples are supposed to appear centered on the player
	// Q: why does the engine deal with hitbox top-left origins instead of sprite centres?
	// A: because for performance reasons the engine relies more frequently on the former - e.g. for collisions and drawchain
	//    processing. the latter are needed less often - or calculated cheaply where needed.

	entity_t **ppeoption = g_pe_options;
	for (s16 spr = 0; spr < c_max_multiples; spr++)
	{
		entity_t *peoption = *ppeoption++;

		s16 history_index = ((spr+1) << (c_option_separation+1));	// (+1 because the buffer is x/y pairs)
		s16 historyx = g_player_position_history[history_index + 0];	// x
		s16 historyy = g_player_position_history[history_index + 1];	// y

		// set option upper-left coordinate based on history of player upper-left coordinates, taking both origins into account
		peoption->rx = g_viewport_worldx + historyx + self.ox - peoption->ox;
		peoption->ry = g_viewport_worldy + historyy + self.oy - peoption->oy;

		// overcomplicated way to bounce a 3-frame animation back and forth. a small table of [frame],[next] would be better
		s16 frame = tmod(((spr<<1)+(g_animtime>>6)), (2+2)+1);
		frame = xabs<s16>(frame - 2);

		peoption->frame = frame;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// load entity coordinates to allow interactions between player craft and other objects
	// note: also need to load this *before* spawning weapon shots from player entity or they will lag..

	self.rx = g_player_worldx;
	self.ry = g_player_worldy;

	// ---------------------------------------------------------------------------------------------------------------------
	// fire control

	if (key_states[ScanCode_RSHIFT] || key_states[ScanCode_LSHIFT]/* || 
		key_states[ScanCode_FSLASH]*/)
	{
		// this is a mask to break up the shooting pattern aross multiples and across subsequent frames
		// so we don't get too many bullets appearing at any one time
		static s16 pewcounter = 0;

		if (g_lasertype)
		{
			s16 type = (EntityType_LASER-1) + g_lasertype;
			EntityType etype = EntityType(type);

			// todo: player shots could be processed as multiple [0] with the others being [1...n]
			if ((pewcounter++ & 0x18) == 0)
			{
				EntitySpawnRel
				(
					etype, 
					g_pe_player, 
					8,0
				);
			}

			for (s16 o = 0; o < g_num_multiples; ++o)
			{
				if ((pewcounter++ & 0x18) == 0)
				{
					EntitySpawnRel
					(
						etype, 
						g_pe_options[o], 
						8,0
					);
				}
			}
		}
		else
		{
			s16 projectile_filter = pewcounter++;

			if ((projectile_filter & 0x7) == 0)
			{
				// todo: player shots could be processed as multiple [0] with the others being [1...n]
				EntitySpawnRel
				(
					EntityType_BULLET, 
					g_pe_player, 
					8,0
				);

				for (s16 o = 0; o < g_num_multiples; ++o)
				{
					EntitySpawnRel
					(
						EntityType_BULLET, 
						g_pe_options[o], 
						8,0
					);
				}
			}

			// missiles
			if (g_num_multiples > 0)
			{
				if ((projectile_filter & 0x7) == 0)
				{
					// this spreads out the missile shooting pattern over time and between multiples
					// by cycling through player-multiple index and skipping the shot if no such
					// shooter exists
					s16 index = ((projectile_filter & 0x3f) >> 3) - 1;

					if (index < 0)
					{
						EntitySpawnRel
						(
							EntityType_MISSILE, 
							g_pe_player, 
							0,0
						);
					}
					else
					{
						// launch missiles only for enabled multiples
						if (index < g_num_multiples)
							EntitySpawnRel
							(
								EntityType_MISSILE, 
								g_pe_options[index], 
								0,0
							);
					}
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: wavespawn

void wavespawn_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move wave-spawner vertically
	// note: horizontal spacing is achieved naturally as each spawn is delayed in time
	self.ry += self.vy;

	if ((self.counter & self.damage) == 0)
	{
		// spawn the enemy and let it go about its business

		// avoid saturating the entity chain with enemies - leave appropriate space for player bullets
		if (entities_freelist_count > (c_max_entities >> 2))
			EntitySpawnFrom
			(
				EntityType(self.frame),
				_pself
			);

		// delete self as soon as wave is complete (controlled by counter configured when spawner was created)
		if (self.counter == 0)
			EntityRemove(_pself);
	}

	// decrement counter towards wave completion
	self.counter--;
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: nme: amoeba

void nmeamoeba_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// we can make the object wobbly by steering some of the time, and misbehaving the rest of the time
	if (self.counter > 30)
	{
		// seek player
		// note: this is a slow-moving thing so we're using fixedpoint coordinates & velocities
		if (g_player_worldx > self.rx)
			self.frx += 0xc000;
		else
			self.frx -= 0x8000;

		if (g_player_worldy > self.ry)
			self.fry += 0x18000;
		else
			self.fry -= 0x18000;
	}
	else
	if (self.counter > 10)
	{
		// cheapo wobble effect #1
		self.rx += (((self.id + self.counter) & 8) - 4) >> 1;
		self.ry += ((self.counter & 4) - 2) >> 1;
	}
	else
	if (self.counter > 0)
	{
		// cheapo wobble effect #2
		self.rx += ((self.counter & 4) - 2) >> 1;
		self.ry += (((self.id + self.counter) & 8) - 4) >> 1;
	}
	else
	{
		// repeat!
		self.counter = 40;
	}

	// advance the 'wobble counter'
	self.counter--;

	// animate the object (0...5 and loop)
	if ((self.counter & 3) == 0)
	{
		self.frame++;
		if (self.frame >= 6)
			self.frame = 0;

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-16);
		u16 spy = self.ry - (g_viewport_worldy-16);
		if ((spx > (VIEWPORT_XSIZE+32)) || (spy > (VIEWPORT_YSIZE+32)))
			EntityRemove(_pself);
	}
}

void nmeamoeba_dash_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	self.frx += self.fvx;
	self.ry += self.vy;

	if ((self.counter & 31) == 0)
	{
		// seek player
		if (g_player_worldx > self.rx)
			self.fvx = 0x12000;
		else
			self.fvx = -0xc000;

		if (g_player_worldy > self.ry)
			self.vy = 1;
		else
			self.vy = -1;
	}


	// advance the 'wobble counter'
	self.counter--;

	// animate the object (0...5 and loop)
	if ((self.counter & 1) == 0)
	{
		self.frame++;
		if (self.frame >= 6)
			self.frame = 0;

		// kill ASAP when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-16);
		u16 spy = self.ry - (g_viewport_worldy-16);
		if ((spx > (VIEWPORT_XSIZE+32)) || (spy > (VIEWPORT_YSIZE+32)))
			EntityRemove(_pself);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: nme: spinny

void nmespinny_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	s16 c = self.counter;

	// occasionally shift movement pattern
	if ((c & 15) == 0)
	{
		c = (c >> 4) & 15;

		if (c == 1)
		{
			self.vy = 2;
			self.vx = 2+g_scrolldir-1;
		}
		else
		if (c == 9)
		{
			self.vy = -2;
			self.vx = 2+g_scrolldir-1;
		}
		else
		{
			self.vy = 0;
			self.vx = -2+g_scrolldir-1;
		}
	}

	self.counter++;

	// update position
	self.rx += self.vx;
	self.ry += self.vy;

	// continuous animation
	if (self.counter & 1)
	{
		self.frame++;
		if (self.frame >= 4)
			self.frame = 0;

		// just kill object beyond left screen edge
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		if (self.rx < (g_viewport_worldx-16))
			EntityRemove(_pself);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: nme: fly


void nmefly_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	s16 c = self.counter;

	// pseudo-random dropping of diamond mines.
	// note: the unique self::id field is used to keep individual entity decisions de-synchronized so they
	// don't all drop mines on the same frames. this trick is used throughout the demo to balance costs.
	if (((c^self.id) & 0xff) == 0)
		EntitySpawnFrom(EntityType_NMEMINE, _pself);

	// occasionally shift movement pattern
	if (((c+self.id) & 7) == 0)
	{

		s16 dx = g_pe_player->rx - self.rx;
		s16 dy = g_pe_player->ry - self.ry;

		// if too far away, navigate semi-randomly
		// note: our atan2 angle table only works for short vectors
		if ((((u16)(dx+96)) > 192) || 
			(((u16)(dy+96)) > 192) || 
			((dx & 0xc) == 0))
		{
			// select random direction
			const s32 *pdir = &s_fdirs_32ang[(self.id << 1) & 63];
			s32 fdx = pdir[0];
			s32 fdy = pdir[1] >> 1;

			if (((u16)(dy+16)) < 32)
			{
				// if near player-y, evade
				if (dy < 0)
					fdy = 0x14000;
				else
				if (dy >= 0)
					fdy = -0x14000;
			}
			else
			if (((u16)(dy+24)) > 48)
			{
				// otherwise drift towards player-y
				if (dy <= 0)
					fdy = -0x08000;
				else
				if (dy > 0)
					fdy = 0x08000;
			}

			// construct 4:4-bit rotation code from velocity vector
			s16 atancode = (((fdy>>14) & 0xf) << 4) | ((fdx>>14) & 0xf);

			// todo: (+4 & 15) can be baked into table but only if all sprites use the same frame mapping
			self.frame = (s_atan2_16frames[atancode] + 4) & 15;

			self.fvx = fdx;
			self.fvy = fdy;
		}
		// otherwise, navigate towards player
		else
		{
			// note: our atan2 angle table only works for short vectors so we normalize the distance vector

			// get 8:8-bit normalization paircode
			int normcode = ((dy & 0xFF) << 8) | (dx & 0xFF);
			u8 npair = g_normtab[normcode];

			// retrieve vector
			dy = (s8)npair >> 4;
			dx = ((s8)(npair << 4)) >> 4;

			// construct 4:4-bit rotation code from velocity vector
			s16 atancode = (((dy>>1) & 0xf) << 4) | ((dx>>1) & 0xf);

			// todo: (+4 & 15) can be baked into table but only if all sprites use the same frame mapping
			self.frame = (s_atan2_16frames[atancode] + 4) & 15;

			self.fvx = (s32)dx << (16-3);
			self.fvy = (s32)dy << (16-3);
		}

		// kill ASAP when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-16);
		u16 spy = self.ry - (g_viewport_worldy-16);
		if ((spx > (VIEWPORT_XSIZE+64)) || (spy > (VIEWPORT_YSIZE+32)))
			EntityRemove(_pself);
	}

	self.counter++;

	// update fixedpoint position
	self.frx += self.fvx;
	self.fry += self.fvy;
}

// ---------------------------------------------------------------------------------------------------------------------
// helper: provides a generic shooting function for some entities

static inline void nmeshoot(entity_t& self)
{
	entity_t *ppellet = EntitySpawnFrom
	(
		EntityType_NMEPELLET,
		&self
	);

	if (ppellet)
	{
		s16 dx = g_pe_player->rx - self.rx;
		s16 dy = g_pe_player->ry - self.ry;

		if ((((u16)(dx+128)) > 255) || 
			(((u16)(dy+128)) > 255))
		{
			// if too far away for good aim, use approximation (deliberately poor aim)
			dx >>= 6;
			dy >>= 6;
		}

		// note: our atan2 angle table only works for short vectors

		// get 8:8-bit normalization paircode
		int normcode = ((dy & 0xFF) << 8) | (dx & 0xFF);
		u8 npair = g_normtab[normcode];

		// retrieve vector
		dy = (s8)npair >> 4;
		dx = ((s8)(npair << 4)) >> 4;

		// aim pellet
		ppellet->fvx = (s32)dx << 14;
		ppellet->fvy = (s32)dy << 14;
	}
}

void nmefly2_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	s16 c = self.counter;

	// occasionally shift movement pattern
	if (((c+self.id) & 7) == 0)
	{
		s16 dx = g_pe_player->rx - self.rx;
		s16 dy = g_pe_player->ry - self.ry;

		// if too far away, advance towards player
		// note: our atan2 angle table only works for short vectors

		if ((((u16)(dx+128)) > 255) || 
			(((u16)(dy+128)) > 255))
		{
			s32 fdx = -0x18000;
			s32 fdy = 0;			

			// construct 4:4-bit rotation code from velocity vector
			s16 atancode = (((fdy>>14) & 0xf) << 4) | ((fdx>>14) & 0xf);

			// todo: (+4 & 15) can be baked into table but only if all sprites use the same frame mapping
			self.frame = (s_atan2_16frames[atancode] + 4) & 15;

			self.fvx = fdx;
			self.fvy = fdy;
		}
		// otherwise, race towards player
		else
		if (self.fvy == 0)
		{
			// get 8:8-bit normalization paircode
			int normcode = ((dy & 0xFF) << 8) | (dx & 0xFF);
			u8 npair = g_normtab[normcode];

			// retrieve vector
			dy = (s8)npair >> 4;
			dx = ((s8)(npair << 4)) >> 4;

			// construct 4:4-bit rotation code from velocity vector
			s16 atancode = (((dy>>1) & 0xf) << 4) | ((dx>>1) & 0xf);

			// todo: (+4 & 15) can be baked into table but only if all sprites use the same frame mapping
			self.frame = (s_atan2_16frames[atancode] + 4) & 15;

			self.fvx = (s32)dx << (16-2);
			self.fvy = (s32)dy << (16-2);
		}

		// kill ASAP when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-16);
		u16 spy = self.ry - (g_viewport_worldy-16);
		if ((spx > (VIEWPORT_XSIZE+64)) || (spy > (VIEWPORT_YSIZE+32)))
			EntityRemove(_pself);
	}

	// update position
	self.frx += self.fvx;
	self.fry += self.fvy;

	self.counter++;

	// random shooting
	// note: the unique self::id field is used to keep individual entity decisions de-synchronized so they
	// don't all act on the same frames. this trick is used throughout the demo to balance costs.
	if (((self.counter + self.id) & 0x7f) == 0)
		nmeshoot(self);
}

void nmequad_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// this object starts off with no player interaction flag - the dictionary defines it this way so
	// it acts like a ghost for a short period.
	// the interaction flag is enabled after the object has materialized via animation, after which it
	// can collide with the player.

	if (self.frame < 4)
	{
		if ((++self.counter & 15) == 0)
		{
			if (self.frame == 3)
			{
				// initially inert - but once fully materialized, THEN enable interaction with player... 
				self.f_interactswith |= EntityFlag_PLAYER;

				// and set trajectory...

				// separation vector
				s16 dx = g_pe_player->rx - self.rx;
				s16 dy = g_pe_player->ry - self.ry;

				// get 8:8-bit normalization paircode
				// note: if distance > +/-128, this may fail in a random direction. but we're ok with that.
				int normcode = ((dy & 0xFF) << 8) | (dx & 0xFF);
				u8 npair = g_normtab[normcode];

				// retrieve normal vector
				dy = (s8)npair >> 4;
				dx = ((s8)(npair << 4)) >> 4;

				// set velocity vector
				self.fvx = (s32)dx << 14;
				self.fvy = (s32)dy << 14;
			}

			// materialize...
			self.frame++;
		}
	}
	else
	{
		// seek player
		self.frx += self.fvx;
		self.fry += self.fvy;

		// lazy offscreen check - reduces cost a little
		if ((++self.counter & 15) == 0)
		{
			// kill when it goes offscreen
			// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
			u16 spx = self.rx - (g_viewport_worldx-16);
			u16 spy = self.ry - (g_viewport_worldy-16);
			if ((spx > (VIEWPORT_XSIZE+128)) || (spy > (VIEWPORT_YSIZE+16)))
				EntityRemove(_pself);
		}
	}
}

void nmesquare_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// first tick will initialize direction (we could swap the tick func - but less clear)
	if (self.counter == 0)
	{
		if (self.ry > g_pe_player->ry)
			self.vy = -1;
		else
			self.vy = 1;
	}

	// animate every 4th tick
	if ((++self.counter & 3) == 0)
	{
		// 6-frame looping animation
		self.frame++;
		if (self.frame >= 6)
			self.frame = 0;

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-32);
		u16 spy = self.ry - (g_viewport_worldy-32);
		if ((spx > (VIEWPORT_XSIZE+64)) || (spy > (VIEWPORT_YSIZE+32)))
		{
			EntityRemove(_pself);
			return;
		}
	}

	// advance
	self.ry += self.vy;
	// nudge when shot
	self.frx += self.fvx; self.fvx >>= 1;

	s16 pcx = (g_pe_player->rx + g_pe_player->ox);
	s16 pcy = (g_pe_player->ry + g_pe_player->oy);
	s16 scx = (self.rx + self.ox);
	s16 scy = (self.ry + self.oy);

	// detonate when close to player
	s16 dx = (pcx - scx) + 64;
	s16 dy = (pcy - scy) + 40;
	if (((u16)dx < 128) && ((u16)dy < 80))
	{
		// explosion
		entity_t * pespark = EntitySpawnFrom
		(
			EntityType_WSPARK, 
			_pself
		);

		// this scatters some shrapnel in fixed directions, biased towards the player position
		entity_t * pe0 = EntitySpawnFrom
		(
			EntityType_NMEPELLET, 
			_pself
		);
		if (pe0)
		{
			if (pcx > scx)
				pe0->vx = 2;
			else
				pe0->vx = -2;
			pe0->fvy = -0x8000;
		}

		entity_t * pe1 = EntitySpawnFrom
		(
			EntityType_NMEPELLET, 
			_pself
		);
		if (pe1)
		{
			if (pcx > scx)
				pe1->vx = 2;
			else
				pe1->vx = -2;
			pe1->fvy = 0x8000;
		}

		entity_t * pe4 = EntitySpawnFrom
		(
			EntityType_NMEPELLET, 
			_pself
		);
		if (pe4)
		{
			pe4->vx = 0;
			if (pcy > scy)
				pe4->vy = 2;
			else
				pe4->vy = -2;
		}

		// finally remove the mine itself
		EntityRemove(_pself);
	}
	//else
	//{
	//}
}


void nmetie_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move
	self.frx -= 0x8000;

	// animate every 4th tick
	if ((++self.counter & 3) == 0)
	{
		// occasionally shoot heavy laser
		if ((self.counter & 0x7f) == 0)
			EntitySpawnFrom
			(
				EntityType_NMELASER,
				&self
			);

		// 8-frame looping animation
		self.frame++;
		if (self.frame >= 8)
			self.frame = 0;

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-128);
		if (spx > (320+256))
			EntityRemove(_pself);
	}
}

void nmedrone_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move
	self.frx -= 0x8000;

	// animate every 4th tick
	s16 c = (++self.counter ^ self.id);

	if ((c & 0x3) == 0)
	{
		// 5-frame looping animation
		self.frame++;
		if (self.frame >= 5)
			self.frame = 0;

		// randomly drop mines
		s16 crand = (c ^ (g_frame_prng>>4));

		if ((crand & 0x1f) == 0)
			EntitySpawnFrom
			(
				EntityType_NMEMINE,
				&self
			);

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-128);
		if (spx > (320+256))
			EntityRemove(_pself);
	}
}

void nmecylon_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move horizontally, fixed movement
	self.rx -= 1;

	// generate sinewave-like y movement using a changing velocity

	s16 v = (self.counter+16)>>5;

	if (((v + 0) & 0x1) == 0)
		self.fvy += 0x2000;
	else
		self.fvy -= 0x2000;

	// move vertically in wave pattern
	self.fry += self.fvy;

	// animate every 4th tick
	if ((++self.counter & 3) == 0)
	{
		// 8-frame looping animation
		self.frame++;
		if (self.frame >= 8)
			self.frame = 0;

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-128);
		if (spx > (320+256))
			EntityRemove(_pself);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// generic enemy bullet/pellet thingy

void nmepellet_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// continue on trajectory
	self.frx += self.fvx;
	self.fry += self.fvy;

	if ((--self.counter & 7) == 0)
	{
		// lazy kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - g_viewport_worldx;
		u16 spy = self.ry - g_viewport_worldy;
		if ((spx >= VIEWPORT_XSIZE) || (spy >= VIEWPORT_YSIZE))
			EntityRemove(_pself);
	}
}

void nmelaser_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move in the initiated direction (well, in this demo it's always be towards the right but lets pretend its generic)
	self.rx += self.vx;
	// sneaky! when the player moves up/down, the lasers follow on the y axis instead of lagging behind like bullets, 
	// because the laser is a child entity of the player and can watch its parent's state...
	//self.ry = self.pparent->ry + self.pparent->oy;

	// terminate offscreen. careful to use world coordinates.
	// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
	// todo: can precalculate left/right limits once per frame and save precious cycles
	if (self.rx <= (g_viewport_worldx-30))
		EntityRemove(_pself);
}

void nmemine_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// animate
	if ((--self.counter & 7) == 0)
	{
		// flip anim frames 0/1
		self.frame ^= 1;

		// hack/slash/cheat: round x to 8 pixels to keep this a word-width sprite
		// this is possible because the object doesn't move and hardscroll handles the pixel offsets
		// note: requires sprite rectangle to be equivalent to hitbox rectangle
		self.rx &= -8;

		// lazy kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - g_viewport_worldx;
		if (spx >= 320)
			EntityRemove(_pself);
	}
}

void nmejelly_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	self.frx += self.fvx;
	self.fry += self.fvy;

	self.fvx += (s32(self.id + g_frame_prng)>>3 & 0xfff) - 0x800;
	self.fvy += (s32(self.id ^ g_frame_prng)>>3 & 0xfff) - 0x800;

	if ((--self.counter & 7) == 0)
	{
		// lazy kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-32);
		u16 spy = self.ry - (g_viewport_worldy-32);
		if ((spx > (VIEWPORT_XSIZE+128)) || (spy > (VIEWPORT_YSIZE+32)))
			EntityRemove(_pself);
	}
}

void walker_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	self.frx += self.fvx;

	// animate the object (slowly) though frames 0,1,2, then simply remove

	if ((self.counter & 3) == 0)
	{
		if ((self.counter & 15) == 0)
			nmeshoot(self);

		// animate through 4 frames
		self.frame = (self.frame + 1) & 3;

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-32);
		if (spx > (320+128))
			EntityRemove(_pself);
	}	

	self.counter++;
}

// ---------------------------------------------------------------------------------------------------------------------
// general purpose death animation tick function - for most common cases

void generic_deathanim_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// continue on trajectory
	self.rx += self.vx;
	self.ry += self.vy;

	// animate the object until preset frame (damage) reached, then delete self
	// note: we can reuse 'damage' variable for this because it's free when interactions are off
	if (self.counter-- & 1)
	{
		self.frame++;
		if (self.frame >= self.damage)
			EntityRemove(_pself);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: laser beam

void laser_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// sneaky!
	// we maintain [health] as a relative xoffset from the player, and sum with the player's 
	// position every tick. this forces the lasers be locked into the player's coordinate space
	// so they track all movements - i.e. they don't compress together or spread apart while the
	// player moves, as bullet projectiles do. bogus relativity! :) this is possible because
	// each object's pparent points at it's creator
	self.health += self.vx;
	self.rx = self.health + self.pparent->rx + self.pparent->ox;
	self.ry = self.pparent->ry + self.pparent->oy - self.oy;

	// terminate offscreen. careful to use world coordinates.
	// todo: can precalculate left/right limits once per frame and save precious cycles
	if ((self.rx >= (g_viewport_worldx + 320)) /*|| (self.rx <= (g_viewport_worldx - 32))*/)
		EntityRemove(_pself);
}

void bullet_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move in the initiated direction (well, in this demo it's always be towards the right but lets pretend its generic)
	self.rx += self.vx;

	// terminate offscreen. careful to use world coordinates.
	// todo: can precalculate left/right limits once per frame and save precious cycles
	if ((self.rx >= (g_viewport_worldx + 320)) /*|| (self.rx <= (g_viewport_worldx - 32))*/)
		EntityRemove(_pself);
}

void missile_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// move in the initiated direction (well, in this demo it's always be towards the right but lets pretend its generic)
	self.rx += self.vx;
	self.ry += self.vy;

	s16 c = self.counter++;
	if ((c & 3) == 0)
	{
		// every 4th tick, check status and recompute direction

		// construct 4:4-bit rotation code from velocity vector
		s16 atancode = ((self.vy & 0xf) << 4) | (self.vx & 0xf);
		self.health = (s_atan2_16frames[atancode] + 12) & 15;

		// terminate offscreen. careful to use world coordinates.
		// todo: can precalculate left/right limits once per frame and save precious cycles
		if ((self.rx >= (g_viewport_worldx + 320)) /*|| (self.rx <= (g_viewport_worldx - 32))*/)
			EntityRemove(_pself);
	}

	// artificial 'ground' stops fall
	if (self.ry > (240-24))
	{
		self.ry = (240-24);
		self.vy = 0;
	}

	// turn missile graphic towards computed direction
	if (self.frame < self.health)
		self.frame++;
	else
	if (self.frame > self.health)
		self.frame--;

}

// ---------------------------------------------------------------------------------------------------------------------
// tick: laser contact spark (actually, a pink splat/puff :-/ )

void wspark_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// animate the object @ 25hz, then die
	if (self.counter-- & 1)
	{
		self.frame++;
		if (self.frame >= 5)	// end at 6th frame (0-4 is valid)
			EntityRemove(_pself);
	}	
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: player crash-and-burn, after 'death'

void player_death_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// advance position from velocity vec (no longer under player control)
	self.rx += self.vx;
	self.ry += self.vy;

	// every 4th frame, spawn new flame puff
	if ((self.counter & 3) == 0)
	{
		// slightly randomize (!) spawn offset from ship
		entity_t *peflame = EntitySpawnRel
		(
			EntityType_DFLAME, 
			_pself,
			(((g_frame_prng>>2) & 1) << 2) - 2,
			((g_animtime & 1) << 2) - 2
		);
		// init flame anim counter
		// todo: can just be baked into dictionary. put here for clarity.
		if (peflame)
			peflame->counter = 7;
	}

	// every 16th frame, increment y-velocity (acc. under gravity)
	if ((self.counter & 15) == 0)
		self.vy += 1;

	// animate the object to full tilt for crash & burn!
	if (self.counter-- & 1)
	{
		if (self.frame < 8)
			self.frame++;
	}

	// finally remove the player from the system when it passes the bottom of the screen. then it's over.
	if (self.ry >= VIEWPORT_YSIZE)
		EntityRemove(_pself);
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: 'flames' from dying player

void dflame_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// animate the object (slowly) though frames 0,1,2, then simply remove ASAP
	if (--self.counter < 0)
	{
		self.counter = 7;

		if (self.frame >= 2)
			EntityRemove(_pself);

		// tip: can continue to modify object even if 'removed', without causing harm, thanks to deferred removal system
		self.frame++;
	}	
}

// ---------------------------------------------------------------------------------------------------------------------
// tick: powerup anim

void powerup1_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// animate the object (slowly) though frames 0,1,2, then simply remove ASAP
	if (--self.counter < 0)
	{
		self.counter = 3;

		// animate through 4 frames
		self.frame = (self.frame + 1) & 3;

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-32);
		if (spx > (320+32))
			EntityRemove(_pself);
	}	
}

void powerup2_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// animate the object (slowly) though frames 0,1,2, then simply remove ASAP
	if (--self.counter < 0)
	{
		self.counter = 3;

		// animate through 4 frames
		self.frame = 4 + ((self.frame + 1) & 3);

		// kill when it goes offscreen
		// note: we use unsigned comparison for fast 2D window check (-ve values look like large +ve values)...
		u16 spx = self.rx - (g_viewport_worldx-32);
		if (spx > (320+32))
			EntityRemove(_pself);
	}	
}

void smartbomb_fntick(entity_t *_pself)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// just delete self one tick after exploding all the things
	if (self.counter-- < 0)
		EntityRemove(_pself);
}

// =====================================================================================================================
//	collision reaction functions
// =====================================================================================================================

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: player vs other

void play_fncol(entity_t *_pself, entity_t *_pother)
{

#if (ENABLE_PLAYER_RANDOMKILL)
	static s16 s_randomkill = 0;
	if (s_randomkill++ > (((g_frame_prng>>5) & 127) + 128))
	{
		s_randomkill = 0;
	}
	else
	{
		return;
	}
#else
#if (0==ENABLE_PLAYER_COLLISIONS)
		return;
#endif
#endif


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
		self.fntick = &player_death_fntick;

		// make the object crash in a gravity well (initial y velocity = 0, but will increment)
		self.vx = 1;
		self.vy = 0;

//		AGT_MusicStop();
////		AGT_MusicLoad("g3gover.bmz");
//		AGT_MusicLoad("g1coin.bmz");
//		AGT_MusicStart(0);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: powerup vs player

// yellow extras powerup
void powerup1_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// some 'padding' for a more convincing pickup
	self.health--;
	if (self.health < 0)
	{
		// hack: only provide laser if at least 1 multiple, otherwise its too difficult too early
		if (g_num_multiples > 0)
			g_lasertype = (g_lasertype + 1) & 3;

		if ((g_lasertype & 1) == 0)
		{
			// grant special powers! (no UI menu, so use a fixed sequence for simplicity :-/ )
			if (g_num_multiples < c_max_multiples)
			{
				// turn on the multiple
				g_pe_options[g_num_multiples]->drawtype = EntityDraw_EMXSPRQ;

				// increment the count, for shooting control etc.
				g_num_multiples++;
			}
		}

		// remove powerup
		EntityRemove(_pself);
	}
}

// blue spartbomb powerup
void powerup2_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// some 'padding' for a more convincing pickup
	self.health--;
	if (self.health < 0)
	{
		// kill all-the-things by spawning a giant death-slab
		EntitySpawnRel(EntityType_SMARTBOMB, s_pe_viewport, c_viewport_xmargin, c_viewport_ymargin);

		// remove powerup
		EntityRemove(_pself);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: NME vs other

void nmeamoeba_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// deduct damage caused by other
	self.health -= _pother->damage;
	self.draw_hitflash += 3;

	// nudge the nme back from the collision (fake!)
	// todo: this is the worst kind of crap hack but it'll do
	self.rx += 5;

	// if damage is excessive, convert object into inert animation with new tick function
	if (self.health <= 0)
	{
		// clear all properties except tick/2D, so the 'death puff' doesn't try to interact with anything
		self.f_self &= EntityFlag_TICKED|EntityFlag_SPATIAL;
		self.f_interactswith = 0;

		// change tick function to simply animate then kill itself...
		self.fntick = &generic_deathanim_fntick;

		// make the object fly away in some quasi-random direction
		self.vx = 3;
		self.vy = (g_frame_prng & 2) - 1;

		// start animating death from frame #5, die before frame #12
		self.frame = 5;
		self.damage = 12;
	}
	else
	// otherwise just get annoyed and dash the player by changing the tick function to an aggressive one
	if (self.health <= 80)
		self.fntick = &nmeamoeba_dash_fntick;
}

void nmesquare_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// deduct damage caused by other
	self.health -= _pother->damage;
	self.draw_hitflash += 3;

	// nudge the nme back from the collision (fake!)
	// todo: this is the worst kind of crap hack but it'll do
	self.fvx += 5<<16;

	// deduct damage caused by other
	self.health -= _pother->damage;

	if (self.health <= 0)
	{
		// explosion
		entity_t * pespark = EntitySpawnFrom
		(
			EntityType_WSPARK, 
			_pself
		);

		// randomly spawn a powerup
		if ((g_sequence-- & 0x15) == 0)
		{
			EntitySpawnFrom
			(
				EntityType_POWERUP2,
				_pself
			);
		}

		EntityRemove(_pself);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: generic explosion-death for the most common, small-sprite case

void nmegeneric_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// deduct damage caused by other
	self.health -= _pother->damage;
	self.draw_hitflash += 3;

	if (self.health <= 0)
	{
		// explosion
		entity_t * pespark = EntitySpawnFrom
		(
			EntityType_WSPARK, 
			_pself
		);

		// randomly spawn a powerup
		if ((g_sequence-- & 0x15) == 0)
		{
			EntityType bonustype = EntityType_POWERUP1;

			// rarely yield a smartbomb instead!
			if ((g_player_worldy & 31) == 0)
				bonustype = EntityType_POWERUP2;

			EntitySpawnFrom
			(
				bonustype,
				_pself
			);
		}

		// kill self
		EntityRemove(_pself);
	}
}

void nmejelly_fncol(entity_t *_pself, entity_t *_pother)
{
	// get non-aliasable reference to object
	entity_t &self = *_pself;

	// deduct damage caused by other
	self.health -= _pother->damage;
	self.draw_hitflash += 3;

	// explode into a collection of amoeba entities
	if (self.health <= 0)
	{
		EntitySpawnRel
		(
			EntityType_NMEAMOEBA, 
			_pself,
			12,1
		);
		EntitySpawnRel
		(
			EntityType_NMEAMOEBA, 
			_pself,
			-6,5
		);
		EntitySpawnRel
		(
			EntityType_NMEAMOEBA, 
			_pself,
			-5,-7
		);
		//EntitySpawnFrom
		//(
		//	EntityType_NMEAMOEBA, 
		//	_pself
		//);

		// kill self
		EntityRemove(_pself);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// collision reaction: laser beam vs any other

void laser_fncol(entity_t *_pself, entity_t *_pother)
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

	// player objects...

	// player																					// tick player first...
	//	summary:
	//	- the player!
	{ &play_fntick,&play_fncol,0,0,				{0},{0},{0},{0},0,0,0,0,		0,0,			30,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_PLAYER|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_POWERUP },
	// options																					// ...options second
	//	summary:
	//	- glowing inert blob which follows player, mimics weaponry
	{ 0,0,0,0,									{0},{0},{0},{0},0,0,0,0,		1,0,			0,0,0,0,			EntityDraw_NONE,0,		EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },
	// laser weapon																				// ...then everything else
	//	summary:
	//	- fast moving laser, speed = 32
	{ &laser_fntick,&laser_fncol,0,0,			{0},{0},{32},{0},0,0,0,0,		2,0,			8,15,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_BULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_NME },
	{ &laser_fntick,&laser_fncol,0,0,			{0},{0},{32},{0},0,0,0,0,		2,0,			8,15,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_BULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_NME },
	{ &laser_fntick,&laser_fncol,0,0,			{0},{0},{32},{0},0,0,0,0,		2,0,			8,15,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_BULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_NME },
	// bullet weapon																			// ...then everything else
	//	summary:
	//	- normal bullet with speed = 16
	{ &bullet_fntick,&laser_fncol,0,0,			{0},{0},{16},{0},0,0,0,0,		2,0,			0,15,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_BULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_NME },
	// missile weapon																			// ...then everything else
	//	summary:
	//	- missile with diagonal trajectory
	//	- slow firing rate, but lots of damage!
	{ &missile_fntick,&laser_fncol,0,0,			{0},{0},{4},{4},0,0,0,0,		2,0,			2,50,2,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_BULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_NME },
	// smartbomb weapon																			// smartbomb ticks last, after it works
	//	summary:
	//	- big invisible collider which briefly covers the screen, damages all enemies and dies a frame later
	{ &smartbomb_fntick,0,0,0,					{0},{0},{0},{0},320,240,0,0,	3,0,			0,999,0,0,			EntityDraw_NONE,0,		EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_NME },

	// inert effects...

	// weapon spark [DISABLED]
	//	summary:
	//	- spark animation when player weapon contacts something (currently more of a 'splat')
	{ &wspark_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		2,0,			0,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },

	// death flame
	//	summary:
	//	- trail produced by player ship upon destruction
	{ &dflame_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		2,0,			0,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },


	// powerups...

	// powerup cell #1 (yellow)
	//	summary:
	//	- adds to player weaponry in fixed sequence
	{ &powerup1_fntick,&powerup1_fncol,0,0,		{0},{0},{0},{0},0,0,0,0,		2,0,			0,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_POWERUP|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },

	// powerup cell #2 (blue)
	//	summary:
	//	- smartbomb, affects only enemy objects currently onscreen
	{ &powerup2_fntick,&powerup2_fncol,0,0,		{0},{0},{0},{0},0,0,0,0,		2,0,			0,0,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_POWERUP|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },


	// enemy objects...

	// wavespawn																
	//	summary:
	//	- generic spawnpoint which spits out other enemies at specified intervals
	//	- the 'frame' field is used to specify which enemy type
	//	- the 'counter' field determines the length of time for spawning a pattern (longer = more things spawned)
	//	- the 'fvx/fvy' velocity vector can be set to define the separation distance/direction between each spawn
	//	- the 'damage' field is a bitmask which determines frequency of spawning e.g. 1,3,7,15,31 etc... with larger values spawning at half the frequency of the previous
	//	- object is itself invisible and does not interact
	//	- destroys itself when counter depletes
	{ &wavespawn_fntick,0,0,0,					{0},{0},{0},{0},0,0,0,0,		1,0,			0,0,0,128,			EntityDraw_NONE,0,		EntityFlag_TICKED, 0 },


	// nme:pellet
	//	summary:
	//	- generic round bullet fired by most enemies
	{ &nmepellet_fntick,0,0,0,					{0},{0},{0},{0},0,0,0,0,		2,0,			0,10,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_NMEBULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },

	// nme:laser
	//	summary:
	//	- horizontal beam fired by a few enemies
	{ &nmelaser_fntick,0,0,0,					{0},{0},{-2},{0},0,0,0,0,		2,0,			0,12,0,0,			EntityDraw_EMXSPRQ,0,	EntityFlag_NMEBULLET|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER },

	// nme:walker
	// summary: 
	//	- enters from right 
	//	- wanders along ground
	{ &walker_fntick,&nmegeneric_fncol,0,0,		{0},{0},{-1},{0},0,0,0,0,		2,0,			200,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:spinny
	// summary: 
	//	- enters from right in wave formation
	//	- zig-zags in predictable pattern
	{ &nmespinny_fntick,&nmegeneric_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			10,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:fly
	//	summary:
	//	- 360' zrotation
	//	- seeks player, tries to avoid player's primary weapon
	//	- leaves trail of diamonds, which damage on contact
	{ &nmefly_fntick,&nmegeneric_fncol,0,0,		{0},{0},{0},{0},0,0,0,0,		2,0,			20,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:fly2
	//	summary: 
	//	- 360' zrotation
	//	- advances from right, tries to ram player
	//	- shoots pellets
	{ &nmefly2_fntick,&nmegeneric_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			10,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:amoeba
	//	summary: 
	//	- wobbles roughly towards player, very tough, rushes player when damaged
	//	- has custom death/explode animation
	//	- can convert to rock obstacle
	{ &nmeamoeba_fntick,&nmeamoeba_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			50,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:quad
	//	summary: 
	//	- materializes in groups around player
	//	- once materialized, will try to ram player but can't steer once set in motion
	//	- can't harm player while materializing, but can be shot
	{ &nmequad_fntick,&nmegeneric_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			10,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_BULLET },

	// nme:square
	//	summary: 
	//	- appear in vertical waves from spawnpoint at top/bottom of map
	//	- proximity mine, explodes if player gets too near
	//	- very tough. nudged when shot.
	{ &nmesquare_fntick,&nmesquare_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			80,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_BULLET },

	// nme:mine
	//	summary: 
	//	- diamond-shaped obstacle left behind by other enemies
	//	- causes damage on contact
	{ &nmemine_fntick,&nmegeneric_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			0,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:tie
	//	summary: 
	//	- drifts in from right in waves, in a straight line
	//	- shoots horizontal laser beams
	{ &nmetie_fntick,&nmegeneric_fncol,0,0,		{0},{0},{0},{0},0,0,0,0,		2,0,			100,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:jelly
	//	summary: 
	//	- gets in the way, absorbing bullets
	//	- doesn't harm player on contact
	{ &nmejelly_fntick,&nmejelly_fncol,0,0,		{0},{0},{0},{0},0,0,0,0,		2,0,			350,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_BULLET },

	// nme:drone
	//	summary: 
	//	- drifts in from right in waves, in a straight line
	//	- leaves behind diamonds
	{ &nmedrone_fntick,&nmegeneric_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			200,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },

	// nme:cylon
	//	summary: 
	//	- drifts in from right in waves, in a wobbly formation
	{ &nmecylon_fntick,&nmegeneric_fncol,0,0,	{0},{0},{0},{0},0,0,0,0,		2,0,			10,10,0,0,			EntityDraw_EMXSPR,0,	EntityFlag_NME|EntityFlag_TICKED|EntityFlag_SPATIAL,	EntityFlag_PLAYER | EntityFlag_BULLET },
};

//----------------------------------------------------------------------------------------------------------------------
//	Music fixup callback: opportunity to set the instruments and multiplex routing for specific songs
//----------------------------------------------------------------------------------------------------------------------

#if defined(ENABLE_AGT_AUDIO)
void AGT_MusicFixupCallback(const char* const _name, remap_ft &remap_func)
{
	// todo: crude - provide a proper callback filter
	if (strncmp(_name, "salslash", 8) == 0)
	{
		// shift down half octave
		for (s16 c = 0; c < 16; c++)
			g_bmm_channel_octave_map[c]		= -6*1;

		// shift bass up +1 octave relative to song
		g_bmm_channel_octave_map[9-1]		= 6*1;

		// remap selected instruments (default: YMI_Piano)
		g_instrument_map[gmi_SynthBass1]	= YMI_FretPhaseTri;
		// replace instrument on MIDI channel #9
		channel_gmi_overrides[9-1]			= gmi_SynthBass1;

		// manual MIDI->YM routing for 16 midi channels
		static const s8 routing[16] = 
		{ 
			1,2,1,2,	1,2,0,1,	0,(-1),2,1,		2,0,1,2
		};

		// ...aaaand we're done
		set_channel_routing(routing);
	}
}
#endif // defined(ENABLE_AGT_AUDIO)
