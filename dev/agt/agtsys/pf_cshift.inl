//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield region update implementation
//----------------------------------------------------------------------------------------------------------------------


	inline void PR_shift_column_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{						
		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)


		if (pj == pj2)
			return;

		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);
		const s32* pmap = &map_[mi + mul_map_x(mj)];	

		const u16* ptiles = &tiledata_[0];

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi * c_tilelinewords_)];

		s16 dstlineskip = (virtual_linewords_ << 1) - 8;

		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
			move.w	#0xf00,0x64.w;			\
"										
#endif
"											\
			move.l	%10,%%a4;				 	\
			move.l	%9,%%d7;				 	\
			move.l	%8,%%a0;				 	\
			moveq	#0,%%d2;					\
			move.w	%6,%%d2;					\
			lsl.l	#2,%%d2;					\
											\
			move.l	%%a4,%%a3;					\
			add.l	%12,%%a3;					\
											\
			move.w	%3,%%d4; 					\
			sub.w	%1,%%d4; 					\
											\
			move.w	%11,%%d1; 					\
											\
			jsr		_PR_shift_column_Npix;	\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
			move.w	#0x000,0x64.w;			\
"										
#endif
	     	: 
			: "m"(pi), "m"(pj), 
			  "m"(pi), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr), "m"(dstlineskip), 
			  "m"(virtual_pagebytes_)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a1", "%%a3", "%%a4", "%%a5",
			  "cc"
		);
	}

	inline void P_shift_column_direct(s16 _mi, s16 _mj, s16 pi, s16 pj, s16 pi2, s16 pj2)
	{						
		// start mi,mj modulo map dimensions
		// WRAPAROUND_MAPS will modulo mi,mj map lookup during draw, if enabled
		// but recommend disable WRAPAROUND_MAPS for extra speed in loop 
		// (its only useful if map is smaller than playfield)
		
		if (pj == pj2)
			return;

		s16 mi = modulo_map_x(_mi);
		s16 mj = modulo_map_y(_mj);
		const s32* pmap = &map_[mi + mul_map_x(mj)];	

		const u16* ptiles = &tiledata_[0];

		u16* dstaddr = &framebuffer_[(pj * rowskipwords_) + (pi * c_tilelinewords_)];
		
		__asm__ __volatile__ (
#ifdef TIMING_RASTERS
"											\
			move.w	#0xf00,0xffff8240.w;	\
			move.w	#0xf00,0x64.w;			\
"										
#endif
"											\
			move.l	%10,%%a4;				 	\
			move.l	%9,%%d7;				 	\
			move.l	%8,%%a0;				 	\
			moveq	#0,%%d2;					\
			move.w	%6,%%d2;					\
			lsl.l	#2,%%d2;					\
											\
			move.w	%3,%%d4; 					\
			sub.w	%1,%%d4; 					\
											\
			jsr		_P_shift_column_Npix;	\
											\
"
#ifdef TIMING_RASTERS
"											\
			move.w	#0x000,0xffff8240.w;	\
			move.w	#0x000,0x64.w;			\
"										
#endif
	     	: 
			: "m"(pi), "m"(pj), 
			  "m"(pi), "m"(pj2),
			  "m"(mi), "m"(mj), 
			  "m"(map_tiles_x_), "m"(map_tiles_y_), 
			  "m"(pmap), "m"(ptiles), "m"(dstaddr)
			: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7",
			  "%%a0", "%%a1", "%%a3", "%%a4", "%%a5",
			  "cc"
		);
	}
