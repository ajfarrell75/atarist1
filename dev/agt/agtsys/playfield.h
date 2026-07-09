//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield systems
//----------------------------------------------------------------------------------------------------------------------

#ifndef playfield_h_
#define playfield_h_

//#define AGT_CONFIG_PRINT_PLAYFIELD_METRICS

//----------------------------------------------------------------------------------------------------------------------

#include "drawcontext.h"

extern "C" u16 * volatile g_rst_pfb;
extern "C" s32 * volatile g_rst_pflineidx;
extern "C" volatile s32 g_rst_vpagebytes;
extern "C" volatile s16 g_rst_vpageh;
extern "C" volatile restorestate_t * volatile g_rst_state;

extern "C" s16 * volatile dm_dstaddr;
extern "C" s16 * volatile dm_dirtymask;
extern "C" s16 * volatile dm_dirty_oct;
extern "C" s16 * volatile dm_dirty_oco;
//extern "C" s16 * volatile dm_lmask;
extern "C" s16 * volatile dm_ptiles;
extern "C" s16 * volatile dm_pltiles;
extern "C" s32 * volatile dm_pmap;
extern "C" s32 * volatile dm_plmap;
extern "C" volatile s32 dm_virtual_pagebytes;
extern "C" volatile s16 dm_visible_linebytes;
extern "C" volatile s16 dm_map_tiles_x;
extern "C" volatile s16 dm_map_tiles_y;
extern "C" volatile s16 dm_pi;
extern "C" volatile s16 dm_pi2;
extern "C" volatile s16 dm_pj;
extern "C" volatile s16 dm_pj2;
extern "C" volatile s16 dm_maskstride;
extern "C" volatile s16 dm_ocmstride;
extern "C" volatile s16 dm_lineskip;
extern "C" volatile s16 dm_dstlinewid;
extern "C" volatile s16 dm_vth;

extern "C" void do_dirtyrect_pqrs();
extern "C" void do_dirtyrect_pq();
extern "C" void do_dirtyrect_pr();
extern "C" void do_dirtyrect_p();

extern "C" void do_dirtyrect_pr8();

extern "C" int AGT_MarkDirtyRectangle(u16 *_pfb, int _x, int _y, int _xs, int _ys);

extern "C" int AGT_PlayfieldRestoreInit();

// ---------------------------------------------------------------------------------------------------------------------
// reset sprite clearing stacks - called before first frame to be drawn in each new scene/level

extern "C" int AGT_PlayfieldRestoreReset();

// ---------------------------------------------------------------------------------------------------------------------
// restore sprite footprints using Blitter/CPU (STE)

extern "C" int AGT_BLiT_PlayfieldRestore();
extern "C" int AGT_CPU_PlayfieldRestore();

// ---------------------------------------------------------------------------------------------------------------------
// restore sprite footprints using CPU only (STF)

extern "C" int AGT_STF_PlayfieldRestore();

//----------------------------------------------------------------------------------------------------------------------

// scanwalk mode (consume padding scanlines instead of looping on h axis)

// PPPPPPPP*
// PPPPPPPP*
// PPPPPPPP*
// PPPPPPPP*
// *********
// RRRRRRRR*
// RRRRRRRR*
// RRRRRRRR*
// RRRRRRRR*
// *********
// xxxxxxxxx
// xxxxxxxxx padding lines for scanwalk
// xxxxxxxxx
// xxxxxxxxx

// full looping mode

// PPPPPPPP*QQQQQQQQ*
// PPPPPPPP*QQQQQQQQ*
// PPPPPPPP*QQQQQQQQ*
// PPPPPPPP*QQQQQQQQ*
// ******************
// RRRRRRRR*SSSSSSSS*
// RRRRRRRR*SSSSSSSS*
// RRRRRRRR*SSSSSSSS*
// RRRRRRRR*SSSSSSSS*
// ******************

// ---------------------------------------------------------------------------------------------------------------------

enum PFLayer
{
	PFLayer_Base = 1,
	PFLayer_Overlay = 2,
};

enum RestoreMode
{
	RestoreMode_NONE,
	RestoreMode_SAVERESTORE,
	RestoreMode_PAGERESTORE
};

extern "C" s16 g_tshift;
extern "C" s16 g_displayservice_consumed_lines[];
extern "C" s16 g_xpreshift;

//extern "C" int STF_MaskEdgeL(u16* _pfb, int _xoff, int _yoff, int _height);
//extern "C" int STF_MaskEdgeR(u16* _pfb, int _xoff, int _yoff, int _height);
//extern "C" int STF_RestoreEdges(int _xoff);

// ---------------------------------------------------------------------------------------------------------------------

class occlusionmap_t
{
public:

	occlusionmap_t()
		: oct_alloc_(nullptr),
		  oct_(nullptr), 
		  oco_(nullptr),
		  stride_(0)
	{
	}

	void release()
	{
		delete[] oct_alloc_; oct_alloc_ = nullptr;
		oct_ = oco_ = nullptr;
	}

	void allocate(s16 _xt, s16 _yt, s16 _gxt, s16 _gyt)
	{
		// 1 word per 16 tiles, rounded up to 32 tiles (1 longword, 32 bits)

		s16 guardx_tiles = ((_gxt + 31) & -32);
		s16 guardx_words = (guardx_tiles >> 4);
		//
		s16 stride_tiles = ((_xt + 31) & -32) + guardx_tiles;
		s16 stride_words = (stride_tiles >> 4);
		//
		s16 guardy_words = stride_words * _gyt;
		s16 guard_words = guardx_words + guardy_words;

		stride_ = stride_words << 1;

		s32 size_words = stride_words * (_yt + (_gyt<<1));

		oct_alloc_ = new s16[size_words << 1];
		for (int w = 0; w < size_words; w++)
			oct_alloc_[w] = -1;

		oct_ = &oct_alloc_[0 + guard_words];
		oco_ = &oct_[size_words];

#if defined(AGT_CONFIG_PRINT_PLAYFIELD_METRICS)
		printf("ocm.stride_tiles: %d\n", (int)stride_tiles); 
		printf("ocm.stride_words: %d\n", (int)stride_words); 
		printf("ocm.size_words: %d\n", (int)size_words); 
#endif
	}

	~occlusionmap_t()
	{
		release();
	}

	s16 *oct_alloc_;
	s16 *oct_;
	s16 *oco_;
	s16 stride_;
};

class dirtytilemap_t
{
public:

	dirtytilemap_t()
		: dirtymask_(nullptr),
			dirtymasklines_(nullptr),
			dirtymaskwidth_(0),
			dirtymaskstride_(0)
	{
	}

	void release()
	{
		delete[] dirtymask_; dirtymask_ = nullptr;
		delete[] dirtymasklines_; dirtymasklines_ = nullptr;
	}

	void allocate(s16 _xt, s16 _yt)
	{
		// stride-rounding needs to match OCM system (32 tile bits) for integration step
//			int stride_blocks = ((_xt + 7) >> 3);
		int stride_blocks = ((_xt + 31) & -32) >> 3;

		dirtymaskwidth_ = stride_blocks;
		dirtymaskstride_ = dirtymaskwidth_ << 1;

		// 1 word per 8 bits of mask (8 unused bits)
		s32 size_words = dirtymaskwidth_ * _yt;

		dirtymask_ = new s16[size_words];
		//qmemclr(dirtymask_, size_words << 1);

		dirtymasklines_ = new s16*[_yt];
		for (s16 n = 0; n < _yt; ++n)
			dirtymasklines_[n] = &dirtymask_[n * dirtymaskwidth_];

#if defined(AGT_CONFIG_PRINT_PLAYFIELD_METRICS)
		printf("dtm.stride_blocks: %d\n", (int)stride_blocks); 
		printf("dtm.stride: %d\n", (int)dirtymaskstride_); 
		printf("dtm.size_words: %d\n", (int)size_words); 
#endif
	}

	~dirtytilemap_t()
	{
		release();
	}

	s16* dirtymask_;
	s16** dirtymasklines_;
	s16 dirtymaskwidth_;
	s16 dirtymaskstride_;
};

// ---------------------------------------------------------------------------------------------------------------------

static inline void lcopytorow(s32* _src, s32* _dst, s16 _count)
{
	s32 *s = _src;
	s32 *d = _dst;
	s16 c = _count-1;
	do
	{
//		*d++ = *s++;
		__asm__ __volatile__
		( "move.l (%0)+,(%1)+;" : "+a"(s), "+a"(d) : : );
	} while (--c != -1);
}

static inline void lcopytocolumn(s32* _src, s32* _dst, s16 _dy, s16 _stride)
{
	s32 *s = _src;
	s32 *d = _dst;

	s16 yc = _dy-1;
	do
	{
//		*d = *s++;
    	__asm__ __volatile__
	   	( "move.l (%0)+,(%1);" : "+a"(s) : "a"(d) : );

		d += _stride;

	} while (--yc != -1);
}

static inline void lcopytorect(s32* _src, s32* _dst, s16 _dx, s16 _dy, s16 _stride)
{
	s32 *s = _src;
	s32 *d = _dst;

	s16 xcc = _dx-1;
	s16 yc = _dy-1;
	do
	{
		s32 *dt = d; d += _stride;
		s16 xc = xcc;
		do
		{
//			*dt++ = *s++;
    		__asm__ __volatile__
	    	( "move.l (%0)+,(%1)+;" : "+a"(s), "+a"(dt) : : );

		} while (--xc != -1);
	} while (--yc != -1);
}

static inline void lcopyrecttorect(s32* _src, s32* _dst, s16 _w, s16 _h, s16 _sstride, s16 _dstride)
{
/*	printf("w:%d h:%d ss:%d ds:%d\n",
		_w, _h,
		_sstride, _dstride
		);
*/
	s32 *s = _src;
	s32 *d = _dst;

	s16 xcc = _w - 1;
	s16 yc = _h - 1;
	do
	{
		s32 *dt = d; d += _dstride;
		s32 *st = s; s += _sstride;
		s16 xc = xcc;
		do
		{
			__asm__ __volatile__
				("move.l (%0)+,(%1)+;" : "+a"(st), "+a"(dt) : : );

		} while (--xc != -1);
	} while (--yc != -1);
}

// ---------------------------------------------------------------------------------------------------------------------

#define SPECULATIVE_FILL_METHOD (0)
//#define BLITTER_HOG
//#define WRAPAROUND_MAPS

// ---------------------------------------------------------------------------------------------------------------------

enum UpdateMode
{
	UpdateMode_NORMAL,
	UpdateMode_STF_AHEAD_MARGIN,
	UpdateMode_STF_AHEAD_BUFFER
};

enum PlayfieldAttributes
{
	PFA_None			= 0,
	PFA_TileAnimation	= 1<<0,		// allow map to be edited on-the-fly, with only the modified tiles refreshed on the next update
	PFA_OcclusionMaps	= 1<<1,		// use a different mechanism to restore very large sprites with embedded occlusion maps
	PFA_DualLayer		= 1<<2,		// [W.I.P] draw a second transparent/masked map on top of the main opaque map, for detail variation
	PFA_DualFieldSpr	= 1<<3,		// draw dual-field sprites even on single-field backgrounds
	PFA_UpdateFullTiles	= 1<<4,		// draw whole tiles in scrollmargin, (i.e. don't use the direction-speculative incremental update algorithm)
	//
	PFA_Defaults = PFA_None
};

// all of this might be deprecated in favour of STE-only support. maintaining STF support amid STE upgrades is a *lot* of hassle.
enum PlayfieldHardware
{
	PFH_STE				= 1<<0,		// use STE hardware scrolling, virtual linewidth
	PFH_Blitter			= 1<<1,		// [TODO] has no effect, needs to be tied to blitter code which is assumed always true on STE
	PFH_STF_SyncShift	= 1<<2,		// use LJBK shifter trickery to simulate STE hardscroll on STF
	//
	PFH_Defaults = PFH_STE|PFH_Blitter
};

template 
<
	// tilesize in 2^n
	s16 _tilestep, 
	// max scroll on any axis (pixels)
	s16 _max_scroll,
	// configure vertical scrolling
	VScrollMode _vscroll,
	// configure horizontal scrolling
	HScrollMode _hscroll,
	// configure background restore mode
	RestoreMode _restore = RestoreMode_NONE,
	// enable speculative tile optimization (more complicated logic, less tile drawing)
	UpdateMode _updatemode = UpdateMode_NORMAL,
	// enable support for various features
	PlayfieldHardware _hwattr = PFH_Defaults,
	// enable support for various features
	PlayfieldAttributes _attr = PFA_Defaults,
	// guardband regions
	int _h_guardband = AGT_CONFIG_SYS_GUARDX,
	int _v_guardband = AGT_CONFIG_SYS_GUARDY,
	//
	class mapmul_t = s16
>
class playfield_template
{
public:
	
	// ---------------------------------------------------------------------------------------------------------------------
	// configure behaviour
	// ---------------------------------------------------------------------------------------------------------------------

	static_assert(!((_attr & PFA_OcclusionMaps) && (_attr & PFA_DualLayer)), "playfield config error: cannot combine occlusion maps with dual-layer background!");
	static_assert(!((_attr & PFA_OcclusionMaps) && (_tilestep == 3)), "playfield config error: cannot combine occlusion maps with 8x8 tilemap! 16x16 only...");
	static_assert(!((_attr & PFA_OcclusionMaps) && (_hscroll == HScrollMode_LOOPBACK)), "playfield config error: cannot combine occlusion maps with H_LOOPBACK");

	static const int c_guardx = _h_guardband; //(_hscroll != HScrollMode_NONE) ? _h_guardband : 32;
	static const int c_guardy = _v_guardband; //(_vscroll != VScrollMode_NONE) ? _v_guardband : 32;
	static const int c_dst_linewid = (20+1+((c_guardx+15)>>4)) << 3;

	static const bool c_speculative_updates = !(_attr & PFA_UpdateFullTiles);

	// todo: remove
	//static const bool _blitter = false;

	static const bool c_ste_ = (_hwattr & PFH_STE);
	static const bool c_hsoftscroll_ = !c_ste_;
	static const bool c_blitter_ = false;//(_hwattr & PFH_Blitter);
	static const bool c_force_dualfield_drawing_ = (_attr & PFA_DualFieldSpr);
	static const UpdateMode c_updatemode_ = _updatemode;

	// todo: plane support mostly hardwired for now due to specific drawing routs
	static const int c_planestep_ = 2;
	static const int c_planes_ = 1<<c_planestep_;

	static const int c_preshift_stepshift_ = 2;
	static const int c_preshift_step_ = 1<<c_preshift_stepshift_;

//	static const int c_tileshift_b = 7;

	static const int c_tileshift_b_ = (_tilestep + _tilestep + c_planestep_) - 3;
	static const int c_tileshift_w_ = c_tileshift_b_ - 1;

	static const bool c_mapwrapping_ = false;
	static const bool c_dynamic_ = (_attr & PFA_TileAnimation);
	static const bool c_tilerestore_ = (_attr & PFA_OcclusionMaps);
	//static const bool c_dynamiclayer_ = _dynamic;
	static const bool c_twolayer_ = (_attr & PFA_DualLayer);

	// buffer speculation is for writing tile slices to future buffers ahead of time
	static const bool c_hbufferspeculation_ = (!c_ste_) && (c_updatemode_ == UpdateMode_STF_AHEAD_BUFFER);
	static const bool c_hsyncshift_ = (_hwattr & PFH_STF_SyncShift) && (!c_ste_); // ljbk display only

	// tilesize
	static const s16 c_tilestep_ = _tilestep;

	// update method
	static const bool c_scanwalk_ = (_hscroll == HScrollMode_SCANWALK);

	// only update tile columns if full horizontal scrolling is possible
	static const bool c_updatecols_ = (_hscroll != HScrollMode_NONE) && (_hscroll != HScrollMode_MARGIN);
	// only update tile rows if full vertical scrolling is possible
	static const bool c_updaterows_ = (_vscroll != VScrollMode_NONE) && (_vscroll != VScrollMode_MARGIN);
	static const bool c_hscroll_ = (_hscroll != HScrollMode_NONE);
	static const bool c_vscroll_ = (_vscroll != VScrollMode_NONE);

	static const bool c_hdynamic_ = c_dynamic_ && c_updatecols_;
	static const bool c_vdynamic_ = c_dynamic_ && c_updaterows_;

	// only speculate on H axis if there is offscreen margin (only possible on STE with hscroll+linewid)
	static const bool c_hspeculative_ = c_updatecols_ && ((c_ste_ && c_speculative_updates) || (c_updatemode_ == UpdateMode_STF_AHEAD_MARGIN));
	// only speculate via buffers if we're using them (STF preshifted framebuffers)
//	static const bool c_hbufferspeculative_ = _speculative && c_updatecols && (!_ste);
	// always possible to speculate on V axis
	static const bool c_vspeculative_ = /*_speculative &&*/ c_updaterows_ && c_speculative_updates;
	static const bool c_stf_maskedspeculation_ = (c_updatemode_ == UpdateMode_STF_AHEAD_MARGIN) && !c_ste_ && c_hspeculative_/* && !c_hbufferspeculation_*/;

	static const s16 c_hmargin_ = (s16)(int)c_hspeculative_; // 0 or 1
	static const s16 c_vmargin_ = (s16)(int)c_vspeculative_; // 0 or 1

	static const bool c_hloop_ = (_hscroll == HScrollMode_LOOPBACK);
	static const bool c_vloop_ = (_vscroll == VScrollMode_LOOPBACK);

	// configure PQRS buffer update pattern
	static const bool c_updateP_ = c_updatecols_ || c_updaterows_;
	static const bool c_updateQ_ = (_hscroll == HScrollMode_LOOPBACK);
	static const bool c_updateR_ = (_vscroll == VScrollMode_LOOPBACK) || (_restore == RestoreMode_PAGERESTORE);
	static const bool c_updateS_ = c_updateQ_ && c_updateR_;

	// internal stuff for tiles
	static const s16 c_tilesize_ = 1 << c_tilestep_;
	static const s16 c_tilelinewords_ = c_tilesize_ >> 2;
	static const s16 c_tilewords_ = c_tilelinewords_ * c_tilesize_;
	static const int c_tiles_per_word_ = (c_tilesize_ == 8) ? 2 : 1;
	// number of additional tiles visible @ hscroll=15 for this tilesize (not less than 1)
	static const int c_hscrolltiles_ = (16 / c_tilesize_) > 0 ? (16 / c_tilesize_) : 1;

	// maximum number of virtual tiles possible on any axis
	static const s16 c_max_vt_ = _max_scroll / c_tilesize_;

	static const int c_hspeculative_margin_ = c_hspeculative_ ? c_tilesize_ : 0;
	static const int c_hspeculative_margin_size_ = c_hspeculative_margin_ * 2;
	static const int c_vspeculative_margin_ = c_vspeculative_ ? c_tilesize_ : 0;
	static const int c_vspeculative_margin_size_ = c_vspeculative_margin_ * 2;

	static const s16 c_minsafex = c_hspeculative_margin_;
	static const s16 c_minsafey = c_vspeculative_margin_;

	// ---------------------------------------------------------------------------------------------------------------------

	class mapcontext
	{
	private:

		void release()
		{
			delete[] modulo_map_x_; modulo_map_x_ = 0;
			delete[] modulo_map_y_; modulo_map_y_ = 0;
			delete[] mul_map_x_; mul_map_x_ = 0;
		}

		void prepare()
		{
			srcywrap_ = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		

			delete[] mul_map_x_; 
			mul_map_x_ = new mapmul_t[map_tiles_y_];
			for (s16 n = 0; n < map_tiles_y_; ++n)
				mul_map_x_[n] = n * map_tiles_x_;

			{
				//s16 xo = c_max_vt_ / map_tiles_x_;
				//s16 yo = c_max_vt_ / map_tiles_y_;
					
				delete[] modulo_map_x_; 
				delete[] modulo_map_y_; 

				modulo_map_x_ = new s16[c_max_vt_<<1];
				modulo_map_y_ = new s16[c_max_vt_<<1];

				for (s16 n = 0; n < c_max_vt_<<1; ++n)
				{
					s16 v = n-c_max_vt_;
					modulo_map_x_[n] = tmod16(v, map_tiles_x_);
					modulo_map_y_[n] = tmod16(v, map_tiles_y_);
				}
			}
		}

	public:

		mapcontext()
			: modulo_map_x_(nullptr),
			  modulo_map_y_(nullptr),
			  mul_map_x_(nullptr),
			  map_tiles_x_(0),
			  map_tiles_y_(0),
			  srcywrap_(0),
			  map_(nullptr),
			  mapoverlay_(nullptr)
		{
		}

		// owned: map addressing storage
		s16* modulo_map_x_;
		s16* modulo_map_y_;
		mapmul_t* mul_map_x_;

		// internal: shadowed externally for speed
		s16 map_tiles_x_;
		s16 map_tiles_y_;
		s16 srcywrap_;
		s32* map_;
		s32* mapoverlay_;

		void setmap(s32* _map)
		{
			map_tiles_x_ = s16(_map[0]);
			map_tiles_y_ = s16(_map[1]);
			map_ = &_map[2];

			prepare();
		}

		void setmap(worldmap* _map)
		{
			map_tiles_x_ = _map->getwidth();
			map_tiles_y_ = _map->getheight();
			map_ = _map->get();

			prepare();
		}

		void setoverlaymap(s32* _map)
		{
			mapoverlay_ = &_map[2];
		}

		void setoverlaymap(worldmap* _map)
		{
			mapoverlay_ = _map->get();
		}

		template<PFLayer _layercode = PFLayer_Base>
		agt_inline s32 sample(s16 _x, s16 _y)
		{
			s32 val;

			s32 index = mul_map_x_[_y] + _x;
			if (_layercode & PFLayer_Base)
				val = map_[index];
			if (_layercode & PFLayer_Overlay)
				val = mapoverlay_[index];

			return val;
		}

		template<PFLayer _layercode = PFLayer_Base>
		agt_inline void multimod(s16 _x, s16 _y, s32 _rawidx)
		{
			if (c_dynamic_)
			{
				s32 index = mul_map_x_[_y] + _x;
				if (_layercode & PFLayer_Base)
					map_[index] = _rawidx;
				if (_layercode & PFLayer_Overlay)
					mapoverlay_[index] = _rawidx;
			}
		}

		template<PFLayer _layercode = PFLayer_Base>
		void multimod_row(s16 _x, s16 _y, s16 _dx, s32 *_source)
		{
			if (c_dynamic_)
			{
				// update map
				s32 index = mul_map_x_[_y] + _x;
				if (_layercode & PFLayer_Base)
					lcopytorow(_source, &map_[index], _dx);
				if (_layercode & PFLayer_Overlay)
					lcopytorow(_source, &mapoverlay_[index], _dx);
			}
		}

		template<PFLayer _layercode = PFLayer_Base>
		void multimod_column(s16 _x, s16 _y, s16 _dy, s32 *_source)
		{
			// todo: prototype version - move this stuff to 68k
			if (c_dynamic_)
			{
				// update map
				s32 index = mul_map_x_[_y] + _x;
				if (_layercode & PFLayer_Base)
					lcopytocolumn(_source, &map_[index], _dy, map_tiles_x_);
				if (_layercode & PFLayer_Overlay)
					lcopytocolumn(_source, &mapoverlay_[index], _dy, map_tiles_x_);
			}
		}

		template<PFLayer _layercode = PFLayer_Base>
		void multimod_rect(s16 _x, s16 _y, s16 _dx, s16 _dy, s32 *_source)
		{
			if (c_dynamic_)
			{
				// update map
				s32 index = mul_map_x_[_y] + _x;
				if (_layercode & PFLayer_Base)
					lcopytorect(_source, &map_[index], _dx, _dy, map_tiles_x_);
				if (_layercode & PFLayer_Overlay)
					lcopytorect(_source, &mapoverlay_[index], _dx, _dy, map_tiles_x_);
			}
		}

		template<PFLayer _layercode = PFLayer_Base>
		void multimod_copy(s16 _sx, s16 _sy, s16 _dx, s16 _dy, s16 _w, s16 _h, s32 *_source, s16 _sstride)
		{
			if (c_dynamic_)
			{
				// update map
				s32 *src = &_source[int(_sy * _sstride) + _sx];

				s32 dindex = mul_map_x_[_dy] + _dx;
				
				if (_layercode & PFLayer_Base)
					lcopyrecttorect(src, &map_[dindex], _w, _h, _sstride, map_tiles_x_);

				if (_layercode & PFLayer_Overlay)
					lcopyrecttorect(src, &mapoverlay_[dindex], _w, _h, _sstride, map_tiles_x_);

			}
		}

		~mapcontext()
		{
			release();
		}
	};

	// ---------------------------------------------------------------------------------------------------------------------

	class fieldcontext
	{
	private:

		void release()
		{
			delete[] quant_vpage_x_; quant_vpage_x_ = 0;
			delete[] quant_vpage_y_; quant_vpage_y_ = 0;
			delete[] modulo_vpage_x_; modulo_vpage_x_ = 0;
			delete[] modulo_vpage_y_; modulo_vpage_y_ = 0;
			delete[] lineindex_; lineindex_ = 0;
			delete[] linemod_; linemod_ = 0;

			initialized_ = false;
		}

	public:

		fieldcontext()
			: initialized_(false),
			  quant_vpage_x_(nullptr),
			  quant_vpage_y_(nullptr),
			  modulo_vpage_x_(nullptr),
			  modulo_vpage_y_(nullptr),
			  lineindex_alloc_(nullptr), lineindex_(nullptr),
			  linemod_alloc_(nullptr), linemod_(nullptr)
		{
		}

		// owned: page addressing & wrap-mod storage
		s16* quant_vpage_x_;
		s16* quant_vpage_y_;
		s16* modulo_vpage_x_;
		s16* modulo_vpage_y_;
		s32 *lineindex_alloc_;
		s16 *linemod_alloc_;
		s32 *lineindex_;
		s16 *linemod_;

		bool initialized_;

		void prepare(s16 _vpage_tiles_x, s16 _vpage_tiles_y)
		{
			if (!initialized_)
			{
				s16 xo = c_max_vt_ / _vpage_tiles_x;
				s16 yo = c_max_vt_ / _vpage_tiles_y;
				s16 maxlines = c_guardy + (_vpage_tiles_y * c_tilesize_ * (c_updateR_ ? 2 : 1));
				
				delete[] quant_vpage_x_; 
				delete[] quant_vpage_y_; 
				delete[] modulo_vpage_x_; 
				delete[] modulo_vpage_y_; 
				delete[] lineindex_alloc_;
				delete[] linemod_alloc_;

				quant_vpage_x_ = new s16[c_max_vt_<<1];
				quant_vpage_y_ = new s16[c_max_vt_<<1];
				modulo_vpage_x_ = new s16[c_max_vt_<<1];
				modulo_vpage_y_ = new s16[c_max_vt_<<1];
				lineindex_alloc_ = new s32[maxlines];
				linemod_alloc_ = new s16[maxlines];

				for (s16 n = 0; n < c_max_vt_<<1; ++n)
				{
					s16 v = n-c_max_vt_;
					quant_vpage_x_[n] = ((v + (xo * _vpage_tiles_x)) / _vpage_tiles_x) - xo;
					quant_vpage_y_[n] = ((v + (yo * _vpage_tiles_y)) / _vpage_tiles_y) - yo;
					modulo_vpage_x_[n] = tmod16(v, _vpage_tiles_x);
					modulo_vpage_y_[n] = tmod16(v, _vpage_tiles_y);
				}
			
				// generate playfield workbuffer scanline modulo & index
				// todo: 8x8 mode
				s16 linewidth = (_vpage_tiles_x * (c_updateQ_ ? 2 : 1)) * (c_tilelinewords_<<1);
				for (s16 n = 0; n < maxlines; ++n)
				{
					s16 v = n-c_guardy;
					lineindex_alloc_[n] = s32(v * linewidth);
					linemod_alloc_[n] = tmod16(v, (_vpage_tiles_y * c_tilesize_));
				}

				lineindex_ = &lineindex_alloc_[c_guardy];
				linemod_ = &linemod_alloc_[c_guardy];

				pprintf("playfield context initialized...\n");
				initialized_ = true;
			}
		}

		~fieldcontext()
		{
			release();
		}
	};

	// ---------------------------------------------------------------------------------------------------------------------

	mapcontext *pmapcontext_;
	fieldcontext *pfieldcontext_;
	
	s16 display_width_;
	s16 display_height_;
	s16 virtual_width_;
	s16 virtual_height_;
	
	s16 visible_tiles_x_;			// tiles potentially visible, including HW scroll margin but excluding scroll update margin
	s16 visible_tiles_y_;			// tiles potentially visible, including HW scroll margin but excluding scroll update margin

	s16 vpage_tiles_xg_;			// tiles for a single virtual page, including scroll update margin & guardband regions
	s16 vpage_tiles_yg_;			// tiles for a single virtual page, including scroll update margin & guardband regions

	s16 vpage_tiles_xu_;			// tiles for a single virtual page, including scroll update margin
	s16 vpage_tiles_yu_;			// tiles for a single virtual page, including scroll update margin

//	s16 scrollmargin_tiles_x_;		// visible tiles, extended to include any speculative scrollmargin
//	s16 scrollmargin_tiles_y_;		// visible tiles, extended to include any speculative scrollmargin
	
	s16 virtual_tiles_x_;			// total allocated tiles for all vpages, including any loopback, if used
	s16 virtual_tiles_y_;			// total allocated tiles for all vpages, including any loopback/pagerestore
	
	s16 wrap_width_;
	s16 wrap_height_;
	
	s16 virtual_linewords_;
//	s16 visible_linewords_;
	s16 vpage_linewords_;
	
	s16 xpos_;
	s16 ypos_;
	s16 last_xpos_;
	s16 last_ypos_;
		
	s16 txpos_;	
	s16 typos_;
	s16 last_txpos_;
	s16 last_typos_;

	s16 tx_last_dy_;
	s16 ty_last_dx_;
	
	s16 map_tiles_x_;
	s16 map_tiles_y_;
	s32 srcywrap_;
	s32* map_;
	s32* mapoverlay_;

	// private, owned storage
	s16* quant_vpage_x_;
	s16* quant_vpage_y_;
	s16* modulo_vpage_x_;
	s16* modulo_vpage_y_;
	s16* modulo_map_x_;
	s16* modulo_map_y_;
	mapmul_t* mul_map_x_;
	u8 *display_alloc_;
	
	s16 rowskipwords_;
	s16 virtual_linebytes_;
	s16 visible_linebytes_;
	s32 virtual_pagebytes_;
	
	u32 bufferwords_;

	s16 buffer_;
	s16 field_;
	u16 *display_;
	u16 *framebuffer_;
		
	tileset* tiles_;
	const u16* tiledata_;
	tileset* tilesoverlay_;
	const u16* tileoverlaydata_;
	
	restorestate_t restorestate_;

	s32 tilecount_;
	
	mersenne_twister twister_;

	s16 drawshift_;
	s16 xpreshift_;
	s16 last_slice_;
	s16 slice_;
	s16 maskedges_;

	s16 dtx1_;
	s16 dty1_;
	s16 dtx2_;
	s16 dty2_;

	dirtytilemap_t dtm_animation_;
	dirtytilemap_t dtm_bgrestore_;

//	s32* dirtytiles_;
//	s16* layermask_;
//	s16 dirtynum_;
	//s16** layermasklines_;

	occlusionmap_t ocm_;

	// ---------------------------------------------------------------------------------------------------------------------

//	playfield_template();	
//	~playfield_template();

//	void init(s16 _buffer, s16 _field, s16 _display_width, s16 _display_height);

	// ---------------------------------------------------------------------------------------------------------------------

	inline void setcontext(mapcontext *_pmctx, fieldcontext *_pfctx)
	{
		pmapcontext_ = _pmctx;
		pfieldcontext_ = _pfctx;
	}

	// associate fixed preshift offset with this playfield buffer
	inline void setpreshift(s16 _xps)
	{
		xpreshift_ = _xps;
	}

	// set speculative slice to render (for buffer speculation)
	agt_inline void setslice(s16 _s)
	{
		slice_ = _s<<c_preshift_stepshift_;
	}

	agt_inline void setpos(s16 _x, s16 _y)
	{
		// todo: not for hbufferspec
		if (c_hsyncshift_)
			xpos_ = (_x & 0xFFFC) + xpreshift_;
		else
			xpos_ = _x;

		// don't permit playfield offsets on axes where scrolling is disabled
		if (!c_hscroll_) _x = 0;
		if (!c_vscroll_) _y = 0;

		ypos_ = _y;
		txpos_ = xpos_ >> c_tilestep_;
		typos_ = ypos_ >> c_tilestep_;

//#if defined(LJBK_SYNC)
//#if defined(LJBK_4SYNC)
////		s16 xr = _x & 0xf;
////		if (xr < 8)
////			g_tshift = xr & 3;
////		else
////			g_tshift = xr & 7;
//		g_tshift = xpos_ & 3;
//#else
//		g_tshift = xpos_ & 7;
//#endif
//#else
//		g_tshift = xpos_ & (c_tilesize_-1);
//#endif

		if (!c_ste_ && !c_hspeculative_ && !c_hbufferspeculation_)
		{
			// suppress redrawing of shifted tilestrips for no effective change in tile position or shift in this buffer
			bool draw = (ypos_ != last_ypos_) || 
						(txpos_ != last_txpos_) || 
						((xpos_ & 3) != (last_xpos_ & 3));

			drawshift_ = draw;
		}
		else
			drawshift_ = 1;

		//drawshift_ = 1;
	}
		
	//void setmap(s32* _map)
	//{
	//	map_tiles_x_ = s16(_map[0]);
	//	map_tiles_y_ = s16(_map[1]);
	//	map_ = &_map[2];

	//	srcywrap_ = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		

	//	delete[] mul_map_x_; 
	//	mul_map_x_ = new s16[map_tiles_y_];
	//	for (s16 n = 0; n < map_tiles_y_; ++n)
	//		mul_map_x_[n] = n * map_tiles_x_;
	//}

	//void setmap(worldmap* _map)
	//{
	//	map_tiles_x_ = _map->getwidth();
	//	map_tiles_y_ = _map->getheight();
	//	map_ = _map->get();

	//	srcywrap_ = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		

	//	delete[] mul_map_x_; 
	//	mul_map_x_ = new s16[map_tiles_y_];
	//	for (s16 n = 0; n < map_tiles_y_; ++n)
	//		mul_map_x_[n] = n * map_tiles_x_;
	//}

	void settiles(tileset* _tiles)
	{	
		tiles_ = _tiles;
		tiledata_ = tiles_->get();
	}

	void setoverlaytiles(tileset* _tiles)
	{
		tilesoverlay_ = _tiles;
		tileoverlaydata_ = tilesoverlay_->get();
	}

	inline void select(shifter_common& _shift, s16 _viewbind, s16 _yoff)
	{
		u32 addr = (u32)display_;

		s16 mx = xpos_;
		s16 my = ypos_ + _yoff;

		if (c_hloop_)
			mx = tmod16(mx, wrap_width_);
		if (c_vloop_)
			my = tmod16(my, wrap_height_);

		_shift.setdisplay(field_, addr, 320/*display_width_*/, virtual_width_, mx, my, _viewbind);

/*		if (c_scanwalk_)
			// todo: fast mod here
//			_shift.setdisplay(field_, addr, display_width_, virtual_width_, xpos_, ypos_);
			_shift.setdisplay(field_, addr, display_width_, virtual_width_, xpos_, tmod(ypos_, wrap_height_));
		else
			_shift.setdisplay(field_, addr, display_width_, virtual_width_, tmod(xpos_, wrap_width_), tmod(ypos_, wrap_height_));
*/

		tilecount_ = 0;
	}
	
	void clear(u16* _addr, u32 _words)
	{
		for (u32 c = 0; c < _words; ++c)
			_addr[c] = 0;
	}
	
	void set(u16* _addr, u32 _words, u16 _word)
	{
		for (u32 c = 0; c < _words; ++c)
			_addr[c] = _word;
	}
	
	inline s16 quant_vpage_x(const s16 _vx) const
	{
		return quant_vpage_x_[_vx + c_max_vt_];
	}

	inline s16 quant_vpage_y(const s16 _vy) const
	{
		return quant_vpage_y_[_vy + c_max_vt_];
	}

	inline s16 modulo_vpage_x(const s16 _vx) const
	{
		return modulo_vpage_x_[_vx + c_max_vt_];
	}

	inline s16 modulo_vpage_y(const s16 _vy) const
	{
		return modulo_vpage_y_[_vy + c_max_vt_];
	}

	inline s16 modulo_map_x(const s16 _vx) const
	{
		return modulo_map_x_[_vx + c_max_vt_];
	}

	inline s16 modulo_map_y(const s16 _vy) const
	{
		return modulo_map_y_[_vy + c_max_vt_];
	}

	inline mapmul_t mul_map_x(const s16 _mx) const
	{
		return mul_map_x_[_mx];
	}

	// ---------------------------------------------------------------------------------------------------------------------

	inline void prepare_blitter()
	{
		reg16(ffff8a36) = c_planes_;		// tile planes (1-4)
		reg16(ffff8a28) = 0xffff;		// em1
		reg16(ffff8a2a) = 0xffff;		// em2
		reg16(ffff8a2c) = 0xffff;		// em3
		reg16(ffff8a20) = 2;			// sxi
		reg16(ffff8a22) = 2;			// syi
		reg16(ffff8a2e) = 2;			// dxi
		reg16(ffff8a30) = (virtual_linewords_<<1)-((c_tilesize_>>1)-2);	// dyi
		reg8(ffff8a3d) = 0;				// skew		
		reg8(ffff8a3a) = 2;				// hop
		reg8(ffff8a3b) = 3;				// op
		
	#ifdef FASTPAGE_TILES
		reg32(ffff8a24) = (u32)tiledata_;	// src
	#endif	
	}

#if (0)
	inline void dotile(s16 _t, s16 _i, s16 _j)		
	{
		_i = xmod(_i, virtual_tiles_x_);
		_j = xmod(_j, virtual_tiles_y_);
		
		// todo: 8x8 mode
		const u16* srcaddr = &tiledata_[_t << c_tileshift_w];
		u16* dstaddr = display_ + (_j * c_tilesize_ * virtual_linewords_) + (_i * c_tilelinewords_);
		
		// blitter version...
#ifdef TIMING_RASTERS		
		reg16(ffff8240) = 0x00f;
		reg16(00000064) = 0x00f;
#endif
		reg32(ffff8a24) = (u32)srcaddr;		// sa
		reg32(ffff8a32) = (u32)dstaddr;		// da
		reg16(ffff8a36) = c_tilelinewords_;	// xs
		reg16(ffff8a38) = c_tilesize_;			// ys
		reg8(ffff8a3c) = 0xc0;				// hog & go
#ifdef TIMING_RASTERS		
		reg16(ffff8240) = 0x000;
		reg16(00000064) = 0x000;
#endif
/*		
		// clunky C version...
		
		register u16 vlw = virtual_linewords_;
		for (register s16 yy = c_tilesize_ - 1; yy >= 0; --yy)
		{
			dstaddr[0] = srcaddr[0];
			dstaddr[1] = srcaddr[1];
			dstaddr[2] = srcaddr[2];
			dstaddr[3] = srcaddr[3];
			dstaddr += vlw;
			srcaddr += 4;
		}
*/		
	}
#endif

/*
	inline void fill_block(s16 _mi, s16 _mj, s16 _tx1, s16 _ty1, s16 _tx2, s16 _ty2)
	{
		s16 mj = _mj;
		for (s16 j = _ty1; j < _ty2; ++j, ++mj)
		{
			register s16 mi = _mi;
			for (register s16 i = _tx1; i < _tx2; ++i, ++mi)
			{
				register s16 mmi = xmod(mi, map_tiles_x_);
				register s16 mmj = xmod(mj, map_tiles_y_);				
				register u16 t = (u16)(map_[mmi + (mmj * map_tiles_x_)]);
				dotile(t,i,j);
			}
		}
	}
*/

	// =====================================================================================================================
	// optimized m68k implementations for tile fillers
	// =====================================================================================================================

	// STE and non-shifting versions
	#include "pf_block.inl"				// fill X*Y block at a time
	#include "pf_specc.inl"				// fill speculative column slices
	#include "pf_specr.inl"				// fill speculative row slices

	// STF tile-shifting versions
	#include "pf_cshift.inl"			// shift full column at a time
	#include "pf_specshiftc.inl"		// shift speculative column slices
	#include "pf_specshiftr.inl"		// shift speculative row slices

	#include "pf_mapmod.inl"				// refill dirty tiles

	// =====================================================================================================================

	// debugging only - fill block using individual tile calls
	inline void fill_block_indirect(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{						
		for (s16 jj = 0; jj < (pj2-pj); ++jj)
		{
			for (s16 ii = 0; ii < (pi2-pi); ++ii)
			{
				if (c_updateP_ && c_updateQ_ && c_updateR_ && c_updateS_)
					PQRS_fill_block_direct(_mi+ii, _mj+jj, pi+ii, pj+jj, pi+ii+1, pj+jj+1);
				else
				if (c_updateP_ && c_updateQ_)
					PQ_fill_block_direct(_mi+ii, _mj+jj, pi+ii, pj+jj, pi+ii+1, pj+jj+1);
				else
				if (c_updateP_ && c_updateR_)
					PR_fill_block_direct(_mi+ii, _mj+jj, pi+ii, pj+jj, pi+ii+1, pj+jj+1);
				else
				if (c_updateP_)
					P_fill_block_direct(_mi+ii, _mj+jj, pi+ii, pj+jj, pi+ii+1, pj+jj+1);
			}
		}
	}

	inline void fill_specrow_vx_sane(s16 _mi, s16 _mj, s16 pi, s16 _vy1, s16 pi2, s16 pjy1, s16 pjy2)
	{
		if (c_vloop_)
		{
			// v-wrapping case

			// start modulo virtual playfield dimensions
			s16 pj = modulo_vpage_y(_vy1);

			if (c_updateQ_)
				PQRS_fill_specrow_direct(_mi, _mj, pi, pj, pi2, pjy1, pjy2);
			else
				PR_fill_specrow_direct(_mi, _mj, pi, pj, pi2, pjy1, pjy2);
		}
		else
		if (c_updateR_)
		{
			// non-v-wrapping case which still requires shadow vpage R (pagerestore mode)

			if (c_updateQ_)
				PQRS_fill_specrow_direct(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
			else
				PR_fill_specrow_direct(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
		}
		else
		{
			if (c_updateQ_)
				PQ_fill_specrow_direct(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
			else
				P_fill_specrow_direct(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
		}
				
		//// block entirely within virtual playfield			
		//if (c_scanwalk_)
		//	PR_fill_specrow_direct(_mi, _mj, pi, pj, pi2, pjy1, pjy2);
		//else
		//	PQRS_fill_specrow_direct(_mi, _mj, pi, pj, pi2, pjy1, pjy2);
	}

	inline void shift_specrow_vx_sane(s16 _mi, s16 _mj, s16 pi, s16 _vy1, s16 pi2, s16 pjy1, s16 pjy2)
	{
		if (c_vloop_)
		{
			// v-wrapping case

			// start modulo virtual playfield dimensions
			s16 pj = modulo_vpage_y(_vy1);

			PR_shift_specrow_direct(_mi, _mj, pi, pj, pi2, pjy1, pjy2);
		}
		else
		if (c_updateR_)
		{
			// non-v-wrapping case which still requires shadow vpage R (pagerestore mode)

			PR_shift_specrow_direct(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
		}
		else
		{
			P_shift_specrow_direct(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
		}
	}

	inline void fill_speccolumn_vy_sane(s16 _mi, s16 _mj, s16 _vx1, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
	{
		if (c_hloop_)
		{
			// start modulo virtual playfield dimensions
			//s16 pi = xmod<s16>(_vx1, vpage_tiles_x_);
			s16 pi = modulo_vpage_x(_vx1);

			if (c_updateR_)
				PQRS_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
			else
				PQ_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
		}
		else
		{
			s16 pi = _vx1;

			if (c_updateR_)
				PR_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
			else
				P_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
		}

/*		if (c_scanwalk_)
		{
			// in this mode, columns are un-modded on x because virtual horizontal playfield is infinite/non-looping
			PR_fill_speccolumn_direct(_mi, _mj, _vx1, pj, pj2, pjy1, pjy2);
		}
		else
		{
			// start modulo virtual playfield dimensions
			//s16 pi = xmod<s16>(_vx1, vpage_tiles_x_);
			s16 pi = modulo_vpage_x(_vx1);
					
			// block entirely within virtual playfield			
			PQRS_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
		}
*/
	}	

	inline void shift_speccolumn_vy_sane(s16 _mi, s16 _mj, s16 _vx1, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
	{
		s16 pi = _vx1;

		if (c_updateR_)
			PR_shift_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
		else
			P_shift_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
	}	

	inline void fill_block_vx_sane(s16 _mi, s16 _mj, s16 pi, s16 _vy1, s16 pi2, s16 _vy2)
	{
		if (_vy1 == _vy2)
			return;

		if (c_vloop_)
		{
			// v-wrapping case

			// start modulo virtual playfield dimensions
			//s16 pj = xmod<s16>(_vy1, vpage_tiles_y_);
			s16 pj = modulo_vpage_y(_vy1);

			// end modulo virtual playfield dimensions
			//s16 pj2 = xmod<s16>(_vy2-1, vpage_tiles_y_)+1;
			s16 pj2 = modulo_vpage_y(_vy2-1)+1;
					
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pj2 <= pj)
			{
				// block straddles virtual y-wrap, so split in two										
				s16 o = vpage_tiles_yg_ - pj;
				if (c_updateQ_)
				{
					PQRS_fill_block_direct(_mi, _mj+o, pi,  0, pi2, pj2);	
					PQRS_fill_block_direct(_mi, _mj,	pi, pj, pi2, vpage_tiles_yg_);
				}
				else
				{
					PR_fill_block_direct(_mi, _mj+o, pi,  0, pi2, pj2);	
					PR_fill_block_direct(_mi, _mj,	pi, pj, pi2, vpage_tiles_yg_);
				}
			}
			else		
			{
				// block entirely within virtual playfield			
				if (c_updateQ_)
					PQRS_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
				else
					PR_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
			}
		}
		else
		if (c_updateR_)
		{
			// non-v-wrapping case which still requires shadow vpage R (pagerestore mode)

			if (c_updateQ_)
				PQRS_fill_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
			else
				PR_fill_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
		else
		{
			if (c_updateQ_)
				PQ_fill_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
			else
				P_fill_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
	}

	inline void dirty_block_vx_sane(s16 _mi, s16 _mj, s16 pi, s16 _vy1, s16 pi2, s16 _vy2)
	{
		if (_vy1 == _vy2)
			return;

		if (c_vloop_)
		{
			// v-wrapping case

			// start modulo virtual playfield dimensions
			//s16 pj = xmod<s16>(_vy1, vpage_tiles_y_);
			s16 pj = modulo_vpage_y(_vy1);

			// end modulo virtual playfield dimensions
			//s16 pj2 = xmod<s16>(_vy2-1, vpage_tiles_y_)+1;
			s16 pj2 = modulo_vpage_y(_vy2-1)+1;
					
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pj2 <= pj)
			{
				// block straddles virtual y-wrap, so split in two										
				s16 o = vpage_tiles_yg_ - pj;
				if (c_updateQ_)
				{
					PQRS_dirty_block_direct(_mi, _mj+o, pi,  0, pi2, pj2);	
					PQRS_dirty_block_direct(_mi, _mj,	pi, pj, pi2, vpage_tiles_yg_);
				}
				else
				{
					PR_dirty_block_direct(_mi, _mj+o, pi,  0, pi2, pj2);	
					PR_dirty_block_direct(_mi, _mj,	pi, pj, pi2, vpage_tiles_yg_);
				}
			}
			else		
			{
				// block entirely within virtual playfield			
				if (c_updateQ_)
					PQRS_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
				else
					PR_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
			}
		}
		else
		if (c_updateR_)
		{
			// non-v-wrapping case which still requires shadow vpage R (pagerestore mode)

			if (c_updateQ_)
				PQRS_dirty_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
			else
				PR_dirty_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
		else
		{
			if (c_updateQ_)
				PQ_dirty_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
			else
				P_dirty_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
	}

	inline void P_shift_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{
		s16 o = 0;
		for (s16 c = pi; c < pi2; c++, o++)
			P_shift_column_direct(_mi + o, _mj, c, pj, c, pj2);
	}

	inline void PR_shift_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{
		s16 o = 0;
		for (s16 c = pi; c < pi2; c++, o++)
			PR_shift_column_direct(_mi + o, _mj, c, pj, c, pj2);
	}

	inline void shift_block_vx_sane(s16 _mi, s16 _mj, s16 pi, s16 _vy1, s16 pi2, s16 _vy2)
	{
		if (_vy1 == _vy2)
			return;

		if (c_vloop_)
		{
			// v-wrapping case

			// start modulo virtual playfield dimensions
			//s16 pj = xmod<s16>(_vy1, vpage_tiles_y_);
			s16 pj = modulo_vpage_y(_vy1);

			// end modulo virtual playfield dimensions
			//s16 pj2 = xmod<s16>(_vy2-1, vpage_tiles_y_)+1;
			s16 pj2 = modulo_vpage_y(_vy2-1)+1;
					
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pj2 <= pj)
			{
				// block straddles virtual y-wrap, so split in two										
				s16 o = vpage_tiles_yg_ - pj;
				PR_shift_block_direct(_mi, _mj+o, pi,  0, pi2, pj2);	
				PR_shift_block_direct(_mi, _mj,	pi, pj, pi2, vpage_tiles_yg_);
			}
			else		
			{
				// block entirely within virtual playfield			
				PR_shift_block_direct(_mi, _mj, pi, pj, pi2, pj2);
			}
		}
		else
		if (c_updateR_)
		{
			// non-v-wrapping case which still requires shadow vpage R (pagerestore mode)

			PR_shift_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
		else
		{
			P_shift_block_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
	}


	inline void shift_column_vx_sane(s16 _mi, s16 _mj, s16 pi, s16 _vy1, s16 pi2, s16 _vy2)
	{
		if (_vy1 == _vy2)
			return;

		if (c_vloop_)
		{
			// v-wrapping case

			// start modulo virtual playfield dimensions
			//s16 pj = xmod<s16>(_vy1, vpage_tiles_y_);
			s16 pj = modulo_vpage_y(_vy1);

			// end modulo virtual playfield dimensions
			//s16 pj2 = xmod<s16>(_vy2-1, vpage_tiles_y_)+1;
			s16 pj2 = modulo_vpage_y(_vy2-1)+1;
					
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pj2 <= pj)
			{
				// block straddles virtual y-wrap, so split in two										
				s16 o = vpage_tiles_yg_ - pj;
				{
					PR_shift_column_direct(_mi, _mj+o, pi,  0, pi2, pj2);	
					PR_shift_column_direct(_mi, _mj,	pi, pj, pi2, vpage_tiles_yg_);
				}
			}
			else		
			{
				// block entirely within virtual playfield			
				PR_shift_column_direct(_mi, _mj, pi, pj, pi2, pj2);
			}
		}
		else
		if (c_updateR_)
		{
			// non-v-wrapping case which still requires shadow vpage R (pagerestore mode)

			PR_shift_column_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
		else
		{
			P_shift_column_direct(_mi, _mj, pi, _vy1, pi2, _vy2);
		}
	}


	inline void fill_specrow_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vx2, s16 pjy1, s16 pjy2)
	{
		if (_vx1 == _vx2)
			return;

		if (!c_hloop_)
		{
			// in this mode, rows are never clipped
			fill_specrow_vx_sane(_mi, _mj, _vx1, _vy1, _vx2, pjy1, pjy2);
		}
		else
		{
			// quantize row size into playfield pages
			s16 di1 = quant_vpage_x(_vx1);
			s16 di2 = quant_vpage_x(_vx2-1);
				
			s16 pi = modulo_vpage_x(_vx1);
			
			if (di2 > di1)
			{
				s16 vxR = (di1+1) * vpage_tiles_xg_;
				s16 pi2 = vpage_tiles_xg_;//modulo_vpage_x(vxR-1)+1;
				
				// split row at first partition
				//fill_specrow_virtual(_mi, _mj, _vx1, _vy1, vxR, pjy1, pjy2);
				fill_specrow_vx_sane(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
				fill_specrow_virtual(_mi+(vxR-_vx1), _mj, vxR, _vy1, _vx2, pjy1, pjy2);
			}
			else
			{
				s16 pi2 = modulo_vpage_x(_vx2-1)+1;
				fill_specrow_vx_sane(_mi, _mj, pi, _vy1, pi2, pjy1, pjy2);
			}
		}
	}

	inline void shift_specrow_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vx2, s16 pjy1, s16 pjy2)
	{
		if (_vx1 == _vx2)
			return;

		// in this mode, rows are never clipped
		shift_specrow_vx_sane(_mi, _mj, _vx1, _vy1, _vx2, pjy1, pjy2);
	}

	inline void fill_speccolumn_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vy2, s16 pjy1, s16 pjy2)
	{
		if (_vy1 == _vy2)
			return;

		if (!c_vloop_)
		{
			// in this mode, columns are never clipped
			fill_speccolumn_vy_sane(_mi, _mj, _vx1, _vy1, _vy2, pjy1, pjy2);
		}
		else
		{
			// quantize row size into playfield pages
			s16 di1 = quant_vpage_y(_vy1);
			s16 di2 = quant_vpage_y(_vy2-1);
			
			s16 pj = modulo_vpage_y(_vy1);
					
			if (di2 > di1)
			{
				s16 vyR = (s16(di1)+1) * vpage_tiles_yg_;
				s16 pj2 = vpage_tiles_yg_;//modulo_vpage_y(vyR-1)+1;
							
				// split row at first partition
				//fill_speccolumn_virtual(_mi, _mj, _vx1, _vy1, vyR, pjy1, pjy2);
				fill_speccolumn_vy_sane(_mi, _mj, 	_vx1, pj, pj2, 					pjy1, pjy2);
				fill_speccolumn_virtual(_mi, _mj+(vyR-_vy1), _vx1, vyR, _vy2, pjy1, pjy2);
			}
			else
			{
				s16 pj2 = modulo_vpage_y(_vy2-1)+1;
				fill_speccolumn_vy_sane(_mi, _mj, 	_vx1, pj, pj2, 					pjy1, pjy2);			
			}
		}
	}

	inline void shift_speccolumn_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vy2, s16 pjy1, s16 pjy2)
	{
		if (_vy1 == _vy2)
			return;

		if (!c_vloop_)
		{
			// in this mode, columns are never clipped
			shift_speccolumn_vy_sane(_mi, _mj, _vx1, _vy1, _vy2, pjy1, pjy2);
		}
		else
		{
			// quantize row size into playfield pages
			s16 di1 = quant_vpage_y(_vy1);
			s16 di2 = quant_vpage_y(_vy2-1);
			
			s16 pj = modulo_vpage_y(_vy1);
					
			if (di2 > di1)
			{
				s16 vyR = (s16(di1)+1) * vpage_tiles_yg_;
				s16 pj2 = vpage_tiles_yg_;//modulo_vpage_y(vyR-1)+1;
							
				// split row at first partition
				//fill_speccolumn_virtual(_mi, _mj, _vx1, _vy1, vyR, pjy1, pjy2);
				shift_speccolumn_vy_sane(_mi, _mj, 	_vx1, pj, pj2, 					pjy1, pjy2);
				shift_speccolumn_virtual(_mi, _mj+(vyR-_vy1), _vx1, vyR, _vy2, pjy1, pjy2);
			}
			else
			{
				s16 pj2 = modulo_vpage_y(_vy2-1)+1;
				shift_speccolumn_vy_sane(_mi, _mj, 	_vx1, pj, pj2, 					pjy1, pjy2);			
			}
		}
	}

	inline void fill_block_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vx2, s16 _vy2)
	{
		if (_vx1 == _vx2)
			return;

		if (!c_hloop_)
		{
			// in this mode, rows are never clipped
			fill_block_vx_sane(_mi, _mj, _vx1, _vy1, _vx2, _vy2);
		}
		else
		{
			// start modulo virtual playfield dimensions
			//s16 pi = xmod<s16>(_vx1, vpage_tiles_x_);
			s16 pi = modulo_vpage_x(_vx1);
			// end modulo virtual playfield dimensions
			//s16 pi2 = xmod<s16>(_vx2-1, vpage_tiles_x_)+1;
			s16 pi2 = modulo_vpage_x(_vx2-1)+1;
			
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pi2 <= pi)
			{				
				// block straddles virtual x-wrap, so split in two
				s16 o = vpage_tiles_xg_ - pi;				
				fill_block_vx_sane(_mi+o, _mj,  0, _vy1, pi2, 		   _vy2);
				fill_block_vx_sane(_mi,   _mj, pi, _vy1, vpage_tiles_xg_, _vy2);
			}
			else
			{
				// block entirely within virtual playfield
				fill_block_vx_sane(_mi, _mj, pi, _vy1, pi2, _vy2);
			}
		}
	}

	inline void dirty_block_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vx2, s16 _vy2)
	{
		if (_vx1 == _vx2)
			return;

		if (!c_hloop_)
		{
			// in this mode, rows are never clipped
			dirty_block_vx_sane(_mi, _mj, _vx1, _vy1, _vx2, _vy2);
		}
		else
		{
			// start modulo virtual playfield dimensions
			//s16 pi = xmod<s16>(_vx1, vpage_tiles_x_);
			s16 pi = modulo_vpage_x(_vx1);
			// end modulo virtual playfield dimensions
			//s16 pi2 = xmod<s16>(_vx2-1, vpage_tiles_x_)+1;
			s16 pi2 = modulo_vpage_x(_vx2-1)+1;
			
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pi2 <= pi)
			{				
				// block straddles virtual x-wrap, so split in two
				s16 o = vpage_tiles_xg_ - pi;				
				dirty_block_vx_sane(_mi+o, _mj,  0, _vy1, pi2, 		   _vy2);
				dirty_block_vx_sane(_mi,   _mj, pi, _vy1, vpage_tiles_xg_, _vy2);
			}
			else
			{
				// block entirely within virtual playfield
				dirty_block_vx_sane(_mi, _mj, pi, _vy1, pi2, _vy2);
			}
		}
	}


	inline void shift_block_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vx2, s16 _vy2)
	{
		if (_vx1 == _vx2)
			return;

		if (!c_hloop_)
		{
			// in this mode, rows are never clipped
			shift_block_vx_sane(_mi, _mj, _vx1, _vy1, _vx2, _vy2);
		}
		else
		{
			// start modulo virtual playfield dimensions
			//s16 pi = xmod<s16>(_vx1, vpage_tiles_x_);
			s16 pi = modulo_vpage_x(_vx1);
			// end modulo virtual playfield dimensions
			//s16 pi2 = xmod<s16>(_vx2-1, vpage_tiles_x_)+1;
			s16 pi2 = modulo_vpage_x(_vx2-1)+1;
			
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pi2 <= pi)
			{				
				// block straddles virtual x-wrap, so split in two
				s16 o = vpage_tiles_xg_ - pi;				
				shift_block_vx_sane(_mi+o, _mj,  0, _vy1, pi2, 		   _vy2);
				shift_block_vx_sane(_mi,   _mj, pi, _vy1, vpage_tiles_xg_, _vy2);
			}
			else
			{
				// block entirely within virtual playfield
				shift_block_vx_sane(_mi, _mj, pi, _vy1, pi2, _vy2);
			}
		}
	}

	inline void shift_columns_virtual(s16 _mi, s16 _mj, s16 _vx1, s16 _vy1, s16 _vx2, s16 _vy2)
	{
		if (_vx1 == _vx2)
			return;

		if (!c_hloop_)
		{
			// in this mode, rows are never clipped
			shift_column_vx_sane(_mi, _mj, _vx1, _vy1, _vx2, _vy2);
		}
		else
		{
			// start modulo virtual playfield dimensions
			//s16 pi = xmod<s16>(_vx1, vpage_tiles_x_);
			s16 pi = modulo_vpage_x(_vx1);
			// end modulo virtual playfield dimensions
			//s16 pi2 = xmod<s16>(_vx2-1, vpage_tiles_x_)+1;
			s16 pi2 = modulo_vpage_x(_vx2-1)+1;
			
			// ordering faults indicate playfield wrap @ destination, so we split the fill
			// into sub-blocks, potentially on both axes at once

			if (pi2 <= pi)
			{				
				// block straddles virtual x-wrap, so split in two
				s16 o = vpage_tiles_xg_ - pi;				
				shift_column_vx_sane(_mi+o, _mj,  0, _vy1, pi2, 		   _vy2);
				shift_column_vx_sane(_mi,   _mj, pi, _vy1, vpage_tiles_xg_, _vy2);
			}
			else
			{
				// block entirely within virtual playfield
				shift_column_vx_sane(_mi, _mj, pi, _vy1, pi2, _vy2);
			}
		}
	}

#define fill_rows_virtual fill_block_virtual
#define fill_columns_virtual fill_block_virtual

#define shift_rows_virtual shift_block_virtual
//#define shift_columns_virtual shift_block_virtual

playfield_template()
	: pmapcontext_(0),
	  display_width_(0), display_height_(0),
	  virtual_width_(0), virtual_height_(0),
	  visible_tiles_x_(0), visible_tiles_y_(0),
	  virtual_tiles_x_(0), virtual_tiles_y_(0),
	  vpage_tiles_xu_(0), vpage_tiles_yu_(0),
	  vpage_tiles_xg_(0), vpage_tiles_yg_(0),
	  //scrollmargin_tiles_x_(0), scrollmargin_tiles_y_(0),
	  wrap_width_(0), wrap_height_(0),
	  bufferwords_(0),
	  display_alloc_(0), display_(0),
	  buffer_(0), field_(0),
	  xpos_(0), ypos_(0), last_xpos_(0), last_ypos_(0),
	  txpos_(0), last_txpos_(0),
	  typos_(0), last_typos_(0),
	  ty_last_dx_(0), tx_last_dy_(0),
	  tiles_(0), tilesoverlay_(0),
	  tiledata_(0), tileoverlaydata_(0),
	  map_(0), mapoverlay_(0), 
	  map_tiles_x_(0), map_tiles_y_(0),
	  tilecount_(0),
	  quant_vpage_x_(0), quant_vpage_y_(0),
	  modulo_vpage_x_(0), modulo_vpage_y_(0),
	  modulo_map_x_(0), modulo_map_y_(0),
	  mul_map_x_(0),
	  xpreshift_(0), drawshift_(1), 
	  slice_(0), last_slice_(0),
#if defined(AGT_CONFIG_MAPMOD_BOUNDS)
	  dtx1_(0x7fff), dty1_(0x7fff),
	  dtx2_(0x8000), dty2_(0x8000),
#else
	  dtx1_(0), dty1_(0),
	  dtx2_(0), dty2_(0),
#endif
	  //dirtytiles_(0),
	  //dirtynum_(0),
/*	  dirtymask_(0), //layermask_(0),
	  dirtymasklines_(0), //layermasklines_(0),
	  dirtymaskwidth_(0),
	  dirtymaskstride_(0),
	  dirty_oct_(0), 
	  dirty_oco_(0), 
	  dirty_ocmstride_(0),
*/	  maskedges_(0)
{
	restorestate_.init();
}

~playfield_template()
{
	dprintf("destroy playfield\n");
	delete[] display_alloc_;

//	if (c_dynamic_)
//	{
//		delete[] dirtytiles_;
//		delete[] dtm_animation_.dirtymask_;
//		delete[] dtm_animation_.dirtymasklines_;
//		delete[] ocm_.oct_;
//		delete[] ocm_.oco_;
//	}

	delete[] restorestate_.pstack;

	//if (c_dynamiclayer_)
	//{
	//	delete[] layermask_;
	//	delete[] layermasklines_;
	//}
}

void init(s16 _buffer, s16 _field, s16 _display_width, s16 _display_height, s16 _x, s16 _y, s16 _displaymode)
{
	buffer_ = _buffer;
	field_ = _field;

	// shadow map context info
	xassert(pmapcontext_);

	modulo_map_x_ = pmapcontext_->modulo_map_x_;
	modulo_map_y_ = pmapcontext_->modulo_map_y_;
	mul_map_x_ = pmapcontext_->mul_map_x_;
	map_tiles_x_ = pmapcontext_->map_tiles_x_;
	map_tiles_y_ = pmapcontext_->map_tiles_y_;
	srcywrap_ = pmapcontext_->srcywrap_;
	map_ = pmapcontext_->map_;
	mapoverlay_ = pmapcontext_->mapoverlay_;

	xassert(map_);

	last_xpos_ = 0;
	last_ypos_ = 0;
	last_txpos_ = 0;
	last_typos_ = 0;
	setpos(_x, _y);

/*	g_tshift = _x & (c_tilesize_-1);

	xpos_ = last_xpos_ = _x;
	ypos_ = last_ypos_ = _y;

	txpos_ =  last_txpos_ = _x / c_tilesize_;
	typos_ =  last_typos_ = _y / c_tilesize_;
*/

	display_width_ = _display_width;
	display_height_ = _display_height;

	// dimensions for tiles potentially visible at any time - active display_ area only
	// notes:
	// STE adds one extra visible word-column when hscroll is active
	// all systems add one extra visible row when vscroll is active
	// vertical height always rounded up to next tile
	if (c_ste_)
		visible_tiles_x_ = ((display_width_ + (c_tilesize_-1)) / c_tilesize_) + ((/*c_updatecols_ &&*/ c_ste_) ? c_hscrolltiles_ : 0);  // keep horizontal margin for fixed 184 byte line
	else
		visible_tiles_x_ = ((display_width_ + (c_tilesize_-1)) / c_tilesize_) - (c_stf_maskedspeculation_ ? 2 : 0);
		
	visible_tiles_y_ = ((display_height_ + (c_tilesize_-1)) / c_tilesize_) + (c_updaterows_ ? 1 : 0);

	// dimensions for virtual page size used for tile updating - larger than display area + scroll margin
	// note: hardcode tile based width to make vpage exactly 184 bytes per line, to accommodate other drawing functions

	if (c_ste_)
	{
		vpage_tiles_xu_ = visible_tiles_x_ + c_tiles_per_word_ * 2;

/*		if (c_tilesize_ == 16)
			vpage_tiles_x_ = visible_tiles_x_ + 2;//(c_hspeculative_ ? 2 : 0);
		else
		if (c_tilesize_ == 8)
				vpage_tiles_x_ = visible_tiles_x_ + 4;//(c_hspeculative_ ? 4 : 0);
		else
			xassert(false);
*/
	}
	else
	{
		vpage_tiles_xu_ = visible_tiles_x_ + (c_stf_maskedspeculation_ ? 2 : 0);
	}

	vpage_tiles_yu_ = visible_tiles_y_ + (c_vspeculative_ ? 2 : 0);

	// determine size of margin *before* guardband expansion
	s16 margin_tiles_x = vpage_tiles_xu_ - visible_tiles_x_;
	s16 margin_tiles_y = vpage_tiles_yu_ - visible_tiles_y_;
	s16 margin_pixels_x = margin_tiles_x << c_tilestep_;
	s16 margin_pixels_y = margin_tiles_y << c_tilestep_;


	// incorporate x-guardband in virtual page (absorbing any existing margin)
	//vpage_tiles_xg_ = vpage_tiles_xu_ + ((xmax(0, c_guardx - margin_pixels_x)) >> 4) * c_tiles_per_word_;
	vpage_tiles_xg_ = visible_tiles_x_ + ((xmax<s16>(c_guardx, margin_pixels_x)) >> 4) * c_tiles_per_word_;

	// incorporate y-guardband in virtual page (absorbing any existing margin)
	//vpage_tiles_yg_ = vpage_tiles_yu_ + ((xmax(0, c_guardy - margin_pixels_y)) >> 4) * c_tiles_per_word_;
	vpage_tiles_yg_ = visible_tiles_y_ + ((xmax<s16>(c_guardy, margin_pixels_y)) >> 4) * c_tiles_per_word_;


// reference window
//visible_tiles_x_ = 12;
//visible_tiles_y_ = 8;

// correct for initial-fill
//vpage_tiles_xu_ = visible_tiles_x_ + 2;
//vpage_tiles_yu_ = visible_tiles_y_ + 2;

	// cached calculation for end of update area, for incremental tile filling
//	scrollmargin_tiles_x_ = 4 + ((c_hspeculative_ /*&& !c_hbufferspeculation_*/) ? visible_tiles_x_ : vpage_tiles_xu_);
//	scrollmargin_tiles_y_ = 4 + (c_vspeculative_ ? visible_tiles_y_ : vpage_tiles_yu_);

//	scrollmargin_tiles_x_ = visible_tiles_x_; //vpage_tiles_xu_;
//	scrollmargin_tiles_y_ = visible_tiles_y_; //vpage_tiles_yu_;

	// allocate & prepare tables for final page dimensions
	pfieldcontext_->prepare(vpage_tiles_xg_, vpage_tiles_yg_);
	quant_vpage_x_ = pfieldcontext_->quant_vpage_x_;
	quant_vpage_y_ = pfieldcontext_->quant_vpage_y_;
	modulo_vpage_x_ = pfieldcontext_->modulo_vpage_x_; 
	modulo_vpage_y_ = pfieldcontext_->modulo_vpage_y_; 

	// dimensions for virtual playfield  (not same as map dimensions)
	// visible plus row/column update margin, x2 for circular loop
	if (!c_updateQ_)
		virtual_tiles_x_ = vpage_tiles_xg_;
	else
		virtual_tiles_x_ = vpage_tiles_xg_ * 2;

	if (!c_updateR_)	
		virtual_tiles_y_ = vpage_tiles_yg_;
	else
		virtual_tiles_y_ = vpage_tiles_yg_ * 2;
	
	// todo: ?
	wrap_width_ = vpage_tiles_xg_ * c_tilesize_;
	wrap_height_ = vpage_tiles_yg_ * c_tilesize_;
			
	// pixel dimensions of virtual playfield (not same as map dimensions)
	virtual_width_ = virtual_tiles_x_ * c_tilesize_;
	virtual_height_ = virtual_tiles_y_ * c_tilesize_;

	// words per virtual playfield 'scanline'
	virtual_linewords_ = virtual_tiles_x_ * c_tilelinewords_;
	//visible_linewords_ = visible_tiles_x_ * c_tilelinewords_;
	vpage_linewords_ = vpage_tiles_xg_ * c_tilelinewords_;
		
	rowskipwords_ = (c_tilesize_ * virtual_linewords_);
	virtual_linebytes_ = virtual_linewords_ << 1;
	visible_linebytes_ = vpage_linewords_ << 1;
	virtual_pagebytes_ = virtual_linebytes_ * wrap_height_;	
	
	// words per virtual playfield buffer

	bufferwords_ = virtual_linewords_ * virtual_height_;

	if (c_scanwalk_)
	{
		// pad by ~100 extra scanlines in scanwalk mode, to eliminate loopback buffer
		bufferwords_ += (c_max_vt_ * c_tilelinewords_);
	}

	int guardx_safety_words = ((c_guardx + (16-1)) >> 4) << 2;

	// artificial padding for y guardband should absorb the speculative update margin, if active
	// careful: don't -ve pad!
	s16 guardy1_pad = xmax(0, (c_guardy - c_vspeculative_margin_));
	int guardy1_safety_words = ((guardy1_pad + (16-1)) & -16) * virtual_linewords_;

	// pad for left guardband
	bufferwords_ += guardx_safety_words;
	// pad for top guardband
	bufferwords_ += guardy1_safety_words;
	// pad for bottom guardband
	//bufferwords_ += guardy2_safety_words;

//	bufferwords_ += 10000;

	pprintf("init playfield:\n");
#if defined(AGT_CONFIG_PRINT_PLAYFIELD_METRICS)
	printf("dw:%d, vrw:%d, vrtw:%d\n", (int)display_width_, (int)virtual_width_, (int)virtual_tiles_x_);
	printf("dh:%d, vrh:%d, vrth:%d\n", (int)display_height_, (int)virtual_height_, (int)virtual_tiles_y_);
	printf("vstx:%d, vpgxu:%d, vpgxg:%d\n", (int)visible_tiles_x_, (int)vpage_tiles_xu_, (int)vpage_tiles_xg_);
	printf("vsty:%d, vpgyu:%d, vpgyg:%d\n", (int)visible_tiles_y_, (int)vpage_tiles_yu_, (int)vpage_tiles_yg_);
	printf("wrw:%d, wrh:%d\n", (int)wrap_width_, (int)wrap_height_);
	printf("c_hmargin_:%d c_guardx:%d\n", (int)c_hmargin_, (int)c_guardx);
	printf("c_vmargin_:%d c_guardy:%d\n", (int)c_vmargin_, (int)c_guardy);
	printf("c_tilesize_:%d\n", (int)c_tilesize_);
//	printf("c_tileplanewords_:%d\n", (int)c_tileplanewords_);
	printf("c_tilelinewords_:%d\n", (int)c_tilelinewords_);
	printf("virtual_linewords_ = %d\n", (int)virtual_linewords_);
	printf("virtual_height_ = %d\n", (int)virtual_height_);
	printf("virtual_pagebytes_ = %d\n", (int)virtual_pagebytes_);
	printf("bufferwords_:%d\n", (int)bufferwords_);
	if (c_scanwalk_) printf("scanwalk padding words = %d\n", (int)(c_max_vt_ * c_tilelinewords_));
	printf("preshift: %d\n", (int)xpreshift_); 
#endif

	// todo: can be shared between connected playfields
/*	{	
		s16 xo = c_max_vt_ / vpage_tiles_x_;
		s16 yo = c_max_vt_ / vpage_tiles_y_;
		
		//di1 = ((_vx1 + (vpage_tiles_x_ << 3)) / vpage_tiles_x_) - 8
		delete[] quant_vpage_x_; 
		quant_vpage_x_ = new s16[c_max_vt_<<1];
		for (s16 n = 0; n < c_max_vt_<<1; ++n)
		{
			s16 v = n-c_max_vt_;
			quant_vpage_x_[n] = ((v + (xo * vpage_tiles_x_)) / vpage_tiles_x_) - xo;
		}
	
		delete[] quant_vpage_y_; 
		quant_vpage_y_ = new s16[c_max_vt_<<1];
		for (s16 n = 0; n < c_max_vt_<<1; ++n)
		{
			s16 v = n-c_max_vt_;
			quant_vpage_y_[n] = ((v + (yo * vpage_tiles_y_)) / vpage_tiles_y_) - yo;
		}
		
		delete[] modulo_vpage_x_; 
		modulo_vpage_x_ = new s16[c_max_vt_<<1];
		for (s16 n = 0; n < c_max_vt_<<1; ++n)
		{
			s16 v = n-c_max_vt_;
			modulo_vpage_x_[n] = tmod16(v, vpage_tiles_x_);
		}
		
		delete[] modulo_vpage_y_; 
		modulo_vpage_y_ = new s16[c_max_vt_<<1];
		for (s16 n = 0; n < c_max_vt_<<1; ++n)
		{
			s16 v = n-c_max_vt_;
			modulo_vpage_y_[n] = tmod16(v, vpage_tiles_y_);
		}
	}
*/
	// todo: can be shared between connected playfields
	//{
	//	s16 xo = c_max_vt_ / map_tiles_x_;
	//	s16 yo = c_max_vt_ / map_tiles_y_;
	//		
	//	delete[] modulo_map_x_; 
	//	modulo_map_x_ = new s16[c_max_vt_<<1];
	//	for (s16 n = 0; n < c_max_vt_<<1; ++n)
	//	{
	//		s16 v = n-c_max_vt_;
	//		modulo_map_x_[n] = tmod16(v, map_tiles_x_);
	//	}
	//	
	//	delete[] modulo_map_y_; 
	//	modulo_map_y_ = new s16[c_max_vt_<<1];
	//	for (s16 n = 0; n < c_max_vt_<<1; ++n)
	//	{
	//		s16 v = n-c_max_vt_;
	//		modulo_map_y_[n] = tmod16(v, map_tiles_y_);
	//	}
	//}

	// dirty tile management: per-playfield state

	if (c_dynamic_)
	{
		dtm_animation_.release();
		dtm_animation_.allocate(map_tiles_x_, map_tiles_y_);
	}

	if (c_tilerestore_)
	{
		dtm_bgrestore_.release();
		dtm_bgrestore_.allocate(map_tiles_x_, map_tiles_y_);

		// OCM allocator requires guardband (in tiles) for occlusion masks drawn with -ve coords
		static const s16 guard_tiles_x = c_guardx >> c_tilestep_;
		static const s16 guard_tiles_y = c_guardy >> c_tilestep_;

		ocm_.release();
		ocm_.allocate(map_tiles_x_, map_tiles_y_, guard_tiles_x, guard_tiles_y);

		//// test: checker pattern for mask
		//for (s16 y = 0; y < map_tiles_y_; y++)
		//{
		//	for (s16 x = 0; x < map_tiles_x_/4; x++)
		//	{
		//		if (((x^y) & 1) || (x < 2) || (y < 2))
		//		{
		//			s16 xf = x & 7;
		//			s16 xb = x >> 3;

		//			dirtymask_[xb + (y*dirtymaskstride_>>1)] |= (0x8000 >> xf);
		//		}
		//	}
		//}
	}

	//if (c_dynamiclayer_)
	//{
	//	delete[] layermask_;
	//	layermask_ = new s16[dmsize];
	//	qmemclr(layermask_, dmsize<<1);

	//	delete[] layermasklines_; 
	//	layermasklines_ = new s16*[map_tiles_y_];
	//	for (s16 n = 0; n < map_tiles_y_; ++n)
	//		layermasklines_[n] = &layermask_[n * dirtymaskwidth_];
	//}

	delete[] restorestate_.pstack;
	restorestate_.pstack = new s32[5 * 256];
	restorestate_.pstack[0] = 0;
	restorestate_.ptide = &restorestate_.pstack[1];

	delete[] display_alloc_;
	int display_alloc_bytes = (bufferwords_<<1) + 256;
	display_alloc_ = new u8[display_alloc_bytes];
	//qmemclr(display_alloc_, display_alloc_bytes);
	qmemset(display_alloc_, 0xFF, display_alloc_bytes);

//	display_ = (u16*)(((u32)(display_alloc_) + (160*14) + 255) & -256);
//	framebuffer_ = display_ + (160*7);

	// the displayservice may steal some invisible lines from the true physical display, resulting
	// in the display starting 'late' in memory. e.g. STF synscroll always steals some lines as does 
	// the 240 line overscan mode on STE. 
	// to compensate for this we allocate the same amount of memory as usual, but we offset the
	// display_ base by the missing line count before alignment, and add it back to the framebuffer
	// address for writing graphics. in this way no memory is wasted.

	s16 consumed_lines = g_displayservice_consumed_lines[_displaymode];
	s16 displaysvc_consumed_words = (consumed_lines * virtual_linewords_) + (c_stf_maskedspeculation_ ? 4 : 0);

#if defined(AGT_CONFIG_PRINT_PLAYFIELD_METRICS)
	printf("displaymode:%d\n", (int)_displaymode);
	printf("consumed_lines:%d\n", (int)consumed_lines);
	printf("consumed_words:%d\n", (int)displaysvc_consumed_words);
#endif


	// display_:			**************** <display base may have -ve offset from allocation e.g. STF syncscroll dead lines>
	//
	// display_alloc_:		guardx+guardy1	<allocated, writable area begins here - includes x & y guardband safety zones>
	// framebuffer_int_:	guardy1			<internal line addressing begins here, includes y guardband safety zone, no -ve y permitted>
	// framebuffer_ext_:	visible			<external line addressing begins here, excludes guardband safety zones, -ve y permitted

	display_ = (u16*)
	(
		(
			(
				(u32)(display_alloc_) - (displaysvc_consumed_words<<1)
			) 
			+ (guardx_safety_words<<1)		// left guard padding
			+ (guardy1_safety_words<<1)		// upper guard padding
			+ 255
		) 
		& -256
	);
	framebuffer_ = display_ + displaysvc_consumed_words;

	// HACK: to view guard region vs sprite clipping activity
	//display_ -= guardy1_safety_words;
	//display_ -= guardx_safety_words;
	//display_ -= virtual_linewords_ * 64;

//	qmemclr(display_, bufferwords_<<1);
//	clear(display_, bufferwords_);
	//set(display_, bufferwords_, 0xAAAA);
	
	if (c_blitter_)
		prepare_blitter();

#if (1)

/*	s16 txp = txpos_-c_hmargin_;
	s16 typ = typos_-c_vmargin_;

	if (c_scanwalk_ && txp < 0)
		txp = 0;

	// prefill all buffers, prepared for wrap
	if (c_ste_)
	{
		fill_block_virtual
		(
			txp, typ,
			txp, typ, 
			txpos_-c_hmargin_ + vpage_tiles_x_, typos_-c_vmargin_ + vpage_tiles_y_
		);
	}
	else
	{
		shift_block_virtual
	(
		txp, typ,
		txp, typ, 
		txpos_-c_hmargin_ + vpage_tiles_x_, typos_-c_vmargin_ + vpage_tiles_y_
	);
	}

//	last_xpos_ = xpos_;
//	last_ypos_ = ypos_;
		
//	last_txpos_ = txpos_;
//	last_typos_ = typos_;
*/

	fill_abs();

#endif

#if (0)

	s16 txp = txpos_;
	s16 typ = typos_;

	// prefill all buffers, prepared for wrap
	fill_rows_virtual
	(
		txp, typ,
		txp, typ, 
		txp + vpage_tiles_x_, typ + vpage_tiles_y_
	);

#endif

}

// ---------------------------------------------------------------------------------------------------------------------

	//s16 txp = txpos_-c_hmargin_;
	//s16 typ = typos_-c_vmargin_;

	//if (c_scanwalk_ && txp < 0)
	//	txp = 0;

	//// prefill all buffers, prepared for wrap
	//fill_rows_virtual
	//(
	//	txp, typ,
	//	txp, typ, 
	//	txpos_-c_hmargin_ + vpage_tiles_x_, typos_-c_vmargin_ + vpage_tiles_y_
	//);

void fill_abs()
{
	s16 txp = txpos_-c_hmargin_;
	s16 typ = typos_-c_vmargin_;
	g_xpreshift = xpreshift_;

	if (c_scanwalk_ && txp < 0)
		txp = 0;

	// prefill all buffers, prepared for wrap
	//fill_rows_virtual
	//(
	//	txp, typ,
	//	txp, typ, 
	//	txpos_-c_hmargin_ + vpage_tiles_x_, typos_-c_vmargin_ + vpage_tiles_y_
	//);

	if (c_ste_)
	{
		fill_block_virtual
		(
			txp, typ,
			txp, typ, 
			txpos_-c_hmargin_ + vpage_tiles_xu_, typos_-c_vmargin_ + vpage_tiles_yu_
		);
	}
	else
	{		
		shift_block_virtual
		(
			txp, typ,
			txp, typ, 
			txpos_-c_hmargin_ + vpage_tiles_xu_, typos_-c_vmargin_ + vpage_tiles_yu_
		);
	}

	last_xpos_ = xpos_;
	last_ypos_ = ypos_;
		
	last_txpos_ = txpos_;
	last_typos_ = typos_;
}

// ---------------------------------------------------------------------------------------------------------------------

agt_outline void warp_catchup()
{
		// 'warp' by at least viewport dimensions - full refill
#ifdef TIMING_RASTERS_MAIN		
		reg16(ffff8240) = 0x400;
		reg16(00000064) = 0x400;
#endif		

		// prefill all buffers, prepared for wrap
#if (1)
		s16 txp = txpos_-c_hmargin_;
		s16 typ = typos_-c_vmargin_;

		//if (c_scanwalk_ && txp < 0)
		//	txp = 0;

	//	int qqq = 200000; while (qqq--) { };
	
		//dbg_validate_restorestack(0x500);
		if (c_ste_)
		fill_block_virtual
		(
			txp, typ,
			txp, typ, 
			txpos_-c_hmargin_ + vpage_tiles_xu_, 
			typos_-c_vmargin_ + vpage_tiles_yu_
		);	
		else
		shift_block_virtual
		(
			txp, typ,
			txp, typ, 
			txpos_-c_hmargin_ + vpage_tiles_xu_, 
			typos_-c_vmargin_ + vpage_tiles_yu_
		);	
		//dbg_validate_restorestack(0x050);

#endif
#ifdef TIMING_RASTERS_MAIN
		reg16(ffff8240) = 0xf00;
		reg16(00000064) = 0xf00;
#endif		

#if !defined(AGT_CONFIG_STF_NOEDGEMASKING)
		if (c_hsyncshift_ && c_updatecols_)
		{
			maskedges_ |= 3;
			//STF_MaskEdgeL(framebuffer_, xpos_, ypos_, display_height_);
			//STF_MaskEdgeR(framebuffer_, xpos_, ypos_, display_height_);
		}	
#endif
	
	last_txpos_ = txpos_;
	last_typos_ = typos_;
	last_xpos_ = xpos_;
	last_ypos_ = ypos_;
	last_slice_ = slice_;
}

void hybrid_fill()
{
	s16 dx = (xpos_ - last_xpos_);
	s16 dy = (ypos_ - last_ypos_);
	g_xpreshift = xpreshift_;

	if ((dx == 0) && c_hbufferspeculation_)
		dx = tsign16(last_slice_ - slice_);
//		dx = 1;

	//if ((dx == 0) && 
	//	(dy == 0))
	//{
	//	return;
	//}

	if (c_blitter_)
		prepare_blitter();		

	// obtain tile scroll fragments (remainder of tile-quantized position)
	s16 lxf = last_xpos_ & (c_tilesize_-1);
	s16 lyf = last_ypos_ & (c_tilesize_-1);
	s16 xf = xpos_ & (c_tilesize_-1);
	s16 yf = ypos_ & (c_tilesize_-1);

/*
	// round up tile delta for any nonzero movement so we can draw some part of a row
			
	if (dx > 0)
		tdx = (((dx + (c_tilesize_ - 1)) & -c_tilesize_) >> 4);
	else
		tdx = -(((-dx + (c_tilesize_ - 1)) & -c_tilesize_) >> 4);

	if (dy > 0)
		tdy = (((dy + (c_tilesize_ - 1)) & -c_tilesize_) >> 4);
	else
		tdy = -(((-dy + (c_tilesize_ - 1)) & -c_tilesize_) >> 4);
*/			

	// refresh dirty tiles before incremental updates
	if (c_ste_ && (c_dynamic_ || c_tilerestore_))
	{
		if (c_tilerestore_)
		{
#if defined(AGT_CONFIG_DEBUG_OCCLUSION)
			dbg_vis_occlusion();
#endif
			// find the viewport rectangle
			s16 tx1 = txpos_-c_hmargin_;
			s16 ty1 = typos_-c_vmargin_;

/*			// never refresh across map left edge
			if (c_scanwalk_ && (tx1 < 0))
				tx1 = 0;
*/
			s16 tx2 = txpos_-(c_hmargin_) + vpage_tiles_xu_;
			s16 ty2 = typos_-(c_vmargin_) + vpage_tiles_yu_;

			//printf("tx1:%d ty1:%d\n", (int)tx1, (int)ty1);

			//OCM_integrate_window(tx1, ty1, tx2, ty2);

			// integrate both active pages for v-loopback
			s16 pi1 = txpos_;
			if (c_hloop_)
				pi1 = modulo_vpage_x(txpos_);
			s16 pi2 = pi1+vpage_tiles_xg_;

/*			{
	//			s16 pj1 = modulo_vpage_y(typos_);
	//			s16 pj2 = pj1+vpage_tiles_y_;

				s16 pj1 = 0;
				s16 pj2 = virtual_tiles_y_;

				OCM_integrate_window(pi1, pj1, pi2, pj2);
			}
*/
			if (1)
			{
			// TODO: check this against tilerestore sample

			// update page fragments one at a time with correct map coords
			{
				// current, visible window
				s16 pj1 = typos_;
				if (c_vloop_)
					pj1 = modulo_vpage_y(pj1);
				s16 pj2 = pj1+vpage_tiles_yu_;
				OCM_integrate_window(pi1, pj1, pi2, pj2);
				OCM16_refresh_window(txpos_, typos_, pi1, pj1, pi2, pj2);
			}

			// repaint virtual page fragments according to direction of scroll
			// without this, BGRESTORE can't expect a pristine restore source for non-OCM refreshed
			// sprites if OCM sprites have left traces there.

			if (dy < 0)
			{
				// upper fragment (one page ahead)
				s16 pj1 = 0;
				s16 pj2 = (typos_-vpage_tiles_yg_);
				if (c_vloop_)
					pj2 = modulo_vpage_y(pj2);
				OCM_integrate_window(pi1, pj1, pi2, pj2);
				OCM16_refresh_window(txpos_, typos_+vpage_tiles_yg_-(pj2-pj1), pi1, pj1, pi2, pj2);
			}

			if (dy > 0)
			{
				// lower fragment (one page behind)
				s16 pj1 = modulo_vpage_y(typos_+vpage_tiles_yg_);
				s16 pj2 = virtual_tiles_y_ - pj1;
				OCM_integrate_window(pi1, pj1, pi2, pj2);
				OCM16_refresh_window(txpos_, typos_-vpage_tiles_yg_, pi1, pj1, pi2, pj2);
			}

			}

		} // c_tilerestore_

		if (c_dynamic_)
		{

#define AGT_CONFIG_MAPMOD_BOUNDS
#define AGT_CONFIG_MAPMOD_SWEEP_SPEED (4)

		// vertical sweep

#if defined(AGT_CONFIG_MAPMOD_SWEEP)

		static const s16 speed = AGT_CONFIG_MAPMOD_SWEEP_SPEED;

		s16 ymin = (typos_ - c_vmargin_) & -AGT_CONFIG_MAPMOD_SWEEP_SPEED;
		s16 ymax = (ymin + vpage_tiles_yu_) & -AGT_CONFIG_MAPMOD_SWEEP_SPEED;

		dty2_ = dty1_ + speed;
		{
			// find the viewport rectangle
			s16 tx1 = txpos_-c_hmargin_;
			s16 ty1 = typos_-c_vmargin_;
			// never refresh across map left edge
			if (c_scanwalk_ && (tx1 < 0))
				tx1 = 0;

			s16 tx2 = txpos_-(c_hmargin_) + vpage_tiles_xu_;
			s16 ty2 = typos_-(c_vmargin_) + vpage_tiles_yu_;

			// find intersection of 2 rectangles (viewport, edited region)
			ty1 = xmax(ty1, dty1_);
			ty2 = xmin(ty2, dty2_);

			if (ty2 > ty1)
			{
				dirty_block_virtual
				(
					tx1, ty1,
					//
					tx1, ty1, 
					tx2, ty2
				);
			}
		}
		dty1_ += speed;
		if (dty1_ > ymax-speed)
			dty1_ = ymin;
#endif

#if defined(AGT_CONFIG_MAPMOD_BOUNDS)
		if ((dtx1_ < dtx2_) && (dty1_ < dty2_))
		{
			// find the viewport rectangle
			s16 tx1 = txpos_-c_hmargin_;
			s16 ty1 = typos_-c_vmargin_;
			// never refresh across map left edge
			if (c_scanwalk_ && (tx1 < 0))
				tx1 = 0;

			s16 tx2 = txpos_-(c_hmargin_) + vpage_tiles_xu_;
			s16 ty2 = typos_-(c_vmargin_) + vpage_tiles_yu_;

			// find intersection of 2 rectangles (viewport, edited region)
			tx1 = xmax(tx1, dtx1_);
			ty1 = xmax(ty1, dty1_);
			tx2 = xmin(tx2, dtx2_);
			ty2 = xmin(ty2, dty2_);

			// refresh only the minmax bounded area
			if ((tx2 > tx1) && (ty2 > ty1))
			{
				dirty_block_virtual
				(
					tx1, ty1,
					//
					tx1, ty1, 
					tx2, ty2
				);
			}

			// reset edited region
			dtx1_ = dty1_ = 0x7fff;
			dtx2_ = dty2_ = -1;
		}
#endif

#if defined(AGT_CONFIG_MAPMOD_FULL)
		{
			// find the viewport rectangle
			s16 tx1 = txpos_-c_hmargin_;
			s16 ty1 = typos_-c_vmargin_;
			// never refresh across map left edge
			if (c_scanwalk_ && (tx1 < 0))
				tx1 = 0;

			s16 tx2 = txpos_-(c_hmargin_) + vpage_tiles_xu_;
			s16 ty2 = typos_-(c_vmargin_) + vpage_tiles_yu_;

			{
				dirty_block_virtual
				(
					tx1, ty1,
					//
					tx1, ty1, 
					tx2, ty2
				);
			}
		}
#endif
		} // c_dynamic_
	}

	// shortcut incremental if no movement
	if ((dx == 0) && 
		(dy == 0))
	{
//		printf("no deltas!\n");
		return;
	}

	// determine draw delta in terms of tile rows/columns
	s16 tx = txpos_;
	s16 ty = typos_;
	s16 tdx = (tx - last_txpos_);
	s16 tdy = (ty - last_typos_);

	//if (!c_hspeculative_)
	//{
	//	if ((tdx == 0) && (dy == 0))
	//		return;
	//}

	// software h-scroll requires shifted tiles to be redrawn on any pixel delta
	//if (c_hsoftscroll_ && !c_hspeculative_ && !c_hbufferspeculation_)
	//	if (tdx == 0)
	//		tdx = tsign16(dx);
			//tdx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
	if (c_hsoftscroll_ && c_hbufferspeculation_)
//		tdx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
		tdx = 1;

	// 'last' scroll position derived from new tile deltas
	s16 txl = tx - tdx;
	s16 tyl = ty - tdy;

	const s16 atdx = xabs(tdx);
	const s16 atdy = xabs(tdy);

	bool warp = 
		((atdx >= visible_tiles_x_) || 
		 (atdy >= visible_tiles_y_));

	if (warp)
	{
		warp_catchup();
	}
	else
	{

	s16 x1 = tx;
	s16 y1 = ty;
	s16 x2 = tx + visible_tiles_x_; //scrollmargin_tiles_x_;
	s16 y2 = ty + visible_tiles_y_; //scrollmargin_tiles_y_;

//	printf("x1:%d, x2:%d\n", x1, x2);
//	printf("y1:%d, y2:%d\n", y1, y2);

	s16 hs_fill_y1 = y1;
	s16 hs_fill_y2 = y2;
	s16 vs_fill_x1 = x1;
	s16 vs_fill_x2 = x2;


	//// initial hscroll fill column, assuming no change in y
	//s16 hs_fill_y1 = ty;
	//s16 hs_fill_y2 = ty + vpage_tiles_y_;
	//s16 vs_fill_x1 = tx;
	//s16 vs_fill_x2 = tx + vpage_tiles_x_;

	//if (!c_vspeculative_)
	//{
	//	if (tdy > 0)
	//	{
	//		// don't ever fill more than a visible screen's worth
	//		if (tdy > vpage_tiles_y_)
	//			tdy = vpage_tiles_y_;

	//		// adjust height of hscrollfill column for change in y
	//		//hs_fill_y2 -= tdy;
	//	}			
	//	else if (tdy < 0)
	//	{
	//		// don't ever fill more than a visible screen's worth
	//		if (tdy < -vpage_tiles_y_)
	//			tdy = -vpage_tiles_y_;

	//		// adjust height of hscrollfill column for change in y
	//		//hs_fill_y1 -= tdy;
	//	}
	//}

	//if (!c_hspeculative_)
	//{
	//	if (tdx > 0)
	//	{
	//		// don't ever fill more than a visible screen's worth
	//		if (tdx > vpage_tiles_x_)
	//			tdx = vpage_tiles_x_;

	//		// adjust width of vscrollfill row for change in x
	//		vs_fill_x2 -= tdx;
	//	}			
	//	else if (tdx < 0)
	//	{
	//		// don't ever fill more than a visible screen's worth
	//		if (tdx < -vpage_tiles_x_)
	//			tdx = -vpage_tiles_x_;

	//		// adjust width of vscrollfill row for change in x
	//		vs_fill_x1 -= tdx;
	//	}
	//}


#if !(SPECULATIVE_FILL_METHOD-0)
	// method 0: easy but expensive...
	// use longer rows/columns which overlap at ends to cover missed speculations at corners
	if (c_vspeculative_ && c_updatecols_)
	{
		hs_fill_y1 -= 1;
		hs_fill_y2 += 1;
	}
	if (c_hspeculative_ && c_updaterows_)
	{
		vs_fill_x1 -= 1;
		vs_fill_x2 += 1;
	}
#endif

//	printf("x1:%d, x2:%d\n", vs_fill_x1, vs_fill_x2);
//	printf("y1:%d, y2:%d\n", hs_fill_y1, hs_fill_y2);

/*		
	if (tdy > 0)
	{
		// don't ever fill more than a visible screen's worth
		if (tdy > vpage_tiles_y_)
			tdy = vpage_tiles_y_;

		// adjust height of hscrollfill column for change in y
		//hs_fill_y2 -= tdy;
	}			
	else if (tdy < 0)
	{
		// don't ever fill more than a visible screen's worth
		if (tdy < -vpage_tiles_y_)
			tdy = -vpage_tiles_y_;

		// adjust height of hscrollfill column for change in y
		//hs_fill_y1 -= tdy;
	}
*/

	if (c_updaterows_ && c_vspeculative_ && (c_ste_ || drawshift_))
	{
	if (dy > 0)
	{	
		// fill bottom edge

#if !(SPECULATIVE_FILL_METHOD-1)
		// calculate portion which may have missed previous speculative updates
		if (tx_last_dy_ < tx)
		{
			// 00000000*
			// 00000000*
			// 00000000*
			// 00000000*
			// *****XXXX
			
			// we have scrolled right since the last DY
			// right tiles may be spoiled
			
			s16 patch_x1 = tx_last_dy_ + visible_tiles_x_;
			s16 patch_x2 = x2 + 1;
			s16 patch_y = y2;

			if (c_ste_)
			fill_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//lyf, lyf+dy
			);		
			else
			shift_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//lyf, lyf+dy
			);		
		}
		else if (tx_last_dy_ > tx) // patch lower-left diagonal
		{
			// *00000000
			// *00000000
			// *00000000
			// *00000000
			// XXXX*****

			// we have scrolled left since the last DX
			// left tiles may be spoiled
			
			s16 patch_x1 = x1 - 1;
			s16 patch_x2 = tx_last_dy_;
			s16 patch_y = y2;
						
			if (c_ste_)
			fill_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//lyf, lyf+dy
			);			
			else
			shift_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//lyf, lyf+dy
			);			
		}
		else
		{
			// no change on other axis - speculation should be correct
		}
#endif // SPECULATIVE_FILL_METHOD 1
					
		// 00000000
		// 00000000
		// 00000000
		// 00000000
		// XXXXXXXX

#if 0
		// amortize any single-pixel remainder to minimize double-column updates
		if (lyf == c_tilesize_-1)
			lyf = c_tilesize_;
		if (yf == c_tilesize_-1)
			yf = c_tilesize_;
#endif

		if (yf == lyf)
		{
		}									
		else if (yf > lyf)
		{
			if (c_ste_)
			fill_specrow_virtual(
				vs_fill_x1, y2,
				vs_fill_x1, y2, 
				vs_fill_x2,
				lyf, yf	//lyf+dy
			);	
			else
			shift_specrow_virtual(
				vs_fill_x1, y2,
				vs_fill_x1, y2, 
				vs_fill_x2,
				lyf, yf	//lyf+dy
			);	
		}
		else
		{		
			if (lyf < c_tilesize_)
				{
			if (c_ste_)
			fill_specrow_virtual(
				vs_fill_x1, y2 - 1,
				vs_fill_x1, y2 - 1, 
				vs_fill_x2, 
				lyf, c_tilesize_
			);
			else
			shift_specrow_virtual(
				vs_fill_x1, y2 - 1,
				vs_fill_x1, y2 - 1, 
				vs_fill_x2, 
				lyf, c_tilesize_
			);
				}

			if (yf > 0)	
				{
			if (c_ste_)
			fill_specrow_virtual(
				vs_fill_x1, y2,
				vs_fill_x1, y2, 
				vs_fill_x2, 
				0, yf
			);		
			else
			shift_specrow_virtual(
				vs_fill_x1, y2,
				vs_fill_x1, y2, 
				vs_fill_x2, 
				0, yf
			);	
		}		
	}
		}
	else if (dy < 0)
	{
#if !(SPECULATIVE_FILL_METHOD-1)
		// calculate portion which may have missed previous speculative updates
		if (tx_last_dy_ < tx)
		{
			// *****XXXX
			// 00000000*
			// 00000000*
			// 00000000*
			// 00000000*
						
			s16 patch_x1 = tx_last_dy_ + visible_tiles_x_;
			s16 patch_x2 = x2 + 1;
			s16 patch_y = y1 - 1;
									
			// we have scrolled right since the last DX
			// right tiles may be spoiled
			if (c_ste_)
			fill_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//yf, yf-dy
			);	
			else
			shift_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//yf, yf-dy
			);	
		}
		else if (tx_last_dy_ > tx)	// patch upper-left diagonal
		{
			// XXXX*****
			// *00000000
			// *00000000
			// *00000000
			// *00000000

			s16 patch_x1 = x1 - 1;
			s16 patch_x2 = tx_last_dy_;
			s16 patch_y = y1 - 1;			
						
			// we have scrolled left since the last DX
			// left tiles may be spoiled
			if (c_ste_)
			fill_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//yf, yf-dy
			);		
			else
			shift_specrow_virtual(
				patch_x1, patch_y,
				patch_x1, patch_y, 
				patch_x2,
				0, c_tilesize_//yf, yf-dy
			);		
		}
		else
		{
			// no change on other axis - speculation should be correct
		}	
#endif // SPECULATIVE_FILL_METHOD 1
		
		// XXXXXXXX
		// 00000000
		// 00000000
		// 00000000
		// 00000000

#if 0
		// amortize any single-pixel remainder to minimize double-column updates
		if (lyf == 1)
			lyf = 0;
		if (yf == 1)
			yf = 0;
#endif

		if (yf == lyf)
		{
		}									
		else 
		if (yf < lyf)
		{
			if (c_ste_)
			fill_specrow_virtual(
				vs_fill_x1, y1 - 1,
				vs_fill_x1, y1 - 1, 
				vs_fill_x2,
				yf, lyf	//yf-dy
			);	
			else
			shift_specrow_virtual(
				vs_fill_x1, y1 - 1,
				vs_fill_x1, y1 - 1, 
				vs_fill_x2,
				yf, lyf	//yf-dy
			);	
		}
		else // (yf < lyf)
		{		
			if (lyf > 0)
			{
				if (c_ste_)
				fill_specrow_virtual(
					vs_fill_x1, y1,
					vs_fill_x1, y1, 
					vs_fill_x2, 
					0, lyf
				);
				else
				shift_specrow_virtual(
					vs_fill_x1, y1,
					vs_fill_x1, y1, 
					vs_fill_x2, 
					0, lyf
				);
			} // (lyf > 0)

			if (yf < c_tilesize_)
			{
				if (c_ste_)
				fill_specrow_virtual(
					vs_fill_x1, y1 - 1,
					vs_fill_x1, y1 - 1, 
					vs_fill_x2,
					yf, c_tilesize_
				);		
				else
				shift_specrow_virtual(
					vs_fill_x1, y1 - 1,
					vs_fill_x1, y1 - 1, 
					vs_fill_x2,
					yf, c_tilesize_
				);		
			} // (yf < c_tilesize_)
		} // !(yf < lyf)
	} // (dy < 0)
	} // (c_updaterows_ && c_vspeculative_ && (c_ste_ || drawshift_))
	else // !c_vspeculative_
	if (c_updaterows_)
	{
		s16 vpage_tiles_h = visible_tiles_y_;

		if (tdy > 0)
		{	
			// fill bottom edge
			if (c_ste_)
			fill_rows_virtual(
				vs_fill_x1, (tyl + vpage_tiles_h),
				vs_fill_x1, (tyl + vpage_tiles_h), 
				vs_fill_x2, (ty + vpage_tiles_h)
			);			
			else
			shift_rows_virtual(
				vs_fill_x1, (tyl + vpage_tiles_h),
				vs_fill_x1, (tyl + vpage_tiles_h), 
				vs_fill_x2, (ty + vpage_tiles_h)
			);
		}
		else // (tdy > 0)
		if (tdy < 0)
		{
			// fill top edge
			if (c_ste_)
			fill_rows_virtual(
				vs_fill_x1, ty,
				vs_fill_x1, ty, 
				vs_fill_x2, tyl
			);			
			else
			shift_rows_virtual(
				vs_fill_x1, ty,
				vs_fill_x1, ty, 
				vs_fill_x2, tyl
			);			
		} // (tdy < 0)
	} // (c_updaterows_)

	if (1)
	{
	if (c_updatecols_ && (c_hspeculative_ /*|| c_hbufferspeculation_*/) && (c_ste_ || drawshift_))
	{
	if (dx > 0)
	{		
		// fill right edge
		
#if !(SPECULATIVE_FILL_METHOD-1)
		// calculate portion which may have missed previous speculative updates
		if (ty_last_dx_ < ty)
		{
			// 00000000*
			// 00000000*
			// 00000000X
			// 00000000X
			// ********X

			s16 patch_x = x2;
			s16 patch_y1 = ty_last_dx_ + visible_tiles_y_;
			s16 patch_y2 = y2 + 1;
					
			// we have scrolled down since the last DX
			// lower tiles may be spoiled
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	patch_x, patch_y1, 
			//	patch_x, patch_y1, patch_y2,
			//	0, c_tilesize_//lxf+dx
			//);				
			//else
			fill_speccolumn_virtual(
				patch_x, patch_y1, 
				patch_x, patch_y1, patch_y2,
				0, c_tilesize_//lxf+dx
			);				
		}
		else if (ty_last_dx_ > ty)
		{
			// ********X
			// 00000000X
			// 00000000X
			// 00000000*
			// 00000000*
			
			s16 patch_x = x2;
			s16 patch_y1 = y1 - 1;
			s16 patch_y2 = ty_last_dx_;
			
			// we have scrolled up since the last DX
			// upper tiles may be spoiled
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	patch_x, patch_y1, 
			//	patch_x, patch_y1, patch_y2,
			//	0, c_tilesize_//lxf+dx
			//);				
			//else
			fill_speccolumn_virtual(
				patch_x, patch_y1, 
				patch_x, patch_y1, patch_y2,
				0, c_tilesize_//lxf+dx
			);				
		}
		else
		{
			// no change on other axis - speculation should be correct
		}
#endif // SPECULATIVE_FILL_METHOD
		
		// 00000000X
		// 00000000X
		// 00000000X
		// 00000000X

#if 0
		// amortize any single-pixel remainder to minimize double-column updates
		if (lxf == c_tilesize_-1)
			lxf = c_tilesize_;
		if (xf == c_tilesize_-1)
			xf = c_tilesize_;
#endif

		if (xf == lxf)
		{
		}
		else if (xf > lxf)
		{
#ifdef TIMING_RASTERS_MAIN
			reg16(ffff8240) = 0x0ff;
			reg16(00000064) = 0x0ff;
#endif		
			//if (c_hbufferspeculation_)
			if (c_ste_)
			fill_speccolumn_virtual(
				x2, hs_fill_y1, 
				x2, hs_fill_y1, hs_fill_y2,
				lxf, xf//lxf+dx
			);		
			else
			shift_speccolumn_virtual(
				x2, hs_fill_y1, 
				x2, hs_fill_y1, hs_fill_y2,
				lxf, xf//lxf+dx
			);		
		}
		else
		{

#ifdef TIMING_RASTERS_MAIN		
			reg16(ffff8240) = 0xfff;
			reg16(00000064) = 0xfff;
#endif		
			if (lxf < c_tilesize_)	
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	x2 - 1, hs_fill_y1, 
			//	x2 - 1, hs_fill_y1, hs_fill_y2,
			//	lxf, c_tilesize_
			//);		
			//else
			if (c_ste_)
			fill_speccolumn_virtual(
				x2 - 1, hs_fill_y1, 
				x2 - 1, hs_fill_y1, hs_fill_y2,
				lxf, c_tilesize_
			);		
			else
			shift_speccolumn_virtual(
				x2 - 1, hs_fill_y1, 
				x2 - 1, hs_fill_y1, hs_fill_y2,
				lxf, c_tilesize_
			);		

#ifdef TIMING_RASTERS_MAIN		
			reg16(ffff8240) = 0x444;
			reg16(00000064) = 0x444;
#endif		
			if (xf > 0)	
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	x2, hs_fill_y1, 
			//	x2, hs_fill_y1, hs_fill_y2,
			//	0, xf
			//);		
			//else
			if (c_ste_)
			fill_speccolumn_virtual(
				x2, hs_fill_y1, 
				x2, hs_fill_y1, hs_fill_y2,
				0, xf
			);		
			else
			shift_speccolumn_virtual(
				x2, hs_fill_y1, 
				x2, hs_fill_y1, hs_fill_y2,
				0, xf
			);

		}			
	}
	else if (dx < 0)
	{
	
#if !(SPECULATIVE_FILL_METHOD-1)
		// calculate portion which may have missed previous speculative updates
		if (ty_last_dx_ < ty)
		{
			// *00000000
			// *00000000
			// X00000000
			// X00000000
			// X********

			s16 patch_x = x1 - 1;
			s16 patch_y1 = ty_last_dx_ + visible_tiles_y_;
			s16 patch_y2 = y2 + 1;
									
			// we have scrolled down since the last DX
			// lower tiles may be spoiled
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	patch_x, patch_y1, 
			//	patch_x, patch_y1, patch_y2,
			//	0, c_tilesize_//xf-dx
			//);				
			//else
			fill_speccolumn_virtual(
				patch_x, patch_y1, 
				patch_x, patch_y1, patch_y2,
				0, c_tilesize_//xf-dx
			);				
		}
		else if (ty_last_dx_ > ty)
		{
			// X********
			// X00000000
			// X00000000
			// *00000000
			// *00000000

			s16 patch_x = x1 - 1;
			s16 patch_y1 = y1 - 1;
			s16 patch_y2 = ty_last_dx_;
						
			// we have scrolled up since the last DX
			// upper tiles may be spoiled
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	patch_x, patch_y1, 
			//	patch_x, patch_y1, patch_y2,
			//	0, c_tilesize_//xf-dx
			//);					
			//else
			fill_speccolumn_virtual(
				patch_x, patch_y1, 
				patch_x, patch_y1, patch_y2,
				0, c_tilesize_//xf-dx
			);					
		}
		else
		{
			// no change on other axis - speculation should be correct
		}	
#endif // SPECULATIVE_FILL_METHOD
	
		// X00000000
		// X00000000
		// X00000000
		// X00000000

#if 0
		// amortize any single-pixel remainder to minimize double-column updates
		if (lxf == 1)
			lxf = 0;
		if (xf == 1)
			xf = 0;
#endif

		if (xf == lxf)
		{
		}
		else if (xf < lxf)
		{
#ifdef TIMING_RASTERS_MAIN		
			reg16(ffff8240) = 0xff0;
			reg16(00000064) = 0xff0;
#endif		
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	x1 - 1, hs_fill_y1, 
			//	x1 - 1, hs_fill_y1, hs_fill_y2,
			//	xf, lxf	//xf-dx
			//);		
			//else
			if (c_ste_)
			fill_speccolumn_virtual(
				x1 - 1, hs_fill_y1, 
				x1 - 1, hs_fill_y1, hs_fill_y2,
				xf, lxf	//xf-dx
			);		
			else
			shift_speccolumn_virtual(
				x1 - 1, hs_fill_y1, 
				x1 - 1, hs_fill_y1, hs_fill_y2,
				xf, lxf	//xf-dx
			);		
		}
		else
		{
#ifdef TIMING_RASTERS_MAIN		
			reg16(ffff8240) = 0xff0;
			reg16(00000064) = 0xff0;
#endif		
			if (lxf > 0)	
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	x1, hs_fill_y1, 
			//	x1, hs_fill_y1, hs_fill_y2,
			//	0, lxf
			//);		
			//else
			if (c_ste_)
			fill_speccolumn_virtual(
				x1, hs_fill_y1, 
				x1, hs_fill_y1, hs_fill_y2,
				0, lxf
			);		
			else
			shift_speccolumn_virtual(
				x1, hs_fill_y1, 
				x1, hs_fill_y1, hs_fill_y2,
				0, lxf
			);		

#ifdef TIMING_RASTERS_MAIN		
			reg16(ffff8240) = 0x440;
			reg16(00000064) = 0x440;
#endif		
			if (xf < c_tilesize_)
			//if (c_hbufferspeculation_)
			//shift_speccolumn_virtual(
			//	x1 - 1, hs_fill_y1, 
			//	x1 - 1, hs_fill_y1, hs_fill_y2,
			//	xf, c_tilesize_
			//);		
			//else
			if (c_ste_)
			fill_speccolumn_virtual(
				x1 - 1, hs_fill_y1, 
				x1 - 1, hs_fill_y1, hs_fill_y2,
				xf, c_tilesize_
			);		
			else
			shift_speccolumn_virtual(
				x1 - 1, hs_fill_y1, 
				x1 - 1, hs_fill_y1, hs_fill_y2,
				xf, c_tilesize_
			);		
		}			
	}
	}
	else // c_hspecultive_
	if (c_updatecols_)
	{
		s16 vpage_tiles_w = visible_tiles_x_;

//		printf("xxxx\n");
		if (tdx > 0)
		{
//			printf("xpos!\n");
			// fill right edge
			if (c_ste_)
			fill_columns_virtual(
				(txl + vpage_tiles_w), hs_fill_y1, 
				(txl + vpage_tiles_w), hs_fill_y1, 
				(tx  + vpage_tiles_w), hs_fill_y2
			);
			else
			{
				if (c_hbufferspeculation_)
				{
					//0-4			12+4	0+4
					//4-8			0+4		4+4
					//8-12		4+4		8+4
					//12-16		8+4		12+4

//					printf("l-s: %d %d\n", last_slice_, slice_);

					s16 p = (last_slice_ + c_preshift_step_) & (c_tilesize_-1);
					s16 n = (slice_ + c_preshift_step_);

					if (p == n)
					{
//						printf("p-n: %d %d\n", p, n);
					}
					else
					if (n > p)
					{
//						printf("p-n: %d %d\n", p, n);
						// single
						shift_speccolumn_virtual(
							(txl + vpage_tiles_w), hs_fill_y1, 
							(txl + vpage_tiles_w), hs_fill_y1, hs_fill_y2,
									p, n
						);		
					} // (n > p)
					else
					{
						// split
//						printf("p-ts: %d %d\n", p, c_tilesize_);
						shift_speccolumn_virtual(
							(txl + vpage_tiles_w), hs_fill_y1, 
							(txl + vpage_tiles_w), hs_fill_y1, hs_fill_y2,
							p, c_tilesize_
						);	
//						printf("0-n: %d %d\n", 0, n);
						shift_speccolumn_virtual(
							(txl + vpage_tiles_w), hs_fill_y1, 
							(txl + vpage_tiles_w), hs_fill_y1, hs_fill_y2,
							0, n
						);	
					} // !(n > p)
				} // (c_hbufferspeculation_)
				else
				shift_columns_virtual(
					(txl + vpage_tiles_w), hs_fill_y1, 
					(txl + vpage_tiles_w), hs_fill_y1, 
					(tx  + vpage_tiles_w), hs_fill_y2
				);
#if !defined(AGT_CONFIG_STF_NOEDGEMASKING)
				if (c_hsyncshift_)
					maskedges_ |= 1;
					//STF_MaskEdgeR(framebuffer_, xpos_, ypos_, display_height_);
#endif
			} // !(c_ste_)
		}
		else 
		if (tdx < 0)
		{
//			printf("xneg!\n");
			// fill left edge
			if (c_ste_)
			fill_columns_virtual(
				tx,  hs_fill_y1, 
				tx,  hs_fill_y1, 
				txl, hs_fill_y2
			);
			else
			{
				shift_columns_virtual(
					tx,  hs_fill_y1, 
					tx,  hs_fill_y1, 
					txl, hs_fill_y2
				);

#if !defined(AGT_CONFIG_STF_NOEDGEMASKING)
				if (c_hsyncshift_)
					maskedges_ |= 2;
					//STF_MaskEdgeL(framebuffer_, xpos_, ypos_, display_height_);
#endif
			} //!(c_ste_)
		} // (tdx < 0)
		else
		{
//			printf("l-s: %d %d\n", last_slice_, slice_);
//			printf("no xdelta!\n");
		} // !(tdx < 0)
	} // (c_updatecols_)

	} //1

#if !defined(AGT_CONFIG_STF_NOEDGEMASKING)
	// erase trailing edge
	if (c_hsyncshift_ && c_updatecols_)
	{
		if (dx > 0)
			maskedges_ |= 2;
			//STF_MaskEdgeL(framebuffer_, xpos_, ypos_, display_height_);
		else
		if (dx < 0)
			maskedges_ |= 1;
			//STF_MaskEdgeR(framebuffer_, xpos_, ypos_, display_height_);
	}
#endif

#if !(SPECULATIVE_FILL_METHOD-1)

	// speculative update requires tracking changes in opposite axis since
	// new tiles appearing on the other axis will have been missed

	if (c_hspeculative_ && c_vspeculative_)
	if ((dy > 0) || (dy < 0))
	{
		tx_last_dy_ = tx;
	}

	if (c_hspeculative_ && c_vspeculative_)
	if ((dx > 0) || (dx < 0))
	{
		ty_last_dx_ = ty;
	}
#endif // SPECULATIVE_FILL_METHOD 1

	last_txpos_ = txpos_;
	last_typos_ = typos_;	
	last_xpos_ = xpos_;
	last_ypos_ = ypos_;
	last_slice_ = slice_;//(slice_+c_preshift_step_) & 15;
	} // !warp
}

// ---------------------------------------------------------------------------------------------------------------------


//// signal tile @ (x,y) has been modified by editing map array directly
//FORCEINLINE void touch(s16 _x, s16 _y)
//{
//	if (c_dynamic_)
//	{
//		s32 index = mul_map_x_[_y] + _x;
//		xassert(dirtynum_ < 256);
//		dirtytiles_[dirtynum_++] = index;
//	}
//}
//
//// set tile @ (x,y) by index
//FORCEINLINE void mod(s16 _x, s16 _y, s32 _rawidx)
//{
//	if (c_dynamic_)
//	{
//		s32 index = mul_map_x_[_y] + _x;
//		map_[index] = _rawidx;
//		xassert(dirtynum_ < 256);
//		dirtytiles_[dirtynum_++] = index;
//	}
//}

agt_inline void bgrestore_reset()
{
	restorestate_.pstack[0] = 0;
	restorestate_.ptide = &restorestate_.pstack[1];
}
/*
agt_inline void dbg_validate_restorestack(u16 _col)
{
	if (restorestate_.pstack[0] != 0)
	{
		//AGT_DBG_HALT(_col);
		while(1){*(volatile word_t*)(0xffff8240) ^= _col;}
	}
}
*/
agt_inline void bgrestore()
{
/*	if (restorestate_.pstack[0] != 0)
	{
		while(1){*(volatile word_t*)(0xffff8240) ^= 0x555;}
	}
*/
	//dbg_validate_restorestack(0x555);

	if (restorestate_.ptide != &restorestate_.pstack[1])
	{
		// todo: store these together as an internal context, pass a pointer to it
		g_rst_pfb = framebuffer_;
		g_rst_pflineidx = pfieldcontext_->lineindex_;
		g_rst_vpageh = wrap_height_;
		g_rst_vpagebytes = virtual_pagebytes_;
		g_rst_state = &restorestate_;

		if (c_ste_)
			AGT_BLiT_PlayfieldRestore();
		else
			AGT_STF_PlayfieldRestore();

	//bgrestore_reset();
	}

	//dbg_validate_restorestack(0x005);
}

agt_inline void touch_reset()
{
#if defined(AGT_CONFIG_MAPMOD_BOUNDS)
	dtx1_ = dty1_ = 0x7fff;
	dtx2_ = dty2_ = 0x8000;
#endif
}

agt_inline void touch_region(s16 _x1, s16 _y1, s16 _x2, s16 _y2)
{
#if defined(AGT_CONFIG_MAPMOD_BOUNDS)
	if (_x1 < dtx1_)
		dtx1_ = _x1;
	if (_y1 < dty1_)
		dty1_ = _y1;
	if (_x2 > dtx2_)
		dtx2_ = _x2;
	if (_y2 > dty2_)
		dty2_ = _y2;
#endif
}

template<bool _autoregion = true>
agt_inline void multitouch(s16 _x, s16 _y)
{
	if (c_dynamic_)
	{
		if (_autoregion)
			touch_region(_x, _y, _x+1, _y+1);

		u16 pattern = 0x8000 >> (_x & 7);
		s16 xoffset = (_x >> 3);
		s16 *dirtyline = dtm_animation_.dirtymasklines_[_y];
		dirtyline[xoffset] |= pattern;
	}
}

template<bool _autoregion = true>
void multitouch_row(s16 _x, s16 _y, s16 _dx)
{
	if (c_dynamic_)
	{
		if (_autoregion)
			touch_region(_x, _y, _x+_dx, _y+1);

		// find mask rectangle width
		s16 bx1 = (_x & ~7);
		s16 bx2 = (_x + _dx + 7) & ~7;
		s16 bdx = (bx2 - bx1) >> 3;

		// we can deal with 8,16,24,32-wide tile bitmasks
		if ((bdx > 0)/* && (bdx < 4)*/)
		{				
			// access mask buffer
			s16 xoffset = (_x >> 3);
			s16 *dirtyline = dtm_animation_.dirtymasklines_[_y];
			dirtyline = &dirtyline[xoffset];
			s16 stride = dtm_animation_.dirtymaskwidth_;

			// generate endmasks for all cases
			u16 em1 = (0xffff >> (_x & 7));
			u16 em3 = (0xffff0000UL >> (((_x + _dx - 1) & 7) + 1));

			switch (bdx)
			{
			case 1:
				em1 &= em3;
				dirtyline[0] |= em1;
				break;
			case 2:
				dirtyline[0] |= em1;
				dirtyline[1] |= em3;
				break;
			case 3:
				dirtyline[0] |= em1;
				dirtyline[1] = ~0;
				dirtyline[2] |= em3;
				break;
			default:
				s16 *pd = dirtyline;
				*pd++ |= em1;
				s16 c = (bdx - 2) - 1;
				do { *pd++ = ~0; } while (--c != -1);
				*pd++ |= em3;
				break;
			}
		}
	}
}

template<bool _autoregion = true>
void multitouch_column(s16 _x, s16 _y, s16 _dy)
{
	// todo: prototype version - move this stuff to 68k
	if (c_dynamic_)
	{
		if (_autoregion)
			touch_region(_x, _y, _x+1, _y+_dy);

		// update dirty mask
		u16 pattern = 0x8000 >> (_x & 7);
		s16 xoffset = (_x >> 3);
		s16 *dirtyline = dtm_animation_.dirtymasklines_[_y];
		dirtyline = &dirtyline[xoffset];
		s16 stride = dtm_animation_.dirtymaskwidth_;

		s16 y = _dy-1;
		do
		{
			*dirtyline |= pattern;
			dirtyline += stride;
		} while (--y != -1);
	}
}

// todo: 68k version
template<bool _autoregion = true>
void multitouch_rect(s16 _x, s16 _y, s16 _dx, s16 _dy)
{
	if (c_dynamic_)
	{
		if (_autoregion)
			touch_region(_x, _y, _x+_dx, _y+_dy);

		// find mask rectangle width
		s16 bx1 = (_x & ~7);
		s16 bx2 = (_x + _dx + 7) & ~7;
		s16 bdx = (bx2 - bx1) >> 3;

		// we can deal with 8,16,24-wide tile bitmasks
		if ((bdx > 0)/* && (bdx < 4)*/)
		{				
			// access mask buffer
			s16 xoffset = (_x >> 3);
			s16 *dirtyline = dtm_animation_.dirtymasklines_[_y];
			dirtyline = &dirtyline[xoffset];
			s16 stride = dtm_animation_.dirtymaskwidth_;

			// generate endmasks for all cases
			u16 em1 = (0xffff >> (_x & 7));
			u16 em3 = (0xffff0000UL >> (((_x + _dx - 1) & 7) + 1));

			// loopsize for all cases
			s16 y = _dy-1;

			switch (bdx)
			{
			case 1:
				em1 &= em3;
				do
				{
					dirtyline[0] |= em1;
					dirtyline += stride;
				} while (--y != -1);
				break;
			case 2:
				do
				{
					dirtyline[0] |= em1;
					dirtyline[1] |= em3;
					dirtyline += stride;
				} while (--y != -1);
				break;
			case 3:
				do
				{
					dirtyline[0] |= em1;
					dirtyline[1] = ~0;
					dirtyline[2] |= em3;
					dirtyline += stride;
				} while (--y != -1);
				break;
			default:
				{
					s16 cc = (bdx - 2) - 1;
					do
					{
						s16 *pd = dirtyline;
						dirtyline += stride;
						{
							*pd++ |= em1;
							s16 c = cc;
							do { *pd++ = ~0; } while (--c != -1);
							*pd++ |= em3;
						}
					} while (--y != -1);
				}
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

#if (0)
void settiles(tileset* _tiles)
{	
	tiles_ = _tiles;
	tiledata_ = tiles_->get();
/*
	prepare_blitter();

	// prefill all 4 buffers, prepared for wrap
	fill_rows_virtual
	(
		txpos_-1, typos_-1,
		txpos_-1, typos_-1, 
		txpos_-1 + vpage_tiles_x_, typos_-1 + visible_tiles_y_
	);	
*/
					
/*		
	if (index_ == 1) return;
			
	for (s16 i = 0; i < virtual_tiles_x_; ++i)
	{
		for (s16 j = 0; j < virtual_tiles_y_; ++j)
		{
			//s16 t = (i + j) & 3;
			//s16 t = twister_.genU32() % 100;

			s16 mi = i % map_tiles_x_;
			s16 mj = j % map_tiles_y_;				
			u16 t = u16(map_[mi + (mj * map_tiles_x_)]);
			
			dotile(t,i,j);
		}
	}
*/		
}
#endif

};

// ---------------------------------------------------------------------------------------------------------------------

#endif // playfield_h_

