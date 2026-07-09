
*-------------------------------------------------------*
.macro	dospans
*-------------------------------------------------------*

*-------------------------------------------------------*
.start_spanblock\~:	
*-------------------------------------------------------*

;	a0		spanblock record
;	a1		dst line
;	a2		spanblock offsets
;	a3		BLiTCTRL.w
;	a4		linebytes
;	a5		BLiTDST.w	
;	a6		spanblock index
;	usp		slab_precalcs


;	line skip
	move.w		(a0)+,d4
	beq.s		.no_lineskip_init\~

*-------------------------------------------------------*
*	skip empty lines
*-------------------------------------------------------*

;	todo: data setup pass
;	mulu.w		_g_linebytes,d4
	mulu.w		.sp_linebytes(sp),d4
	add.l		d4,a1
	moveq		#0,d4
	
*-------------------------------------------------------*
.no_lineskip_init\~:
*-------------------------------------------------------*

	move.w		(a0)+,d3
	move.w		(a0)+,d4
	add.w		d1,d3

;	skew/nfsr	1
;	width		1
;	em1		2
;	em3		2
;	dyi		2

*-------------------------------------------------------*
*	single span or rectangular block of spans?
*-------------------------------------------------------*
	tst.w		d6
	beq		.do_sgl\~
	bra		.do_block\~
	
*-------------------------------------------------------*
.wt_next_spanblock\~:	
*-------------------------------------------------------*
.wt\~:	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wt\~	

*-------------------------------------------------------*
.cont_spanblock\~:
*-------------------------------------------------------*

	move.l		a6,a0				; spanblock index
	add.w		(a2)+,a0			; next spanblock
	
*-------------------------------------------------------*

;	a0		spanblock record
;	a1		dst line
;	a2		spanblock offsets
;	a3		BLITCTRL.w
;	a4		linebytes
;	a5		BLiTDST.w
;	a6		spanblock index
;	usp		slab_precalcs


;	line skip
	move.w		(a0)+,d4
	beq.s		.no_lineskip\~

*-------------------------------------------------------*
*	skip empty lines
*-------------------------------------------------------*

;	todo: data setup pass
;	mulu.w		_g_linebytes,d4
	mulu.w		.sp_linebytes(sp),d4
	add.l		d4,a1
	moveq		#0,d4
	
*-------------------------------------------------------*
.no_lineskip\~:
*-------------------------------------------------------*

	move.w		(a0)+,d3
	move.w		(a0)+,d4
	add.w		d1,d3

;	skew/nfsr	1
;	width		1
;	em1		2
;	em3		2
;	dyi		2

*-------------------------------------------------------*
*	single span or rectangular block of spans?
*-------------------------------------------------------*
	move.w		ssb_ys-6(a0),d6
	beq		.do_sgl\~
*-------------------------------------------------------*
*	block of related, unique spans
*-------------------------------------------------------*
.do_block\~:
*-------------------------------------------------------*

*-------------------------------------------------------*
;	dst addr xoffset	
	moveq		#-16,d5
	and.l		d3,d5				; ceil16(x)
	eor.w		d5,d3				; pixel offset
	lsr.w		d5	
	add.l		a1,d5
*-------------------------------------------------------*
;	lsl.w		#4,d4
	or.w		d3,d4
	lsl.w		#3,d4
	move.l		usp,a0
	add.l		d4,a0	
	move.b		(a0)+,BLiTSKEW.w
	move.b		(a0)+,d3
	move.w		(a0)+,BLiTEM1.w
	move.w		(a0)+,BLiTEM3.w
	move.w		(a0)+,BLiTDYI.w
	lea		BLiTXC.w,a0
	move.w		d3,(a0)+
*-------------------------------------------------------*
	moveq		#4,d4
	subq.w		#1,d6
*-------------------------------------------------------*
.spanloop_begin\~:
*-------------------------------------------------------*
	move.w		d4,(a0)				; BLIT: planes
	move.l		d5,(a5)				; dst
	move.b		d2,(a3)
	add.l		a4,d5
	add.w		a4,a1
*-------------------------------------------------------*
.spanloop_parallel\~:
*-------------------------------------------------------*
	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.spanloop_parallel\~
	move.w		d4,(a0)				; BLIT: planes
	move.l		d5,(a5)				; dst
	move.b		d2,(a3)
	add.l		a4,d5
	add.w		a4,a1
.wtb2\~:
	bset.b		d0,(a3)
;	bne.s		.wtb2
	dbra		d6,.spanloop_parallel\~
*-------------------------------------------------------*
.spanloop_final\~:
*-------------------------------------------------------*
	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.spanloop_final\~
*-------------------------------------------------------*
	dbra		d7,.cont_spanblock\~
*-------------------------------------------------------*
	bra		.end\~

*-------------------------------------------------------*
*	single span
*-------------------------------------------------------*
.do_sgl\~:
*-------------------------------------------------------*
;	dst addr xoffset	
	moveq		#-16,d5
	and.l		d3,d5				; ceil16(x)
	eor.w		d5,d3				; pixel offset
	lsr.w		d5
	add.l		a1,d5
*-------------------------------------------------------*
;	lsl.w		#4,d4
	or.w		d3,d4
	lsl.w		#3,d4
	move.l		usp,a0
	add.l		d4,a0	
	move.b		(a0)+,BLiTSKEW.w
	move.b		(a0)+,d3
	move.w		(a0)+,BLiTEM1.w
	move.w		(a0)+,BLiTEM3.w
	move.w		(a0)+,BLiTDYI.w	
*-------------------------------------------------------*
	move.l		d5,(a5)+			; BLiTDST
	move.w		d3,(a5)+			; BLiTXC
	move.w		#4,(a5)				; BLiTYC: planes
	subq		#6,a5
	move.b		d2,(a3)
	add.w		a4,a1
	bset.b		d0,(a3)	
*-------------------------------------------------------*
;	hide the dbra behind the blitter restart
	dbra		d7,.wt_next_spanblock\~

*=======================================================*
;	ensure blit completes on fall-through
.wtend\~:
	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wtend\~
*-------------------------------------------------------*
.end\~:

*-------------------------------------------------------*
	endm
*-------------------------------------------------------*

*-------------------------------------------------------*
.macro	doregions
*-------------------------------------------------------*

_enable_slabrestore_	set	(use_sprite_saverestore|use_sprite_pagerestore)

*-------------------------------------------------------*
.start_spanblock\~:	
*-------------------------------------------------------*

;	a0		*
;	a1		dst line
;	a2		spanblock offsets
;	a3		slab_precalcs
;	a4		linebytes
;	a5		[clearing]
;	a6		spanblock index


;	line skip
	move.w		(a0)+,d4
	beq.s		.no_lineskip_init\~

*-------------------------------------------------------*
*	skip empty lines
*-------------------------------------------------------*

;	todo: data setup pass
;	mulu.w		_g_linebytes,d4
	mulu.w		.sp_linebytes(sp),d4
	add.l		d4,a1
	moveq		#0,d4
	
*-------------------------------------------------------*
.no_lineskip_init\~:
*-------------------------------------------------------*

	move.w		(a0)+,d3
	move.w		(a0)+,d4
	add.w		d1,d3

;	skew/nfsr	1
;	width		1
;	em1		2
;	em3		2
;	dyi		2

*-------------------------------------------------------*
*	single span or rectangular block of spans?
*-------------------------------------------------------*
	tst.w		d6
	beq		.do_sgl\~
	bra		.do_block\~
	

*-------------------------------------------------------*
.cont_spanblock\~:
*-------------------------------------------------------*

	move.l		a6,a0				; spanblock index
	add.w		(a2)+,a0			; next spanblock

;	a0		*
;	a1		dst line
;	a2		spanblock offsets
;	a3		slab_precalcs
;	a4		linebytes
;	a5		[clearing]
;	a6		spanblock index


;	line skip
	move.w		(a0)+,d4
	beq.s		.no_lineskip\~

*-------------------------------------------------------*
*	skip empty lines
*-------------------------------------------------------*

;	todo: data setup pass
;	mulu.w		_g_linebytes,d4
	mulu.w		.sp_linebytes(sp),d4
	add.l		d4,a1
	moveq		#0,d4
	
*-------------------------------------------------------*
.no_lineskip\~:
*-------------------------------------------------------*

	move.w		(a0)+,d3
	move.w		(a0)+,d4
	add.w		d1,d3

;	skew/nfsr	1
;	width		1
;	em1		2
;	em3		2
;	dyi		2

*-------------------------------------------------------*
*	single span or rectangular block of spans?
*-------------------------------------------------------*
	move.w		ssb_ys-6(a0),d6
	beq.s		.do_sgl\~
*-------------------------------------------------------*
*	block of related, unique spans
*-------------------------------------------------------*
.do_block\~:
*-------------------------------------------------------*

	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)
	move.l		a5,d2
	move.w		ssb_y-6(a0),d5
	add.w		.sp_yoff(sp),d5
	bpl.s		.qqb\~
	clr.w		d5
.qqb\~:	add.w		.sp_yc(sp),d5
	move.w		d5,(a5)+
	.endif
	.endif

*-------------------------------------------------------*
;	dst addr xoffset	
	moveq		#-16,d5
	and.l		d3,d5				; ceil16(x)
	eor.w		d5,d3				; pixel offset
	lsr.w		d5	
	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)	
	move.w		d5,(a5)+
	.endif	
	.endif
	add.l		a1,d5
*-------------------------------------------------------*
;	lsl.w		#4,d4
	or.w		d3,d4
	lsl.w		#3,d4

;	dst word count for spanblock at this xoffset
	move.b		1(a3,d4.l),d3

*-------------------------------------------------------*

	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)

	; clearing record

	move.l		d5,(a5)+			; clear addr

	add.w		d3,d3
	move.w		d3,d4

	add.w		d4,d4
	add.w		d4,d4
	move.w		a4,d0
	sub.w		d4,d0

	move.w		d0,(a5)+			; dst skip
	move.w		d3,(a5)+			; word width*2

	moveq		#1,d4
	add.w		d6,d4
	move.w		d4,(a5)+			; lines

	move.l		d2,(a5)+

	.endif
	.endif

*-------------------------------------------------------*
	move.w		a4,d0
	addq.w		#1,d6
	mulu.w		d6,d0
	add.l		d0,a1

*-------------------------------------------------------*
.spanloop_final\~:
*-------------------------------------------------------*

*-------------------------------------------------------*
	dbra		d7,.cont_spanblock\~
*-------------------------------------------------------*
	bra.s		.end\~

*-------------------------------------------------------*
*	single span
*-------------------------------------------------------*
.do_sgl\~:
*-------------------------------------------------------*

	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)
	move.l		a5,d2
	move.w		ssb_y-6(a0),d5
	add.w		.sp_yoff(sp),d5
	bpl.s		.qqs\~
	clr.w		d5
.qqs\~:	add.w		.sp_yc(sp),d5
	move.w		d5,(a5)+
	.endif
	.endif

*-------------------------------------------------------*
;	dst addr xoffset	
	moveq		#-16,d5
	and.l		d3,d5				; ceil16(x)
	eor.w		d5,d3				; pixel offset
	lsr.w		d5
	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)	
	move.w		d5,(a5)+
	.endif	
	.endif
	add.l		a1,d5
*-------------------------------------------------------*
;	lsl.w		#4,d4
	or.w		d3,d4
	lsl.w		#3,d4
	
;	dst word count for spanblock at this xoffset
	move.b		1(a3,d4.l),d3

*-------------------------------------------------------*

	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)

	; clearing record

	move.l		d5,(a5)+			; clear addr

	add.w		d3,d3
	move.w		d3,d4

	add.w		d4,d4
	add.w		d4,d4
	move.w		a4,d0
	sub.w		d4,d0

	move.w		d0,(a5)+			; dst skip
	move.w		d3,(a5)+			; word width*2

	move.w		#1,(a5)+			; lines

	move.l		d2,(a5)+
		
	.endif
	.endif	
	
*-------------------------------------------------------*

	add.w		a4,a1
	
*-------------------------------------------------------*
;	hide the dbra behind the blitter restart
	dbra		d7,.cont_spanblock\~

*=======================================================*

*-------------------------------------------------------*
.end\~:

*-------------------------------------------------------*
	endm
*-------------------------------------------------------*


*-------------------------------------------------------*
.macro	dospans_rst
*-------------------------------------------------------*

_enable_slabrestore_	set	(enable_slabrestore&(use_sprite_saverestore|use_sprite_pagerestore))

*-------------------------------------------------------*
.start_spanblock\~:	
*-------------------------------------------------------*

;	a0		*
;	a1		dst line
;	a2		spanblock offsets
;	a3		BLIT:go
;	a4		linebytes
;	a5		[clearing]
;	a6		spanblock index
;	usp		slab_precalcs


;	line skip
	move.w		(a0)+,d4
	beq.s		.no_lineskip_init\~

*-------------------------------------------------------*
*	skip empty lines
*-------------------------------------------------------*

;	todo: data setup pass
;	mulu.w		_g_linebytes,d4
	mulu.w		.sp_linebytes(sp),d4
	add.l		d4,a1
	moveq		#0,d4
	
*-------------------------------------------------------*
.no_lineskip_init\~:
*-------------------------------------------------------*

	move.w		(a0)+,d3
	move.w		(a0)+,d4
	add.w		d1,d3

;	skew/nfsr	1
;	width		1
;	em1		2
;	em3		2
;	dyi		2

*-------------------------------------------------------*
*	single span or rectangular block of spans?
*-------------------------------------------------------*
	tst.w		d6
	beq		.do_sgl\~
	bra		.do_block\~
	
*-------------------------------------------------------*
.wt_next_spanblock\~:	
*-------------------------------------------------------*
.wt\~:	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wt\~

*-------------------------------------------------------*
.cont_spanblock\~:
*-------------------------------------------------------*

	move.l		a6,a0				; spanblock index
	add.w		(a2)+,a0			; next spanblock

*-------------------------------------------------------*

;	a0		*
;	a1		dst line
;	a2		spanblock offsets
;	a3		BLIT:go
;	a4		linebytes
;	a5		[clearing]
;	a6		spanblock index
;	usp		slab_precalcs


;	line skip
	move.w		(a0)+,d4
	beq.s		.no_lineskip\~

*-------------------------------------------------------*
*	skip empty lines
*-------------------------------------------------------*

;	todo: data setup pass
;	mulu.w		_g_linebytes,d4
	mulu.w		.sp_linebytes(sp),d4
	add.l		d4,a1
	moveq		#0,d4
	
*-------------------------------------------------------*
.no_lineskip\~:
*-------------------------------------------------------*

	move.w		(a0)+,d3
	move.w		(a0)+,d4
	add.w		d1,d3

;	skew/nfsr	1
;	width		1
;	em1		2
;	em3		2
;	dyi		2

*-------------------------------------------------------*
*	single span or rectangular block of spans?
*-------------------------------------------------------*
	move.w		ssb_ys-6(a0),d6
	beq		.do_sgl\~
*-------------------------------------------------------*
*	block of related, unique spans
*-------------------------------------------------------*
.do_block\~:
*-------------------------------------------------------*

	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)
	move.l		a5,d2
	move.w		ssb_y-6(a0),d5
	add.w		.sp_yoff(sp),d5
	move.w		d5,(a5)+
	.endif
	.endif

*-------------------------------------------------------*
;	dst addr xoffset	
	moveq		#-16,d5
	and.l		d3,d5				; ceil16(x)
	eor.w		d5,d3				; pixel offset
	lsr.w		d5	
	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)	
	move.w		d5,(a5)+
	.endif
	.endif
	add.l		a1,d5
*-------------------------------------------------------*
;	lsl.w		#4,d4
	or.w		d3,d4
	lsl.w		#3,d4
	move.l		usp,a0
	add.l		d4,a0	
	move.b		(a0)+,BLiTSKEW.w
	move.b		(a0)+,d3
	move.w		d3,BLiTXC.w
	move.w		(a0)+,BLiTEM1.w
	move.w		(a0)+,BLiTEM3.w
	move.w		(a0)+,BLiTDYI.w
*-------------------------------------------------------*

	.if		(enable_slabrestore)

	.if		(use_sprite_pagerestore)

	add.w		d3,d3
	
	; clearing record
	move.l		d5,(a5)+			; clear addr

	move.l		a4,a0

	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0

	move.w		a0,(a5)+			; dst skip
	move.w		d3,(a5)+			; word width*2

	moveq		#1,d4
	add.w		d6,d4
	move.w		d4,(a5)+			; lines
;	move.w		#1,(a5)+			; lines

	move.l		d2,(a5)+

	moveq		#$80,d2

	.endif


	.if		(use_sprite_saverestore&1)
	
	add.w		d3,d3
	
	; clearing record
	move.l		a5,d2
	move.l		d5,(a5)+			; clear addr

	move.l		a4,a0

	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0

	move.w		a0,(a5)+			; dst skip
	move.w		d3,(a5)+			; word width*2

	moveq		#1,d4
	add.w		d6,d4
	move.w		d4,(a5)+			; lines
;	move.w		#1,(a5)+			; lines

	move.l		d5,a3
;	moveq		#-1,d1
;	add.w		d4,d1
	
	subq.w		#1,d4

	add.w		d3,d3
	neg.w		d3
;	todo: optimize this
.sylp\~:
	jmp		.jtb\~(pc,d3.w)
	.rept		17
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	.endr
.jtb\~:

	add.w		a0,a3

	dbra		d4,.sylp\~

	move.l		d2,(a5)+

	moveq		#$80,d2
	lea		BLiTCTRL.w,a3
	
	.endif
	
	.endif

*-------------------------------------------------------*
	moveq		#4,d4
	subq.w		#1,d6
*-------------------------------------------------------*
.spanloop_begin\~:
*-------------------------------------------------------*
	move.w		d4,BLiTYC.w			; BLIT: planes
	.if		(_enable_slabrestore_)
	move.l		d5,BLiTDST.w			; dst
	.else
	move.l		d5,(a5)				; dst
	.endif
	move.b		d2,(a3)
	add.l		a4,d5
	add.w		a4,a1

;.wtb:	bset.b		d0,(a3)
;	dbra		d6,.spanloop_parallel
;;	bne.s		.wtb
;;	dbra		d6,.spanloop_begin
;*-------------------------------------------------------*
;	dbra		d7,.cont_spanblock
;*-------------------------------------------------------*
;	bra		.end

*-------------------------------------------------------*
.spanloop_parallel\~:
*-------------------------------------------------------*
	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.spanloop_parallel\~
	move.w		d4,BLiTYC.w			; BLIT: planes
	move.l		d5,BLiTDST.w			; dst
	move.b		d2,(a3)
	add.l		a4,d5
	add.w		a4,a1
.wtb2\~:
	bset.b		d0,(a3)
;	bne.s		.wtb2
	dbra		d6,.spanloop_parallel\~
*-------------------------------------------------------*
.spanloop_final\~:
*-------------------------------------------------------*
	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.spanloop_final\~
*-------------------------------------------------------*
	dbra		d7,.cont_spanblock\~
*-------------------------------------------------------*
	bra		.end\~

*-------------------------------------------------------*
*	single span
*-------------------------------------------------------*
.do_sgl\~:
*-------------------------------------------------------*

	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)
	move.l		a5,d6
	move.w		ssb_y-6(a0),d5
	add.w		.sp_yoff(sp),d5
	move.w		d5,(a5)+
	.endif
	.endif

*-------------------------------------------------------*
;	dst addr xoffset	
	moveq		#-16,d5
	and.l		d3,d5				; ceil16(x)
	eor.w		d5,d3				; pixel offset
	lsr.w		d5
	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)	
	move.w		d5,(a5)+
	.endif
	.endif
	add.l		a1,d5
*-------------------------------------------------------*
;	lsl.w		#4,d4
	or.w		d3,d4
	lsl.w		#3,d4
	move.l		usp,a0
	add.l		d4,a0	
	move.b		(a0)+,BLiTSKEW.w
	move.b		(a0)+,d3
	.if		(_enable_slabrestore_)
	move.w		d3,BLiTXC.w
	.endif
	move.w		(a0)+,BLiTEM1.w
;	move.w		#-1,BLiTEM1.w
	move.w		(a0)+,BLiTEM3.w
	move.w		(a0)+,BLiTDYI.w
	

	
	.if		(enable_slabrestore)
	.if		(use_sprite_pagerestore)
	
	add.w		d3,d3
	
	; clearing record
	move.l		d5,(a5)+			; clear addr

	move.l		a4,a0

	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0

	move.w		a0,(a5)+			; dst skip
	move.w		d3,(a5)+			; word width*2

;	moveq		#1,d4
;	add.w		d6,d4
;	move.w		d4,(a5)+			; lines
	move.w		#1,(a5)+			; lines
	
	move.l		d6,(a5)+
	
	.endif
	
	
	.if		(use_sprite_saverestore&1)
	
	add.w		d3,d3
	
	; clearing record
	move.l		a5,d6
	move.l		d5,(a5)+			; clear addr

	move.l		a4,a0

	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0
	sub.w		d3,a0

	move.w		a0,(a5)+			; dst skip
	move.w		d3,(a5)+			; word width*2

;	moveq		#1,d4
;	add.w		d6,d4
;	move.w		d4,(a5)+			; lines
	move.w		#1,(a5)+			; lines

	move.l		d5,a3
;	moveq		#-1,d1
;	add.w		d4,d1

	add.w		d3,d3
	neg.w		d3
;	todo: optimize this
	jmp		.jt\~(pc,d3.w)
	.rept		17
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	.endr
.jt\~:

;	add.w		a0,a3

;	dbra		d1,.sylp

	move.l		d6,(a5)+

	lea		BLiTCTRL.w,a3
	
	.endif
	.endif
	
	
*-------------------------------------------------------*
	.if		(_enable_slabrestore_)
	move.l		d5,BLiTDST.w			; BLiTDST
	move.w		#4,BLiTYC.w			; BLiTYC: planes
	.else
	move.l		d5,(a5)+			; BLiTDST
	move.w		d3,(a5)+			; BLiTXC
	move.w		#4,(a5)				; BLiTYC: planes
	subq		#6,a5
	.endif
	move.b		d2,(a3)
	add.w		a4,a1
	bset.b		d0,(a3)	
*-------------------------------------------------------*
;	hide the dbra behind the blitter restart
	dbra		d7,.wt_next_spanblock\~

*=======================================================*
;	ensure blit completes on fall-through
.wtend\~:
	bset.b		d0,(a3)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wtend\~
*-------------------------------------------------------*
.end\~:

*-------------------------------------------------------*
	.endm
*-------------------------------------------------------*
