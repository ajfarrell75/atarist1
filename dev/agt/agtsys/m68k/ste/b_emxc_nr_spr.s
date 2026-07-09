*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	compound blitter gensprite implementation
* 	path: EMXSPRUR
*-------------------------------------------------------*
*	max source xsize: -
*	max source ysize: viewport clipper ysize
*	x-clipping: guardband reject only
*	y-clipping: guardband reject / embedded EMS
*	clearing: no/custom/user-provided
*	occlusionmaps: no
*-------------------------------------------------------*

	sprite_sc_frame	a0,d0,_AGT_Exception_EMXFrame_68k
			
	pushraster	#$062

*-------------------------------------------------------*

	sprite_spf	a0,d0,d3,a0
				
	; frame fields

	move.w		(a0)+,d3			; w
	move.w		(a0)+,d4			; h
	add.w		(a0)+,d1			; +xo
	add.w		(a0)+,d2			; +yo
			
*-------------------------------------------------------*
*	early accept/reject vs guardband
*-------------------------------------------------------*
	;
	; early x-reject if crossing x-guard
	;
	move.l		.sp_guardwin(sp),a2
	guard_reject	a2,d1,d3,d0,d6,.BLiT_EMXNR_4BPL_end
	;
	; early y-accept if not crossing y-guard
	;
	guard_accept	a2,d2,d4,d5,d6,.BLiT_EMXNR_NoClip_4BPL_go
		
*-------------------------------------------------------*
*	y-clip (EMS) or y-guard
*-------------------------------------------------------*

	; if we don't have embedded EMS colour data...
	
	move.w		.sp_cfield4(sp),d6
	move.l		spf_emx_colour-8(a0,d6.w),d0
	
	; ...we can't y-clip and can't draw here so we bail
	
	beq		.BLiT_EMXNR_4BPL_end
		
	; otherwise proceed with viewport y-clip
	
*-------------------------------------------------------*
*	sprite subject to y-clipping
*-------------------------------------------------------*
.BLiT_EMXNR_YC:
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_DEBUG_SHOWCLIPS
	move.b		#15,BLiTLOP.w
	.endif

	; skew
	moveq		#16-1,d6
	and.w		d1,d6

;	access preshifted mask frame
	add.w		d6,d6
	add.w		d6,d6
	move.l		spf_emx_psmask-8(a0,d6.w),a6
	
;	access colour data
	move.l		d0,a0
	
.BLiT_EMXNR_YC_firstcmp:
	move.l		a6,d6
;	wordwidth of source mask
	move.w		(a6)+,d5			; spf_psm_wordwidth
	move.w		d5,.sp_maskwidth(sp)
	move.w		d5,BLiTXC.w
	move.w		(a6)+,d0
	move.b		d0,BLiTSKEW.w	;sp_skew(sp)
	move.w		(a6)+,BLiTSYI.w
	move.w		(a6)+,BLiTDYI.w
	addq		#2,a6
;	move.w		(a6)+,d0

	; link to next component, if present
	move.l		(a6)+,d7
	beq.s		.BLiT_EMXNR_YC_onlypsm
	add.l		d6,d7
.BLiT_EMXNR_YC_onlypsm:
	move.l		d7,.emxnryc_next_psmask			; spf_psm_nextcmp



;	access colour data
;	move.l		spf_emx_colour-8(a0),a0

;	word-width for this preshift (will be 1, 2 or 3 for normal EMS, more for wide format)
;	todo: relevant size & blitter fields e.g. BLiTSYI could be stored with frame
	move.w		(a0)+,d5			; ***** unused *****


	move.l		a0,.emxnryc_cmp_cplanes
	clr.w		.emxnryc_cmp_masklineoff
	
	; constants
	moveq		#16-1,d6
	moveq		#-16,d7

*-------------------------------------------------------*
*	initial source skip (unclipped case)
*-------------------------------------------------------*
	lea		2.w,a3


	.if		(enable_clipping)

	move.l		.sp_scissorwin(sp),a2

*-------------------------------------------------------*
*	top edge
*-------------------------------------------------------*
	cmp.w		clip_win_y(a2),d2
	bpl.s		.BLiT_EMXNR_YC_safetop
*-------------------------------------------------------*
*	top clipped
*-------------------------------------------------------*
.BLiT_EMXNR_YC_unsafetop:
*-------------------------------------------------------*
	move.w		clip_win_y(a2),d5
	; relative to clipwindow
	sub.w		d5,d2
	; crop lines
	add.w		d2,d4
	ble		.BLiT_EMXNR_YC_end_all

	; adjust src offset in pixelwords*planes*yerr
	neg.w		d2
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0
	lsr.w		#1,d0				; *8
	mulu.w		d2,d0
	add.w		d0,a0				; +8*w*yerr

	move.w		d2,.emxnryc_cmp_masklineoff

	; relative to world
	move.w		clip_win_y(a2),d2
	; ASSUMPTION: sprite is not big enough to cross top+bottom
;	bra.s		.BLiT_EMXNR_YC_safebot

*-------------------------------------------------------*
.BLiT_EMXNR_YC_safetop:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	bottom edge
*-------------------------------------------------------*
	move.w		d2,d0
	add.w		d4,d0				; xr
	sub.w		clip_win_y2(a2),d0		; xr - cx2
	ble.s		.BLiT_EMXNR_YC_safebot
*-------------------------------------------------------*
*	bottom clipped
*-------------------------------------------------------*
.BLiT_EMXNR_YC_unsafebot:
*-------------------------------------------------------*
	sub.w		d0,d4
	ble		.BLiT_EMXNR_YC_end_all
*-------------------------------------------------------*
.BLiT_EMXNR_YC_safebot:
*-------------------------------------------------------*

	.endif		; enable_clipping


*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMXNR_YC_unclipped_got:
*-------------------------------------------------------*

	; dst pixelword xoffset
	moveq		#-16,d0
	and.w		d1,d0
;	lsr.w		#1,d0
	asr.w		#1,d0		; guardband allows -ve x!
	
	add.w		d0,a1

	; dst yoffset (line)
	; todo: just use playfield linetable here
;	mulu.w		#linebytes,d2
;	mulu.w		_g_linebytes,d2
;	add.l		d2,a1

	; address yline	
	move.l		.sp_linetable(sp),a4
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a4,d2.w),a1	; 38c vs 56c for muls (28c excl. linetable fetch)
	
	; find destination word width of whole sprite, all composites
	; to produce a single sprite restore rectangle
	
	move.w		d6,d7
	and.w		d1,d6				; rshift/skew
	add.w		d3,d7				; w+round
	moveq		#-16,d0
	add.w		d7,d6				; rshift+w+round
	and.w		d0,d6				; wceil(rshift+w)

	; dst skip adjust by dst width
	;move.w		_g_linebytes,a4
	move.w		.sp_linebytes(sp),a4
	lsr.w		#1,d6
	sub.w		d6,a4
	lsr.w		#(4-1),d6		; >>4 *2


	move.l		a0,.emxnryc_cmp_src
	move.l		a1,.emxnryc_cmp_dst
	move.w		d4,.emxnryc_cmp_h
	move.w		.sp_maskwidth(sp),d6

*-------------------------------------------------------*
.BLiT_EMXNR_YC_unclipped_blit:
*-------------------------------------------------------*

	move.w		.emxnryc_cmp_masklineoff,d0
	beq.s		.BLiT_EMXNR_YC_noyi
	move.w		d6,d5
	add.w		d5,d5
	mulu.w		.emxnryc_cmp_masklineoff,d5
	add.w		d5,a6
.BLiT_EMXNR_YC_noyi:

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
	add.w		d6,d0
	beq		.BLiT_EMXNR_YC_sgl
	subq.w		#1,d0
	beq		.BLiT_EMXNR_YC_dbl

*-------------------------------------------------------*
.BLiT_EMXNR_YC_tri:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jxn3t(pc,d0.w)
.jxn3t:	bra.s		.jxn3s
	bra.s		.jxn31
	bra.s		.jxn32
	bra.s		.jxn33
	bra.s		.jxn34
	bra.s		.jxn35
	bra.s		.jxn36
	bra.s		.jxn37


	; 12bytes
.jxn3s:	subq.w		#1,d4
.jxn38:	scan_3w
.jxn37:	scan_3w
.jxn36:	scan_3w
.jxn35:	scan_3w
.jxn34:	scan_3w
.jxn33:	scan_3w
.jxn32:	scan_3w
.jxn31:	scan_3w
.jxn30:	dbra		d4,.jxn38

	bra		.BLiT_EMXNR_YC_end

*-------------------------------------------------------*
.BLiT_EMXNR_YC_dbl:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jyn2t(pc,d0.w)
.jyn2t:	bra.s		.jyn2s
	bra.s		.jyn21
	bra.s		.jyn22
	bra.s		.jyn23
	bra.s		.jyn24
	bra.s		.jyn25
	bra.s		.jyn26
	bra.s		.jyn27

	; 12bytes
.jyn2s:	subq.w		#1,d4
.jyn28:	scan_2w
.jyn27:	scan_2w
.jyn26:	scan_2w
.jyn25:	scan_2w
.jyn24:	scan_2w
.jyn23:	scan_2w
.jyn22:	scan_2w
.jyn21:	scan_2w
.jyn20:	dbra		d4,.jyn28

	bra		.BLiT_EMXNR_YC_end

*-------------------------------------------------------*
.BLiT_EMXNR_YC_sgl:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jzn1t(pc,d0.w)
.jzn1t:	bra.s		.jzn1s
	bra.s		.jzn11
	bra.s		.jzn12
	bra.s		.jzn13
	bra.s		.jzn14
	bra.s		.jzn15
	bra.s		.jzn16
	bra.s		.jzn17

	; 10bytes
.jzn1s:	subq.w		#1,d4
.jzn18:	scan_1w
.jzn17:	scan_1w
.jzn16:	scan_1w
.jzn15:	scan_1w
.jzn14:	scan_1w
.jzn13:	scan_1w
.jzn12:	scan_1w
.jzn11:	scan_1w
.jzn10:	dbra		d4,.jzn18

;	bra		.BLiT_EMXNR_YC_end

*-------------------------------------------------------*
.BLiT_EMXNR_YC_end:
*-------------------------------------------------------*

	move.l		.emxnryc_next_psmask,d0
	beq		.BLiT_EMXNR_YC_end_all
	move.l		d0,a6

	move.l		.emxnryc_cmp_cplanes,a0		; colour planes
	
	move.l		a6,d6
;	wordwidth of source mask
	move.w		(a6)+,d5		; spf_psm_wordwidth
	move.w		d5,.sp_maskwidth(sp)
	move.w		d5,BLiTXC.w		; xcount
	move.w		(a6)+,d0		; skew
	move.b		d0,BLiTSKEW.w
	move.w		(a6)+,BLiTSYI.w		; syinc
	move.w		(a6)+,BLiTDYI.w		; dyinc

	move.l		.emxnryc_cmp_src,a0
	move.l		.emxnryc_cmp_dst,a1

	move.w		(a6)+,d0		; wordoffset
	add.w		d0,d0
	add.w		d0,a0
	subq		#2,a0
	add.w		d0,d0
	add.w		d0,d0
	add.w		d0,a1

	; link to next component, if present
	move.l		(a6)+,d7
	beq.s		.BLiT_EMXNR_YC_lastpsm
	add.l		d6,d7
.BLiT_EMXNR_YC_lastpsm:
	move.l		d7,.emxnryc_next_psmask			; spf_psm_nextcmp

*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMXNR_YC_unclipped_got2:
*-------------------------------------------------------*

	move.w		.emxnryc_cmp_h,d4
	move.w		.sp_maskwidth(sp),d6

	altraster	#$021
	
	bra		.BLiT_EMXNR_YC_unclipped_blit

.emxnryc_cmp_src:
	ds.l		1
.emxnryc_cmp_dst:
	ds.l		1	
.emxnryc_cmp_h:
	ds.w		1
.emxnryc_cmp_masklineoff:
	ds.w		1
.emxnryc_cmp_cplanes:
	ds.l		1
.emxnryc_next_psmask:
	ds.l		1
	
.BLiT_EMXNR_YC_end_all:

	;move.w		#2,BLiTSYI.w	; reset for default 32w EMS/EMX

	bra		.BLiT_EMXNR_4BPL_end





	.if		(0)

*-------------------------------------------------------*
*	Entrypoint for EMXSPR with cull-clipping only	
*-------------------------------------------------------*
.BLiT_EMXNR_NoClip_4BPL:
*-------------------------------------------------------*

	sprite_spf	a0,d0,d3,a0
	
	; frame fields

	move.w		(a0)+,d3			; w
	move.w		(a0)+,d4			; h
	add.w		(a0)+,d1			; +xo
	add.w		(a0)+,d2			; +yo
			
*-------------------------------------------------------*
*	initial source skip (unclipped case)
*-------------------------------------------------------*

	.if		(enable_clipping)
	
	move.l		.sp_guardwin(sp),a2
	guard_reject	a2,d1,d3,d0,d6,.BLiT_EMXNR_4BPL_end
	guard_reject	a2,d2,d4,d0,d6,.BLiT_EMXNR_4BPL_end
	
	.endif		;(enable_clipping)
	
	.endif		; (0)

*-------------------------------------------------------*
.BLiT_EMXNR_NoClip_4BPL_go:
*-------------------------------------------------------*

	setraster	#$262

	; skew
	moveq		#16-1,d6
	and.w		d1,d6
	
;	access preshifted maskopt frame
	add.w		d6,d6
	add.w		d6,d6

	move.l		spf_emx_psmaskopt-8(a0,d6.w),a6

;	access colouropt data
	move.w		.sp_cfield4(sp),d6
	move.l		spf_emx_colouropt-8(a0,d6.w),a2

	move.l		a2,BLiTSRC.w
	
	lea		spf_emx_psm_nextcmp+4(a6),a6

	; find destination word width of whole sprite, all composites
	; to produce a single sprite restore rectangle
	
	moveq		#16-1,d6
	
	move.w		d6,d7
	and.w		d1,d6				; rshift/skew
	add.w		d3,d7				; w+round
	moveq		#-16,d0
	add.w		d7,d6				; rshift+w+round
	and.w		d0,d6				; wceil(rshift+w)

	; dst skip adjust by dst width
	;move.w		_g_linebytes,a4
	move.w		.sp_linebytes(sp),a4
	lsr.w		#1,d6
	sub.w		d6,a4
	lsr.w		#(4-1),d6		; >>4 *2
	add.w		d6,d6

	; dst pixelword xoffset
	moveq		#-16,d0
	and.w		d1,d0
	asr.w		#1,d0		; guardband allows -ve x!
	add.w		d0,a1

	; address yline	
	move.l		.sp_linetable(sp),a3
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a3,d2.w),a1	; 38c vs 56c for muls (28c excl. linetable fetch)
	
	lea		$00ff8a38,a0
	lea		$00ff8a28,a2
	lea		$ffff8a32.w,a3
	lea		4(a2),a4
	;lea		$00ff8a2c,a4
	lea		$ffff8a3c.w,a5
	move.l		#$ff000004,d2
	move.l		#$00ffc000,d1

;	IN:
;
;	d0		-
;	d1		$00ffc000
;	d2		$FF000004
;	d3		-
;	d4		-
;	d5		-
;	d6		-
;	d7		-
;	a0		BLiTYC		: $00ff8a38
;	a1		dst
;	a2		BLiTEM1		; $00ff8a28
;	a3		BLiTDST		; $ffff8a32
;	a4		BLiTEM3		; $00ff8a2c
;	a5		BLiTCTRL	; $ffff8a3c
;	a6		-
	
	jsr		(a6)
		
*-------------------------------------------------------*
.BLiT_EMXNR_4BPL_end:
*-------------------------------------------------------*

	popraster

