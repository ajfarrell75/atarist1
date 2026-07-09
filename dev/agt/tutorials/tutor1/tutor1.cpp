//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================

//----------------------------------------------------------------------------------------------------------------------
//	tutorial: The Shifter display interface
//----------------------------------------------------------------------------------------------------------------------
//	controls: none
//----------------------------------------------------------------------------------------------------------------------
//	notes:
//	+	incremental changes from 'tutor0'
//	+	hardware compatibility check
//	+	claim over hardware
//	+	define front and back screen buffers
//	+	initialise the shifter interface
//	+	run a simple 1vbl mainloop with colour changes and scrolling bar pattern
//	+	clean exit
//
//	additional:
//	+	not an ideal starting point for a real project - see tutor4 onwards for a more realistic example
//	+	this helps illustrate the interface to the display state, so it may be used directly by the programmer.
//		however this low level of access is not typical of normal AGT usage for game playfields. later tutorials will 
//		link the shifter interface with other engine layers which provide the extra functionality needed for games.

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
//#include "agtsys/compress.h"

// machine stuff
#include "agtsys/system.h"

// display stuff
#include "agtsys/shifter.h"

// keyboard, joysticks & mouse
//#include "agtsys/input.h"

// background stuff
//#include "agtsys/tileset.h"
//#include "agtsys/worldmap.h"
//#include "agtsys/playfield.h"
//#include "agtsys/arena.h"

// sprite stuff
#include "agtsys/spritesheet.h"
#include "agtsys/slabsheet.h"
//#include "agtsys/spritelib.h"

// entity/gameobject interface
#include "agtsys/entity.h"

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
//	CAUTION: this is only meant to be simple for the tutorial - could end up in TTRam on some Ataris and break...

// physical display size
#define SCREEN_XSIZE (320)
#define SCREEN_YSIZE (240)

// virtual display size including h-scroll margin
#define VIEWPORT_XSIZE (SCREEN_XSIZE)
#define VIEWPORT_YSIZE (SCREEN_YSIZE)


#define SCREEN_LINES_STORAGE (256)

// define word buffers for a couple of hardcoded screens. add a single column of +16 to accommodate hscroll.
u16 screenbuffer0[SCREEN_LINES_STORAGE * ((SCREEN_XSIZE+16)/4)];
u16 screenbuffer1[SCREEN_LINES_STORAGE * ((SCREEN_XSIZE+16)/4)];

// an array of 2 pointers, one for each screenbuffer. used to manually select between them using index 0 or 1.
u16* screenbuffer_ptrs[] =
{
	screenbuffer0,
	screenbuffer1
};

// =====================================================================================================================
//	program entrypoint
// =====================================================================================================================

int AGT_EntryPoint()
{
	// ---------------------------------------------------------------------------------------------------------------------
	//	welcome message

	printf("[A]tari [G]ame [T]ools / dml 2017\n");

	printf("The Shifter display interface\n");


	// ---------------------------------------------------------------------------------------------------------------------
	//	detect hardware & configure AGT

	machinestate.configure();


	// ---------------------------------------------------------------------------------------------------------------------
	//	compatibility check for this example

	if (!bSTE && !bMegaSTE && !bFalcon030)
	{
		// ---------------------------------------------------------------------------------------------------------------------
		// if not one of these 3 machines, complain and bail out...

		printf("unsupported machine!\n");
		printf("press space to quit...\n");
		Crawcin();
	}
	else
	{
		// ---------------------------------------------------------------------------------------------------------------------
		//	...otherwise continue with program

		printf("press space to start...\n");
		// wait for key using BIOS (we haven't installed our AGT input services - TOS still in charge)
		Crawcin();


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
		//	now claim control over all machine resources - display, timers, input etc.

		printf("claim hardware...\n");
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
		shift.set_fieldmode(/*dualfield=*/false);
		shift.init();

		// ---------------------------------------------------------------------------------------------------------------------
		//	load palette for both front and backbuffer 'shiftstates'. we just use some random colours here.

		printf("set palette...\n");	

		s16 rgb = 28123;
		for (s16 c = 0; c < 16; ++c)
		{
			// generate a random colour sequence
			rgb = (rgb * 19237) + 8723;

			// set colour 'c' for logic/backbuffer (next)
			shift.setcolour(0, 0, c, rgb);

			// set colour 'c' for physic/frontbuffer (current)
			shift.setcolour(1, 0, c, rgb);
		}

		// ---------------------------------------------------------------------------------------------------------------------
		//	fill both screenbuffers with some kind of vertical bar pattern, so we can see the scroll
		//	note: some upper lines are blanked out to achieve 240 visible & centered, so we fill some extra lines

		s32 dst = 0;
		for (s32 data = 0; data < (SCREEN_LINES_STORAGE*(20+1)); data++)
		{
			// we write 4 plane-words to 2 buffers at once, on each loop
			screenbuffer0[dst] = 0xF000;
			screenbuffer1[dst] = 0xF000;
			dst++;
			screenbuffer0[dst] = 0xA000;
			screenbuffer1[dst] = 0xA000;
			dst++;
			screenbuffer0[dst] = 0x5000;
			screenbuffer1[dst] = 0x5000;
			dst++;
			screenbuffer0[dst] = 0x1000;
			screenbuffer1[dst] = 0x1000;
			dst++;
		}

		// =====================================================================================================================
		//	mainloop
		// =====================================================================================================================

		// ---------------------------------------------------------------------------------------------------------------------
		//	some persistent variables we're going to need in the mainloop

		s16 backbuffer_num = 0;			// index of current backbuffer (0 or 1)
		s16 xscroll = 0, yscroll = 0;	// scrolling offsets 

		// ---------------------------------------------------------------------------------------------------------------------
		//	start the mainloop - 256 frames and then quit

		for (s16 frames = 0; frames < 256; frames++)
		{

			// ---------------------------------------------------------------------------------------------------------------------
			//	wait for the current VBL period to elapse

			shift.wait_vbl();

			// ---------------------------------------------------------------------------------------------------------------------
			//	cycle colour 0 for a short while, then restore it to black
			//	this represents some dummy mainloop 'work', which might become game logic later on

			for (s32 flash = 0; flash < 1000; flash++)
			{
				reg16(FFFF8240) += 0x237; 
			}
			reg16(FFFF8240) = 0x000;

			// ---------------------------------------------------------------------------------------------------------------------
			//	when we're done with 'work', activate the current backbuffer as the new frontbuffer

			// get pointer to the current backbuffer
			u16 *backbuffer_ptr = screenbuffer_ptrs[backbuffer_num];

			// configure shifter with our buffer, sizes and scroll offsets
			shift.setdisplay(0, (u32)backbuffer_ptr, SCREEN_XSIZE, VIEWPORT_XSIZE, xscroll, yscroll, /*displayport=*/0);

			// 'commit' these shifter changes on the *next* vblank to occur (i.e. the shifter interrupt)
			// (without this, the display changes will only be pending)
			shift.advance(/*displayport=*/0);

			// scroll the screen towards the left, but loop back after 16 pixels
			xscroll = (xscroll + 1) & 15;

			// ---------------------------------------------------------------------------------------------------------------------
			// toggle buffers so next time we write to the new backbuffer

			backbuffer_num = backbuffer_num ^ 1;

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
// empty entity dictionary (required)
// =====================================================================================================================

entitydef_t entity_dictionary[] =
{
};
