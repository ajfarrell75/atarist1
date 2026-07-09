//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

	agt_inline void PQRS16_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{
		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)
		
		if ((pi == pi2) || (pj == pj2))
			return;
	
		//s16 mi = xmod<s16>(_mi, map_tiles_x_);
		//s16 mj = xmod<s16>(_mj, map_tiles_y_);
		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);

//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		
//		s16 virtual_linebytes = virtual_linewords_ << 1;
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;

		//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
		const s32* pmap = &map_[mi + mul_map_x(mj)];	
		const u16* ptiles = &tiledata_[0];

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi * c_tilelinewords_)];
		s16 dstrowskip = (rowskipwords_ << 1) - ((pi2 - pi) << 3);	// todo: c_tilesize_

		if (c_blitter_)
		{
					
		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.w	%16,%%d7;					\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"											\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
			bra		syl%=; 					\
yl%=:										\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 					\
"										
#endif
"											\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 					\
xl%=:										\
			move.l	%%a2,%%d0;					\
			add.l	(%%a0)+,%%d0;				\
			move.l	%%d0,%%a1;					\
"
#ifdef BLITTER_HOG
"											\
			move.b	#0xc0,%%d0;				\
"			
#else		
"											\
			move.b	#0x80,%%d0;				\
"			
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,%%a3;					\
			add.w	%%d7,%%a3;					\
											\
			move.l	%%a3,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"			
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,%%a3;					\
			add.l	%%d6,%%a3;					\
											\
			move.l	%%a3,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif			
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,%%a3;					\
			add.l	%%d6,%%a3;					\
			add.w	%%d7,%%a3;					\
											\
			move.l	%%a3,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif
"											\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;					\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"											\
sxl%=:		dbra	%%d3,xl%=; 				\
											\
			adda.w	%13,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;					\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"											\
syl%=:		dbra	%%d4,yl%=; 				\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a1", "%%a2", "%%a3", "%%a4",
			  "cc"
		);	

		}
		else
		{

		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.w	%16,%%d7;					\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"											\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
			bra		syl%=; 					\
yl%=:										\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 					\
"										
#endif
"											\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 					\
xl%=:										\
			move.l	%%a2,%%d0;					\
			add.l	(%%a0)+,%%d0;				\
											\
			move.l	%%a4,%%a3;					\
			move.l	%%d0,%%a5;					\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;		\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;		\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
											\
			move.l	%%a4,%%a3;					\
			add.w	%%d7,%%a3;					\
			move.l	%%d0,%%a5;					\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
											\
			move.l	%%a4,%%a3;					\
			add.l	%%d6,%%a3;					\
			move.l	%%d0,%%a5;					\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
											\
			move.l	%%a4,%%a3;					\
			add.l	%%d6,%%a3;					\
			add.w	%%d7,%%a3;					\
			move.l	%%d0,%%a5;					\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
											\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;					\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"											\
sxl%=:		dbra	%%d3,xl%=; 				\
											\
			adda.w	%13,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;					\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"											\
syl%=:		dbra	%%d4,yl%=; 				\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_),
			  [c_dlw] "g" (c_dst_linewid)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
			  "cc"
		);	
		}
	}

	agt_inline void PQ16_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{						
//		while (1) { }

		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)
		
		if ((pi == pi2) || (pj == pj2))
			return;
	
		//s16 mi = xmod<s16>(_mi, map_tiles_x_);
		//s16 mj = xmod<s16>(_mj, map_tiles_y_);
		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);

//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		
//		s16 virtual_linebytes = virtual_linewords_ << 1;
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;

		//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
		const s32* pmap = &map_[mi + mul_map_x(mj)];	
		const u16* ptiles = &tiledata_[0];

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi * c_tilelinewords_)];
		s16 dstrowskip = (rowskipwords_ << 1) - ((pi2 - pi) << 3);	// todo: c_tilesize_

		if (c_blitter_)
		{						
		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.w	%16,%%d7;					\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"											\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
			bra		syl%=; 					\
yl%=:										\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 					\
"										
#endif
"											\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 					\
xl%=:										\
			move.l	%%a2,%%d0;					\
			add.l	(%%a0)+,%%d0;				\
			move.l	%%d0,%%a1;					\
"
#ifdef BLITTER_HOG
"											\
			move.b	#0xc0,%%d0;				\
"			
#else		
"											\
			move.b	#0x80,%%d0;				\
"			
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,%%a3;					\
			add.w	%%d7,%%a3;					\
											\
			move.l	%%a3,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"			
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif
"											\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;					\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"											\
sxl%=:		dbra	%%d3,xl%=; 				\
											\
			adda.w	%13,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;					\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"											\
syl%=:		dbra	%%d4,yl%=; 				\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a1", "%%a2", "%%a3", "%%a4",
			  "cc"
		);	
		}
		else
		{

		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.w	%16,%%d7;					\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"											\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
			bra		syl%=; 					\
yl%=:										\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 					\
"										
#endif
"											\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 					\
xl%=:										\
			move.l	%%a2,%%d0;					\
			add.l	(%%a0)+,%%d0;				\
											\
			move.l	%%a4,%%a3;					\
			move.l	%%d0,%%a5;					\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
											\
			move.l	%%a4,%%a3;					\
			add.w	%%d7,%%a3;					\
			move.l	%%d0,%%a5;					\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
											\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;					\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"											\
sxl%=:		dbra	%%d3,xl%=; 				\
											\
			adda.w	%13,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;					\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"											\
syl%=:		dbra	%%d4,yl%=; 				\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_),
			  [c_dlw] "g" (c_dst_linewid)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
			  "cc"
		);	
		}
	}

	agt_inline void PR16_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{						

		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)
		
		if ((pi == pi2) || (pj == pj2))
			return;
	
		//s16 mi = xmod<s16>(_mi, map_tiles_x_);
		//s16 mj = xmod<s16>(_mj, map_tiles_y_);
		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);

//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		
//		s16 virtual_linebytes = virtual_linewords_ << 1;
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;

		//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
		const s32* pmap = &map_[mi + mul_map_x(mj)];	
		const u16* ptiles = &tiledata_[0];

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi * c_tilelinewords_)];
		s16 dstrowskip = (rowskipwords_ << 1) - ((pi2 - pi) << 3);	// todo: c_tilesize_

//		while (pi) { reg16(FFFF8240) = ~reg16(FFFF8240); }
//		while (pj) { reg16(FFFF8240) = ~reg16(FFFF8240); }


		if (c_blitter_)
		{
								
		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
			move.w	#0xf00,0x64.w;			\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.w	%16,%%d7;					\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"											\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
			bra		syl%=; 					\
yl%=:										\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 					\
"										
#endif
"											\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 					\
xl%=:										\
			move.l	%%a2,%%d0;					\
			add.l	(%%a0)+,%%d0;				\
			move.l	%%d0,%%a1;					\
"
#ifdef BLITTER_HOG
"											\
			move.b	#0xc0,%%d0;				\
"			
#else		
"											\
			move.b	#0x80,%%d0;				\
"			
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,%%a3;					\
			add.l	%%d6,%%a3;					\
											\
			move.l	%%a3,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif			
"											\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;					\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"											\
sxl%=:		dbra	%%d3,xl%=; 				\
											\
			adda.w	%13,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;					\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"											\
syl%=:		dbra	%%d4,yl%=; 				\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
			move.w	#0x000,0x64.w;			\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a1", "%%a2", "%%a3", "%%a4",
			  "cc"
		);	

		}
		else
		{

		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"												\
			move.w	#0xf00,0xffff8240.w;		\
			move.w	#0xf00,0x64.w;				\
"										
#endif
"												\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"												\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"												\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
												\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
												\
			move.w	%13,smc%=+2;				\
			move.l	%%a1,%%usp;					\
			bra		syl%=; 						\
yl%=:											\
"
#ifdef WRAPAROUND_MAPS
"												\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 						\
"										
#endif
"												\
			move.w	%[c_dlw]-8,%%d7;			\
												\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 						\
xl%=:											\
			move.l	%%a2,%%a5;					\
			add.l	(%%a0)+,%%a5;				\
												\
			move.l	%%a4,%%a3;					\
			move.l	%%a4,%%a1;					\
			add.l	%%d6,%%a1;					\
												\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			adda.w	%%d7,%%a3;					\
			move.l	%%d0,(%%a1)+;				\
			adda.w	%%d7,%%a1;					\
												\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"												\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;						\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"												\
sxl%=:		dbra	%%d3,xl%=; 					\
												\
smc%=:		lea		0x1234(%%a4),%%a4;			\
"
#ifdef WRAPAROUND_MAPS
"												\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;						\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"												\
syl%=:		dbra	%%d4,yl%=; 					\
												\
			move.l	%%usp,%%a1;					\
												\
"
#ifdef TIMING_RASTERS
"												\
			move.w	#0x000,0xffff8240.w;		\
			move.w	#0x000,0x64.w;				\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_),
			  [c_dlw] "g" (c_dst_linewid)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
			  "cc"
		);	
		}
	}

	agt_inline void P16_fill_block_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{						
		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)
		
		if ((pi == pi2) || (pj == pj2))
			return;
	
		//s16 mi = xmod<s16>(_mi, map_tiles_x_);
		//s16 mj = xmod<s16>(_mj, map_tiles_y_);
		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);

//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		
//		s16 virtual_linebytes = virtual_linewords_ << 1;
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;

		//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
		const s32* pmap = &map_[mi + mul_map_x(mj)];	
		const u16* ptiles = &tiledata_[0];

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi * c_tilelinewords_)];
		s16 dstrowskip = (rowskipwords_ << 1) - ((pi2 - pi) << 3);	// todo: c_tilesize_

		if (c_blitter_)
		{								
		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
			move.w	#0xf00,0x64.w;			\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.w	%16,%%d7;					\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"											\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
			bra		syl%=; 					\
yl%=:										\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 					\
"										
#endif
"											\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 					\
xl%=:										\
			move.l	%%a2,%%d0;					\
			add.l	(%%a0)+,%%d0;				\
			move.l	%%d0,%%a1;					\
"
#ifdef BLITTER_HOG
"											\
			move.b	#0xc0,%%d0;				\
"			
#else		
"											\
			move.b	#0x80,%%d0;				\
"			
#endif
#ifdef FASTPAGE_TILES	// use short word addressing - all tiles inside aligned 64k window
"											\
			move.w	%%a1,0xffff8a26.w;		\
"										
#else					// long addressing - tiles can be anywhere
"											\
			move.l	%%a1,0xffff8a24.w;		\
"										
#endif
"											\
			move.l	%%a4,0xffff8a32.w;		\
			move.w	#16,0xffff8a38.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"
#ifdef BLITTER_HOG
#else
"											\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
			move.b	%%d0,0xffff8a3c.w;		\
"										
#endif
"											\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;					\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"											\
sxl%=:		dbra	%%d3,xl%=; 				\
											\
			adda.w	%13,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;					\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"											\
syl%=:		dbra	%%d4,yl%=; 				\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
			move.w	#0x000,0x64.w;			\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a1", "%%a2", "%%a3", "%%a4",
			  "cc"
		);	
		}
		else
		{

		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
			move.w	#0xf00,0x64.w;			\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%a2;				 	\
			move.l	%10,%%d1;				 	\
			move.w	%16,%%d7;					\
			move.l	%17,%%d6;					\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%7,%%d4;			 		\
			swap	%%d4;				 		\
"										
#endif
"											\
			move.w	%4,%%d5; 					\
			sub.w	%2,%%d5;		 			\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
			bra		syl%=; 					\
yl%=:										\
"
#ifdef WRAPAROUND_MAPS
"											\
			move.w	%6,%%d3; 					\
			swap 	%%d3; 					\
"										
#endif
"											\
			move.w	%%d5,%%d3; 					\
			move.l	%%d1,%%a0;					\
			add.l	%%d2,%%d1;					\
			bra		sxl%=; 					\
xl%=:										\
			move.l	%%a2,%%d0;					\
			add.l	(%%a0)+,%%d0;				\
											\
			move.l	%%a4,%%a3;					\
			move.l	%%d0,%%a5;					\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
			lea		%[c_dlw]-8(%%a3),%%a3;			\
			move.l	(%%a5)+,(%%a3)+;			\
			move.l	(%%a5)+,(%%a3)+;			\
											\
			addq.l	#8,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d3;						\
			addq.w	#1,%%d3;					\
			cmp.w	%8,%%d3;					\
			bne.s	nxl%=;					\
			sub.w	%%d3,%%d3;					\
			sub.l	%%d2,%%a0;					\
nxl%=:		swap	%%d3;						\
"										
#endif
"											\
sxl%=:		dbra	%%d3,xl%=; 				\
											\
			adda.w	%13,%%a4;					\
"
#ifdef WRAPAROUND_MAPS
"											\
			swap	%%d4;						\
			addq.w	#1,%%d4;					\
			cmp.w	%9,%%d4;					\
			bne.s	nyl%=;					\
			sub.w	%%d4,%%d4;					\
			sub.l	%14,%%d1;					\
nyl%=:		swap	%%d4;						\
"										
#endif
"											\
syl%=:		dbra	%%d4,yl%=; 				\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
			move.w	#0x000,0x64.w;			\
"										
#endif
	     	: 
			: "m"(_mi), "m"(_mj), 
			  "m"(pi), "m"(pj), 
			  "m"(pi2), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstrowskip),
			  "m"(srcywrap_), "m"(virtual_linebytes_), "m"(visible_linebytes_), "m"(virtual_pagebytes_),
			  [c_dlw] "g" (c_dst_linewid)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a2", "%%a3", "%%a4", "%%a5",
			  "cc"
		);	
		}
	}
