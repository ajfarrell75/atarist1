//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

#include "pf_specc16.inl"
#include "pf_specc16m.inl"
#include "pf_specc8.inl"
#include "pf_specc8m.inl"

//----------------------------------------------------------------------------------------------------------------------

inline void PQRS_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{
	if (c_tilestep_ == 4)
		PQRS16_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
}

inline void PQ_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{
	if (c_tilestep_ == 4)
		PQ16_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
}

inline void PR_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{
	if (c_tilestep_ == 4)
	{
		if (c_twolayer_)
			PR16m_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
		else
			PR16_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
	}
	else
	if (c_tilestep_ == 3)
	{
		if (c_twolayer_)
			PR8m_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
		else
			PR8_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
	}
}

inline void P_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{
	if (c_tilestep_ == 4)
		P16_fill_speccolumn_direct(_mi, _mj, pi, pj, pj2, pjy1, pjy2);
}
