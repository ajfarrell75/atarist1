//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// drawcontext holds copy of all relevant fields for the active workbuffer, to permit drawing
// this structure is passed to drawing functions to minimise argument exchanges over the drawing APIs
//----------------------------------------------------------------------------------------------------------------------

#ifndef drawcontext_h_
#define drawcontext_h_

// ---------------------------------------------------------------------------------------------------------------------

typedef struct restorestate_s
{
	s32 *pstack;
	s32 *ptide;

	void init()
	{
		pstack = nullptr;
		ptide = nullptr;
	}

} restorestate_t;

typedef struct drawcontext_s
{
	// playfield buffers, arrays
	u16 *p_pfframebuffer;
	s32 *p_pflineidx;
	s16	*p_pflinemod;

	// playfield info fields
	s32 pfvpagebytes;
	s16 pfvpageh;
	//s16 pfviewy;
	s16 pfsnapadjx;
	s16 pfsnapadjy;
	s16 vpagex;
	s16 vpagey;
	s16 guardx; // virtual guardband - always in hardware
	s16 guardy; // virtual guardband - independent of hardware

	// BG restoration mechanisms

	// page-based rectangle restore
	restorestate_t *prestorestate;

	// BG tile restore & occlusion mapping
	s16 *p_ocm_t;
	s16 *p_ocm_o;
	s16 ocm_stride;

	s16 field;

	//void *pspriteclear_stack_prv;
	//void *pspriteclear_stack_cur;
	//void *pspriteclear_stack_tide;
	//void *pspriteclear_stack_prvtide;

	// vpage-adjusted guard rejection window
	s16 guard_window_x1;
	s16 guard_window_xs;
	s16 guard_window_y1;
	s16 guard_window_ys;
	s16 guard_window_x2;
	s16 guard_window_y2;

	// physical clip region for IMS and some other stuff
	s16 scissor_window_x1p;
	s16 scissor_window_xs;
	s16 scissor_window_y1;
	s16 scissor_window_ys;
	s16 scissor_window_x2p;
	s16 scissor_window_y2;

} drawcontext_t;

extern "C" drawcontext_t g_drawcontext;
extern "C" drawcontext_t *g_pactivedctx;

//----------------------------------------------------------------------------------------------------------------------

#endif // drawcontext_h_

