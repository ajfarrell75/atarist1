*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
* 	SLAB sprite implementation
*-------------------------------------------------------*
*	max source xsize: 256
*	max source ysize: viewport clipper ysize
*-------------------------------------------------------*

*=======================================================*
*	entrypoint
*=======================================================*

	sprite_sc_frame	a0,d0,_AGT_Exception_SLSFrame_68k
		
	pushraster	#$404

	
	move.w		_g_drawcontext+dctx_guardwindow_y1,.sp_yc(sp)

	; 'hitflash' for strobing shape to single colour on damage etc.
	; todo: opt
	
;	move.b		#3,BLiTLOP.w
;	tst.w		d7
;	beq.s		.slb_nohitflash
;	bpl.s		.slb_hitflash
;.slb_blinky:
;	lsr.w		#1,d7
;	bcs		.slb_end	
;	bra.s		.slb_nohitflash
;.slb_hitflash:
;	move.b		#15,BLiTLOP.w	
;.slb_nohitflash:
	
	; access sprite frame table
	
	add.w		d0,d0				; todo: game layer can prescale this
	add.w		d0,d0
	move.l		sls_frameindex(a0,d0.w),a0	; access sprite frame data
			
	move.w		d1,.sp_x(sp)
	bmi		.slb_end			; slabs can't cope with -ve x even in guardband
	move.w		d2,.sp_y(sp)

	bra.s		.slb_begin
	
.slb_component:		

	move.l		d0,a0
	move.w		.sp_x(sp),d1
	move.w		.sp_y(sp),d2
	move.l		.sp_framebuf(sp),a1
	
.slb_begin:			
	move.l		slf_nextcmp(a0),.sp_nextcmp(sp)

;	move.l		slf_nextcmp(a0),d0
;	beq		.sq
;	move.l		d0,a0
;	bra		.get
;	move.l		slf_nextcmp(a0),d0
;	move.l		d0,.sp_nextcmp(sp)
;	beq		.sq
;	move.l		d0,a0	
;.sq:
			
	; remaining spanblocks
		
	moveq		#-1,d7
	add.w		slf_spanblocks(a0),d7
	
	; frame coord. fields
	
	move.w		slf_w(a0),d3			; w
	move.w		slf_h(a0),d4			; h
	beq		.slb_degen
	add.w		slf_xo(a0),d1			; +xo
	add.w		slf_yo(a0),d2			; +yo

	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)
	move.w		d2,d0
	sub.w		.sp_yc(sp),d0
	move.w		d0,.sp_yoff(sp)
	.endif
	.endif
	
	; relative base for offset tables
	
	lea		slf_indexbase(a0),a6		; indexbase
	
	move.l		a6,a2

	cmp.w		.sp_yc(sp),d2
	bpl.s		.slb_clip_safetop

*-------------------------------------------------------*
.slb_clip_top:
*-------------------------------------------------------*
*	top clipping is a bit complex, but relative to 
*	drawing the object and limits on slab drawing
*	count generally, the overhead is neglegible	
*-------------------------------------------------------*

	sub.w		.sp_yc(sp),d2
	neg.w		d2				; frame rect +ve crop
	cmp.w		d4,d2
	bge		.slb_degen			; completely cropped?
	
	move.w		d2,d5				; save crop

	lsl.w		#3,d2
	lea		(a6,d2.w),a0			; index clipper

	add.w		(a0)+,a2			; slc_tyc_offset : first spanblock record = [clipper + yc]
	move.w		(a0)+,d7			; slc_tyc_remain : spanblocks remaining for this yc
	subq.w		#1,d7

	moveq		#0,d4
	move.w		(a0),d4				; slc_tyc_data
	lea		(a6,d4.l),a3			; +64k reach for span pixel data
	

; A     0
; B ____4____
; B          8
; B
; C     16

;	address first spanblock record

	move.l		a6,a0				; spanblock index
	add.w		(a2)+,a0			; next spanblock

;	real cropping is (y+dy) since dy = optional empty lines at start of block


;	-1	dy=2
;	-1
;	X1	y=2,ys=3-1
;	X1
;	X1
;	-2	dy=2
;	-2
;	X2 	y=7,ys=3-1
;	X2				; d5(crop)=8
;	X2

;	srcy = ssb_y+sp_yoff, clamped to 0

	move.w		.sp_yc(sp),d2

;	incorporate skipped region into ytop

	sub.w		ssb_dy(a0),d2

	move.w		ssb_ys(a0),d6

	sub.w		ssb_y(a0),d5	; d5-7=1
	bmi.s		.slb_nocrop
	

;	crop first block size by yc-(y+dy)

	sub.w		d5,d6
	bmi		.slb_degen

;	index source by yc-(y+dy) pixeldata lines

	lsl.w		#3,d5
	mulu.w		ssb_dxw(a0),d5
	add.l		d5,a3

;	slab size is restricted - if clipped at top, 
;	can't also be clipped at bottom	

	bra		.slb_clip_safebot
	
.slb_nocrop:

;	bra		*

;	compensate dsty by -ve cropped amount since skipped region will update dsty before writing spans

	sub.w		d5,d2	
	
;	slab size is restricted - if clipped at top, 
;	can't also be clipped at bottom	

	bra		.slb_clip_safebot
						
*-------------------------------------------------------*
.slb_clip_safetop:
*-------------------------------------------------------*
*	not clipped at window top
*	may still be clipped at window bottom
*-------------------------------------------------------*

;	access first spanblock reference

	add.w		slc_tyc_offset(a6),a2

;	access spanblock record

	move.l		a6,a0				; index base
	add.w		(a2)+,a0

;	first block is uncropped - draw all lines

	move.w		ssb_ys(a0),d6

;	first block uses unadjusted start of pixeldata
	
	move.l		ssb_data(a0),a3

;	test window bottom

	add.w		d2,d4
	cmp.w		_g_drawcontext+dctx_guardwindow_y2,d4
	ble		.slb_clip_safebot

*-------------------------------------------------------*
.slb_clip_bot:
*-------------------------------------------------------*
*	bottom clipping is achieved by drawing
*	(spanblocks-nc) normally and then one final
*	block with cropped height, if required
*-------------------------------------------------------*

;	test for fully clipped out

	cmp.w		_g_drawcontext+dctx_guardwindow_y2,d2
	bge		.slb_degen

;	ys: recover slab ysize

	move.w		d4,d5
	sub.w		d2,d5

;	d2		slab_y1
;	d4		slab_y2
;	d5		slab_ys

;	byc: amount to crop from full slab height (up to max of slab height)
	
	sub.w		_g_drawcontext+dctx_guardwindow_y2,d4

;	d4		+ve crop

;	byr: amount rmaining of full slab height (up to max of slab height)
	
	sub.w		d4,d5
	move.w		d5,.sp_yrem(sp)
	
;	d5		slab_ys_cropped

;	index clipper to obtain (spanblocks-1)-1 for main, unclipped portion

	lsl.w		#3,d4
	move.w		slc_byc_remain(a6,d4.w),d7

;	d7		spanblocks_remaining[crop]-1

;	load initial block source

	move.l		a3,BLiTSRC.w

;	address initial line

;	mulu.w		_g_linebytes,d2
;	add.l		d2,a1	

	; address yline	
	move.l		.sp_linetable(sp),a3
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a3,d2.w),a1			; 38c vs 56c for muls (28c excl. linetable fetch)

;	usp tends to be used by engine for context/object/sprite-restore,
;	but here its more valuable for an inner loop so we need to save it first

	move.l		usp,a5				; usp (auto sprite restore) is preserved via a5

	.if		(0=(enable_slabrestore&(use_sprite_saverestore|use_sprite_pagerestore)))
	.if		(preserve_usp)			; auto sprite restore is off, so
	move.l		a5,.sp_save_usp(sp)		; usp needs preserved explicitly
	.endif
	lea		BLiTDST.w,a5
	.endif

	lea		BLiTCTRL.w,a3
	lea		slab_precalcs,a4
	move.l		a4,usp
	;move.w		_g_linebytes,a4
	move.w		.sp_linebytes(sp),a4

	moveq		#7,d0
	moveq		#$80,d2
	moveq		#0,d3
	moveq		#0,d4

	subq.w		#1,d7
	bmi		.slb_bc_no_main
	
;	(spanblocks-nc) in d7
;	(spans-1) in d6

	dospans_rst

	move.l		a6,a0				; spanblock index
	add.w		(a2)+,a0			; next spanblock

.slb_bc_no_main:

;	always render one final (cropped) block - 0 or more full blocks before this

	move.w		.sp_yrem(sp),d6
	sub.w		ssb_y(a0),d6		; block remainder
	ble		.slb_done

	subq.w		#1,d6		
	moveq		#1-1,d7
	
;	(spanblocks-1) in d7
;	(spans-1) in d6

;	a0		spanblock record
;	a1		dst line
;	a2		spanblock offsets
;	a3		BLiTCTRL.w
;	a4		linebytes
;	a5		BLiTDST.w	
;	a6		spanblock index
;	usp		slab_precalcs
		
	dospans_rst
	
	bra		.slb_done
		
*-------------------------------------------------------*
.slb_clip_safebot:
*-------------------------------------------------------*

;	load initial block source

	move.l		a3,BLiTSRC.w

;	address initial line

;	muls.w		_g_linebytes,d2
;	add.l		d2,a1

	; address yline	
	move.l		.sp_linetable(sp),a5
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a5,d2.w),a1			; 38c vs 56c for muls (28c excl. linetable fetch)
	
;	setup

;	usp tends to be used by engine for context/object/sprite-restore,
;	but here its more valuable for an inner loop so we need to save it first

	move.l		usp,a5				; usp (auto sprite restore) is preserved via a5

	.if		(0=(enable_slabrestore&(use_sprite_saverestore|use_sprite_pagerestore)))
	.if		(preserve_usp)			; auto sprite restore is off, so
	move.l		a5,.sp_save_usp(sp)		; usp needs preserved explicitly
	.endif
	lea		BLiTDST.w,a5
	.endif
	
	lea		BLiTCTRL.w,a3
	lea		slab_precalcs,a4
	move.l		a4,usp
	;move.w		_g_linebytes,a4
	move.w		.sp_linebytes(sp),a4

	moveq		#7,d0
	moveq		#$80,d2
	moveq		#0,d3
	moveq		#0,d4

;	(spanblocks-1) in d7
;	(spans-1) in d6

	dospans_rst

*-------------------------------------------------------*
.slb_done:
*-------------------------------------------------------*

	.if		((enable_slabrestore&(use_sprite_saverestore|use_sprite_pagerestore)))
	move.l		a5,usp
	.else
	.if		(preserve_usp)			; auto sprite restore is off, so
	move.l		.sp_save_usp(sp),a5		; usp needs preserved explicitly
	move.l		a5,usp
	.endif
	.endif

*-------------------------------------------------------*
.slb_degen:
*-------------------------------------------------------*

	move.l		.sp_nextcmp(sp),d0
	bne		.slb_component
	
;	move.l		d0,a0
;	move.w		.sp_x(sp),d1
;	move.w		.sp_y(sp),d2
;	move.l		.sp_framebuf(sp),a1
;	bra		.slb_component
;.sk:

.slb_end:

	popraster
	
*=======================================================*
*	exitpoint
*=======================================================*