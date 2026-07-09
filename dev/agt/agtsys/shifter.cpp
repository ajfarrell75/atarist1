//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	Shifter display interface: generic
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

#include <mint/sysbind.h>

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

// ---------------------------------------------------------------------------------------------------------------------

#include "shifter.h"

// ---------------------------------------------------------------------------------------------------------------------

extern "C" 
{
	volatile shiftgroup g_groups[c_max_shiftgroups*c_max_views];

	volatile shiftgroup *g_pgroups[c_max_views][c_max_shiftgroups]; // *g_pgroups[] = { &g_groups[0], &g_groups[1] };
	//volatile shiftgroup **g_views[c_max_views];
	//volatile s16 g_active_buffer[c_max_views] = { -1 };    
	volatile shiftgroup* volatile gp_active_group[c_max_views] = { 0 };
	volatile s16 g_vbl = 0;
	volatile s16 g_fieldmask = 0;
	s16 g_frame = 0;
}

// ---------------------------------------------------------------------------------------------------------------------

void shifter_common::save()
{
	save_physbase = Physbase();
//	save_logbase = Logbase();
//	save_res = Getrez();
	
	get_hwpalette(save_palette_);
}

void shifter_common::init_common()
{	
	int n = 0;
	for (int v = 0; v < c_max_views; v++)
	{
		for (int i = 0; i < c_max_shiftgroups; i++, n++)
		{
			volatile shiftgroup *pgroup = &g_groups[n];
			g_pgroups[v][i] = pgroup;
			pgroup->pshiftfields_[0] = &pgroup->shiftfields_[0];
			pgroup->pshiftfields_[1] = &pgroup->shiftfields_[1];
		}
		//g_views[v] = g_pggroups[v];
	}

	s_shifter = this;
}

void shifter_common::restore_common()
{
	// todo: should save/restore regs instead of using this API - lazy today
	for (int f = 0; f < c_max_shiftfields; f++)
	{
		setdisplay(f, (u32)save_physbase, 320, 320, 0, 0, 0); //update();	
		//setdisplay(1, (u32)save_physbase, 160, 160, 0, 0); //update();
	}
	advance(0);
	
	// VSync
	s16 field_index = reg16(464);
	while (reg16(464) == field_index) { }
				
	set_hwpalette(save_palette_);

//	Setscreen(save_logbase, save_physbase, save_res);
}

static inline s16 ring_delta(s16 high, s16 low)
{
	s16 delta = (s16)((u16)high - (u16)low);
	return delta;
}

void shifter_common::wait_vbl_minbound(s16 &r_counter, s16 _count)
{
	// note: g_vbl decrements!
	s16 future = r_counter - _count;
	s16 sample;
	while (ring_delta(future, (sample = g_vbl)) < 0) { }
	r_counter = sample;
}

void shifter_common::wait_vbl()
{
	s16 field_index = g_vbl;
	while (g_vbl == field_index) { }
}

// ---------------------------------------------------------------------------------------------------------------------

// global shifter instance for VBL callback
shifter_common* shifter_common::s_shifter = 0;

// ---------------------------------------------------------------------------------------------------------------------
