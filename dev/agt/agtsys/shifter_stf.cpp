//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	Shifter display interface: STF specific
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

// ---------------------------------------------------------------------------------------------------------------------

#include "shifter.h"

// ---------------------------------------------------------------------------------------------------------------------

extern "C" void STF_ConfigureDisplayService();
extern "C" void STF_InstallDisplayService();
extern "C" void STF_RemoveDisplayService();

// ---------------------------------------------------------------------------------------------------------------------

void shifter_stf::configure(s16 _hscrollmask)
{
	scrollmask_ = _hscrollmask;

	STF_ConfigureDisplayService();
}

void shifter_stf::init()
{	
	init_common();

	STF_InstallDisplayService();
}

void shifter_stf::restore()
{
	restore_common();

	STF_RemoveDisplayService();
}

// ---------------------------------------------------------------------------------------------------------------------

/*
0000	0	0
1000	1	8
0001	2	1
1001	3	9
0010	4	2
1010	5	a
0011	6	3
1011	7	b
0100	8	4
1100	9	c
0101	a	5
1101	b	d
0110	c	6
1110	d	e
0111	e	7
1111	f	f
*/
static s16 s_reswizzle[] = { 0x0, 0x8, 0x1, 0x9, 0x2, 0xa, 0x3, 0xb, 0x4, 0xc, 0x5, 0xd, 0x6, 0xe, 0x7, 0xf };
static s16 s_unswizzle[] = { 0x0, 0x2, 0x4, 0x6, 0x8, 0xa, 0xc, 0xe, 0x1, 0x3, 0x5, 0x7, 0x9, 0xb, 0xd, 0xf };

static s16 s_lumoffset[] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0xf, 0xf };

static inline u16 offset_stfcol(u16 _rgb444, s16 _cindex, s16 _field, s16 _offset)
{
	s16 ste_r = (_rgb444 >> 8) & 0xF;
	s16 ste_g = (_rgb444 >> 4) & 0xF;
	s16 ste_b = (_rgb444) & 0xF;

	if ((_cindex + _field) & 1)
	{
		ste_r = s_reswizzle[s_lumoffset[s_unswizzle[ste_r] + _offset]];
		ste_b = s_reswizzle[s_lumoffset[s_unswizzle[ste_b] + _offset]];
	}
	else
	{
		ste_g = s_reswizzle[s_lumoffset[s_unswizzle[ste_g] + _offset]];
	}
	
	return (ste_r << 8 | ste_g << 4 | ste_b);
}

void shifter_stf::setcolour(s16 _buffer, s16 _field, s16 _ci, u16 _c)
{
	volatile shiftgroup& g = *(g_pgroups[0][_buffer]);//g_groups[_buffer];
	volatile shiftfield& s = *(g.pshiftfields_[_field]);//g.shiftfields_[_field];	

	s.palette_[_ci]    = offset_stfcol(_c, _ci, 0, 1); 
	s.palette_[_ci+16] = offset_stfcol(_c, _ci, 1, 1);
}

void shifter_stf::setpalette(s16 _buffer, s16 _field, u16* _colours)
{
	volatile shiftgroup& g = *(g_pgroups[0][_buffer]);//g_groups[_buffer];
	volatile shiftfield& s = *(g.pshiftfields_[_field]);//g.shiftfields_[_field];	

	for (s16 c = 0; c < 16; ++c)
	{
		u16 col = _colours[c];	

		s.palette_[c]    = offset_stfcol(col, c, 0, 1); 
		s.palette_[c+16] = offset_stfcol(col, c, 1, 1);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
