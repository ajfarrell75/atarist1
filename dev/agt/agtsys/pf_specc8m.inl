//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

agt_inline void PQRS8m_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
}

agt_inline void PQ8m_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
}

agt_inline void PR8m_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
	// start mi,mj modulo map dimensions
	
	if ((pj == pj2) || (pjy1 == pjy2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	s32 mapoff = mul_map_x(mj) + mi;
	const s32* pmap = &map_[mapoff];	
	const s32* pmapoverlay = &mapoverlay_[mapoff];
	const u16* ptiles = &tiledata_[pjy2 << 1];
	const u16* ptilesoverlay = &tileoverlaydata_[pjy2 << 2];

	//u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + ((pi & -2) * 2)];
		
	s16 dstrowskip = (rowskipwords_ << 1);

	__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"												\
			move.w	#0xf00,0xffff8240.w;		\
			move.w	#0xf00,0x64.w;				\
"										
#endif
"												\
rwsmc%=:	bra.w	rewrite%=;					\
												\
resume%=:	move.l	%[dstaddr],%%a4;			\
			move.l	%[virtual_pagebytes],%%d6;	\
												\
			move.w	%[pj2],%%d4;				\
			sub.w	%[pj],%%d4;					\
												\
			moveq	#0,%%d2;					\
			move.w	%[map_tiles_x],%%d2;		\
			lsl.l	#2,%%d2;					\
												\
			moveq	#8,%%d3;					\
			add.w	%[pjy1],%%d3;				\
			sub.w	%[pjy2],%%d3;				\
			move.w	%%d3,%%d0;					\
			lsl.w	#3,%%d3;					\
			sub.w	%%d0,%%d3;					\
			add.w	%%d3,%%d3;					\
												\
			lea		yle%=(%%pc,%%d3.w),%%a0;	\
			lea		2+smc_h%=(%%pc),%%a5;		\
			sub.l	%%a5,%%a0;					\
			move.w	%%a0,(%%a5);				\
												\
			moveq	#1,%%d3;					\
			and.w	%[pi],%%d3;					\
			add.w	%%d3,%%a4;					\
												\
			move.l	%[pmap1],%%a0;			 	\
			move.l	%[pmap2],%%a2;			 	\
			move.l	%[ptiles1],%%d5;		 	\
			move.l	%[ptiles2],%%d7;		 	\
												\
			move.w	%[dstrowskip],%%d3;			\
												\
			move.l	%%a1,%%usp;					\
			move.l	%%a6,smc_t%=+2;				\
			bra		syle%=;						\
												\
rewrite%=:										\
			move.w	#0x4e71,rwsmc%=+0;			\
			move.w	#0x4e71,rwsmc%=+2;			\
			lea		rwbeg%=(%%pc),%%a0;			\
			lea		rwend%=(%%pc),%%a2;			\
rw_next%=:	move.w	(%%a0)+,%%d1;				\
			cmp.w	#0x01CB,%%d1;				\
			bne.s	rw_no_0%=;					\
rw_do%=:	move.w	(%%a0),%%d1;				\
			move.w	%%d1,%%d2;					\
			lsr.w	#1,%%d2;					\
			mulu.w	%[c_dlw],%%d2;				\
			and.w	#1,%%d1;					\
			add.w	%%d2,%%d1;					\
			move.w	%%d1,(%%a0)+;				\
			bra.s	rw_done%=;					\
rw_no_0%=:	cmp.w	#0x01C9,%%d1;				\
			beq.s	rw_do%=;					\
rw_done%=:	cmp.l	%%a2,%%a0;					\
			blo.s	rw_next%=;					\
			bra		resume%=;					\
yle%=:											\
rwbeg%=:										\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,14+0(%%a3);			\
			movep.l	%%d0,14+0(%%a1);			\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,12+0(%%a3);			\
			movep.l	%%d0,12+0(%%a1);			\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,10+0(%%a3);			\
			movep.l	%%d0,10+0(%%a1);			\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,8+0(%%a3);			\
			movep.l	%%d0,8+0(%%a1);			\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,6+0(%%a3);			\
			movep.l	%%d0,6+0(%%a1);			\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,4+0(%%a3);			\
			movep.l	%%d0,4+0(%%a1);			\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,2+0(%%a3);			\
			movep.l	%%d0,2+0(%%a1);			\
			move.l	-(%%a6),%%d0;				\
			and.l	-(%%a5),%%d0;				\
			or.l	-(%%a6),%%d0;				\
			movep.l	%%d0,0+0(%%a3);			\
			movep.l	%%d0,0+0(%%a1);			\
rwend%=:										\
												\
			add.w	%%d3,%%a4;					\
			add.w	%%d2,%%a0;					\
			add.w	%%d2,%%a2;					\
												\
syle%=:											\
			move.l	%%d5,%%a5;					\
			add.l	(%%a0),%%a5;				\
			move.l	%%d7,%%a6;					\
			add.l	(%%a2),%%a6;				\
												\
			move.l	%%a4,%%a3;					\
			move.l	%%a4,%%a1;					\
			add.l	%%d6,%%a1;					\
												\
smc_h%=:										\
			dbra	%%d4,yle%=;					\
												\
			move.l	%%usp,%%a1;					\
smc_t%=:	lea		0x123456,%%a6;				\
"
#ifdef TIMING_RASTERS
"												\
			move.w	#0x000,0xffff8240.w;		\
			move.w	#0x000,0x64.w;				\
"										
#endif
     	: 
		: [pjy1] "m"(pjy1), 
		  [pjy2] "m"(pjy2), 
		  [pi] "m"(pi), 
		  [pj] "m"(pj), 
		  [pj2] "m"(pj2),
		  [map_tiles_x] "m"(map_tiles_x_), 
		  [pmap1] "m"(pmap), 
		  [pmap2] "m"(pmapoverlay), 
		  [ptiles1] "m"(ptiles), 
		  [ptiles2] "m"(ptilesoverlay), 
		  [dstaddr] "m"(dstaddr), 
		  [dstrowskip] "m"(dstrowskip),
		  [virtual_pagebytes] "m"(virtual_pagebytes_),
		  [c_dlw] "g"(c_dst_linewid)
		: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
		  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
		  "cc"
	);
}

agt_inline void P8m_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
}
