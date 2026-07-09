//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

inline void PQRS16_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
	// start mi,mj modulo map dimensions
	// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
	// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
	// (its only useful if map is smaller than playfield)

	if ((pj == pj2) || (pjy1 == pjy2))
		return;

//		s16 mi = xmod<s16>(_mi, map_tiles_x_);
//		s16 mj = xmod<s16>(_mj, map_tiles_y_);
	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);
	//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
	const s32* pmap = &map_[mi + mul_map_x(mj)];	

//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		
//		s16 virtual_linebytes = virtual_linewords_ << 1;
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;
	
	const u16* ptiles = &tiledata_[pjy1 << 2];	// todo: c_tilesize_
//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];
	
//		s16 virtual_linebytes = virtual_linewords_ << 1;
	
	s16 dstrowskip = (rowskipwords_ << 1) - (1 << 3);	// todo: c_tilesize_
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size
	
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;
							
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
		move.w	%1,%%d3; 					\
		sub.w	%0,%%d3;		 			\
										\
		move.w	%5,%%d4; 					\
		sub.w	%3,%%d4; 					\
		bra		syl%=; 					\
yl%=:										\
		move.l	%%d1,%%a0;					\
		add.l	%%d2,%%d1;					\
										\
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
		move.w	%%d3,0xffff8a38.w;		\
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
		move.w	%%d3,0xffff8a38.w;		\
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
		move.w	%%d3,0xffff8a38.w;		\
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
		move.w	%%d3,0xffff8a38.w;		\
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
		: "m"(pjy1), "m"(pjy2), 
		  "m"(pi), "m"(pj), 
		  "m"(pi), "m"(pj2),
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
		moveq	#16,%%d3;					\
		add.w	%0,%%d3;					\
		sub.w	%1,%%d3;					\
		lsl.w	#3,%%d3;					\
										\
		move.w	%5,%%d4; 					\
		sub.w	%3,%%d4; 					\
		bra		syl%=; 					\
yl%=:										\
		move.l	%%d1,%%a0;					\
		add.l	%%d2,%%d1;					\
										\
		move.l	%%a2,%%d0;					\
		add.l	(%%a0)+,%%d0;				\
										\
		move.l	%%a4,%%a3;					\
		move.l	%%d0,%%a5;					\
		jmp		.j0%=(%%pc,%%d3.w);			\
.j0%=:		move.l	(%%a5)+,(%%a3)+;			\
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
		lea		%[c_dlw]-8(%%a3),%%a3;			\
jt0%=:										\
		move.l	%%a4,%%a3;					\
		add.w	%%d7,%%a3;					\
		move.l	%%d0,%%a5;					\
		jmp		.j1%=(%%pc,%%d3.w);			\
.j1%=:		move.l	(%%a5)+,(%%a3)+;			\
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
		lea		%[c_dlw]-8(%%a3),%%a3;			\
jt1%=:										\
		move.l	%%a4,%%a3;					\
		add.l	%%d6,%%a3;					\
		move.l	%%d0,%%a5;					\
		jmp		.j2%=(%%pc,%%d3.w);			\
.j2%=:		move.l	(%%a5)+,(%%a3)+;			\
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
		lea		%[c_dlw]-8(%%a3),%%a3;			\
jt2%=:										\
		move.l	%%a4,%%a3;					\
		add.l	%%d6,%%a3;					\
		add.w	%%d7,%%a3;					\
		move.l	%%d0,%%a5;					\
		jmp		.j3%=(%%pc,%%d3.w);			\
.j3%=:		move.l	(%%a5)+,(%%a3)+;			\
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
		lea		%[c_dlw]-8(%%a3),%%a3;			\
jt3%=:										\
		addq.l	#8,%%a4;					\
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
		: "m"(pjy1), "m"(pjy2), 
		  "m"(pi), "m"(pj), 
		  "m"(pi), "m"(pj2),
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

inline void PQ16_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
//		while (1) { }

	// start mi,mj modulo map dimensions
	// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
	// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
	// (its only useful if map is smaller than playfield)
	
	if ((pj == pj2) || (pjy1 == pjy2))
		return;

//		s16 mi = xmod<s16>(_mi, map_tiles_x_);
//		s16 mj = xmod<s16>(_mj, map_tiles_y_);
	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);
	//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
	const s32* pmap = &map_[mi + mul_map_x(mj)];	

//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		
//		s16 virtual_linebytes = virtual_linewords_ << 1;
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;
	
	const u16* ptiles = &tiledata_[pjy1 << 2];	// todo: c_tilesize_
//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];
	
//		s16 virtual_linebytes = virtual_linewords_ << 1;
	
	s16 dstrowskip = (rowskipwords_ << 1) - (1 << 3);	// todo: c_tilesize_
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size
	
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;

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
		move.w	%1,%%d3; 					\
		sub.w	%0,%%d3;		 			\
										\
		move.w	%5,%%d4; 					\
		sub.w	%3,%%d4; 					\
		bra		syl%=; 					\
yl%=:										\
		move.l	%%d1,%%a0;					\
		add.l	%%d2,%%d1;					\
										\
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
		move.w	%%d3,0xffff8a38.w;		\
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
		move.w	%%d3,0xffff8a38.w;		\
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
		: "m"(pjy1), "m"(pjy2), 
		  "m"(pi), "m"(pj), 
		  "m"(pi), "m"(pj2),
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
		moveq	#16,%%d3;					\
		add.w	%0,%%d3;					\
		sub.w	%1,%%d3;					\
		lsl.w	#3,%%d3;					\
										\
		move.w	%5,%%d4; 					\
		sub.w	%3,%%d4; 					\
		bra		syl%=; 					\
yl%=:										\
		move.l	%%d1,%%a0;					\
		add.l	%%d2,%%d1;					\
										\
		move.l	%%a2,%%d0;					\
		add.l	(%%a0)+,%%d0;				\
										\
		move.l	%%a4,%%a3;					\
		move.l	%%d0,%%a5;					\
		jmp		.j0%=(%%pc,%%d3.w);			\
.j0%=:		move.l	(%%a5)+,(%%a3)+;			\
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
		lea		%[c_dlw]-8(%%a3),%%a3;			\
jt0%=:										\
		move.l	%%a4,%%a3;					\
		add.w	%%d7,%%a3;					\
		move.l	%%d0,%%a5;					\
		jmp		.j1%=(%%pc,%%d3.w);			\
.j1%=:		move.l	(%%a5)+,(%%a3)+;			\
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
		lea		%[c_dlw]-8(%%a3),%%a3;			\
jt1%=:										\
		addq.l	#8,%%a4;					\
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
		: "m"(pjy1), "m"(pjy2), 
		  "m"(pi), "m"(pj), 
		  "m"(pi), "m"(pj2),
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

inline void PR16_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
	// start mi,mj modulo map dimensions
	
	if ((pj == pj2) || (pjy1 == pjy2))
		return;

	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);

	s32 mapoff = mul_map_x(mj) + mi;
	const s32* pmap = &map_[mapoff];	
	const u16* ptiles = &tiledata_[pjy1 << 2];

	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];
		
	s16 dstrowskip = (rowskipwords_ << 1);

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
			move.w	%[pj2],%%d4;				\
			sub.w	%[pj],%%d4;					\
												\
			moveq	#0,%%d2;					\
			move.w	%[map_tiles_x],%%d2;		\
			lsl.w	#2,%%d2;					\
			move.w	%%d2,smc_m1%=+2;			\
												\
			moveq	#16,%%d3;					\
			add.w	%[pjy1],%%d3;				\
			sub.w	%[pjy2],%%d3;				\
			lsl.w	#4,%%d3;					\
												\
			lea		yle%=(%%pc,%%d3.w),%%a0;	\
			lea		2+smc_h%=(%%pc),%%a5;		\
			sub.l	%%a5,%%a0;					\
			move.w	%%a0,(%%a5);				\
												\
			move.l	%[pmap1],%%a0;			 	\
			move.l	%[ptiles1],%%d5;		 	\
												\
			move.w	%[dstrowskip],%%d3;			\
			move.w	%[c_dlw]-8,%%d2;				\
												\
			move.l	%%a1,%%usp;					\
			bra		syle%=;						\
												\
yle%=:											\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			move.l	(%%a5)+,%%d0;				\
			move.l	%%d0,(%%a3)+;				\
			move.l	%%d0,(%%a1)+;				\
			add.w	%%d2,%%a3;					\
			add.w	%%d2,%%a1;					\
												\
			add.w	%%d3,%%a4;					\
smc_m1%=:	lea		0x1234(%%a0),%%a0;			\
												\
syle%=:											\
			move.l	%%d5,%%a5;					\
			add.l	(%%a0),%%a5;				\
												\
			move.l	%%a4,%%a3;					\
			move.l	%%a4,%%a1;					\
			add.l	%%d6,%%a1;					\
												\
smc_h%=:										\
			dbra	%%d4,yle%=;					\
												\
			move.l	%%usp,%%a1;					\
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
		  [ptiles1] "m"(ptiles), 
		  [dstaddr] "m"(dstaddr), 
		  [dstrowskip] "m"(dstrowskip),
		  [virtual_pagebytes] "m"(virtual_pagebytes_),
		  [c_dlw] "g" (c_dst_linewid)
		: "%%d0", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6",
		  "%%a0", "%%a3", "%%a4", "%%a5",
		  "cc"
	);
}

inline void P16_fill_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
{						
//		while (1) { }

	// start mi,mj modulo map dimensions
	// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
	// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
	// (its only useful if map is smaller than playfield)

	if ((pj == pj2) || (pjy1 == pjy2))
		return;

//		s16 mi = xmod<s16>(_mi, map_tiles_x_);
//		s16 mj = xmod<s16>(_mj, map_tiles_y_);
	s16 mi = modulo_map_x(_mi);
	s16 mj = modulo_map_y(_mj);
	//const s32* pmap = &map_[mi + (mj * map_tiles_x_)];	
	const s32* pmap = &map_[mi + mul_map_x(mj)];	

//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size		
//		s16 virtual_linebytes = virtual_linewords_ << 1;
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;
	
	const u16* ptiles = &tiledata_[pjy1 << 2];	// todo: c_tilesize_
//		s32 rowskipwords = (c_tilesize_ * virtual_linewords_);
	u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];
	
//		s16 virtual_linebytes = virtual_linewords_ << 1;
	
	s16 dstrowskip = (rowskipwords_ << 1) - (1 << 3);	// todo: c_tilesize_
//		s32 srcywrap = (map_tiles_x_ * map_tiles_y_) << 2;	// todo: map element size
	
//		s16 visible_linebytes = vpage_linewords_ << 1;
//		s32 virtual_pagebytes = virtual_linebytes * wrap_height_;

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
		move.w	%1,%%d3; 					\
		sub.w	%0,%%d3;		 			\
										\
		move.w	%5,%%d4; 					\
		sub.w	%3,%%d4; 					\
		bra		syl%=; 					\
yl%=:										\
		move.l	%%d1,%%a0;					\
		add.l	%%d2,%%d1;					\
										\
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
		move.w	%%d3,0xffff8a38.w;		\
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
		: "m"(pjy1), "m"(pjy2), 
		  "m"(pi), "m"(pj), 
		  "m"(pi), "m"(pj2),
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
		moveq	#16,%%d3;					\
		add.w	%0,%%d3;					\
		sub.w	%1,%%d3;					\
		lsl.w	#3,%%d3;					\
										\
		move.w	%5,%%d4; 					\
		sub.w	%3,%%d4; 					\
		bra		syl%=; 					\
yl%=:										\
		move.l	%%d1,%%a0;					\
		add.l	%%d2,%%d1;					\
										\
		move.l	%%a2,%%d0;					\
		add.l	(%%a0)+,%%d0;				\
										\
		move.l	%%a4,%%a3;					\
		move.l	%%d0,%%a5;					\
		jmp		.j0%=(%%pc,%%d3.w);			\
.j0%=:		move.l	(%%a5)+,(%%a3)+;			\
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
		lea		%[c_dlw]-8(%%a3),%%a3;			\
jt0%=:										\
		addq.l	#8,%%a4;					\
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
		: "m"(pjy1), "m"(pjy2), 
		  "m"(pi), "m"(pj), 
		  "m"(pi), "m"(pj2),
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
