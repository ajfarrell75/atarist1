//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

agt_inline void PQRS16m_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
}

agt_inline void PQ16m_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
}

agt_inline void PR16m_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	// start mi,mj modulo map dimensions
	
	if ((pi == pi2) || (pj == pj2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
	s32 mapoff = mul_map_x(mj) + mi;
	const s32* pmap = &map_[mapoff];	
	const s32* pmapoverlay = &mapoverlay_[mapoff];	
	const u16* ptiles = &tiledata_[0];
	const u16* ptiles2 = &tileoverlaydata_[0];

	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi * c_tilelinewords_)];

	// calculate number of word-based skips
	s16 nskips = (pi2 - pi) << 3;
	s16 dstrowskip = (rowskipwords_ << 1) - nskips;

	__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"												\
			move.w	#0xf00,0xffff8240.w;		\
			move.w	#0xf00,0x64.w;				\
"										
#endif
"												\
			move.l	%[dstaddr],%%a4;		 	\
			move.l	%[pmap1],%%a0;			 	\
			move.l	%[pmap2],%%a2;				\
			move.l	%[ptiles1],%%d5;			\
			move.l	%[ptiles2],%%d7;		 	\
			move.l	%[virtual_pagebytes],%%d6;	\
												\
			move.w	%[pi2],%%d0;				\
			sub.w	%[pi],%%d0;		 			\
			move.w	%%d0,smc_w%=+2;				\
												\
			moveq	#0,%%d2;					\
			move.w	%[map_tiles_x],%%d2;		\
			sub.w	%%d0,%%d2;					\
			lsl.w	#2,%%d2;					\
			move.w	%%d2,smc_m1%=+2;			\
			move.w	%%d2,smc_m2%=+2;			\
												\
			move.w	%[pj2],%%d4;				\
			sub.w	%[pj],%%d4;					\
												\
			move.w	%[dstrowskip],smc_d%=+2;	\
			move.w	%[c_dlw]-8,%%d2;			\
												\
			move.l	%%a1,%%usp;					\
			move.l	%%a6,smc_t%=+2;				\
			bra		syl%=; 						\
yl%=:											\
smc_w%=:	move.w	#1234,%%d3;					\
			bra.w	sevn%=;						\
												\
levn%=:		move.l	%%d5,%%a5;					\
			add.l	(%%a0)+,%%a5;				\
			move.l	%%d7,%%a6;					\
			add.l	(%%a2)+,%%a6;				\
												\
			move.l	%%a4,%%a3;					\
			move.l	%%a4,%%a1;					\
			add.l	%%d6,%%a1;					\
												\
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
			addq.l	#8,%%a4;					\
												\
sevn%=:		dbra	%%d3,levn%=;				\
smc_m1%=:	lea		0x1234(%%a0),%%a0;			\
smc_m2%=:	lea		0x1234(%%a2),%%a2;			\
smc_d%=:	lea		0x1234(%%a4),%%a4;			\
syl%=:		dbra	%%d4,yl%=; 					\
												\
			move.l	%%usp,%%a1;					\
smc_t%=:	lea		0x123456,%%a6;				\
												\
"
#ifdef TIMING_RASTERS
"												\
			move.w	#0x000,0xffff8240.w;		\
			move.w	#0x000,0x64.w;				\
"										
#endif
     	:
		: [pi] "m"(pi), 
		  [pj] "m"(pj), 
		  [pi2] "m"(pi2), 
		  [pj2] "m"(pj2),
		  [map_tiles_x] "m"(map_tiles_x_), 
		  [pmap1] "m"(pmap), 
		  [pmap2] "m"(pmapoverlay), 
		  [ptiles1] "m"(ptiles), 
		  [ptiles2] "m"(ptiles2), 
		  [dstaddr] "m"(dstaddr), 
		  [dstrowskip] "m"(dstrowskip),
		  [virtual_pagebytes] "m"(virtual_pagebytes_),
		  [c_dlw] "g" (c_dst_linewid)
		: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
		  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
		  "cc"
	);	
}

agt_inline void P16m_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
}
