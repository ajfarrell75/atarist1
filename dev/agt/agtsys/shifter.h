//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	shifter display interface
//----------------------------------------------------------------------------------------------------------------------

#ifndef shifter_h_
#define shifter_h_

//----------------------------------------------------------------------------------------------------------------------

enum STEDisplayMode
{
	STLow200,
	STLow240,
	STLow256,
	STLow272,
	//
	Nickel
};

enum NickelEvent
{
	NE_PortSelect,		// begin displaying pixels using next queued displayport config (i.e. next viewport)
	NE_ColourBurst,		// write sequence of contiguous palette colours
	NE_RegisterLoad,	// write constants into specific hardware registers, pattern specified by user
	NE_CallBack,		// call some user code
	NE_Blank,			// stop displaying pixels (temporarily) but continue processing events
	NE_Stop,			// stop processing events AND stop displaying pixels
	NE_End				// stop processing events but continue displaying pixels (until the next natural border)
};

enum NickelPort
{
	NickelPort0,	
		NickelPortDefault = NickelPort0,
	NickelPort1,
	NickelPort2,
	NickelPort3,
	NickelPort4,
	NickelPort5,
	NickelPort6,
	NickelPort7,
//	..
};

//----------------------------------------------------------------------------------------------------------------------
// convert R8,G8,B8 to STE swizzled 0x0RGB (compiletime constant)

#define STE_RGB8(_r_,_g_,_b_) \
		(((((((_r_ + 0x07) > 0xFF) ? 0xFF : (_r_ + 0x07)) >> 1) & 0x08) | (((((_r_ + 0x07) > 0xFF) ? 0xFF : (_r_ + 0x07)) >> 5) & 0x07))<<8) | \
		(((((((_g_ + 0x07) > 0xFF) ? 0xFF : (_g_ + 0x07)) >> 1) & 0x08) | (((((_g_ + 0x07) > 0xFF) ? 0xFF : (_g_ + 0x07)) >> 5) & 0x07))<<4) | \
		(((((((_b_ + 0x07) > 0xFF) ? 0xFF : (_b_ + 0x07)) >> 1) & 0x08) | (((((_b_ + 0x07) > 0xFF) ? 0xFF : (_b_ + 0x07)) >> 5) & 0x07))<<0)

//----------------------------------------------------------------------------------------------------------------------

typedef struct nickel_op_s
{
	s16 type;
	s16 line;
	s16 index;
	s16 count;
	void *pvalues;

} nickel_op_t;

//----------------------------------------------------------------------------------------------------------------------

// fixed at 2, even if field interlace (dualfield) is off - in which case both fields are assigned the same page.
static const int c_max_shiftfields = 2;

extern "C" volatile s16 g_fieldmask;

// all hardware state associated with single display 'unit' (i.e. address, scroll, linewidth, palette)
struct shiftfield
{
	u8 ah_;
	u8 am_;
	u8 al_;
	u8 pskip_;
	u8 wskip_;
	u8 _pad_;
	
	u16 palette_[32];

} __attribute__((packed));

// shiftgroup for interlacing displays
struct shiftgroup
{
	// indexed via vbl field in vbl asm code, to reach shiftfields_
	volatile shiftfield* pshiftfields_[c_max_shiftfields];

	shiftfield shiftfields_[c_max_shiftfields];

	u8 lace_;
	u8 _pad_;
	
} __attribute__((packed));
	
// ---------------------------------------------------------------------------------------------------------------------

static const int c_max_shiftgroups = 2;
static const int c_max_views = 16;

extern "C" volatile shiftgroup *g_pgroups[c_max_views][c_max_shiftgroups];
//extern "C" volatile shiftgroup **g_views[c_max_views];
//extern "C" volatile s16 g_active_buffer[c_max_views];
extern "C" volatile shiftgroup* volatile gp_active_group[c_max_views];
extern "C" volatile s16 g_vbl;
extern "C" s16 g_frame;

// ---------------------------------------------------------------------------------------------------------------------

class shifter_common
{
public:
	
	//static volatile s16 s_vbl;

	void* save_physbase;
//	void* save_logbase;
//	short save_res;
//	u32 save_vbl;
//	u32 save_hbl;
//	u8 save_regs[32];
	
/*	volatile shiftgroup groups_[2];
	volatile s16 active_buffer_;
*/
	s16 hidden_buffer_[c_max_views];
	
	shifter_common()
		: save_physbase(0),
		  //save_logbase(0),
		  //save_res(0),
		  //save_vbl(0),
		  //hidden_buffer_(0),
		  fieldmask_(0),
		  current_mode_(0)
	{
		for (int v = 0; v < c_max_views; v++)
			hidden_buffer_[v] = 0;
	}

	~shifter_common()
	{
		dprintf("destroy shifter...\n");
	}

	static void wait_vbl();
	static void wait_vbl_minbound(s16 &r_counter, s16 _count);

	// note: these are masked by subclasses - not virtual!
	void save();
	void init_common();
	void restore_common();

	void set_fieldmode(int _dual) { fieldmask_ = g_fieldmask = (_dual ? 1 : 0); }

	inline void set_hwpalette(s16 *palette)
	{
		__asm__ __volatile__ ("				\
			move.l	%0,%%a0;					\
			movem.l	(%%a0),%%d0-%%d7;				\
			movem.l	%%d0-%%d7,0xFFFF8240.w;		\
		"
			: 
			: "g"(palette)
			: "%%a0", "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7", "cc"
		);	
	}

	inline void get_hwpalette(s16 *palette)
	{
		__asm__ __volatile__ ("				\
			move.l	%0,%%a0;					\
			movem.l	0xFFFF8240.w,%%d0-%%d7;		\
			movem.l	%%d0-%%d7,(%%a0);				\
		"
			: 
			: "g"(palette)
			: "%%a0", "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7", "cc"
		);	
	}

	// set display_ state for one field of hidden buffer, before g_displaylocked it
	inline void setdisplay(s16 _field, u32 _address, s16 _ppixelwidth, s16 _vpixelwidth, u16 _x, u16 _y, s16 _view) const
	{
//		while (_field) { }

//		volatile shiftgroup& vg = g_groups[hidden_buffer_];
		shiftgroup& g = *(shiftgroup*)(g_pgroups[_view][hidden_buffer_[_view]]);
//		shiftfield& s = g.shiftfields_[_field];
		shiftfield& s = *(shiftfield*)(g.pshiftfields_[_field]);

		// shortcut for benefit of vbl asm code (removes struct-size-sensitivity)
		//g.pshiftfields_[_field] = &s;
		
		s32 addr = (s32)_address + ((_x & -16) >> 1) + ((s16)_y * (_vpixelwidth >> 1));

#if defined(STE_RELEASE_CONFIG)
		s16 scroll = _x & 0xf;
#else
		s16 scroll = _x & scrollmask_;
#endif

#if !defined(STF_RELEASE_CONFIG)
//		s16 skip = ((_vpixelwidth - _ppixelwidth) >> 2) - ((scroll > 0) ? 4 : 0);
		s16 skip = ((_vpixelwidth - _ppixelwidth) >> 2) - ((-scroll >> 4) & 4);
#endif // !defined(STF_RELEASE_CONFIG)

#if defined(STF_SPOOF)
		scroll = 0;
		skip = 0;
#endif // defined(STF_SPOOF)

		g.lace_ = scroll & 1;//(_x ^ _y) & 1;

		s.ah_ = (u8)(addr >> 16);
		s.am_ = (u8)(addr >> 8);
		s.al_ = (u8)(addr);

#if !defined(STF_RELEASE_CONFIG)
		s.pskip_ = scroll;
		s.wskip_ = skip;//u8(skip & 0xff);
#endif // !defined(STF_RELEASE_CONFIG)

	}

	// advance display_ queue (i.e. swap buffers)
	inline void advance(int _view)
	{
		//g_active_buffer[_view] 
		s16 next = hidden_buffer_[_view];
		hidden_buffer_[_view] ^= 1;
		gp_active_group[_view] = g_pgroups[_view][next];
	}

	// advance display_ queue (i.e. swap buffers)
	inline void advanceto(int _new, int _view)
	{
		s16 next = hidden_buffer_[_view];
		hidden_buffer_[_view] = _new;
		gp_active_group[_view] = g_pgroups[_view][next];
	}
	
protected:

	s16 save_palette_[16];
//	s16 save_blit_[32];
	s16 fieldmask_;
	s16 scrollmask_;

public:

	int current_mode_;
	
	static shifter_common* s_shifter;
};

// ---------------------------------------------------------------------------------------------------------------------

class shifter_stf : public shifter_common
{
public:

	shifter_stf()
		: shifter_common()
	{
	}

	void configure(s16 _hscrollmask);

	void init();
	void restore();

	void setcolour(s16 _buffer, s16 _field, s16 _ci, u16 _c);
	void setpalette(s16 _buffer, s16 _field, u16* _colours);
};

// default to STF if don't care
//typedef shifter_stf shifter;

// ---------------------------------------------------------------------------------------------------------------------

class shifter_ste : public shifter_common
{
public:

	shifter_ste()
		: shifter_common()
	{
		current_mode_ = (s16)STEDisplayMode::STLow240;
	}

	void configure();

	void init(STEDisplayMode _mode = STEDisplayMode::STLow240);
	void restore();

	void setcolour(s16 _buffer, s16 _field, s16 _ci, u16 _c, s16 _view = 0);
	void setpalette(s16 _buffer, s16 _field, u16* _colours, s16 _view = 0);

	void load_nickel(nickel_op_t *_pevents, int _timing_resilience = 2);
};

// ---------------------------------------------------------------------------------------------------------------------

#define RGB444_TO_STE(_v_) (((_v_ >> 1) & 0x777) | ((_v_ << 3) & 0x888))

// ---------------------------------------------------------------------------------------------------------------------

#endif // shifter_h_
