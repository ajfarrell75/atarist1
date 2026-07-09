//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

agt_inline void PQRS8_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{
}

agt_inline void PQ8_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
}

agt_inline void PR8_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
	// start mi,mj modulo map dimensions
	
	if ((pi == pi2) || (pj == pj2))
		return;

//	return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
	s32 mapoff = mul_map_x(mj) + mi;
	const s32* pmap = &map_[mapoff];	
	const u16* ptiles = &tiledata_[0];

	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + ((pi & -2) * c_tilelinewords_)];

//		01 01 01 01 01 01 6
//		01 01 01 01 01 0  5
//		 1 01 01 01 01 01 6
//		 1 01 01 01 01 0  5

	// calculate number of word-based skips
	// note: 8x8 tiles are effectively 'half-words' with a skip on every 2nd (odd) tile
	// so word skip calculations must be based on floor(i)
	s16 nskips = ((pi2 & -2) - (pi & -2)) << (3-1);
	s16 dstrowskip = (rowskipwords_ << 1) - nskips;

	// todo:
	// - create 2 paths for starting odd/even on all rows (versus testing on each row)
	__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"												\
			move.w	#0xf00,0xffff8240.w;		\
			move.w	#0xf00,0x64.w;				\
"										
#endif
"												\
rwsmc%=:	bra		rewrite%=;					\
												\
resume%=:	move.l	%[dstaddr],%%a4;		 	\
			move.l	%[pmap1],%%a0;			 	\
			move.l	%[ptiles1],%%d5;			\
			move.l	%[virtual_pagebytes],%%d6;	\
												\
			move.w	%[pi2],%%d7;				\
			sub.w	%[pi],%%d7;		 			\
												\
			moveq	#0,%%d2;					\
			move.w	%[map_tiles_x],%%d2;		\
			sub.w	%%d7,%%d2;					\
			lsl.l	#2,%%d2;					\
												\
			move.w	%[pj2],%%d4;				\
			sub.w	%[pj],%%d4;					\
												\
			lea		sodd%=,%%a5;				\
			moveq	#1,%%d0;					\
			and.w	%[pi],%%d0;					\
			bne		odd%=;						\
			lea		sevn%=,%%a5;				\
odd%=:		lea		smc_b%=+2(%%pc),%%a3;		\
			sub.l	%%a3,%%a5;					\
			move.w	%%a5,(%%a3);				\
												\
			move.w	%[dstrowskip],%%a2;			\
												\
			move.l	%%a1,%%usp;					\
			bra		syl%=; 						\
yl%=:											\
smc_w%=:	move.w	%%d7,%%d3;					\
smc_b%=:	bra.w	sevn%=;						\
												\
levn%=:		move.l	%%d5,%%a5;					\
			add.l	(%%a0)+,%%a5;				\
												\
			move.l	%%a4,%%a3;					\
			move.l	%%a4,%%a1;					\
			add.l	%%d6,%%a1;					\
												\
rwbeg%=:										\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,0+0(%%a3);			\
			movep.l	%%d0,0+0(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,2+0(%%a3);			\
			movep.l	%%d0,2+0(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,4+0(%%a3);			\
			movep.l	%%d0,4+0(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,6+0(%%a3);			\
			movep.l	%%d0,6+0(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,8+0(%%a3);			\
			movep.l	%%d0,8+0(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,10+0(%%a3);			\
			movep.l	%%d0,10+0(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,12+0(%%a3);			\
			movep.l	%%d0,12+0(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,14+0(%%a3);			\
			movep.l	%%d0,14+0(%%a1);			\
												\
sodd%=:		dbra	%%d3,lodd%=;				\
			add.w	%%d2,%%a0;					\
			bra		nexty%=;					\
												\
lodd%=:		move.l	%%d5,%%a5;					\
			add.l	(%%a0)+,%%a5;				\
												\
			move.l	%%a4,%%a3;					\
			move.l	%%a4,%%a1;					\
			add.l	%%d6,%%a1;					\
												\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,0+1(%%a3);			\
			movep.l	%%d0,0+1(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,2+1(%%a3);			\
			movep.l	%%d0,2+1(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,4+1(%%a3);			\
			movep.l	%%d0,4+1(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,6+1(%%a3);			\
			movep.l	%%d0,6+1(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,8+1(%%a3);			\
			movep.l	%%d0,8+1(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,10+1(%%a3);			\
			movep.l	%%d0,10+1(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,12+1(%%a3);			\
			movep.l	%%d0,12+1(%%a1);			\
			move.l	(%%a5)+,%%d0;				\
			movep.l	%%d0,14+1(%%a3);			\
			movep.l	%%d0,14+1(%%a1);			\
rwend%=:										\
			addq.l	#8,%%a4;					\
												\
sevn%=:		dbra	%%d3,levn%=;				\
			add.w	%%d2,%%a0;					\
nexty%=:										\
			add.w	%%a2,%%a4;					\
syl%=:		dbra	%%d4,yl%=; 					\
												\
			move.l	%%usp,%%a1;					\
												\
			bra.s	rw_skip%=;					\
rewrite%=:										\
			move.w	#0x4e71,rwsmc%=+0;			\
			move.w	#0x4e71,rwsmc%=+2;			\
			lea		rwbeg%=(%%pc),%%a0;			\
			lea		rwend%=(%%pc),%%a2;			\
rw_next%=:	move.	(%%a0)+,%%d1;				\
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
rw_skip%=:										\
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
		  [ptiles1] "m"(ptiles), 
		  [dstaddr] "m"(dstaddr), 
		  [dstrowskip] "m"(dstrowskip),
		  [virtual_pagebytes] "m"(virtual_pagebytes_),
		  [c_dlw] "g"(c_dst_linewid)
		: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
		  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
		  "cc"
	);	
}

agt_inline void P8_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
{						
}
