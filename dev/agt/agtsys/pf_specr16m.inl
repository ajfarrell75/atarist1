//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

inline void PQRS16m_fill_specrow_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pjy1, s16 pjy2)
{
}

inline void PQ16m_fill_specrow_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pjy1, s16 pjy2)
{
}

inline void PR16m_fill_specrow_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pjy1, s16 pjy2)
{
	// start mi,mj modulo map dimensions

	if ((pi == pi2) || (pjy1 == pjy2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	s32 mapoff = mul_map_x(mj) + mi;
	const s32* pmap = &map_[mapoff];	
	const s32* pmapoverlay = &mapoverlay_[mapoff];
	const u16* ptiles = &tiledata_[pjy1 << 2];
	const u16* ptilesoverlay = &tileoverlaydata_[((pjy1<<1)+pjy1)<<1];

	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];

	__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"												\
			move.w	#0xf00,0xffff8240.w;		\
			move.w	#0xf00,0x64.w;				\
"
#endif
"												\
			move.l	%[dstaddr],%%a4;			\
			move.l	%[virtual_pagebytes],%%d6;	\
												\
			move.w	%[pi2],%%d3;				\
			sub.w	%[pi],%%d3;		 			\
												\
			moveq	#16,%%d4;					\
			add.w	%[pjy1],%%d4;				\
			sub.w	%[pjy2],%%d4;				\
			move.w	%%d4,%%d0;					\
			add.w	%%d4,%%d4;					\
			add.w	%%d0,%%d4;					\
			lsl.w	#3,%%d4;					\
												\
			lea		xl%=(%%pc,%%d4.w),%%a0;		\
			lea		2+smc_w%=(%%pc),%%a5;		\
			sub.l	%%a5,%%a0;					\
			move.w	%%a0,(%%a5);				\
												\
			move.l	%[pmap1],%%a0;			 	\
			move.l	%[pmap2],%%a2;			 	\
			move.l	%[ptiles1],%%d5;		 	\
			move.l	%[ptiles2],%%d7;		 	\
												\
			move.w	%[c_dlw]-8,%%d2;			\
												\
			move.l	%%a1,%%usp;					\
			move.l	%%a6,smc_t%=+2;				\
			bra		sxl%=; 						\
												\
xl%=:		move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a6)+,%%d1;				\
			move.l	%%d1,%%d0;					\
			and.l	(%%a5)+,%%d0;				\
			or.l	(%%a6)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			and.l	(%%a5)+,%%d1;				\
			or.l	(%%a6)+,%%d1;				\
			move.l	%%d1,(%%a3)+;				\
			move.l	%%d1,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
												\
sxl%=:		move.l	%%d5,%%a5;					\
			add.l	(%%a0)+,%%a5;				\
			move.l	%%d7,%%a6;					\
			add.l	(%%a2)+,%%a6;				\
												\
			move.l	%%a4,%%a3;					\
			move.l	%%a4,%%a1;					\
			add.l	%%d6,%%a1;					\
												\
			addq.l	#8,%%a4;					\
												\
smc_w%=:	dbra	%%d3,xl%=; 					\
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
		  [pi2] "m"(pi2), 
		  [map_tiles_x] "m"(map_tiles_x_), 
		  [pmap1] "m"(pmap), 
		  [pmap2] "m"(pmapoverlay), 
		  [ptiles1] "m"(ptiles), 
		  [ptiles2] "m"(ptilesoverlay),  
		  [dstaddr] "m"(dstaddr), 
		  [virtual_pagebytes] "m"(virtual_pagebytes_),
		  [c_dlw] "g" (c_dst_linewid)
		: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
		  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
		  "cc"
	);
}

inline void P16m_fill_specrow_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pjy1, s16 pjy2)
{
}


