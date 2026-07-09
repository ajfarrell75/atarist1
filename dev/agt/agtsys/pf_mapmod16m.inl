//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

agt_inline void PQRS16m_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
	// likely to be deprecated
}

agt_inline void PQ16m_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	// likely to be deprecated
}

agt_inline void PR16m_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{	
	if ((pi == pi2) || (pj == pj2))
		return;
					
	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	// start beyond rightmost tile of last 8bit block, as blocks are drawn right-to-left
	s16 pi_blockr = (pi2 + 7) & ~7;
	s16 mi_blockr = mi + (pi_blockr - pi);

	s32 mapoff = mul_map_x(mj) + mi_blockr;
	const s32* pmap = &map_[mapoff];	
	const s32* pmapoverlay = &mapoverlay_[mapoff];	
	const u16* ptiles = &tiledata_[0];
	const u16* ptilesoverlay = &tileoverlaydata_[0];

	// start beyond rightmost 8block, not leftmost tile
//	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr * c_tilelinewords_)];
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi_blockr << 2)];
	s16 dstrowskip = (rowskipwords_ << 1);

	// todo: pass as struct

	dm_dstaddr = (s16*)dstaddr;
	dm_ptiles = (s16*)ptiles;
	dm_pltiles = (s16*)ptilesoverlay;
	dm_pmap = (s32*)pmap;
	dm_plmap = (s32*)pmapoverlay;
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
"										\
		jsr		_do_dirtyrect_pr16m;	\
"
     	: 
		: 
		: "cc"
	);	
}

agt_inline void P16m_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	// likely to be deprecated
}
