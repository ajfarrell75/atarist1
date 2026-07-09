//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------

	inline void PR_shift_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
	{						
		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)
		
		if ((pj == pj2) || (pjy1 == pjy2))
			return;
	
		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);
		const s32* pmap = &map_[mi + mul_map_x(mj)];	

		const u16* ptiles = &tiledata_[pjy1 << 2];

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];
		
		// vertical space between tile fragments, in bytes
		s16 dstrowskip = (((c_tilesize_+pjy1)-pjy2) * virtual_linewords_) << 1;
	
		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
			move.w	#0xf00,0x64.w;			\
"										
#endif
"											\
			move.l	%[dstaddr],%%a4;		\
			move.l	%[ptiles],%%d7;			\
			move.l	%[pmap],%%a0;			\
			moveq	#0,%%d2;				\
			move.w	%[map_tiles_x_],%%d2;	\
			lsl.l	#2,%%d2;				\
											\
			move.l	%%a4,%%a3;				\
			add.l	%[virtual_pagebytes_],%%a3;				\
											\
			move.w	%[pj2],%%d4; 			\
			sub.w	%[pj],%%d4; 			\
											\
			move.w	%[pjy2],%%d3;			\
			sub.w	%[pjy1],%%d3;			\
											\
			move.w	%[dstrowskip],%%d1;		\
											\
			jsr		_PR_shift_speccolumn_Npix;	\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
			move.w	#0x000,0x64.w;			\
"										
#endif
	     	: 
			: [pjy1]"g"(pjy1), 
			  [pjy2]"g"(pjy2), 
			  [pj]"g"(pj), 
			  [pj2]"g"(pj2),
			  [map_tiles_x_]"g"(map_tiles_x_), 
			  [pmap]"g"(pmap), 
			  [ptiles]"g"(ptiles), 
			  [dstaddr]"g"(dstaddr), 
			  [dstrowskip]"g"(dstrowskip),
			  [virtual_pagebytes_]"g"(virtual_pagebytes_)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a1", "%%a3", "%%a4",
			  "cc"
		);	
	}

	inline void P_shift_speccolumn_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pj2, s16 pjy1, s16 pjy2)
	{						
		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)

		if ((pj == pj2) || (pjy1 == pjy2))
			return;

		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);	
		const s32* pmap = &map_[mi + mul_map_x(mj)];	

		const u16* ptiles = &tiledata_[pjy1 << 2];	// todo: c_tilesize_

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pjy1 * virtual_linewords_) + (pi * c_tilelinewords_)];
		
		// vertical space between tile fragments, in bytes
		s16 dstrowskip = (((c_tilesize_+pjy1)-pjy2) * virtual_linewords_) << 1;

		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
			move.w	#0xf00,0x64.w;			\
"										
#endif
"											\
			move.l	%12,%%a4;				 	\
			move.l	%11,%%d7;				 	\
			move.l	%10,%%a0;				 	\
			moveq	#0,%%d2;					\
			move.w	%8,%%d2;					\
			lsl.l	#2,%%d2;					\
											\
			move.w	%5,%%d4; 					\
			sub.w	%3,%%d4; 					\
											\
			move.w	%1,%%d3;					\
			sub.w	%0,%%d3;					\
											\
			move.w	%13,%%d1;					\
											\
			jsr		_P_shift_speccolumn_Npix;	\
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
