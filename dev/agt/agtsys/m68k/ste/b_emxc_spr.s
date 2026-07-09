*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	compound blitter gensprite implementation
* 	paths: EMXSPR
*-------------------------------------------------------*
*	max source xsize: -
*	max source ysize: viewport clipper ysize
*	x-clipping: guardband reject only
*	y-clipping: guardband reject / embedded EMS
*	clearing: yes/automatic
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
*	early rejection, pre-OCM (OCM cannot clip)
*-------------------------------------------------------*
	;
	; early x-reject if crossing x-guard
	;
	move.l		.sp_guardwin(sp),a2
	guard_reject	a2,d1,d3,d0,d6,.BLiT_EMXCSpr_4BPL_end

*-------------------------------------------------------*
*	EntityDrawFlag_NORESTORE
*-------------------------------------------------------*
	btst		#14,d7
	bne.s		.emxc_shortcut_bgrestore
*-------------------------------------------------------*
*	occlusion mapping	
*-------------------------------------------------------*
	move.w		(a0),d0
	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.w		d0,.emxc_update_prestore1+2
	move.w		d0,.emxc_update_prestore2+2
	.endif
	.endif
	beq.s		.emxc_no_occlusion
*-------------------------------------------------------*

	;
	; early y-reject if crossing y-guard (shortcuts OCM)
	;
	guard_reject	a2,d2,d4,d5,d6,.BLiT_EMXCSpr_4BPL_end
	
*-------------------------------------------------------*
	move.l		a0,-(sp)
	lea		(a0,d0.w),a0		
	jsr		_OCM_occlusion_add_T				
	move.l		(sp)+,a0
*-------------------------------------------------------*	
.emxc_shortcut_bgrestore:
*-------------------------------------------------------*	
	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	moveq		#-bgrframe_size,d0
	move.w		d0,.emxc_update_prestore1+2
	move.w		d0,.emxc_update_prestore2+2
	.endif
	.endif
*-------------------------------------------------------*
	;
	; OCM means no guard cross, so proceed directly to draw
	;
	bra		.BLiT_EMXCSpr_NoClip_4BPL_go

*-------------------------------------------------------*
.emxc_no_occlusion:			
*-------------------------------------------------------*

	.if		(enable_clipping)

	; if we have embedded EMS colour data, can proceed 
	; directly with y-clip, ignoring guardband
	
	move.w		.sp_cfield4(sp),d6
	move.l		spf_emx_colour-8(a0,d6.w),d0
	bne.s		.BLiT_EMXCSpr_YC

	.endif		; (enable_clipping)


	; otherwise we can't clip this primitive:
	;
	; accept within y-guard and proceed directly to draw
	;
	move.l		.sp_guardwin(sp),a2
	lea		clip_win_y(a2),a2
	guard_accept	a2,d2,d4,d5,d6,.BLiT_EMXCSpr_NoClip_4BPL_go
	;
	; ...otherwise bail
	;
	bra		.BLiT_EMXCSpr_4BPL_end

*-------------------------------------------------------*
*	sprite subject to y-clipping
*-------------------------------------------------------*
.BLiT_EMXCSpr_YC:
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
	
.BLiT_EMXCSpr_YC_firstcmp:
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
	beq.s		.BLiT_EMXCSpr_YC_onlypsm
	add.l		d6,d7
.BLiT_EMXCSpr_YC_onlypsm:
	move.l		d7,.emxyc_next_psmask			; spf_psm_nextcmp



;	access colour data
;	move.l		spf_emx_colour-8(a0),a0

;	word-width for this preshift (will be 1, 2 or 3 for normal EMS, more for wide format)
;	todo: relevant size & blitter fields e.g. BLiTSYI could be stored with frame
	move.w		(a0)+,d5			; ***** unused *****


	move.l		a0,.emxyc_cmp_cplanes
	clr.w		.emxyc_cmp_masklineoff
	
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
	bpl.s		.BLiT_EMXCSpr_YC_safetop
*-------------------------------------------------------*
*	top clipped
*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_unsafetop:
*-------------------------------------------------------*
	move.w		clip_win_y(a2),d5
	; relative to clipwindow
	sub.w		d5,d2
	; crop lines
	add.w		d2,d4
	ble		.BLiT_EMXCSpr_YC_end_all

	; adjust src offset in pixelwords*planes*yerr
	neg.w		d2
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0
	lsr.w		#1,d0				; *8
	mulu.w		d2,d0
	add.w		d0,a0				; +8*w*yerr

	move.w		d2,.emxyc_cmp_masklineoff

	; relative to world
	move.w		clip_win_y(a2),d2
	; ASSUMPTION: sprite is not big enough to cross top+bottom
;	bra.s		.BLiT_EMXCSpr_YC_safebot

*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_safetop:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	bottom edge
*-------------------------------------------------------*
	move.w		d2,d0
	add.w		d4,d0				; xr
	sub.w		clip_win_y2(a2),d0			; xr - cx2
	ble.s		.BLiT_EMXCSpr_YC_safebot
*-------------------------------------------------------*
*	bottom clipped
*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_unsafebot:
*-------------------------------------------------------*
	sub.w		d0,d4
	ble		.BLiT_EMXCSpr_YC_end_all
*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_safebot:
*-------------------------------------------------------*

	.endif		; enable_clipping


*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_unclipped_got:
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
	.endif		;(use_sprite_pagerestore)
	.endif		;(enable_restore)

	add.w		d0,a1

	; address yline	
	move.l		.sp_linetable(sp),a4
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a4,d2.w),a1			; 38c vs 56c for muls (28c excl. linetable fetch)

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



	.if		(enable_restore)
	.if		(use_sprite_pagerestore)

	add.w		d6,d6

	; clearing record

	move.l		a1,(a5)+			; clear addr
	move.w		a4,(a5)+			; dst skip
	move.w		d6,(a5)+			; word width*2
	move.w		d4,(a5)+			; lines

	.endif		;(use_sprite_pagerestore)

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
.BLiT_EMXCSpr_YC_sylp
	jmp		.BLiT_EMXCSpr_YC_jt(pc,d6.w)
	rept		9
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	endr
.BLiT_EMXCSpr_YC_jt:
	add.w		a4,a3
	dbra		d1,.BLiT_EMXCSpr_YC_sylp

	.endif		;(use_sprite_saverestore)
	.endif		;(enable_restore)

*-------------------------------------------------------*

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		d5,(a5)+
.emxc_update_prestore1:
	lea		$1234(a5),a5
	move.l		a5,usp	
	.endif		;(use_sprite_saverestore|use_sprite_pagerestore)
	.endif		;(enable_restore)
		

	move.l		a0,.emxyc_cmp_src
	move.l		a1,.emxyc_cmp_dst
	move.w		d4,.emxyc_cmp_h
	move.w		.sp_maskwidth(sp),d6

*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_unclipped_blit:
*-------------------------------------------------------*

	move.w		.emxyc_cmp_masklineoff,d0
	beq.s		.BLiT_EMXCSpr_YC_noyi
	move.w		d6,d5
	add.w		d5,d5
	mulu.w		.emxyc_cmp_masklineoff,d5
	add.w		d5,a6
.BLiT_EMXCSpr_YC_noyi:

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
	beq		.BLiT_EMXCSpr_YC_sgl
	subq.w		#1,d0
	beq		.BLiT_EMXCSpr_YC_dbl

*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_tri:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jx3t(pc,d0.w)
.jx3t:	bra.s		.jx3s
	bra.s		.jx31
	bra.s		.jx32
	bra.s		.jx33
	bra.s		.jx34
	bra.s		.jx35
	bra.s		.jx36
	bra.s		.jx37


	; 12bytes
.jx3s:	subq.w		#1,d4
.jx38:	scan_3w
.jx37:	scan_3w
.jx36:	scan_3w
.jx35:	scan_3w
.jx34:	scan_3w
.jx33:	scan_3w
.jx32:	scan_3w
.jx31:	scan_3w
.jx30:	dbra		d4,.jx38

	bra		.BLiT_EMXCSpr_YC_end

*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_dbl:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jy2t(pc,d0.w)
.jy2t:	bra.s		.jy2s
	bra.s		.jy21
	bra.s		.jy22
	bra.s		.jy23
	bra.s		.jy24
	bra.s		.jy25
	bra.s		.jy26
	bra.s		.jy27

	; 12bytes
.jy2s:	subq.w		#1,d4
.jy28:	scan_2w
.jy27:	scan_2w
.jy26:	scan_2w
.jy25:	scan_2w
.jy24:	scan_2w
.jy23:	scan_2w
.jy22:	scan_2w
.jy21:	scan_2w
.jy20:	dbra		d4,.jy28

	bra		.BLiT_EMXCSpr_YC_end

*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_sgl:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jz1t(pc,d0.w)
.jz1t:	bra.s		.jz1s
	bra.s		.jz11
	bra.s		.jz12
	bra.s		.jz13
	bra.s		.jz14
	bra.s		.jz15
	bra.s		.jz16
	bra.s		.jz17

	; 10bytes
.jz1s:	subq.w		#1,d4
.jz18:	scan_1w
.jz17:	scan_1w
.jz16:	scan_1w
.jz15:	scan_1w
.jz14:	scan_1w
.jz13:	scan_1w
.jz12:	scan_1w
.jz11:	scan_1w
.jz10:	dbra		d4,.jz18

;	bra		.BLiT_EMXCSpr_YC_end

*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_end:
*-------------------------------------------------------*

	move.l		.emxyc_next_psmask,d0
	beq		.BLiT_EMXCSpr_YC_end_all
	move.l		d0,a6

	move.l		.emxyc_cmp_cplanes,a0		; colour planes
	
	move.l		a6,d6
;	wordwidth of source mask
	move.w		(a6)+,d5		; spf_psm_wordwidth
	move.w		d5,.sp_maskwidth(sp)
	move.w		d5,BLiTXC.w		; xcount
	move.w		(a6)+,d0		; skew
	move.b		d0,BLiTSKEW.w
	move.w		(a6)+,BLiTSYI.w		; syinc
	move.w		(a6)+,BLiTDYI.w		; dyinc

	move.l		.emxyc_cmp_src,a0
	move.l		.emxyc_cmp_dst,a1

	move.w		(a6)+,d0		; wordoffset
	add.w		d0,d0
	add.w		d0,a0
	subq		#2,a0
	add.w		d0,d0
	add.w		d0,d0
	add.w		d0,a1

	; link to next component, if present
	move.l		(a6)+,d7
	beq.s		.BLiT_EMXCSpr_YC_lastpsm
	add.l		d6,d7
.BLiT_EMXCSpr_YC_lastpsm:
	move.l		d7,.emxyc_next_psmask			; spf_psm_nextcmp

*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMXCSpr_YC_unclipped_got2:
*-------------------------------------------------------*

	move.w		.emxyc_cmp_h,d4
	move.w		.sp_maskwidth(sp),d6

	altraster	#$021
	
	bra		.BLiT_EMXCSpr_YC_unclipped_blit

.emxyc_cmp_src:
	ds.l		1
.emxyc_cmp_dst:
	ds.l		1	
.emxyc_cmp_h:
	ds.w		1
.emxyc_cmp_masklineoff:
	ds.w		1
.emxyc_cmp_cplanes:
	ds.l		1
.emxyc_next_psmask:
	ds.l		1
	
.BLiT_EMXCSpr_YC_end_all:

	;move.w		#2,BLiTSYI.w	; reset for default 32w EMS/EMX

	bra		.BLiT_EMXCSpr_4BPL_end





	.if		(0)

*-------------------------------------------------------*
*	Entrypoint for EMXSPR with cull-clipping only	
*-------------------------------------------------------*
.BLiT_EMXCSpr_NoClip_4BPL:
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
	guard_reject	a2,d1,d3,d0,d6,.BLiT_EMXCSpr_4BPL_end
	guard_reject	a2,d2,d4,d0,d6,.BLiT_EMXCSpr_4BPL_end
		
	.endif		;(enable_clipping)
	
	.endif		; (0)

*-------------------------------------------------------*
.BLiT_EMXCSpr_NoClip_4BPL_go:
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

	.if		(enable_restore&use_sprite_pagerestore)

	move.l		usp,a5
	move.l		a5,d5
	
	move.w		d2,(a5)+			; liney
	move.w		d0,(a5)+			; xoffbytes

	.endif		;(enable_restore&use_sprite_pagerestore)

	; address yline	
	move.l		.sp_linetable(sp),a3
	add.w		d2,d2
	add.w		d2,d2
	add.l		(a3,d2.w),a1			; 38c vs 56c for muls (28c excl. linetable fetch)

	.if		(enable_restore&use_sprite_pagerestore)
	
	move.l		a1,(a5)+			; clear addr
		
	move.w		a4,(a5)+
	move.w		d6,(a5)+	
	move.w		d4,(a5)+
	move.l		d5,(a5)+	
.emxc_update_prestore2:
	lea		$1234(a5),a5
	move.l		a5,usp	

	.endif		;(enable_restore&use_sprite_pagerestore)
	
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
.BLiT_EMXCSpr_4BPL_end:
*-------------------------------------------------------*

	popraster

