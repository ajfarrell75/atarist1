//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	example: test different sprite formats EMS, EMX, EMH, SLAB on same spritedata with timing information
//----------------------------------------------------------------------------------------------------------------------
//	controls: see bg.png background image
//----------------------------------------------------------------------------------------------------------------------
//	notes: none

//----------------------------------------------------------------------------------------------------------------------

// can disable some formats here, although they can be toggled with keys 1-4 anyway
#define TEST_EMX
#define TEST_EMH
#define TEST_EMS
#define TEST_SLAB

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

#include <mint/sysbind.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "agtsys/common_cpp.h"

// basic memory interface
#include "agtsys/ealloc.h"
//#include "agtsys/compress.h"

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

// ---------------------------------------------------------------------------------------------------------------------7
// define some entity types

enum EntityType : int
{
	// special viewport entity, tracks visible world
	EntityType_VIEWPORT,

	// sprite object
	EntityType_EMS,
	EntityType_EMX,
	EntityType_EMH,
	EntityType_SLAB,

	// end!
	EntityType_MAX_
};

// ---------------------------------------------------------------------------------------------------------------------7
// some things we currently need to define even if we don't use them

static entity_t *s_pe_viewport = NULL;

static drawcontext_t drawcontext;

// ---------------------------------------------------------------------------------------------------------------------
//	hardware interfaces

// interface to hardware claim (for save/restore & clean exit)
machine machinestate;

// interface to STE display services
shifter_ste shift;

// ---------------------------------------------------------------------------------------------------------------------
//	MiNTlib builds need need to define a local superstack. otherwise, AGT does it during CRT startup.
// ---------------------------------------------------------------------------------------------------------------------

S_SUPER_SSP(AGT_CONFIG_STACK);

AGT_MAIN;

// ---------------------------------------------------------------------------------------------------------------------
//	define some space for the display.

// physical display size
#define SCREEN_XSIZE (320)
#define SCREEN_YSIZE (272)

// virtual display size including h-scroll margin
#define VIEWPORT_XSIZE (SCREEN_XSIZE)
#define VIEWPORT_YSIZE (SCREEN_YSIZE)

// ---------------------------------------------------------------------------------------------------------------------
//	configure our scrolling playfield system

// define a playfield config for 8-way scrolling 
typedef playfield_template
<
    /*tilesize=*/4,
    /*max_scroll=*/8192,
    /*vscroll=*/VScrollMode_LOOPBACK,			// loopback = effectively infinite vertical scroll
    /*hscroll=*/HScrollMode_SCANWALK,			// scanwalk = finite, but cheap with huge scroll range
    /*restore=*/RestoreMode_PAGERESTORE,		// pagerestore = restore sprites from a shadow playfield page
    /*updatemode=*/UpdateMode_NORMAL,
	/*hwattr=*/PlayfieldHardware				// hardware configuration
	(
		PFH_STE|PFH_Blitter
	),
	/*pfattr=*/PlayfieldAttributes				// enable specific playfield features
	(
		PFA_None
	),
	/*guardx=*/64,								// x guardband size (NOTE: must generate EMX with '-emgx 64')
	/*guardy=*/64								// y guardband size

> playfield_t;

// configure double-buffering
static const int c_num_buffers = 2;

// define an arena (world) composed of N such playfields (N-buffered)
typedef arena_template
<
    /*buffers=*/c_num_buffers,
    /*singleframe=*/false,
    /*dualfield=*/false,
    /*playfield_type=*/playfield_t
> arena_t;

// ---------------------------------------------------------------------------------------------------------------------
//	playfield assets

// playfield tile assets
tileset mytiles;

// playfield map assets
worldmap mymap;

// ---------------------------------------------------------------------------------------------------------------------
// sprite assets

// test sprite
spritesheet ems_asset;
spritesheet emx_asset;
spritesheet emh_asset;
slabsheet slab_draw_asset;
slabsheet slab_clear_asset;

// ---------------------------------------------------------------------------------------------------------------------
//	playfield world coordinates

// clipping margin outside of the scrolling viewport, to accommodate the largest typical sprite before deletion etc.
static const s16 c_viewport_xmargin = playfield_t::c_guardx;
static const s16 c_viewport_ymargin = playfield_t::c_guardy;

// world map safety margin (in pixels) for speculative scrolling algorithm
static const int c_scroll_safety_margin = 16;

// viewport (scroll) starting position in world map
static s16 g_viewport_worldx = c_scroll_safety_margin; 
static s16 g_viewport_worldy = c_scroll_safety_margin;

// current sprite animation frame (change with +/- keys)
s16 g_sprframe = 0;
// toggle rasters on/off
s16 g_rasters = 0;

s16 g_enabled = 0; // tracks which objects are enabled
s16 g_cal_draw = 0; // calibrated time wasted when drawing nothing (fixed engine overhead)
s16 g_cal_clr = 0; // calibrated time wasted when clearing nothing (fixed engine overhead)

// ---------------------------------------------------------------------------------------------------------------------
//	sincos table

#include "tables/sintab32.h"

// =====================================================================================================================
//	program entrypoint
// =====================================================================================================================

int AGT_EntryPoint()
{
	// ---------------------------------------------------------------------------------------------------------------------
	//	welcome message

	printf("[A]tari [G]ame [T]ools / dml 2022\n");

	printf("Test: sprite format performance\n");

	// ---------------------------------------------------------------------------------------------------------------------
	//	detect hardware & configure AGT

	machinestate.configure();


	// ---------------------------------------------------------------------------------------------------------------------
	//	compatibility check for this example

	if (!bSTE)
	{
		// ---------------------------------------------------------------------------------------------------------------------
		// if not one of these machines, complain and bail out...

		printf("unsupported machine!\n");
		printf("press space to quit...\n");
		Crawcin();
	}
	else
	{
		// ---------------------------------------------------------------------------------------------------------------------
		//	...otherwise continue with program

		// ---------------------------------------------------------------------------------------------------------------------
		//	save machine state for a clean exit

		printf("save machine state...\n");
		machinestate.save();
		
		// ---------------------------------------------------------------------------------------------------------------------
		//	shifter (display) initialisation
		//	pull any machine-specific metrics out of the display service & save state before any drawing systems are used

		printf("configure shifter...\n");
		shift.configure();
		shift.save();

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map tiles

		printf("loading tiles...\n");
		mytiles.load_cct("bg.cct");

		// ---------------------------------------------------------------------------------------------------------------------
		//	load map
		
		printf("loading map...\n");
		mymap.load_ccm("bg.ccm");

		// ---------------------------------------------------------------------------------------------------------------------
		//	get map dimensions (x16 -> in pixels) to help keep scroll inside the map limits

		s16 map_xtiles = mymap.getwidth();
		s16 map_ytiles = mymap.getheight();

		s16 map_xpixels = map_xtiles << 4;
		s16 map_ypixels = map_ytiles << 4;


		// max scroll is map size minus screen dimensions (plus safety margin for the scrolling axes)
		s16 map_xlimit = (map_xpixels-VIEWPORT_XSIZE-c_scroll_safety_margin);
		s16 map_ylimit = (map_ypixels-VIEWPORT_YSIZE-c_scroll_safety_margin);

		// ---------------------------------------------------------------------------------------------------------------------
		//	create an instance of the arena (our game world), connected to the shifter interface

		printf("creating world...\n");
		arena_t myworld(shift, SCREEN_XSIZE, SCREEN_YSIZE);

		// ---------------------------------------------------------------------------------------------------------------------
		//	associate map & tiles with the arena playfield(s)
	
		printf("assigning world map...\n");
		myworld.setmap(&mymap);

		printf("assigning world tiles to each playfield buffer...\n");
		for (int b = myworld.c_num_buffers_-1; b >= 0; b--)
		{
			myworld.select_buffer(b);
			myworld.settiles(&mytiles, &mytiles);
		}

		// ---------------------------------------------------------------------------------------------------------------------
		//	load sprite gfx assets
		// ---------------------------------------------------------------------------------------------------------------------

		// each format loaded will reduce this accordingly if frame count is lower
		int lastframe = 100;

#if defined(TEST_EMS)
		ems_asset.load("1ems.ems");
		EntityDefAsset_Sprite(EntityType_EMS,  &ems_asset);
		lastframe = xmin(lastframe, ems_asset.get()->framecount - 1);
#endif

#if defined(TEST_EMX)
		emx_asset.load("2emx.emx");
		EntityDefAsset_Sprite(EntityType_EMX,  &emx_asset);
		lastframe = xmin(lastframe, emx_asset.get()->framecount - 1);
#endif

#if defined(TEST_EMH)
		emh_asset.load("3emh.emh");
		EntityDefAsset_Sprite(EntityType_EMH,  &emh_asset);
		lastframe = xmin(lastframe, emh_asset.get()->framecount - 1);
#endif

#if defined(TEST_SLAB)
		AGT_BLiT_SlabSetupOnce();

		slab_draw_asset.load_sls("4slab.sls");
		slab_clear_asset.load_sls("4slab.slr");

		sls_pair slab_assetpair;
		slab_assetpair.assign(slab_draw_asset.get(), slab_clear_asset.get());
		EntityDefAsset_Slab(EntityType_SLAB, &slab_assetpair);
		lastframe = xmin(lastframe, slab_draw_asset.get()->framecount - 1);
#endif

		// ---------------------------------------------------------------------------------------------------------------------

		AGT_Sys_ConsoleMode(AGTConsole_MINIMISED);

		// ---------------------------------------------------------------------------------------------------------------------

		printf("ready... (press key)\n");
		Crawcin();

		// ---------------------------------------------------------------------------------------------------------------------
		//	now claim control over all machine resources - display, timers, input etc.

		printf("claim hardware...\n");
		machinestate.claim();

		// ---------------------------------------------------------------------------------------------------------------------
		//	install input service for keyboard, joysticks, mouse
		//	note: joysticks/pads not yet tied - will be fixed soon

		AGT_InstallInputService();

		// ---------------------------------------------------------------------------------------------------------------------
		//	initialize display
		// ---------------------------------------------------------------------------------------------------------------------

		printf("init shifter...\n");
		shift.set_fieldmode(/*dualfield=*/false);

		// use the tallest display mode so we have plenty of room with lower console enabled
		shift.init(STLow272);

		// ---------------------------------------------------------------------------------------------------------------------
		//	load palette for both front and backbuffer 'shiftstates'. we use the tile library palette.

		printf("set palette...\n");	

		for (s16 c = 0; c < 16; ++c)
		{
			// logic (next)
			shift.setcolour(0, 0, c, mytiles.getcol(c));

			// physic (current)
			shift.setcolour(1, 0, c, mytiles.getcol(c));
		}

#if defined(TIMING_RASTERS_MAIN)
		// save copy of BG colour for rasters
		s16 bgcol = mytiles.getcol(0);
#endif
		// ---------------------------------------------------------------------------------------------------------------------
		//	init world with initial viewport coordinates

		printf("init world viewport...\n");
		myworld.init(g_viewport_worldx, g_viewport_worldy);

		// =====================================================================================================================
		//	mainloop setup
		// =====================================================================================================================

		// ---------------------------------------------------------------------------------------------------------------------
		//	initialize entity manager - removes any lingering entities, sets things up for a new session

		EntitySetup(EntityType_MAX_);

		// ---------------------------------------------------------------------------------------------------------------------
		//	create entities needed for this session - Viewport

		// create a viewport entity so we can track its relationship with everything else in the world
		// note: viewport is larger than physical screen, by at least playfield guardband (viewport margin)
		s_pe_viewport = EntitySpawn
		(
			EntityType_VIEWPORT, 
			g_viewport_worldx-c_viewport_xmargin, 
			g_viewport_worldy-c_viewport_ymargin
		);

		// select as active viewport
		EntitySelectViewport(s_pe_viewport);

		// =====================================================================================================================
		//	mainloop
		// =====================================================================================================================

		// ---------------------------------------------------------------------------------------------------------------------
		//	some persistent variables we're going to need in the mainloop

		s16 backbuffer_num = 0;			// index of current backbuffer (0 or 1)

		// =====================================================================================================================
		//	spawn objects
		
		entity_t *pe;
		
#if defined(TEST_EMS)
		pe = EntitySpawn(EntityType_EMS,  160,20);
#endif

#if defined(TEST_EMX)
		pe = EntitySpawn(EntityType_EMX,  160,60);
#endif

#if defined(TEST_EMH)
		pe = EntitySpawn(EntityType_EMH,  160,100);
#endif

#if defined(TEST_SLAB)
		pe = EntitySpawn(EntityType_SLAB, 160,140);
#endif


		// =====================================================================================================================
		//	must link any pending entities before starting mainloop (viewport is an entity and viewport scans rely on it)

		EntityExecuteAllLinks();

		// ---------------------------------------------------------------------------------------------------------------------
		//	start the mainloop - run forever

		s16 s_vbl_check;
		s16 s_vbl_overrun;

		while (1)
		{

			// ---------------------------------------------------------------------------------------------------------------------
			//	schedule timing measurements & refresh console every 64th frame

			s16 do_scanmeasure = 0;
			static s16 s_decay = 0;
			if (--s_decay < 0)
			{
				do_scanmeasure = 1;
				s_decay = 64;
			}

			// ---------------------------------------------------------------------------------------------------------------------
			//	handle global keyboard inputs (per-object inputs are handled in the object's tick code)

			if (debounced_key_releases[ScanCode_EQU])
			{
				debounced_key_releases[ScanCode_EQU] = 0;
				if (g_sprframe < lastframe)
					g_sprframe++;
			}	
			else
			if (debounced_key_releases[ScanCode_MINUS])
			{
				debounced_key_releases[ScanCode_MINUS] = 0;
				if (g_sprframe > 0)
					g_sprframe--;
			}
			else
			if (debounced_key_releases[ScanCode_R])
			{
				debounced_key_releases[ScanCode_R] = 0;
				g_rasters ^= 1;
			}	

			// ---------------------------------------------------------------------------------------------------------------------
			//	wait for the current VBL period to elapse

			s_vbl_overrun = (s_vbl_check != g_vbl); // true if all drawing took >1 VBL, scanline measurements will be wrong

			if (g_rasters)
				TIMING_MAIN(bgcol);

			shift.wait_vbl();

			s_vbl_check = g_vbl;


			// ---------------------------------------------------------------------------------------------------------------------
			//	now wait for scanline timer to start (wastes a small amount of VBL time, but makes timing more accurate)

			STE_WaitDisplayTimerStart();

			// ---------------------------------------------------------------------------------------------------------------------
			//	move viewport

			// manually update the viewport first. we don't need a full tick for this but we do need to keep the viewport sorted
			g_pe_viewport->rx = g_viewport_worldx - c_viewport_xmargin;
			g_pe_viewport->ry = g_viewport_worldy - c_viewport_ymargin;

			// ---------------------------------------------------------------------------------------------------------------------
			//	tick entities, link any new spawns

			EntityTickAll();

			EntityExecuteAllLinks();


			// ---------------------------------------------------------------------------------------------------------------------
			//	update the background & scroll

			// select the next workbuffer (..of two - we're using double buffering here)
			myworld.select_buffer(backbuffer_num % c_num_buffers);

			// update the playfield coordinates for this draw pass
			myworld.setpos(g_viewport_worldx, g_viewport_worldy);


			s32 scanmeasure_beginclear = STE_GetDisplayTimer();

			if (g_rasters)
				TIMING_MAIN(0x732);

			// restore playfield from any previous drawing (sprite clearing)
			myworld.bgrestore();

			if (g_rasters)
				TIMING_MAIN(bgcol);

			s32 scanmeasure_endclear = STE_GetDisplayTimer();


			// get the current draw context, needed for entity drawing
			myworld.get_context(drawcontext, 0, VIEWPORT_XSIZE, VIEWPORT_YSIZE);

/*
			static const s16 c_guard_reduce = 0;

			drawcontext.guard_window_x1 += c_guard_reduce;
			drawcontext.guard_window_xs -= (c_guard_reduce*2);
			drawcontext.guard_window_y1 += c_guard_reduce;
			drawcontext.guard_window_ys -= (c_guard_reduce*2);
			drawcontext.guard_window_x2 -= c_guard_reduce;
			drawcontext.guard_window_y2 -= c_guard_reduce;

			// physical clip region for IMS and some other stuff
			drawcontext.scissor_window_x1p += c_guard_reduce;
			drawcontext.scissor_window_xs -= (c_guard_reduce*2)+16;;
			drawcontext.scissor_window_y1 += c_guard_reduce;
			drawcontext.scissor_window_ys -= (c_guard_reduce*2);
			drawcontext.scissor_window_x2p -= c_guard_reduce+16;
			drawcontext.scissor_window_y2 -= c_guard_reduce;
*/
			// fill any background tiles required by this change in coordinates
			myworld.hybrid_fill(false);

			// ---------------------------------------------------------------------------------------------------------------------
			//	draw objects

			if (g_rasters)
				TIMING_MAIN(0x372);

			s32 scanmeasure_begindraw = STE_GetDisplayTimer();

			EntityDrawVisible(&drawcontext);

			s32 scanmeasure_enddraw = STE_GetDisplayTimer();

			if (g_rasters)
				TIMING_MAIN(bgcol);

			// ---------------------------------------------------------------------------------------------------------------------
			//	if a measurement was scheduled, do it now

			if (do_scanmeasure)
			{
				// check to see if measurement is valid or not (can't count scans on a VBL overrun)
				if (s_vbl_overrun)
				{
					eprintf("error: vbl overrun! disable some sprites\n");
				}
				else
				{
					if (g_enabled == 0)
					{
						g_cal_draw = (scanmeasure_enddraw-scanmeasure_begindraw);
						g_cal_clr = (scanmeasure_endclear-scanmeasure_beginclear);
						printf("calibrate: -drw=%d -clr=%d\n", 
							g_cal_draw, 
							g_cal_clr
						);
					}
					else
					{
						printf("measure: tot=%d drw=%d clr=%d\n", 
							scanmeasure_enddraw, 
							(scanmeasure_enddraw-scanmeasure_begindraw)-g_cal_draw, 
							(scanmeasure_endclear-scanmeasure_beginclear)-g_cal_clr
						);
					}
				}
			}

			// ---------------------------------------------------------------------------------------------------------------------
			//	present the current workbuffer as the new frontbuffer

			myworld.activate_buffer();

			// ---------------------------------------------------------------------------------------------------------------------
			//	toggle buffers so next time we write to the new backbuffer

			backbuffer_num++;

			// ---------------------------------------------------------------------------------------------------------------------
			//	update the scroll coordinates

			// ---------------------------------------------------------------------------------------------------------------------
			//	repeat forever...
		}

		// =====================================================================================================================
		//	shutdown: release hardware claim back to TOS
		// =====================================================================================================================

		shift.restore();

		machinestate.restore();

	}

	// ---------------------------------------------------------------------------------------------------------------------
	// back to TOS

	pprintf("terminate...\n");

	return 0;
}

// =====================================================================================================================
// tick function

static const s32 c_movespeed = 1 << 16;

// tick function for smaller objects with no occlusion maps - allow them to enter the guardband
void ems_fntick(entity_t *_pself)
{
	entity_t& self = *_pself;
	self.frame = g_sprframe;

	if (key_states[ScanCode_W])
	{
		self.fry -= c_movespeed;
	}
	if (key_states[ScanCode_S])
	{
		self.fry += c_movespeed;
	}
	if (key_states[ScanCode_A])
	{
		self.frx -= c_movespeed;
	}
	if (key_states[ScanCode_D])
	{
		self.frx += c_movespeed;
	}

	if (debounced_key_releases[ScanCode_1])
	{
		debounced_key_releases[ScanCode_1] = 0;
		self.drawtype ^= EntityDraw_EMSPR;
		g_enabled ^= 1<<0;
	}	
}

void emx_fntick(entity_t *_pself)
{
	entity_t& self = *_pself;
	self.frame = g_sprframe;

	if (key_states[ScanCode_T])
	{
		self.fry -= c_movespeed;
	}
	if (key_states[ScanCode_G])
	{
		self.fry += c_movespeed;
	}
	if (key_states[ScanCode_F])
	{
		self.frx -= c_movespeed;
	}
	if (key_states[ScanCode_H])
	{
		self.frx += c_movespeed;
	}	

	if (debounced_key_releases[ScanCode_2])
	{
		debounced_key_releases[ScanCode_2] = 0;
		self.drawtype ^= EntityDraw_EMXSPR;
		g_enabled ^= 1<<1;
	}	
}

void emh_fntick(entity_t *_pself)
{
	entity_t& self = *_pself;
	self.frame = g_sprframe;

	if (key_states[ScanCode_I])
	{
		self.fry -= c_movespeed;
	}
	if (key_states[ScanCode_K])
	{
		self.fry += c_movespeed;
	}
	if (key_states[ScanCode_J])
	{
		self.frx -= c_movespeed;
	}
	if (key_states[ScanCode_L])
	{
		self.frx += c_movespeed;
	}

	if (debounced_key_releases[ScanCode_3])
	{
		debounced_key_releases[ScanCode_3] = 0;
		self.drawtype ^= EntityDraw_EMHSPR;
		g_enabled ^= 1<<2;
	}	
}

void slb_fntick(entity_t *_pself)
{
	entity_t& self = *_pself;
	self.frame = g_sprframe;

	if (key_states[ScanCode_UP])
	{
		self.fry -= c_movespeed;
	}
	if (key_states[ScanCode_DOWN])
	{
		self.fry += c_movespeed;
	}
	if (key_states[ScanCode_LEFT])
	{
		self.frx -= c_movespeed;
	}
	if (key_states[ScanCode_RIGHT])
	{
		self.frx += c_movespeed;
	}	
	if (debounced_key_releases[ScanCode_4])
	{
		debounced_key_releases[ScanCode_4] = 0;
		self.drawtype ^= EntityDraw_SLAB;
		g_enabled ^= 1<<3;
	}	
}

// =====================================================================================================================
// empty entity dictionary (required)
// =====================================================================================================================

entitydef_t entity_dictionary[EntityType_MAX_] =
{
	// [functions]								[coords]						[control]		[tick state]		[rendering]							[classification properties]
	// tick,collide,asset,hasset				px/y,vx/y,sx/y,ox/y				tickpri,drflag	h,d,frame,counter	drawtype,drawlayer					f_self,f_interactswith	

	// engine objects...

	// viewport
	{ 0,0,0,0,									{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,0,					EntityFlag_VIEWPORT|EntityFlag_SPATIAL,	0 },

	// other objects...

	// entity defaults
	{ &ems_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,EntityLayer_0,		EntityFlag_INERT|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },
	{ &emx_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,EntityLayer_1,	EntityFlag_INERT|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },
	{ &emh_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,EntityLayer_2,	EntityFlag_INERT|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },
	{ &slb_fntick,0,0,0,						{0},{0},{0},{0},0,0,0,0,		0,0,			0,0,0,0,			EntityDraw_NONE,EntityLayer_3,		EntityFlag_INERT|EntityFlag_TICKED|EntityFlag_SPATIAL,	0 },

};
