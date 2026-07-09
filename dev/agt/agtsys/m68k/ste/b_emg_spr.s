*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
* 	blitter sprite implementation
*	paths: EMSPRQ
*-------------------------------------------------------*
*	max source xsize: 32
*	max source ysize: viewport clipper ysize
*	x-clipping: guardband reject only
*	y-clipping: yes
*	clearing: yes/automatic
*	occlusionmaps: no
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_SYS_EMS_XCLIP
enable_x_clipping	set	1
	.else
enable_x_clipping	set	0
	.endif
	
	sprite_sc_frame	a0,d0,_AGT_Exception_EMSFrame_68k
	
	pushraster	#$062

;	move.w		#2,BLiTSYI.w

*-------------------------------------------------------*

	sprite_spf	a0,d0,d3,a0
	
	; frame fields

	move.w		(a0)+,d3			; w
	move.w		(a0)+,d4			; h
	add.w		(a0)+,d1			; +xo
	add.w		(a0)+,d2			; +yo

*-------------------------------------------------------*
*	early rejection
*-------------------------------------------------------*
	;
	; early reject if outside viewport
	;
	move.l		.sp_scissorwin(sp),a2
	vis_reject	a2,d1,d3,d0,d6,.BLiT_EMSpr_4BPL_end
	vis_reject	a2,d2,d4,d0,d6,.BLiT_EMSpr_4BPL_end

*-------------------------------------------------------*

	; skew
	moveq		#16-1,d6
	and.w		d1,d6


	; todo: store skew+nfsr data in preshift
	move.b		d6,.sp_skew(sp)

;	access preshifted mask frame
	add.w		d6,d6
	add.w		d6,d6
	move.l		spf_em_psmask-8(a0,d6.w),a6
;	wordwidth of source mask
	move.w		(a6)+,d5
	move.w		d5,.sp_maskwidth(sp)

	lea		12(a6),a6

;	access colour data
	move.w		.sp_cfield4(sp),d5
	move.l		spf_em_colour-8(a0,d5.w),a0

;	todo: relevant size & blitter fields should be stored with preshift
	move.w		(a0)+,d5

	; constants
	moveq		#16-1,d6
	moveq		#-16,d7


	.if		(enable_clipping)

*-------------------------------------------------------*
*	initial source skip (unclipped case)
*-------------------------------------------------------*

	.if		(enable_x_clipping)

	move.l		.sp_scissorwin(sp),a2
	lea		2.w,a3

	.else		; (enable_x_clipping)

	move.l		.sp_guardwin(sp),a2
	guard_reject	a2,d1,d3,d0,d5,.BLiT_EMSpr_4BPL_end
.rewind	set		(dctx_scissorwindow-dctx_guardwindow)-4
	lea		.rewind(a2),a2

;	move.l		.sp_guardwin(sp),a2
;	
;	cmp.w		clip_win_x(a2),d1
;	bmi		.BLiT_EMSpr_4BPL_end
;
;	move.w		d1,d0
;	add.w		d3,d0				; xr	
;	sub.w		clip_win_x2(a2),d0		; xr - cx2
;	bgt		.BLiT_EMSpr_4BPL_end
;
;	lea		dctx_scissorwindow-dctx_guardwindow(a2),a2
	
	.endif		; (enable_x_clipping)

*-------------------------------------------------------*
*	top edge
*-------------------------------------------------------*
	cmp.w		clip_win_y(a2),d2
	bpl.s		.clip_safe_top
*-------------------------------------------------------*
*	top clipped
*-------------------------------------------------------*
.clip_unsafe_top:
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_DEBUG_SHOWCLIPS
	move.b		#15,BLiTLOP.w
	.endif
	
	move.w		clip_win_y(a2),d5
	; relative to clipwindow
	sub.w		d5,d2
	; crop lines
	add.w		d2,d4
	ble		.BLiT_EMSpr_4BPL_end

	; adjust src offset in pixelwords*planes*yerr
	neg.w		d2
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0
	lsr.w		#1,d0				; *8
	mulu.w		d2,d0
	add.w		d0,a0				; +8*w*yerr

	; adjust mask offset in pixelwords*yerr
	move.w		.sp_maskwidth(sp),d5
	add.w		d5,d5
	mulu.w		d2,d5
	add.w		d5,a6

	; relative to world
	move.w		clip_win_y(a2),d2
	; ASSUMPTION: sprite is not big enough to cross top+bottom
	bra.s		.clip_safe_bot

*-------------------------------------------------------*
.clip_safe_top:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	bottom edge
*-------------------------------------------------------*
	move.w		d2,d0
	add.w		d4,d0				; xr
	sub.w		clip_win_y2(a2),d0			; xr - cx2
	ble.s		.clip_safe_bot
*-------------------------------------------------------*
*	bottom clipped
*-------------------------------------------------------*
.clip_unsafe_bot:
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_DEBUG_SHOWCLIPS
	move.b		#15,BLiTLOP.w
	.endif
	
	sub.w		d0,d4
	ble		.BLiT_EMSpr_4BPL_end
*-------------------------------------------------------*
.clip_safe_bot:
*-------------------------------------------------------*

	.if		(enable_x_clipping)

*-------------------------------------------------------*
*	left edge
*-------------------------------------------------------*
	; NISR: no initial source read (i.e. shiftregisters preloaded with 0)
	cmp.w		clip_win_x(a2),d1
	bpl.s		.clip_safe_left
*-------------------------------------------------------*
*	left clipped
*-------------------------------------------------------*
.clip_unsafe_left:
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_DEBUG_SHOWCLIPS
	move.b		#15,BLiTLOP.w
	.endif
	
	; FISR: force initial source read (i.e. shiftregisters preloaded with 1st data word)
	or.b		#$80,.sp_skew(sp)

	; relative to clipwindow
	sub.w		clip_win_x(a2),d1

	; adjust dest width (ignore FISR part)

	move.w		d3,d5

	add.w		d1,d3
	ble		.BLiT_EMSpr_4BPL_end

	; adjust src offset by clipped pixelwords (include FISR part)

	move.w		d6,d0
	sub.w		d1,d0
	and.w		d7,d0
	lsr.w		#4-1,d0				; w*2
	lea		-2(a0,d0.w),a0			; offset colour
	add.w		d0,a6				; offset mask

	; adjust colour src skip by ceil'd change in width
	; don't bother with same for mask, since EMs are hardcoded

	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0				; ceil new width
	add.w		d6,d5
	and.w		d7,d5				; ceil old width

	sub.w		d0,d5				; delta
	lsr.w		#4-1,d5
	lea		-2(a3,d5.w),a3			; adjust (+ FISR)

	; relative to world
	move.w		clip_win_x(a2),d1
	; ASSUMPTION: sprite is not big enough to cross left+right
	bra		.BLiT_EMSpr_Clip_4BPL_xclipped_go


*-------------------------------------------------------*
.clip_safe_left:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	right edge
*-------------------------------------------------------*
	move.w		d1,d0
	add.w		d3,d0				; xr
	sub.w		clip_win_x2(a2),d0			; xr - cx2
	ble		.clip_safe_right
*-------------------------------------------------------*
*	right clipped
*-------------------------------------------------------*
.clip_unsafe_right:
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_DEBUG_SHOWCLIPS
	move.b		#15,BLiTLOP.w
	.endif

	move.w		d3,d5

	; adjust pixel width
	sub.w		d0,d3
	ble		.BLiT_EMSpr_4BPL_end

	; adjust by ceil'd change in width
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0				; ceil new width
	add.w		d6,d5
	and.w		d7,d5				; ceil old width


	; adjust colour src skip by ceil'd change in width
	; don't bother with same for mask, since EMs are hardcoded

	sub.w		d0,d5				; delta
	lsr.w		#4-1,d5
	add.w		d5,a3				; adjust (no FISR)

;	bra		.BLiT_EMSpr_Clip_4BPL_xclipped_go


*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMSpr_Clip_4BPL_xclipped_go:
*-------------------------------------------------------*
;			.abs
*-------------------------------------------------------*
;.sp_skew:		ds.w	1
;.sp_maskwidth:		ds.w	1
;.sp_colrwidth:		ds.w	1
;.sp_maskptr:		ds.l	1
;.sp_frame_:
*-------------------------------------------------------*
;			.text
*-------------------------------------------------------*

;	bra		AGT_BLiT_EMSprDrawUnClipped_end

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		usp,a5
	move.l		a5,d5
	.endif
	.endif

	; dst pixelword xoffset
	moveq		#-16,d0
	and.w		d1,d0
	lsr.w		#1,d0		; real x clip doesn't allow -ve x

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
	; todo: worth the overhead? table pointer needs refetched...
	move.l		.sp_linetable(sp),a4
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a4,d2.w),a1


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


.NFSR_test:

	; NFSR test

	and.w		d7,d0
	sub.w		d6,d0
	beq.s		.EFSR2


*-------------------------------------------------------*
.NFSR2:
*-------------------------------------------------------*
	; NFSR
	or.b		#$40,.sp_skew(sp)

.EFSR2:


	; dst skip adjust by dst width
;	move.w		_g_linebytes,a4
	move.w		.sp_linebytes(sp),a4
	lsr.w		#1,d6				; *8
	sub.w		d6,a4

	moveq		#8+2,d0
	sub.w		d6,d0
;	move.w		d6,d0
;	neg.w		d0
;	add.w		#8+2,d0
	move.w		d0,BLiTDYI.w

	move.w		a3,BLiTSYI.w			; src yinc

	move.b		.sp_skew(sp),BLiTSKEW.w

	; word-width
	lsr.w		#(4-1),d6			; >>4 *2
	move.w		d6,BLiTXC.w			; BLIT: width

	.if		(enable_restore)
	.if		(use_sprite_pagerestore)

	add.w		d6,d6

	; clearing record

	move.l		a1,(a5)+			; clear addr
	move.w		a4,(a5)+			; dst skip
	move.w		d6,(a5)+			; word width*2
	move.w		d4,(a5)+			; lines

	.endif

	.if		(use_sprite_saverestore)

	add.w		d6,d6

	; clearing record

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
.sylp:	jmp		.jt(pc,d6.w)
	rept		9
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	endr
.jt:	add.w		a4,a3
	dbra		d1,.sylp

	.endif
	.endif

*-------------------------------------------------------*

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		d5,(a5)+
	move.l		a5,usp
	.endif
	.endif


*-------------------------------------------------------*
.BLiT_EMSpr_Clip_4BPL_xclipped_blit:
*-------------------------------------------------------*
;			.abs
*-------------------------------------------------------*
;.sp_skew:		ds.w	1
;.sp_maskwidth:		ds.w	1
;.sp_colrwidth:		ds.w	1
;.sp_maskptr:		ds.l	1
;.sp_frame_:
*-------------------------------------------------------*
;			.text
*-------------------------------------------------------*


	move.l		a0,BLiTSRC.w


*-------------------------------------------------------*

	lea		BLiTYC.w,a0
	lea		BLiTCTRL.w,a5
	lea		BLiTEM3.w,a4
	lea		BLiTDST.w,a3
	lea		BLiTEM1.w,a2
	moveq		#$c0,d1
	moveq		#4,d2
;	move.w		_g_linebytes,d3
	move.w		.sp_linebytes(sp),d3

*-------------------------------------------------------*

	moveq		#-2,d0
	add.w		.sp_maskwidth(sp),d0
	beq		.xc_dbl_word


*-------------------------------------------------------*
.xc_tri_word_b:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0

	add.w		d0,d0				; *2
	move.w		d0,d5
	lsl.w		#3,d5				; *16
	sub.w		d5,d0				; *-14
	lsr.w		#3,d4	
	
	jmp		.j3b0(pc,d0.w)
	
;	add.w		d0,d0
;	lsr.w		#3,d4
;	jmp		.j3bt(pc,d0.w)
;.j3bt:	bra.s		.j3b0
;	bra.s		.j3b1
;	bra.s		.j3b2
;	bra.s		.j3b3
;	bra.s		.j3b4
;	bra.s		.j3b5
;	bra.s		.j3b6
;	bra.s		.j3b7

.j3b8:	scan_32wc		
.j3b7:	scan_32wc		
.j3b6:	scan_32wc		
.j3b5:	scan_32wc		
.j3b4:	scan_32wc		
.j3b3:	scan_32wc		
.j3b2:	scan_32wc		
.j3b1:	scan_32wc		
.j3b0:	dbra		d4,.j3b8

	; reset constant SYI for sake of unclipped faspath
	move.w		#2,BLiTSYI.w

	bra		.BLiT_EMSpr_4BPL_end

*-------------------------------------------------------*
.xc_dbl_word:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.j2t(pc,d0.w)
.j2t:	bra.s		.j20
	bra.s		.j21
	bra.s		.j22
	bra.s		.j23
	bra.s		.j24
	bra.s		.j25
	bra.s		.j26
	bra.s		.j27

.j28:	scan_2wc
.j27:	scan_2wc
.j26:	scan_2wc
.j25:	scan_2wc
.j24:	scan_2wc
.j23:	scan_2wc
.j22:	scan_2wc
.j21:	scan_2wc
.j20:	dbra		d4,.j28

	; reset constant SYI for sake of unclipped faspath
	move.w		#2,BLiTSYI.w

	bra		.BLiT_EMSpr_4BPL_end


*-------------------------------------------------------*
.clip_safe_right:
*-------------------------------------------------------*

	.endif		; enable_x_clipping

	.endif		; enable_clipping


*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMSpr_Clip_4BPL_unclipped_got:
*-------------------------------------------------------*

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		usp,a5
	move.l		a5,d5
	.endif
	.endif

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
	; todo: just use playfield linetable here
;	mulu.w		#linebytes,d2
;	mulu.w		_g_linebytes,d2
;	add.l		d2,a1

	move.l		.sp_linetable(sp),a4
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a4,d2.w),a1

	; find destination word width of shifted sprite

	move.w		d6,d7

	and.w		d1,d6				; rshift

	add.w		d3,d7				; w+round

	moveq		#-16,d0
	add.w		d7,d6				; rshift+w+round
	and.w		d0,d6				; wceil(rshift+w)


	; NFSR test
	; note: this and some other values could be looked up from ((x&15):srcw)

	and.w		d7,d0
	sub.w		d6,d0
	beq.s		.EFSR
.NFSR:	or.b		#$40,.sp_skew(sp)
.EFSR:

	; dst skip adjust by dst width
	;move.w		_g_linebytes,a4
	move.w		.sp_linebytes(sp),a4
	lsr.w		#1,d6
	sub.w		d6,a4

	moveq		#8+2,d0
	sub.w		d6,d0
	move.w		d0,BLiTDYI.w

;	move.w		a3,BLiTSYI.w

	move.b		.sp_skew(sp),BLiTSKEW.w


	; word-width
	lsr.w		#(4-1),d6			; >>4 *2
	move.w		d6,BLiTXC.w			; BLIT: width



	.if		(enable_restore)
	.if		(use_sprite_pagerestore)

	add.w		d6,d6

	; clearing record

	move.l		a1,(a5)+			; clear addr
	move.w		a4,(a5)+			; dst skip
	move.w		d6,(a5)+			; word width*2
	move.w		d4,(a5)+			; lines

	.endif

	.if		(use_sprite_saverestore)

	add.w		d6,d6

	; clearing record

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
.sylp:	jmp		.jt(pc,d6.w)
	rept		9
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	endr
.jt:	add.w		a4,a3
	dbra		d1,.sylp

	.endif
	.endif

*-------------------------------------------------------*

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		d5,(a5)+
	move.l		a5,usp
	.endif
	.endif


*-------------------------------------------------------*
.BLiT_EMSpr_Clip_4BPL_unclipped_blit:
*-------------------------------------------------------*

	move.l		a0,BLiTSRC.w

*-------------------------------------------------------*

	lea		BLiTYC.w,a0
	lea		BLiTCTRL.w,a5
	lea		BLiTEM3.w,a4
	lea		BLiTDST.w,a3
	lea		BLiTEM1.w,a2
	moveq		#$c0,d1
	moveq		#4,d2
	;move.w		_g_linebytes,d3
	move.w		.sp_linebytes(sp),d3

*-------------------------------------------------------*

	moveq		#-1,d0
	add.w		.sp_maskwidth(sp),d0
	beq		.uc_sgl_word
	subq.w		#1,d0
	beq		.uc_dbl_word

*-------------------------------------------------------*
.uc_tri_word:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.ja3t(pc,d0.w)
.ja3t:	bra.s		.ja3s
	bra.s		.ja31
	bra.s		.ja32
	bra.s		.ja33
	bra.s		.ja34
	bra.s		.ja35
	bra.s		.ja36
	bra.s		.ja37


	; 12bytes
.ja3s:	subq.w		#1,d4
.ja38:	scan_3w
.ja37:	scan_3w
.ja36:	scan_3w
.ja35:	scan_3w
.ja34:	scan_3w
.ja33:	scan_3w
.ja32:	scan_3w
.ja31:	scan_3w
.ja30:	dbra		d4,.ja38

	bra		.BLiT_EMSpr_4BPL_end

*-------------------------------------------------------*
.uc_dbl_word:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jb2t(pc,d0.w)
.jb2t:	bra.s		.jb2s
	bra.s		.jb21
	bra.s		.jb22
	bra.s		.jb23
	bra.s		.jb24
	bra.s		.jb25
	bra.s		.jb26
	bra.s		.jb27

	; 12bytes
.jb2s:	subq.w		#1,d4
.jb28:	scan_2w
.jb27:	scan_2w
.jb26:	scan_2w
.jb25:	scan_2w
.jb24:	scan_2w
.jb23:	scan_2w
.jb22:	scan_2w
.jb21:	scan_2w
.jb20:	dbra		d4,.jb28

	bra		.BLiT_EMSpr_4BPL_end

*-------------------------------------------------------*
.uc_sgl_word:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jc1t(pc,d0.w)
.jc1t:	bra.s		.jc1s
	bra.s		.jc11
	bra.s		.jc12
	bra.s		.jc13
	bra.s		.jc14
	bra.s		.jc15
	bra.s		.jc16
	bra.s		.jc17

	; 10bytes
.jc1s:	subq.w		#1,d4
.jc18:	scan_1w
.jc17:	scan_1w
.jc16:	scan_1w
.jc15:	scan_1w
.jc14:	scan_1w
.jc13:	scan_1w
.jc12:	scan_1w
.jc11:	scan_1w
.jc10:	dbra		d4,.jc18

;	bra		.BLiT_EMSpr_4BPL_end

*-------------------------------------------------------*
.BLiT_EMSpr_4BPL_end:
*-------------------------------------------------------*

	popraster
