//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

#include "pf_block16.inl"
#include "pf_block16m.inl"
#include "pf_block8.inl"
#include "pf_block8m.inl"

//----------------------------------------------------------------------------------------------------------------------

agt_inline void PQRS_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
	if (c_tilestep_ == 4)
	{
		PQRS16_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
	else
	if (c_tilestep_ == 3)
	{
		PQRS8_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
}

agt_inline void PQ_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	if (c_tilestep_ == 4)
	{
		PQ16_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
	else
	if (c_tilestep_ == 3)
	{
		PQ8_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
}

agt_inline void PR_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	if (c_tilestep_ == 4)
	{
		if (c_twolayer_)
			PR16m_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
		else
			PR16_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
	else
	if (c_tilestep_ == 3)
	{
		if (c_twolayer_)
			PR8m_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
		else
			PR8_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
}

agt_inline void P_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	if (c_tilestep_ == 4)
	{
		P16_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
	else
	if (c_tilestep_ == 3)
	{
		P8_fill_block_direct(_mi, _mj, pi, pj, pi2, pj2);
	}
}

//----------------------------------------------------------------------------------------------------------------------
