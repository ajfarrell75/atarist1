*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
* 	RFill blitter rectangle fill implementation
*-------------------------------------------------------*
*	max source xsize: viewport clipper xsize
*	max source ysize: viewport clipper ysize
*-------------------------------------------------------*

	pushraster	#$305

;	use provided rectangle dimensions, ignore sprite frame info
	
	; sprite clipping window
	;lea		_g_clipwin_gpp,a2
	move.l		.sp_scissorwin(sp),a2
	
	; initial dst skip
	
	;move.w		_g_linebytes,a4
	move.w		.sp_linebytes(sp),a4
		
	; constants
	moveq		#16-1,d6
	moveq		#-16,d7
		
	.if		(enable_clipping)
		
*-------------------------------------------------------*
*	top edge
*-------------------------------------------------------*
	cmp.w		clip_win_y(a2),d2
	bpl.s		.rfill_clip_safe_top
*-------------------------------------------------------*
*	top clipped
*-------------------------------------------------------*
	; relative to clipwindow
	sub.w		clip_win_y(a2),d2
	; crop lines
	add.w		d2,d4
	ble		.AGT_BLiT_RFillDrawUnClipped_end
		
	; relative to world
	move.w		clip_win_y(a2),d2
	; ASSUMPTION: sprite is not big enough to cross top+bottom
	bra.s		.rfill_clip_safe_bot
*-------------------------------------------------------*
.rfill_clip_safe_top:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	bottom edge
*-------------------------------------------------------*
	move.w		d2,d0
	add.w		d4,d0				; xr	
	sub.w		clip_win_y2(a2),d0		; xr - cx2
	ble.s		.rfill_clip_safe_bot
*-------------------------------------------------------*
*	bottom clipped	
*-------------------------------------------------------*
	sub.w		d0,d4
	ble		.AGT_BLiT_RFillDrawUnClipped_end
*-------------------------------------------------------*
.rfill_clip_safe_bot:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	left edge
*-------------------------------------------------------*
	; NISR: no initial source read (i.e. shiftregisters preloaded with 0)
	cmp.w		clip_win_x(a2),d1
	bpl.s		.rfill_clip_safe_left
*-------------------------------------------------------*
*	left clipped
*-------------------------------------------------------*

	; relative to clipwindow
	sub.w		clip_win_x(a2),d1

	add.w		d1,d3
	ble		.AGT_BLiT_RFillDrawUnClipped_end
	
	; relative to world
	move.w		clip_win_x(a2),d1	
	; ASSUMPTION: sprite is not big enough to cross left+right
;	bra		.rfill_clip_safe_right
	bra		.rfill_clip_unsafe_left
	
*-------------------------------------------------------*
.rfill_clip_safe_left:
*-------------------------------------------------------*


*-------------------------------------------------------*
*	right edge
*-------------------------------------------------------*
	move.w		d1,d0
	add.w		d3,d0				; xr	
	sub.w		clip_win_x2(a2),d0		; xr - cx2
	ble		.AGT_BLiT_RFillDrawUnClipped_go	;.rfill_clip_safe_right
*-------------------------------------------------------*
*	right clipped
*-------------------------------------------------------*

	; adjust pixel width
	sub.w		d0,d3
	ble		.AGT_BLiT_RFillDrawUnClipped_end

	bra		.AGT_BLiT_RFillDrawUnClipped_go

*-------------------------------------------------------*
.rfill_clip_safe_right:
*-------------------------------------------------------*
;	bra		AGT_BLiT_RFillDrawUnClipped_go
	
*-------------------------------------------------------*
.rfill_clip_unsafe_left:
*-------------------------------------------------------*

	.endif		; (enable_cliping)


*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.AGT_BLiT_RFillDrawUnClipped_go:
*-------------------------------------------------------*

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		usp,a5
	move.l		a5,a6
	.endif
	.endif

	; constants
	moveq		#16-1,d6
	moveq		#-16,d7
	
	; dst pixelword xoffset
	moveq		#-16,d0
	and.w		d1,d0
;	lsr.w		#1,d0
	asr.w		#1,d0		; guardband allows -ve x!
	
	.if		(enable_restore)
	.if		(use_sprite_pagerestore)	
	move.w		d2,(a5)+
	move.w		d0,(a5)+
	.endif	
	.endif

	add.w		d0,a1
	
	; dst yoffset (line)
;	mulu.w		#linebytes,d2
;	mulu.w		_g_linebytes,d2
;	add.l		d2,a1

	; address yline	
	move.l		.sp_linetable(sp),a3
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a3,d2.w),a1			; 38c vs 56c for muls (28c excl. linetable fetch)
		
	; find destination word width of shifted sprite
;	moveq		#16-1,d6
	move.w		d6,d7

	and.w		d1,d6				; rshift
	move.l		d6,d1

	add.w		d3,d7				; w+round

	moveq		#-16,d0	
	add.w		d7,d6				; rshift+w+round
	and.w		d0,d6				; wceil(rshift+w)

*-------------------------------------------------------*

	; dst skip adjust by dst width	
	lsr.w		#1,d6
	sub.w		d6,a4

	; shifted left-edge mask
	moveq		#-1,d0
	lsr.w		d1,d0

	; shifted right-edge mask	
	add.w		d1,d3
	subq.w		#1,d3
	and.w		#16-1,d3
	addq.w		#1,d3
	moveq		#-1,d2
	lsr.w		d3,d2
	not.w		d2

	; word-width				
	lsr.w		#(4-1),d6			; >>4 *2
	move.w		d6,BLiTXC.w			; BLIT: width

	cmp.w		#1,d6
	bne.s		.rfmulti
	and.w		d2,d0
.rfmulti:
	move.w		d0,BLiTEM1.w	
	move.w		d2,BLiTEM3.w
	
	.if		(enable_restore)
	.if		(use_sprite_pagerestore)

	add.w		d6,d6

	; clearing record
;	move.l		a5,a6
	
	move.l		a1,(a5)+			; clear addr
	move.w		a4,(a5)+			; dst skip
	move.w		d6,(a5)+			; word width*2
	move.w		d4,(a5)+			; lines

	.endif
	
	.if		(use_sprite_saverestore)
	
	add.w		d6,d6
	
	; clearing record
;	move.l		a5,a6
	move.l		a1,(a5)+			; clear addr
	move.w		a4,(a5)+			; dst skip
	move.w		d6,(a5)+			; word width*2
	move.w		d4,(a5)+			; lines

	move.l		a1,a3
	moveq		#-1,d1
	add.w		d4,d1

	add.w		d6,d6
	neg.w		d6
;	todo: optimize this
.rfill_r_sylp:
	jmp		.rfill_r_jt(pc,d6.w)
	rept		9
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	endr
.rfill_r_jt:
	add.w		a4,a3
	dbra		d1,.rfill_r_sylp

	.endif
	.endif

*-------------------------------------------------------*

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		a6,(a5)+
	move.l		a5,usp
	.endif
	.endif

*-------------------------------------------------------*
*	common to mask and colour
*-------------------------------------------------------*

	addq.l		#8,a4
	move.w		a4,BLiTDYI.w			; dst yinc

	lea		BLiTDST.w,a6
	lea		BLiTCTRL.w,a2
	lea		BLiTYC.w,a5

	.if		(use_hog)
	moveq		#192,d0
	.else
	moveq		#128,d0
	moveq		#7,d1
	.endif

*-------------------------------------------------------*
*	mask
*-------------------------------------------------------*
	lea		BLiTLOP.w,a0

;	move.b		#0,BLiTHOP.w
;	move.b		#6,BLiTLOP.w			; LOP = 1's

	lsr.b		d5
	scs		(a0)
	
;	move.b		#15,BLiTLOP.w			; LOP = 1's
	
;	plane #0

	move.w		d4,(a5)				; BLIT: lines
	move.l		a1,(a6)				; dst
	move.b		d0,(a2)
	addq.l		#2,a1

	.if		(use_hog=0)
.rmlp0:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.rmlp0
	.endif

	lsr.b		d5
	scs		(a0)

;	plane #1

	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	addq.l		#2,a1

	.if		(use_hog=0)
.rmlp1:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.rmlp1
	.endif

	lsr.b		d5
	scs		(a0)

;	plane #2

	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	addq.l		#2,a1

	.if		(use_hog=0)
.rmlp2:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.rmlp2
	.endif

	lsr.b		d5
	scs		(a0)

;	plane #3

	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)

	.if		(use_hog=0)
.rmlp3:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.rmlp3
	.endif
		
;	subq.l		#6,a1

	

*-------------------------------------------------------*
.rfill_clip_out:
*-------------------------------------------------------*
.AGT_BLiT_RFillDrawUnClipped_end:
*-------------------------------------------------------*

	popraster
