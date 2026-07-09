//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

#include "pf_mapmod16.inl"
#include "pf_mapmod16m.inl"
#include "pf_mapmod8.inl"
#include "pf_mapmod8m.inl"

//----------------------------------------------------------------------------------------------------------------------

agt_inline void PQRS_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
	if (c_tilestep_ == 4)
		PQRS16_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	else
	if (c_tilestep_ == 3)
		PQRS8_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
}

agt_inline void PQ_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
	if (c_tilestep_ == 4)
		PQ16_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	else
	if (c_tilestep_ == 3)
		PQ8_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
}

agt_inline void PR_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
	if (c_tilestep_ == 4)
	{
		if (c_twolayer_)
			PR16m_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
		else
			PR16_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
	else
	if (c_tilestep_ == 3)
	{
		if (c_twolayer_)
			PR8m_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
		else
			PR8_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
}

agt_inline void P_dirty_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
	if (c_tilestep_ == 4)
		P16_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	else
	if (c_tilestep_ == 3)
		P8_dirty_block_direct(_mi, _mj, pi, pj, pi2, pj2);
}

//----------------------------------------------------------------------------------------------------------------------
