//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

agt_inline void PQRS16_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
	// likely to be deprecated

	if ((pi == pi2) || (pj == pj2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	// start beyond rightmost tile of last 8bit block, as blocks are drawn right-to-left
	s16 pi_blockr = (pi2 + 7) & ~7;
	s16 mi_blockr = mi + (pi_blockr - pi);

	const s32* pmap = &map_[mi_blockr + mul_map_x(mj)];	
	const u16* ptiles = &tiledata_[0];

	// start beyond rightmost 8block, not leftmost tile
//	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr * c_tilelinewords_)];
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr << 2)];
	s16 dstrowskip = (rowskipwords_ << 1);

	// todo: pass as struct

	dm_dstaddr = (s16*)dstaddr;
	dm_ptiles = (s16*)ptiles;
	dm_pmap = (s32*)pmap;
	dm_virtual_pagebytes = virtual_pagebytes_;
	dm_visible_linebytes = visible_linebytes_;
	dm_map_tiles_x = map_tiles_x_;
	dm_pi = pi;
	dm_pi2 = pi2;
	dm_pj = pj;
	dm_pj2 = pj2;
	dm_lineskip = dstrowskip;
	dm_dstlinewid = c_dst_linewid;

	// mask is dirtymaskstride_ bytes wide (or dirtymaskstride_/2 fields wide: each field is 16bits, only top 8 occupied)
	dm_dirtymask = &dtm_animation_.dirtymask_[(mi>>3) + (mj*dtm_animation_.dirtymaskwidth_)];
	dm_maskstride = dtm_animation_.dirtymaskstride_;

	__asm__ __volatile__ (
"											\
		jsr		_do_dirtyrect_pqrs;			\
"
     	: 
		: 
		: "cc"
	);	
}

agt_inline void PQ16_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	// likely to be deprecated

	if ((pi == pi2) || (pj == pj2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	// start beyond rightmost tile of last 8bit block, as blocks are drawn right-to-left
	s16 pi_blockr = (pi2 + 7) & ~7;
	s16 mi_blockr = mi + (pi_blockr - pi);

	const s32* pmap = &map_[mi_blockr + mul_map_x(mj)];	
	const u16* ptiles = &tiledata_[0];

	// start beyond rightmost 8block, not leftmost tile
//	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr * c_tilelinewords_)];
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr << 2)];
	s16 dstrowskip = (rowskipwords_ << 1);

	// todo: pass as struct

	dm_dstaddr = (s16*)dstaddr;
	dm_ptiles = (s16*)ptiles;
	dm_pmap = (s32*)pmap;
	dm_virtual_pagebytes = virtual_pagebytes_;
	dm_visible_linebytes = visible_linebytes_;
	dm_map_tiles_x = map_tiles_x_;
	dm_pi = pi;
	dm_pi2 = pi2;
	dm_pj = pj;
	dm_pj2 = pj2;
	dm_lineskip = dstrowskip;
	dm_dstlinewid = c_dst_linewid;

	// mask is dirtymaskstride_ bytes wide (or dirtymaskstride_/2 fields wide: each field is 16bits, only top 8 occupied)
	dm_dirtymask = &dtm_animation_.dirtymask_[(mi>>3) + (mj*dtm_animation_.dirtymaskwidth_)];
	dm_maskstride = dtm_animation_.dirtymaskstride_;

	__asm__ __volatile__ (
"											\
		jsr		_do_dirtyrect_pq;			\
"
     	: 
		: 
		: "cc"
	);	
}

agt_inline void PR16_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	if ((pi == pi2) || (pj == pj2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	// start beyond rightmost tile of last 8bit block, as blocks are drawn right-to-left
	s16 pi_blockr = (pi2 + 7) & ~7;
	s16 mi_blockr = mi + (pi_blockr - pi);

	const s32* pmap = &map_[mi_blockr + mul_map_x(mj)];	
	const u16* ptiles = &tiledata_[0];

	// start beyond rightmost 8block, not leftmost tile
//	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr * c_tilelinewords_)];
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr << 2)];
	s16 dstrowskip = (rowskipwords_ << 1);

	// todo: pass as struct

	dm_dstaddr = (s16*)dstaddr;
	dm_ptiles = (s16*)ptiles;
	dm_pmap = (s32*)pmap;
	dm_virtual_pagebytes = virtual_pagebytes_;
	dm_visible_linebytes = visible_linebytes_;
	dm_map_tiles_x = map_tiles_x_;
	dm_pi = pi;
	dm_pi2 = pi2;
	dm_pj = pj;
	dm_pj2 = pj2;
	dm_lineskip = dstrowskip;
	dm_dstlinewid = c_dst_linewid;

	// mask is dirtymaskstride_ bytes wide (or dirtymaskstride_/2 fields wide: each field is 16bits, only top 8 occupied)
	dm_dirtymask = &dtm_animation_.dirtymask_[(mi>>3) + (mj*dtm_animation_.dirtymaskwidth_)];
	dm_maskstride = dtm_animation_.dirtymaskstride_;

//	dm_dirty_oct = &dirty_oct_[(mi>>4) + (mj*(dirty_ocmstride_>>1))];
//	dm_ocmstride = dirty_ocmstride_;

	__asm__ __volatile__ (
"											\
		jsr		_do_dirtyrect_pr16;			\
"
     	: 
		: 
		: "cc"
	);	
}

inline void P16_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	// likely to be deprecated

	if ((pi == pi2) || (pj == pj2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	// start beyond rightmost tile of last 8bit block, as blocks are drawn right-to-left
	s16 pi_blockr = (pi2 + 7) & ~7;
	s16 mi_blockr = mi + (pi_blockr - pi);

	const s32* pmap = &map_[mi_blockr + mul_map_x(mj)];	
	const u16* ptiles = &tiledata_[0];

	// start beyond rightmost 8block, not leftmost tile
//	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr * c_tilelinewords_)];
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr << 2)];
	s16 dstrowskip = (rowskipwords_ << 1);

	// todo: pass as struct

	dm_dstaddr = (s16*)dstaddr;
	dm_ptiles = (s16*)ptiles;
	dm_pmap = (s32*)pmap;
	dm_virtual_pagebytes = virtual_pagebytes_;
	dm_visible_linebytes = visible_linebytes_;
	dm_map_tiles_x = map_tiles_x_;
	dm_pi = pi;
	dm_pi2 = pi2;
	dm_pj = pj;
	dm_pj2 = pj2;
	dm_lineskip = dstrowskip;
	dm_dstlinewid = c_dst_linewid;

	// mask is dirtymaskstride_ bytes wide (or dirtymaskstride_/2 fields wide: each field is 16bits, only top 8 occupied)
	dm_dirtymask = &dtm_animation_.dirtymask_[(mi>>3) + (mj*dtm_animation_.dirtymaskwidth_)];
	dm_maskstride = dtm_animation_.dirtymaskstride_;

//	dm_dirty_oco = dirty_oco_;
//	dm_dirty_oct = &dirty_oct_[(mi>>4) + (mj*(dirty_ocmstride_>>1))];
//	dm_ocmstride = dirty_ocmstride_;

	__asm__ __volatile__ (
"											\
		jsr		_do_dirtyrect_p;			\
"
     	: 
		: 
		: "cc"
	);		
}

inline void OCM16_refresh_window(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	// likely to be deprecated

	if ((pi == pi2) || (pj == pj2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	// start beyond rightmost tile of last 8bit block, as blocks are drawn right-to-left
	s16 pi_blockr = (pi2 + 7) & ~7;
	s16 mi_blockr = mi + (pi_blockr - pi);

	const s32* pmap = &map_[mi_blockr + mul_map_x(mj)];	
	const u16* ptiles = &tiledata_[0];

	// start beyond rightmost 8block, not leftmost tile
//	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr * c_tilelinewords_)];
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr << 2)];
	s16 dstrowskip = (rowskipwords_ << 1);

	// todo: pass as struct

	dm_dstaddr = (s16*)dstaddr;
	dm_ptiles = (s16*)ptiles;
	dm_pmap = (s32*)pmap;
	dm_virtual_pagebytes = virtual_pagebytes_;
	dm_visible_linebytes = visible_linebytes_;
	dm_map_tiles_x = map_tiles_x_;
	dm_map_tiles_y = map_tiles_y_;
	dm_pi = pi;
	dm_pi2 = pi2;
	dm_pj = pj;
	dm_pj2 = pj2;
	dm_lineskip = dstrowskip;
	dm_dstlinewid = c_dst_linewid;

	// mask is dirtymaskstride_ bytes wide (or dirtymaskstride_/2 fields wide: each field is 16bits, only top 8 occupied)
	dirtytilemap_t &dtm = dtm_bgrestore_;//dtm_animation_;
//	dm_dirtymask = &dtm.dirtymask_[(mi>>3) + (mj*dtm.dirtymaskwidth_)];
	dm_dirtymask = &dtm.dirtymask_[(pi>>3) + (pj*dtm.dirtymaskwidth_)];
	dm_maskstride = dtm.dirtymaskstride_;

	__asm__ __volatile__ (
"											\
		jsr		_OCM16_refresh_68k;			\
"
     	: 
		: 
		: "cc"
	);		
}

inline void OCM_integrate_window(s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	if ((pi == pi2) || (pj == pj2))
		return;

	dm_pi = pi;
	dm_pi2 = pi2;
	dm_pj = pj;
	dm_pj2 = pj2;

	dirtytilemap_t &dtm = dtm_bgrestore_;//dtm_animation_;
	dm_dirtymask = dtm.dirtymask_;
	dm_maskstride = dtm.dirtymaskstride_;

	dm_dirty_oco = ocm_.oco_;
	dm_dirty_oct = ocm_.oct_;
	dm_ocmstride = ocm_.stride_;
	dm_dstlinewid = c_dst_linewid;

	dm_map_tiles_x = map_tiles_x_;
	dm_map_tiles_y = map_tiles_y_;

	__asm__ __volatile__ (
"											\
		jsr		_OCM_integrate_68k;		\
"
     	: 
		: 
		: "cc"
	);		
}

/*
inline void P16_combine_occlusion()
{
	dm_dirtymask = dtm_animation_.dirtymask_;
	dm_maskstride = dtm_animation_.dirtymaskstride_;
	dm_dirty_oco = ocm_.oco_;
	dm_dirty_oct = ocm_.oct_;
	dm_ocmstride = ocm_.stride_;
	dm_map_tiles_x = map_tiles_x_;
	dm_map_tiles_y = map_tiles_y_;

	__asm__ __volatile__ (
"											\
		jsr		_do_mapmod_occlusion;		\
"
     	: 
		: 
		: "cc"
	);	
}
*/

inline void dbg_vis_occlusion()
{
	dm_dirtymask = dtm_animation_.dirtymask_;
	dm_maskstride = dtm_animation_.dirtymaskstride_;
	dm_dirty_oco = ocm_.oco_;
	dm_dirty_oct = ocm_.oct_;
	dm_ocmstride = ocm_.stride_;
	dm_map_tiles_x = map_tiles_x_;
	dm_map_tiles_y = map_tiles_y_;
	dm_dstlinewid = c_dst_linewid;

	// todo: need to include guardband area for vis? probably not...
	dm_vth = vpage_tiles_yg_ * (c_vloop_ ? 2 : 1);//virtual_tiles_y_;

	int pi = xpos_>>4;
	if (c_hloop_)
		pi = modulo_vpage_x(pi);

	int pj = ypos_>>4;
	if (c_vloop_)
		pj = modulo_vpage_y(pj);

	dm_pi = xpos_;

	dm_pj = pj;
	dm_pj2 = pj + vpage_tiles_yg_;	// include guardband area to access next page

//	u16* dstaddr = &framebuffer_[(((pj<<4)+(ypos_&15))*c_dst_linewid) + (pi << 2)];
	u16* dstaddr = &framebuffer_[(((pj<<4)+(ypos_&15))*(c_dst_linewid>>1)) + (pi << 2)];
	dm_dstaddr = (s16*)dstaddr;

	__asm__ __volatile__ (
"											\
		jsr		_vis_mapmod_occlusion;		\
"
     	: 
		: 
		: "cc"
	);	
}

//----------------------------------------------------------------------------------------------------------------------
