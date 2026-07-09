//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// 	world arena: creates world instance from multiple playfield buffers each with separate state
//----------------------------------------------------------------------------------------------------------------------

#ifndef arena_h_
#define arena_h_

// ---------------------------------------------------------------------------------------------------------------------

extern "C" s16 g_linebytes;
extern "C" s16 g_xpreshift;

// ---------------------------------------------------------------------------------------------------------------------
// sprite clipping window (world coordinates)
// todo: this is now part of drawcontext struct and will soon be deprecated

extern "C" int STF_MaskEdgeL(u16* _pfb, int _xoff, int _yoff, int _height);
extern "C" int STF_MaskEdgeR(u16* _pfb, int _xoff, int _yoff, int _height);

// ---------------------------------------------------------------------------------------------------------------------

// execute a block of code per active buffer:field state
#define EXECUTE_PER_FIELD(_x_) \
	for (s16 b = 0; b != c_num_buffers_; b++) \
	{ \
		s16 b_ = b; \
		if (c_dualfield_ && c_singleframe_) \
		{ \
			int f_ = 0; \
			_x_; \
		} \
		else \
		{ \
			int f_ = 0; \
			_x_; \
			if (c_dualfield_) \
			{ \
				int f_ = 1; \
				_x_; \
			} \
		} \
	}

#define EXECUTE_BUFFER_FIELDS(_x_) \
	{ \
		if (c_dualfield_ && c_singleframe_) \
		{ \
			int f_ = 0; \
			_x_; \
		} \
		else \
		{ \
			int f_ = 0; \
			_x_; \
			if (c_dualfield_) \
			{ \
				int f_ = 1; \
				_x_; \
			} \
		} \
	}

// execute a block of code per active buffer state
#define EXECUTE_PER_BUFFER(_x_) \
	for (s16 b = 0; b != c_num_buffers_; b++) \
	{ \
		s16 b_ = b; \
		_x_; \
	}

// ---------------------------------------------------------------------------------------------------------------------
// arena

template<s16 _num_buffers, bool _singleframe, bool _dualfield, class _playfield_type>
class arena_template
{
public:

	typedef _playfield_type playfield_type;

	static const int c_num_buffers_ = _num_buffers;
	static const bool c_singleframe_ = _singleframe;
	static const bool c_dualfield_ = _dualfield;
	static const bool c_force_dualfield_drawing_ = playfield_type::c_force_dualfield_drawing_;
		
	s16 display_width_;
	s16 display_height_;
	s16 viewport_xoffset_;
	s16 viewport_yoffset_;

	// context holds memory resources shared between related playfields
	typename playfield_type::mapcontext mapctx0_;
//	typename playfield_type::mapcontext mapctx1_;
	typename playfield_type::fieldcontext pfctx_;
	
	class buffer
	{
	public:
		
		playfield_type fields[c_max_shiftfields];	
	};
		
	buffer buffers_[c_num_buffers_];

	// todo: only single-field mapmod support so far
	s16 **dirtymaskindex_[c_num_buffers_][2];
	s16 *dirtylineseq_[8];

	s16 buffer_index_;
	//s16 field_index_;
	//s16 dualfield_mask_;
	buffer *pactive_;
	s16 viewbind_;
	s16 viewbind_yoff_;
	//s16 xpos_;
	//s16 ypos_;

	shifter_common& shift_;
	
	arena_template(shifter_common& _shift, s16 _display_width, s16 _display_height/*, int _dualfield_mask*/)
		: shift_(_shift),
		  display_width_(_display_width),
		  display_height_(_display_height),
		  viewport_xoffset_(0),
		  viewport_yoffset_(0),
		  buffer_index_(0), //field_index_(0),
		  //xpos_(0), ypos_(0),
		  pactive_(0),
		  viewbind_(0), viewbind_yoff_(0)
		  //dualfield_mask_(_dualfield_mask)
	{

		EXECUTE_PER_FIELD({buffers_[b_].fields[f_].setcontext(&mapctx0_, &pfctx_);});

#if 0
		// make sure all playfields share common context
		for (s16 b = 0; b < c_num_buffers_; b++)
		{
/*
			if (c_singleframe)
			{
				// only one field, but still using two buffers to interlace display
				if (b & 1)
				{
					buffers_[b].fields[0].setcontext(&mapctx1_, &pfctx_);
					buffers_[b].fields[1].setcontext(&mapctx1_, &pfctx_);
				}
				else
				{
					buffers_[b].fields[0].setcontext(&mapctx0_, &pfctx_);
					buffers_[b].fields[1].setcontext(&mapctx0_, &pfctx_);
				}
			}
			else
			{
				// separate context for each field
				buffers_[b].fields[0].setcontext(&mapctx0_, &pfctx_);
				buffers_[b].fields[1].setcontext(&mapctx1_, &pfctx_);
			}
*/
			buffers_[b].fields[0].setcontext(&mapctx0_, &pfctx_);
			buffers_[b].fields[1].setcontext(&mapctx0_, &pfctx_);

			//// shortcut to dirty tile masks
			//dirtymaskindex_[b] = buffers_[b].fields[0].dirtymasklines_;
		}
#endif
	}
	
	~arena_template()
	{
		dprintf("destroy arena\n");
	}
/*
	inline void dbg_verify(u16 _col)
	{
		EXECUTE_PER_FIELD({buffers_[b_].fields[f_].dbg_validate_restorestack(_col);});
	}
*/
	inline void snapview(drawcontext_t& r_ctx, s16 _vpw, s16 _vph, s16 _vpxo, s16 _vpyo)
	{
		buffer& b = *pactive_;
		playfield_type& pf = b.fields[0];

		// generate loopback snapping adjustments

		s16 snapadjx = 0;
		s16 snapadjy = 0;

		if (pf.c_hloop_)
			snapadjx = pf.xpos_ - tmod16(pf.xpos_, pf.wrap_width_);

		if (pf.c_vloop_)
			snapadjy = pf.ypos_ - tmod16(pf.ypos_, pf.wrap_height_);

		// produce workbuffer page coordinates which follow any hops performed by the playfield engine
		r_ctx.vpagex = pf.xpos_ - snapadjx;
		r_ctx.vpagey = pf.ypos_ - snapadjy;

		// for sprites:
		// incorporate  optional viewport offset compensation, where
		// sprites require drawing contiguously over viewport seams
		r_ctx.pfsnapadjx = snapadjx + _vpxo;
		r_ctx.pfsnapadjy = snapadjy + _vpyo;

		// generate clipping window
		const s16 guardx = playfield_type::c_guardx;
		const s16 guardy = playfield_type::c_guardy;
		r_ctx.guardx = guardx;
		r_ctx.guardy = guardy;

		// physical scissor/clip (IMS,EMS,RFILL)
		r_ctx.scissor_window_x1p = (r_ctx.vpagex & -16);
		r_ctx.scissor_window_x2p = (r_ctx.scissor_window_x1p + _vpw + 16);
		r_ctx.scissor_window_y1 =   r_ctx.vpagey;
		r_ctx.scissor_window_y2 =   r_ctx.vpagey + _vph;
		r_ctx.scissor_window_xs =   r_ctx.scissor_window_x2p - r_ctx.scissor_window_x1p;
		r_ctx.scissor_window_ys =   r_ctx.scissor_window_y2  - r_ctx.scissor_window_y1;

		// guardband rejection
		r_ctx.guard_window_x1 = r_ctx.vpagex - guardx; //(r_ctx.vpagex & -16) - guardx;
		r_ctx.guard_window_y1 =  r_ctx.vpagey - guardy;
		r_ctx.guard_window_x2 = (r_ctx.guard_window_x1 + _vpw) + (guardx*2);
		r_ctx.guard_window_y2 =  r_ctx.vpagey + _vph + guardy;
		r_ctx.guard_window_xs =  r_ctx.guard_window_x2 - r_ctx.guard_window_x1;
		r_ctx.guard_window_ys =  r_ctx.guard_window_y2 - r_ctx.guard_window_y1;
	}

	// retrieve digest of draw context fields for low level graphics functions
	inline void get_context(drawcontext_t& r_ctx, s16 _field, s16 _vpw, s16 _vph)
	{
		prf_push(prf_arena_misc);

		// playfield field active
		int pf_field_active = ((int)(!c_singleframe_ && c_dualfield_));// & dualfield_mask_);
		int sp_field_active = (int)c_dualfield_ | (int)c_force_dualfield_drawing_;

		int pf_field_idx = _field & pf_field_active;
		int sp_field_idx = _field & sp_field_active;

		playfield_type &pf = buffers_[buffer_index_].fields[pf_field_idx];

		r_ctx.p_pfframebuffer = pf.framebuffer_;
		r_ctx.p_pflinemod = pf.pfieldcontext_->linemod_;
		r_ctx.p_pflineidx = pf.pfieldcontext_->lineindex_;

		r_ctx.pfvpagebytes = pf.virtual_pagebytes_;
		r_ctx.pfvpageh = pf.wrap_height_;

		r_ctx.prestorestate = &pf.restorestate_;
		r_ctx.p_ocm_t = pf.ocm_.oct_;
		r_ctx.p_ocm_o = pf.ocm_.oco_;
		r_ctx.ocm_stride = pf.ocm_.stride_;

		//int field_active = (int)c_force_dualfield_drawing_ | ((int)c_dualfield_ & dualfield_mask_);

		// in single-vbl dual-field mode, buffer acts as field
		//r_ctx.field = (sp_field_idx & _field) | ((sp_field_idx^1) & buffer_index_);
		r_ctx.field = (s16)sp_field_idx;

		snapview(r_ctx, _vpw, _vph, viewport_xoffset_, viewport_yoffset_);

		// TODO: get rid of this shared global
		g_drawcontext = r_ctx;

		prf_pop();
	}

	void select_buffer(s16 _index)
	{
		prf_push(prf_arena_misc);

		buffer_index_ = _index;

		pactive_ = &buffers_[buffer_index_];
		playfield_type& pf0 = pactive_->fields[0];

		// publish metrics for writing to this backbuffer
		g_linebytes = pf0.virtual_linewords_ << 1;
		g_xpreshift = pf0.xpreshift_;

		prf_pop();
	}
	
	void bgrestore_init()
	{
		prf_push(prf_arena_bgrestore);

		s16 b = c_num_buffers_-1;
		do {
			buffer& rb = buffers_[b];
			EXECUTE_BUFFER_FIELDS({rb.fields[f_].bgrestore_init();});
/*
			rb.fields[0].bgrestore_init();
			if (c_dualfield_ && dualfield_mask_)
				rb.fields[1].bgrestore_init();
*/
		} while (--b != -1);		

		prf_pop();
	}

	void bgrestore_reset()
	{
		prf_push(prf_arena_bgrestore);

		s16 b = c_num_buffers_-1;
		do {
			buffer& rb = buffers_[b];

			EXECUTE_BUFFER_FIELDS({rb.fields[f_].bgrestore_reset();});
/*
			rb.fields[0].bgrestore_reset();
			if (c_dualfield_ && dualfield_mask_)
				rb.fields[1].bgrestore_reset();
*/
		} while (--b != -1);		

		prf_pop();
	}

	void bgrestore_flush()
	{
		prf_push(prf_arena_bgrestore);

		buffer& b = *pactive_;

		EXECUTE_BUFFER_FIELDS({b.fields[f_].bgrestore_reset();});
/*
		if (c_dualfield_ && dualfield_mask_)
		{
			// 2 fields at once
			b.fields[0].bgrestore_reset();
			b.fields[1].bgrestore_reset();
		}
		else
		{
			// one field
			b.fields[0].bgrestore_reset();
		}		
*/
		prf_pop();
	}

	void bgrestore()
	{
		prf_push(prf_arena_bgrestore);

		buffer& b = *pactive_;

		EXECUTE_BUFFER_FIELDS({b.fields[f_].bgrestore();});
/*
		if (c_dualfield_ && dualfield_mask_)
		{
			// 2 fields at once
			b.fields[0].bgrestore();
			b.fields[1].bgrestore();
		}
		else
		{
			// one field
			b.fields[0].bgrestore();
		}
*/	
		prf_pop();
	}

	void activate_buffer()
	{
		prf_push(prf_arena_misc);

		buffer& b = *pactive_; //buffers_[buffer_index_];

		EXECUTE_BUFFER_FIELDS({b.fields[f_].select(shift_,viewbind_,viewbind_yoff_);});

//		b.fields[0].select(shift_,_viewbind);
//		if (c_dualfield_ && dualfield_mask_)
//			b.fields[1].select(shift_,_viewbind);

		shift_.advance(viewbind_);

		g_frame++;

		prf_pop();
	}

	void modview(s16 &r_x, s16 &r_y)
	{
		prf_push(prf_arena_misc);

		buffer& b = *pactive_;
		playfield_type& pf = b.fields[0];

		if (pf.c_hloop_)
			r_x = tmod16(pf.xpos_, pf.wrap_width_>>0);
		if (pf.c_vloop_)
			r_y = tmod16(pf.ypos_, pf.wrap_height_>>0);

		prf_pop();
	}

	void setpos(s16 _x, s16 _y)
	{
		prf_push(prf_arena_misc);

		// track current scroll position
		//xpos_ = _x;
		//ypos_ = _y;

		// don't permit playfield offsets on axes where scrolling is disabled

		// protect against -ve map coords for speculative updates
		if (!playfield_type::c_hscroll_)
			_x = 0;
		else
		if (_x < playfield_type::c_hspeculative_margin_)
			_x = playfield_type::c_hspeculative_margin_;

		if (!playfield_type::c_vscroll_)
			_y = 0;
		else
		if (_y < 0)
			_y = 0;
//		if (_y < playfield_type::c_vspeculative_margin_)
//			_y = playfield_type::c_vspeculative_margin_;

		buffer& b = *pactive_; //buffers_[buffer_index_];

		EXECUTE_BUFFER_FIELDS({b.fields[f_].setpos(_x, _y);});

/*		b.fields[0].setpos(_x, _y);		
		if (c_dualfield_ && dualfield_mask_)
			b.fields[1].setpos(_x, _y);		
*/
		prf_pop();
	}
		
	void setmap(s32* _map0/*, s32* _map1*/)
	{
		mapctx0_.setmap(_map0);
		//mapctx1_.setmap(_map1);
		//buffer& b = buffers_[buffer_index_];
		//b.fields[0].setmap(_map0);
		//if (dualfield_)
		//	b.fields[1].setmap(_map1);
	}
	
	void setmap(worldmap* _map0/*, worldmap* _map1*/)
	{
		mapctx0_.setmap(_map0);
		//mapctx1_.setmap(_map1);
		//buffer& b = buffers_[buffer_index_];
		//b.fields[0].setmap(_map0);
		//if (dualfield_)
		//	b.fields[1].setmap(_map1);
	}

	void setoverlaymap(s32* _map0/*, s32* _map1*/)
	{
		mapctx0_.setoverlaymap(_map0);
		//mapctx1_.setoverlaymap(_map1);
	}
	
	void setoverlaymap(worldmap* _map0/*, worldmap* _map1*/)
	{
		mapctx0_.setoverlaymap(_map0);
		//mapctx1_.setoverlaymap(_map1);
	}

	void settiles(tileset* _tiles0, tileset* _tiles1)
	{
		buffer& b = *pactive_;

		b.fields[0].settiles(_tiles0);
		if (c_dualfield_ /*&& dualfield_mask_*/ && !c_singleframe_)
			b.fields[1].settiles(_tiles1);
	}	

	void setoverlaytiles(tileset* _tiles0, tileset* _tiles1)
	{
		buffer& b = *pactive_;

		b.fields[0].setoverlaytiles(_tiles0);
		if (c_dualfield_ /*&& dualfield_mask_*/ && !c_singleframe_)
			b.fields[1].setoverlaytiles(_tiles1);
	}	

	void setpreshift(s16 _xps)
	{
		buffer& b = *pactive_;

		b.fields[0].setpreshift(_xps);
		if (c_dualfield_ /*&& dualfield_mask_*/ && !c_singleframe_)
			b.fields[1].setpreshift(_xps);
	}

	// ------------------------------------------------------------------------
	// todo: prototype versions - these to 68k

	// mark rectangular region of map as containing dirty tiles
	agt_hotcode agt_inline void touch_region(s16 _x1, s16 _y1, s16 _x2, s16 _y2)
	{
		if (playfield_type::c_dynamic_)
		{
			EXECUTE_PER_FIELD({buffers_[b_].fields[f_].touch_region(_x1, _y1, _x2, _y2);});
/*
			if (c_dualfield_ && c_singleframe)
			{
				buffers_[b].fields[field_index_].touch_region(_x1, _y1, _x2, _y2);
			}
			else
			{
				buffers_[b].fields[0].touch_region(_x1, _y1, _x2, _y2);
				if (c_dualfield_ && dualfield_mask_)
					buffers_[b].fields[1].touch_region(_x1, _y1, _x2, _y2);
			}
*/
		}
	}

	// mark individual tile as dirty, optionally updating modified region
	template<bool _autoregion = true>
	agt_hotcode agt_inline void multitouch(s16 _x, s16 _y)
	{
		if (playfield_type::c_dynamic_)
		{
			if (_autoregion)
			{
				s16 x2 = _x+1;
				s16 y2 = _y+1;

				EXECUTE_PER_FIELD({buffers_[b_].fields[f_].touch_region(_x, _y, x2, y2);});
			}

			u16 pattern = 0x8000 >> (_x & 7);
			s16 xoffset = (_x >> 3);

			EXECUTE_PER_FIELD({s16 *dirtyline = (dirtymaskindex_[b_][f_])[_y];dirtyline[xoffset] |= pattern;});
/*
			s16 **plineseq = dirtylineseq_;

			s16 *dl = *plineseq++;
			do 
			{
				dl[xoffset] |= pattern;
			} while (dl = *plineseq++);
*/
		}
	}

	// mark tile row as dirty, optionally updating modified region
	template<bool _autoregion = true>
	agt_hotcode void multitouch_row(s16 _x, s16 _y, s16 _dx)
	{
		if (playfield_type::c_dynamic_)
		{
			s16 dirtymaskwidth = buffers_[0].fields[0].dtm_animation_.dirtymaskwidth_;

			if (_autoregion)
			{
				s16 x2 = _x+_dx;
				s16 y2 = _y+1;

				EXECUTE_PER_FIELD({buffers_[b_].fields[f_].touch_region(_x, _y, x2, y2);});
			}

			// find mask rectangle width
			s16 bx1 = (_x & ~7);
			s16 bx2 = (_x + _dx + 7) & ~7;
			s16 bdx = (bx2 - bx1) >> 3;

			// we can deal with 8,16,24,32-wide tile bitmasks
			if (bdx > 0)
			{			
				// access mask buffer
				s16 xoffset = (_x >> 3);
				s16 stride = dirtymaskwidth;

				// generate sequence of DM buffers to be updated by this operation
				{
					s16 i = 0;
					EXECUTE_PER_FIELD({dirtylineseq_[i++] = (dirtymaskindex_[b_][f_])[_y]+xoffset;});
					// terminate sequence
					dirtylineseq_[i] = 0;
				}

				// generate endmasks for all cases
				u16 em1 = (0xffff >> (_x & 7));
				u16 em3 = (0xffff0000UL >> (((_x + _dx - 1) & 7) + 1));

				s16 **plineseq = dirtylineseq_;

				switch (bdx)
				{
				case 1:
					em1 &= em3;
					{
						s16 *dl = *plineseq++;
						do 
						{
							dl[0] |= em1;
						} while (dl = *plineseq++);
					}
					break;
				case 2:
					{
						s16 *dl = *plineseq++;
						do 
						{
							dl[0] |= em1;
							dl[1] |= em3;
						} while (dl = *plineseq++);
					}
					break;
				case 3:
					{
						s16 *dl = *plineseq++;
						do 
						{
							dl[0] |= em1;
							dl[1] = ~0;
							dl[2] |= em3;
						} while (dl = *plineseq++);
					}
					break;
				default:
					{
						s16 *dl = *plineseq++;
						do 
						{
							*dl++ |= em1;
							s16 c = (bdx - 2) - 1;
							do { 
								*dl++ = ~0;
							} while (--c != -1);
							*dl++ |= em3;
						} while (dl = *plineseq++);
					}
					break;
				}
			}
		}
	}

	// mark tile column as dirty, optionally updating modified region
	template<bool _autoregion = true>
	agt_hotcode agt_inline void multitouch_column(s16 _x, s16 _y, s16 _dy)
	{
		if (playfield_type::c_dynamic_)
		{
			s16 dirtymaskwidth = buffers_[0].fields[0].dtm_animation_.dirtymaskwidth_;

			if (_autoregion)
			{
				s16 x2 = _x+1;
				s16 y2 = _y+_dy;

				EXECUTE_PER_FIELD({buffers_[b_].fields[f_].touch_region(_x, _y, x2, y2);});
			}

			// update dirty mask
			u16 pattern = 0x8000 >> (_x & 7);
			s16 xoffset = (_x >> 3);
			s16 stride = dirtymaskwidth;

			// generate sequence of DM buffers to be updated by this operation
			{
				s16 i = 0;
				EXECUTE_PER_FIELD({dirtylineseq_[i++] = (dirtymaskindex_[b_][f_])[_y]+xoffset;});
				// terminate sequence
				dirtylineseq_[i] = 0;
			}

			s16 **plineseq = dirtylineseq_;

			s16 *dl = *plineseq++;
			do 
			{
				s16 y = _dy-1;
				do
				{
					dl[0] |= pattern; 
					dl += stride;
				} while (--y != -1);
			} while (dl = *plineseq++);
		}
	}

	// mark tile rectangle as dirty, optionally updating modified region
	template<bool _autoregion = true>
	agt_hotcode void multitouch_rect(s16 _x, s16 _y, s16 _dx, s16 _dy)
	{
		if (playfield_type::c_dynamic_)
		{
			s16 dirtymaskwidth = buffers_[0].fields[0].dtm_animation_.dirtymaskwidth_;

			if (_autoregion)
			{
				s16 x2 = _x+_dx;
				s16 y2 = _y+_dy;

				EXECUTE_PER_FIELD({buffers_[b_].fields[f_].touch_region(_x, _y, x2, y2);});
			}

			// find mask rectangle width
			s16 bx1 = (_x & ~7);
			s16 bx2 = (_x + _dx + 7) & ~7;
			s16 bdx = (bx2 - bx1) >> 3;

			// we can deal with 8,16,24,32-wide tile bitmasks
			if (bdx > 0)
			{				
				// access mask buffer
				s16 xoffset = (_x >> 3);
				s16 stride = dirtymaskwidth;

				// generate sequence of DM buffers to be updated by this operation
				{
					s16 i = 0;
					EXECUTE_PER_FIELD({dirtylineseq_[i++] = (dirtymaskindex_[b_][f_])[_y]+xoffset;});
					// terminate sequence
					dirtylineseq_[i] = 0;
				}

				// generate endmasks for all cases
				u16 em1 = (0xffff >> (_x & 7));
				u16 em3 = (0xffff0000UL >> (((_x + _dx - 1) & 7) + 1));

				s16 **plineseq = dirtylineseq_;

				switch (bdx)
				{
				case 1:	
					em1 &= em3;
					{
						s16 *dl = *plineseq++;
						do 
						{
							s16 y = _dy-1;
							do
							{
								dl[0] |= em1;
								dl += stride;
							} while (--y != -1);
						} while (dl = *plineseq++);
					}
					break;
				case 2:
					{
						s16 *dl = *plineseq++;
						do 
						{
							s16 y = _dy-1;
							do
							{
								dl[0] |= em1;
								dl[1] |= em3;
								dl += stride;

							} while (--y != -1);

						} while (dl = *plineseq++);
					}
					break;
				case 3:
					{
						s16 *dl = *plineseq++;
						do 
						{
							s16 y = _dy-1;
							do
							{
								dl[0] |= em1;
								dl[1] = ~0;
								dl[2] |= em3;
								dl += stride;
							} while (--y != -1);
						} while (dl = *plineseq++);
					}
					break;
				default:
					{
						s16 *dl = *plineseq++;
						do 
						{
							s16 y = _dy-1;

							s16 cc = (bdx - 2) - 1;
							do
							{
								s16 *pdl = dl; 
								dl += stride;

								*pdl++ |= em1;

								s16 c = cc;
								do { 							
									*pdl++ = ~0; 
								} while (--c != -1);

								*pdl++ |= em3;

							} while (--y != -1);
						} while (dl = *plineseq++);
					}
					break;
				}
			}
		}
	}

#if (0)
	// signal multiple map tiles modified before next refresh: single tile at a time
	// note: no dualfield support yet - will implement shared dualfield maps first
	void multitouch(s16 _x, s16 _y)
	{
		// mark rectangle on all active fields
		s16 b = c_num_buffers_-1;
		do {
			buffer& rb = buffers_[b];
			rb.fields[0].multitouch(_x, _y);
			//if (dualfield_)
			//	rb.fields[1].multitouch(_x, _y);
		} while (--b != -1);		
	}

	// signal multiple map tiles modified before next refresh: map row
	// note: no dualfield support yet - will implement shared dualfield maps first
	void multitouch_row(s16 _x, s16 _y, s16 _dx)
	{
		// mark row on all active fields
		s16 b = c_num_buffers_-1;
		do {
			buffer& rb = buffers_[b];
			rb.fields[0].multitouch_row(_x, _y, _dx);
			//if (dualfield_)
			//	rb.fields[1].multitouch_row(_x, _y, _dx);
		} while (--b != -1);
	}

	// signal multiple map tiles modified before next refresh: map column
	// note: no dualfield support yet - will implement shared dualfield maps first
	void multitouch_column(s16 _x, s16 _y, s16 _dy)
	{
		// mark column  on all active fields
		s16 b = c_num_buffers_-1;
		do {
			buffer& rb = buffers_[b];
			rb.fields[0].multitouch_column(_x, _y, _dy);
			//if (dualfield_)
			//	rb.fields[1].multitouch_column(_x, _y, _dy);
		} while (--b != -1);
	}

	// signal multiple map tiles modified before next refresh: map rectangle
	// note: no dualfield support yet - will implement shared dualfield maps first
	void multitouch_rect(s16 _x, s16 _y, s16 _dx, s16 _dy)
	{
		// mark rect on all active fields
		// todo: 
		s16 b = c_num_buffers_-1;
		do {
			buffer& rb = buffers_[b];
			rb.fields[0].multitouch_rect(_x, _y, _dx, _dy);
			//if (dualfield_)
			//	rb.fields[1].multitouch_rect(_x, _y, _dx, _dy);
		} while (--b != -1);
	}
#endif

	//

	template<PFLayer _layercode = PFLayer_Base>
	agt_inline s32 sample(s16 _x, s16 _y)
	{
		// update map tiles
		return mapctx0_.template sample<_layercode>(_x, _y);
	}

	// modify multiple map tiles before next refresh: single tile at a time (using plain index, i.e. not pre-formatted for map)
	// note: no dualfield support yet - will implement shared dualfield maps first
	template<PFLayer _layercode = PFLayer_Base, bool _autoregion = true>
	void multimod_idx(s16 _x, s16 _y, s32 _i)
	{
		mmprintf("multimod_idx(%d,%d,%d) @ frame %d\n",
			(int)_x, (int)_y, (int)_i, (int)g_frame);

		// update map tiles
		s32 rawidx = _i << (playfield_type::c_tileshift_b_ + (_layercode & PFLayer_Overlay) ? 1 : 0);

		mapctx0_.template multimod<_layercode>(_x, _y, rawidx);

		multitouch<_autoregion>(_x, _y);	
	}

	// modify multiple map tiles before next refresh: single tile at a time
	// note: no dualfield support yet - will implement shared dualfield maps first
	template<PFLayer _layercode = PFLayer_Base, bool _autoregion = true>
	agt_inline void multimod(s16 _x, s16 _y, s32 _rawidx)
	{
		mmprintf("multimod(%d,%d,$%08x) @ frame %d\n",
			(int)_x, (int)_y, (int)_rawidx, (int)g_frame);

		// update map tiles
		mapctx0_.template multimod<_layercode>(_x, _y, _rawidx);

		multitouch<_autoregion>(_x, _y);
	}

	// modify multiple map tiles before next refresh: map row
	// note: no dualfield support yet - will implement shared dualfield maps first
	template<PFLayer _layercode = PFLayer_Base, bool _autoregion = true>
	void multimod_row(s16 _x, s16 _y, s16 _dx, s32 *_source)
	{
		mmprintf("multimod_row(%d,%d,%d) @ frame %d\n",
			(int)_x, (int)_y, (int)_dx, (int)g_frame);

		// update map tiles
		mapctx0_.template multimod_row<_layercode>(_x, _y, _dx, _source);

		multitouch_row<_autoregion>(_x, _y, _dx);
	}

	// modify multiple map tiles before next refresh: map column
	// note: no dualfield support yet - will implement shared dualfield maps first
	template<PFLayer _layercode = PFLayer_Base, bool _autoregion = true>
	void multimod_column(s16 _x, s16 _y, s16 _dy, s32 *_source)
	{
		mmprintf("multimod_column(%d,%d,%d) @ frame %d\n",
			(int)_x, (int)_y, (int)_dy, (int)g_frame);

		mapctx0_.template multimod_column<_layercode>(_x, _y, _dy, _source);

		multitouch_column<_autoregion>(_x, _y, _dy);
	}

	// modify multiple map tiles before next refresh: map rectangle
	template<PFLayer _layercode = PFLayer_Base, bool _autoregion = true>
	void multimod_rect(s16 _dx, s16 _dy, s16 _w, s16 _h, s32 *_source)
	{
		mmprintf("multimod_rect(%d,%d,%d,%d) @ frame %d\n",
			(int)_dx, (int)_dy, (int)_w, (int)_h, (int)g_frame);

		mapctx0_.template multimod_rect<_layercode>(_dx, _dy, _w, _h, _source);

		multitouch_rect<_autoregion>(_dx, _dy, _w, _h);
	}

	template<PFLayer _layercode = PFLayer_Base, bool _autoregion = true>
	void multimod_copy(s16 _sx, s16 _sy, s16 _dx, s16 _dy, s16 _w, s16 _h, const worldmap& _source)
	{
		mmprintf("multimod_rect(%d,%d,%d,%d,%d,%d) @ frame %d\n",
			(int)_sx, (int)_sy, (int)_dx, (int)_dy, (int)_w, (int)_h, (int)g_frame);

		mapctx0_.template multimod_copy<_layercode>(_sx, _sy, _dx, _dy, _w, _h, _source.get(), _source.getwidth());

		multitouch_rect<_autoregion>(_dx, _dy, _w, _h);
	}

	//

	void fill_abs(bool _force)
	{
		prf_push(prf_arena_fill);

		buffer& b = *pactive_; //buffers_[buffer_index_];

		EXECUTE_BUFFER_FIELDS({b.fields[f_].fill_abs();});
/*
		if (c_dualfield_ && dualfield_mask_)
		{
			if (c_singleframe && !_force)
			{
				b.fields[field_index_].fill_abs();
			}
			else
			{
				b.fields[0].fill_abs();
				b.fields[1].fill_abs();
			}
		}
		else
		{
			b.fields[0].fill_abs();
		}
*/
		prf_pop();
	}	

	void fill(bool _force)
	{
		prf_push(prf_arena_fill);

		buffer& b = *pactive_; //buffers_[buffer_index_];
	
		EXECUTE_BUFFER_FIELDS({b.fields[f_].fill();});
/*
		if (c_dualfield_ && dualfield_mask_)
		{
			if (c_singleframe && !_force)
			{
				b.fields[field_index_].fill();
			}
			else
			{
				b.fields[0].fill();
				b.fields[1].fill();
			}
		}
		else
		{
			b.fields[0].fill();
		}
*/
		//b.fields[0].fill();
		//if (dualfield_)
		//	b.fields[1].fill();

		prf_pop();
	}
	
	void speculative_fill(bool _force)
	{
		prf_push(prf_arena_fill);

		buffer& b = *pactive_; //buffers_[buffer_index_];

		EXECUTE_BUFFER_FIELDS({b.fields[f_].speculative_fill();});
/*
		if (c_dualfield_ && dualfield_mask_)
		{
			if (c_singleframe && !_force)
			{
				b.fields[field_index_].speculative_fill();
			}
			else
			{
				b.fields[0].speculative_fill();
				b.fields[1].speculative_fill();
			}
		}
		else
		{
			b.fields[0].speculative_fill();
		}
*/
//		b.fields[0].speculative_fill();
//		if (dualfield_)
//			b.fields[1].speculative_fill();

		prf_pop();
	}	

	void hybrid_fill(bool _force)
	{
		prf_push(prf_arena_fill);

		buffer& b = *pactive_; //buffers_[buffer_index_];

		EXECUTE_BUFFER_FIELDS({b.fields[f_].hybrid_fill();});
/*
		if (c_dualfield_ && dualfield_mask_)
		{
			if (c_singleframe && !_force)
			{
				b.fields[field_index_].hybrid_fill();
			}
			else
			{
				b.fields[0].hybrid_fill();
				b.fields[1].hybrid_fill();
			}
		}
		else
		{
			b.fields[0].hybrid_fill();
		}
*/
		//b.fields[0].hybrid_fill();
		//if (dualfield_)
		//	b.fields[1].hybrid_fill();

		prf_pop();
	}	

	void hybrid_fill_bufferspec()
	{
		prf_push(prf_arena_fill);

		buffer& b = *pactive_; //buffers_[buffer_index_];
		s16 x = b.fields[0].xpos_;
		s16 y = b.fields[0].ypos_;

		s16 i = buffer_index_;

		for (s16 s = 0; s < c_num_buffers_-1; s++)
		{
			buffer& sb = buffers_[i++];
			i &= (c_num_buffers_-1);

			sb.fields[0].setpos(x++,y);
			sb.fields[0].setslice(s);
			sb.fields[0].bgrestore();
			sb.fields[0].hybrid_fill();
		}

		prf_pop();
	}	

	//void hybrid_fill_dbpreshifts(s16 _strobe)
	//{
	//	buffer& b = *pactive_; //buffers_[buffer_index_];
	//	s16 x = b.fields[0].xpos_;
	//	s16 y = b.fields[0].ypos_;

	//	s16 i = buffer_index_;

	//	const s16 preshifts = c_num_buffers_>>1;
	//	for (s16 s = 0; s < preshifts; s++)
	//	{
	//		buffer& sb = buffers_[strobe | i++];
	//		i &= (preshifts-1);

	//		sb.fields[0].setpos(x,y);
	//		sb.fields[0].hybrid_fill();
	//	}
	//}

	agt_inline void bind_displayport(s16 _port, s16 _yoff = 0)
	{
		viewbind_ = _port;
		viewbind_yoff_ = _yoff;
	}

	agt_inline void set_viewport_offset(s16 _vpxo, s16 _vpyo)
	{
		viewport_xoffset_ = _vpxo;
		viewport_yoffset_ = _vpyo;
	}

	void reset(s16 x, s16 y, bool _nowait = false)
	{
		prf_push(prf_arena_misc);

		s16 tb = buffer_index_;
		//s16 tf = field_index_;
		s16 curr_vbl = g_vbl;
		for (int b = c_num_buffers_-1; b >= 0; b--)
		{
			if (!_nowait)
			{
				while (g_vbl == curr_vbl) { /* spin until counter changes */ }
				curr_vbl = g_vbl;
			}

			select_buffer((tb + b) % c_num_buffers_);
			setpos(x,y);
			fill_abs(true);

			if (!_nowait)
				activate_buffer();
		}
		//select_buffer(tb,tf);

		prf_pop();
	}

	void init(s16 x, s16 y)
	{
		prf_push(prf_arena_misc);

		for (s16 b = 0; b < c_num_buffers_; ++b)
		{
			buffer& rb = buffers_[b];

//			if (dualfield_)
//			{
//				//if (c_singleframe)
//				//{
//				//	rb.fields[0].init(b, 0, display_width_, display_height_, x, y);
//				//	rb.fields[1].init(b, 1, display_width_, display_height_, x, y);
//				//}
//				//else
//				{
////					b.fields[0].speculative_fill();
////					b.fields[1].speculative_fill();
//					rb.fields[0].init(b, 0, display_width_, display_height_, x, y);
//					rb.fields[1].init(b, 1, display_width_, display_height_, x, y);
//				}
//			}
//			else
//			{
//				rb.fields[0].init(b, 0, display_width_, display_height_, x, y);
//			}

			rb.fields[0].init(b, 0, display_width_, display_height_, x, y, (s16)shift_.current_mode_);
			if (c_dualfield_ /*&& dualfield_mask_*/ && !c_singleframe_)
				rb.fields[1].init(b, 1, display_width_, display_height_, x, y, (s16)shift_.current_mode_);

			// shortcut to dirty tile masks
			dirtymaskindex_[b][0] = buffers_[b].fields[0].dtm_animation_.dirtymasklines_;
			if (c_dualfield_ && !c_singleframe_)
				dirtymaskindex_[b][1] = buffers_[b].fields[1].dtm_animation_.dirtymasklines_;
		}
		
		select_buffer(buffer_index_);
		setpos(x,y);
		//activate_buffer();	

		prf_pop();
	}

	// mask left/right edges for stf hardscroll, with optional background auto-restore
	// note: auto-restore may not always be required depending on scroll type, rate and number of buffers
	void stf_maskedges(drawcontext_t& _ctx, bool _autorestore)
	{
		prf_push(prf_arena_fill);

		STF_MaskEdgeL(_ctx.p_pfframebuffer, _ctx.vpagex, _ctx.vpagey, display_height_);	
		STF_MaskEdgeR(_ctx.p_pfframebuffer, _ctx.vpagex, _ctx.vpagey, display_height_);
		if (_autorestore)
		{
			s16 xalign = _ctx.vpagex & -16;
			AGT_MarkDirtyRectangle(_ctx.p_pfframebuffer, xalign,		_ctx.vpagey, 1, display_height_);
			AGT_MarkDirtyRectangle(_ctx.p_pfframebuffer, xalign+320-16, _ctx.vpagey, 1, display_height_);
		}

		prf_pop();
	}

		
};

// ---------------------------------------------------------------------------------------------------------------------

#endif // arena_h_
