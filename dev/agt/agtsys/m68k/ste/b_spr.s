*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
* 	IMSPR blitter sprite implementation
*-------------------------------------------------------*
*	max source xsize: viewport clipper xsize
*	max source ysize: viewport clipper ysize
*-------------------------------------------------------*

	sprite_sc_frame	a0,d0,_AGT_Exception_IMSFrame_68k
			
	pushraster	#$227

	sprite_spf	a0,d0,d3,a0
			
	; frame fields
	
	move.w		spf_w(a0),d3			; w
	move.w		spf_h(a0),d4			; h
	add.w		spf_xo(a0),d1			; +xo
	add.w		spf_yo(a0),d2			; +yo
			
	
	; sprite clipping window
	;lea		_g_clipwin_gpp,a2
	move.l		.sp_scissorwin(sp),a2
	
	; initial dst skip
	
;	lea		linebytes.w,a4			; dst stride = linebytes-spritedstlinebytes
	move.w		_g_linebytes,a4
	
;	.if		(1)
	
	; frame fields
	
	lea		spf_data(a0),a0
;	move.l		spf_data(a0),a0

	
	; access sprite mask
	
;	move.w		d3,d0
;	add.w		#16-1,d0
;	lsr.w		#4,d0
;	mulu.w		d4,d0
;	add.w		d0,d0
;	move.l		a0,a6	
;	lea		(a0,d0.w),a6
	

	
	
	; skew
	moveq		#16-1,d6
	and.w		d1,d6
;	move.b		d6,$ffff8a3d.w	
	move.b		d6,.sp_skew(sp)
	
	; constants
	moveq		#16-1,d6
	moveq		#-16,d7
		
		
	; initial source skip (unclipped case)
	
	lea		10.w,a3




	.if		(enable_clipping)
		
*-------------------------------------------------------*
*	top edge
*-------------------------------------------------------*
	cmp.w		clip_win_y(a2),d2
	bpl.s		.ims_clip_safe_top
*-------------------------------------------------------*
*	top clipped
*-------------------------------------------------------*
	move.w		clip_win_y(a2),d5
	; relative to clipwindow
	sub.w		d5,d2
	; crop lines
	add.w		d2,d4
	ble		.AGT_BLiT_IMSprDrawUnClipped_end
	; adjust src offset in pixelwords*planes
	neg.w		d2
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0

	lsr.w		#1,d0
	move.w		d0,d5
	lsr.w		#2,d5
	add.w		d5,d0

	mulu.w		d2,d0
	add.l		d0,a0
		
	; relative to world
	move.w		clip_win_y(a2),d2
	; ASSUMPTION: sprite is not big enough to cross top+bottom
	bra.s		.ims_clip_safe_bot
*-------------------------------------------------------*
.ims_clip_safe_top:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	bottom edge
*-------------------------------------------------------*
	move.w		d2,d0
	add.w		d4,d0				; xr	
	sub.w		clip_win_y2(a2),d0			; xr - cx2
	ble.s		.ims_clip_safe_bot
*-------------------------------------------------------*
*	bottom clipped	
*-------------------------------------------------------*
	sub.w		d0,d4
	ble		.AGT_BLiT_IMSprDrawUnClipped_end
*-------------------------------------------------------*
.ims_clip_safe_bot:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	left edge
*-------------------------------------------------------*
	; NISR: no initial source read (i.e. shiftregisters preloaded with 0)
	;cmp.w		clip_x1p(a2),d1
	cmp.w		clip_win_x(a2),d1
	bpl.s		.ims_clip_safe_left
*-------------------------------------------------------*
*	left clipped
*-------------------------------------------------------*
	; FISR: force initial source read (i.e. shiftregisters preloaded with 1st data word)
;	tas.b		$ffff8a3d.w
;	or.b		#$80,$ffff8a3d.w
	or.b		#$80,.sp_skew(sp)

	; relative to clipwindow
	;sub.w		clip_x1p(a2),d1
	sub.w		clip_win_x(a2),d1
	; find minimum pixelwords off left edge
;	move.w		d1,d0
;	neg.w		d0				; pixels off left

	; adjust dest width (ignore FISR part)
	
	move.w		d3,d5
	
;	add.w		d0,d3
	add.w		d1,d3
	ble		.AGT_BLiT_IMSprDrawUnClipped_end

	; adjust src offset by clipped pixelwords (include FISR part)

	move.w		d1,d0
	neg.w		d0
	add.w		d6,d0
	and.w		d7,d0
	lsr.w		#1,d0
	move.w		d0,d1
	lsr.w		#2,d1
	add.w		d1,d0
	lea		-10(a0,d0.w),a0
	
;	and.w		d7,d1
;	asr.w		#1,d1
;	move.w		d1,d0
;	asr.w		#2,d0
;	add.w		d1,d0
;	lea		10(a0,d0.w),a0

	; adjust src skip by ceil'd change in width
	
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0				;#-16,d0
	add.w		d6,d5
	and.w		d7,d5
	sub.w		d5,d0
	neg.w		d0

;	lsr.w		#4-1,d0		
;	add.w		.index(pc,d0.w),a3
	
	lsr.w		#1,d0
	move.w		d0,d5
	lsr.w		#2,d5
	add.w		d5,d0
	lea		-10(a3,d0.w),a3
	
	; relative to world
;	move.w		clip_x1p(a2),d1	
	move.w		clip_win_x(a2),d1
	; ASSUMPTION: sprite is not big enough to cross left+right
;	bra		.ims_clip_safe_right
	bra		.ims_clip_unsafe_left
	
;.index:	dc.w		-1*10,0*10,1*10,2*10,3*10,4*10,5*10,6*10,7*10,8*10,9*10,10*10

*-------------------------------------------------------*
.ims_clip_safe_left:
*-------------------------------------------------------*


*-------------------------------------------------------*
*	right edge
*-------------------------------------------------------*
	move.w		d1,d0
	add.w		d3,d0				; xr	
;	sub.w		clip_x2p(a2),d0			; xr - cx2
	sub.w		clip_win_x2(a2),d0
	ble		.AGT_BLiT_IMSprDrawUnClipped_go	;.ims_clip_safe_right
*-------------------------------------------------------*
*	right clipped
*-------------------------------------------------------*
	move.w		d3,d5

	; adjust pixel width
	sub.w		d0,d3
	ble		.AGT_BLiT_IMSprDrawUnClipped_end

	; adjust by ceil'd change in width
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0				;#-16,d0
	add.w		d6,d5
	and.w		d7,d5
	sub.w		d5,d0
	neg.w		d0
	
	; adjust src skip
	lsr.w		#1,d0
	add.w		d0,a3				; +8*s
	lsr.w		#2,d0				; +2*s
	add.w		d0,a3

	bra		.AGT_BLiT_IMSprDrawUnClipped_go

*-------------------------------------------------------*
.ims_clip_safe_right:
*-------------------------------------------------------*
;	bra		AGT_BLiT_IMSprDrawUnClipped_go
	
*-------------------------------------------------------*
.ims_clip_unsafe_left:
*-------------------------------------------------------*

	.endif		; (enable_cliping)


*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.AGT_BLiT_IMSprDrawUnClipped_go:
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
	;lsr.w		#1,d0
	asr.w		#1,d0		; IMSpr doesn't use guardband but accept -ve x for consistency with others
	
	.if		(enable_restore)
	.if		(use_sprite_pagerestore)	
	move.w		d2,(a5)+
	move.w		d0,(a5)+
	.endif	
	.endif

	add.w		d0,a1
	
	; dst yoffset (line)
;	mulu.w		#linebytes,d2
	mulu.w		_g_linebytes,d2
	add.l		d2,a1
	
	; find destination word width of shifted sprite
;	moveq		#16-1,d6
	move.w		d6,d7

	and.w		d1,d6				; rshift
	move.l		d6,d1

	add.w		d3,d7				; w+round
;	add.w		spf_w(a0),d7			; w+round
;	move.w		spf_w(a0),d7			; w
;	add.w		#16-1,d7			; w+round

	moveq		#-16,d0	
	add.w		d7,d6				; rshift+w+round
	and.w		d0,d6				; wceil(rshift+w)


.AGT_BLiT_IMSprDrawUnClipped_NFSR_test:

	; NFSR test

;	or.b		#$40,$ffff8a3d.w		; BLIT: nfsr
	
	and.w		d7,d0
	sub.w		d6,d0
	beq		.AGT_BLiT_IMSprDrawUnClipped_EFSR


*-------------------------------------------------------*
.AGT_BLiT_IMSprDrawUnClipped_NFSR:
*-------------------------------------------------------*
	; NFSR
;	or.b		#$40,$ffff8a3d.w		; BLIT: nfsr
	or.b		#$40,.sp_skew(sp)

.AGT_BLiT_IMSprDrawUnClipped_EFSR:

	; dst skip adjust by dst width	
	lsr.w		#1,d6
	sub.w		d6,a4

	; final height
;	move.w		d4,d7				; h

	move.b		.sp_skew(sp),BLiTSKEW.w
	move.w		a3,BLiTSYI.w			; src yinc


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
	bne.s		.multi
	and.w		d2,d0
.multi:	
	move.w		d0,BLiTEM1.w	
;	moveq		#-1,d2
	move.w		d2,BLiTEM3.w
	
;	move.w		#-1,BLiTEM2.w
	
;	not.w		d0
;	move.w		d0,d3
;	swap		d0
;	move.w		d3,d0 
	
;	move.w		#-1,$ffff8a2a.w
;	move.w		#-1,$ffff8a2c.w

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
.ims_r_sylp:
	jmp		.ims_r_jt(pc,d6.w)
	rept		9
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	endr
.ims_r_jt:
	add.w		a4,a3
	dbra		d1,.ims_r_sylp

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

	lea		BLiTSRC.w,a3
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

	move.b		#1,BLiTLOP.w			; LOP = AND
	
;	plane #0

	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst
	move.b		d0,(a2)

	.if		(use_hog=0)
.mlp0:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.mlp0
	.endif
;	move.b		#$c0,(a2)


;	plane #1

	addq.l		#2,a1
	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	.if		(use_hog=0)
.mlp1:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.mlp1
	.endif
;	move.b		#$c0,(a2)

;	plane #2

	addq.l		#2,a1
	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	.if		(use_hog=0)
.mlp2:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.mlp2
	.endif
;	move.b		#$c0,(a2)

;	plane #3

	addq.l		#2,a1
	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	.if		(use_hog=0)
.mlp3:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.mlp3
	.endif
		
	subq.l		#6,a1

;	move.b		#$c0,(a2)
	
*-------------------------------------------------------*
*	colour
*-------------------------------------------------------*

	addq.l		#2,a0

	move.b		#7,BLiTLOP.w			; LOP = OR
	
;	plane #0

	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst
	move.b		d0,(a2)
	.if		(use_hog=0)
.wlp0:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wlp0
	.endif
;	move.b		#$c0,(a2)

;	plane #1

	addq.l		#2,a0
	addq.l		#2,a1
	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	.if		(use_hog=0)	
.wlp1:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wlp1
	.endif
;	move.b		#$c0,(a2)

;	plane #2

	addq.l		#2,a0
	addq.l		#2,a1
	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	.if		(use_hog=0)
.wlp2:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wlp2
	.endif
;	move.b		#$c0,(a2)

;	plane #3

	addq.l		#2,a0
	addq.l		#2,a1
	
	move.w		d4,(a5)				; BLIT: lines
	move.l		a0,(a3)				; src
	move.l		a1,(a6)				; dst	
	move.b		d0,(a2)
	.if		(use_hog=0)
.wlp3:	bset.b		d1,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.wlp3
	.endif	
;	move.b		#$c0,(a2)


		
*-------------------------------------------------------*
.ims_clip_out:
*-------------------------------------------------------*
.AGT_BLiT_IMSprDrawUnClipped_end:
*-------------------------------------------------------*

	popraster
