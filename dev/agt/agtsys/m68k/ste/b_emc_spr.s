*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	compound blitter gensprite implementation
* 	paths: EMSPR
*-------------------------------------------------------*
*	max source xsize: -
*	max source ysize: viewport clipper ysize
*	x-clipping: guardband reject only
*	y-clipping: yes
*	clearing: yes/automatic
*	occlusionmaps: yes
*-------------------------------------------------------*

	sprite_sc_frame	a0,d0,_AGT_Exception_EMSFrame_68k

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
	guard_reject	a2,d1,d3,d0,d6,.BLiT_EMCSpr_4BPL_end

*-------------------------------------------------------*
*	EntityDrawFlag_NORESTORE
*-------------------------------------------------------*
	btst		#14,d7
	bne.s		.emc_shortcut_bgrestore
*-------------------------------------------------------*
*	occlusion mapping	
*-------------------------------------------------------*
	move.w		(a0),d0
	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.w		d0,.emc_update_prestore1+2
	.endif
	.endif
	beq.s		.emc_no_occlusion
*-------------------------------------------------------*

	;
	; early y-reject if crossing y-guard (shortcuts OCM)
	;
	guard_reject	a2,d2,d4,d5,d6,.BLiT_EMCSpr_4BPL_end

*-------------------------------------------------------*
	move.l		a0,-(sp)
	lea		(a0,d0.w),a0		
	jsr		_OCM_occlusion_add_T			
	move.l		(sp)+,a0
*-------------------------------------------------------*
.emc_shortcut_bgrestore:
*-------------------------------------------------------*
	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	moveq		#-bgrframe_size,d0
	move.w		d0,.emc_update_prestore1+2
	.endif
	.endif
*-------------------------------------------------------*
	;
	; OCM means no guard cross, but we lose nothing
	; and save some time by allowing y-clip to follow
	;

*-------------------------------------------------------*
.emc_no_occlusion:
*-------------------------------------------------------*

	; skew
	moveq		#16-1,d6
	and.w		d1,d6

;	access preshifted mask frame
	add.w		d6,d6
	add.w		d6,d6
	move.l		spf_em_psmask-8(a0,d6.w),a6
	

.BLiT_EMCSpr_firstcmp:
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
	beq.s		.BLiT_EMCSpr_onlypsm
	add.l		d6,d7
.BLiT_EMCSpr_onlypsm:
	move.l		d7,.emsc_next_psmask			; spf_psm_nextcmp



;	access colour data
	move.w		.sp_cfield4(sp),d5
	move.l		spf_em_colour-8(a0,d5.w),a0

;	move.l		spf_em_colour-8(a0),a0

;	word-width for this preshift (will be 1, 2 or 3 for normal EMS, more for wide format)
;	todo: relevant size & blitter fields e.g. BLiTSYI could be stored with frame
	move.w		(a0)+,d5			; ***** unused *****


	move.l		a0,.emsc_cmp_cplanes
	clr.w		.emsc_cmp_masklineoff
	
;.next_component:

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
	bpl.s		.BLiT_EMCSpr_safetop
*-------------------------------------------------------*
*	top clipped
*-------------------------------------------------------*
.BLiT_EMCSpr_unsafetop:
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_DEBUG_SHOWCLIPS
	move.b		#15,BLiTLOP.w
	.endif

	move.w		clip_win_y(a2),d5
	; relative to clipwindow
	sub.w		d5,d2
	; crop lines
	add.w		d2,d4
	ble		.BLiT_EMCSpr_4BPL_end_all

	; adjust src offset in pixelwords*planes*yerr
	neg.w		d2
	move.w		d3,d0
	add.w		d6,d0
	and.w		d7,d0
	lsr.w		#1,d0				; *8
	mulu.w		d2,d0
	add.w		d0,a0				; +8*w*yerr

	move.w		d2,.emsc_cmp_masklineoff

	; relative to world
	move.w		clip_win_y(a2),d2
	; ASSUMPTION: sprite is not big enough to cross top+bottom
;	bra.s		.BLiT_EMCSpr_safebot

*-------------------------------------------------------*
.BLiT_EMCSpr_safetop:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	bottom edge
*-------------------------------------------------------*
	move.w		d2,d0
	add.w		d4,d0				; xr
	sub.w		clip_win_y2(a2),d0			; xr - cx2
	ble.s		.BLiT_EMCSpr_safebot
*-------------------------------------------------------*
*	bottom clipped
*-------------------------------------------------------*
.BLiT_EMCSpr_unsafebot:
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_DEBUG_SHOWCLIPS
	move.b		#15,BLiTLOP.w
	.endif
	
	sub.w		d0,d4
	ble		.BLiT_EMCSpr_4BPL_end_all
*-------------------------------------------------------*
.BLiT_EMCSpr_safebot:
*-------------------------------------------------------*

	.endif		; enable_clipping


*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMCSpr_Clip_4BPL_unclipped_got:
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

	.if		(1)
	
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
.BLiT_EMCSpr_sylp:	
	jmp		.BLiT_EMCSpr_jt(pc,d6.w)
	rept		9
	move.l		(a3)+,(a5)+
	move.l		(a3)+,(a5)+
	endr
.BLiT_EMCSpr_jt:
	add.w		a4,a3
	dbra		d1,.BLiT_EMCSpr_sylp

	.endif		;(use_sprite_saverestore)
	.endif		;(enable_restore)

*-------------------------------------------------------*

	.if		(enable_restore)
	.if		(use_sprite_saverestore|use_sprite_pagerestore)
	move.l		d5,(a5)+
.emc_update_prestore1:
	lea		$1234(a5),a5
	move.l		a5,usp	
	.endif		;(use_sprite_saverestore|use_sprite_pagerestore)
	.endif		;(enable_restore)
	
	.endif		;1


	move.l		a0,.emsc_cmp_src
	move.l		a1,.emsc_cmp_dst
	move.w		d4,.emsc_cmp_h
	move.w		.sp_maskwidth(sp),d6

*-------------------------------------------------------*
.BLiT_EMCSpr_Clip_4BPL_unclipped_blit:
*-------------------------------------------------------*

	move.w		.emsc_cmp_masklineoff,d0
	beq.s		.BLiT_EMCSpr_noyi
	move.w		d6,d5
	add.w		d5,d5
	mulu.w		.emsc_cmp_masklineoff,d5
	add.w		d5,a6
.BLiT_EMCSpr_noyi:

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
	beq		.BLiT_EMCSpr_sgl
	subq.w		#1,d0
	beq		.BLiT_EMCSpr_dbl

*-------------------------------------------------------*
.BLiT_EMCSpr_tri:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jp3t(pc,d0.w)
.jp3t:	bra.s		.jp3s
	bra.s		.jp31
	bra.s		.jp32
	bra.s		.jp33
	bra.s		.jp34
	bra.s		.jp35
	bra.s		.jp36
	bra.s		.jp37


	; 12bytes
.jp3s:	subq.w		#1,d4
.jp38:	scan_3w
.jp37:	scan_3w
.jp36:	scan_3w
.jp35:	scan_3w
.jp34:	scan_3w
.jp33:	scan_3w
.jp32:	scan_3w
.jp31:	scan_3w
.jp30:	dbra		d4,.jp38

	bra		.BLiT_EMCSpr_4BPL_end

*-------------------------------------------------------*
.BLiT_EMCSpr_dbl:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jq2t(pc,d0.w)
.jq2t:	bra.s		.jq2s
	bra.s		.jq21
	bra.s		.jq22
	bra.s		.jq23
	bra.s		.jq24
	bra.s		.jq25
	bra.s		.jq26
	bra.s		.jq27

	; 12bytes
.jq2s:	subq.w		#1,d4
.jq28:	scan_2w
.jq27:	scan_2w
.jq26:	scan_2w
.jq25:	scan_2w
.jq24:	scan_2w
.jq23:	scan_2w
.jq22:	scan_2w
.jq21:	scan_2w
.jq20:	dbra		d4,.jq28

	bra		.BLiT_EMCSpr_4BPL_end

*-------------------------------------------------------*
.BLiT_EMCSpr_sgl:
*-------------------------------------------------------*
	moveq		#8-1,d0
	and.w		d4,d0
	add.w		d0,d0
	lsr.w		#3,d4
	jmp		.jr1t(pc,d0.w)
.jr1t:	bra.s		.jr1s
	bra.s		.jr11
	bra.s		.jr12
	bra.s		.jr13
	bra.s		.jr14
	bra.s		.jr15
	bra.s		.jr16
	bra.s		.jr17

	; 10bytes
.jr1s:	subq.w		#1,d4
.jr18:	scan_1w
.jr17:	scan_1w
.jr16:	scan_1w
.jr15:	scan_1w
.jr14:	scan_1w
.jr13:	scan_1w
.jr12:	scan_1w
.jr11:	scan_1w
.jr10:	dbra		d4,.jr18

;	bra		.BLiT_EMSpr_4BPL_end

*-------------------------------------------------------*
.BLiT_EMCSpr_4BPL_end:
*-------------------------------------------------------*

	move.l		.emsc_next_psmask,d0
	beq		.BLiT_EMCSpr_4BPL_end_all
	move.l		d0,a6

	move.l		.emsc_cmp_cplanes,a0		; colour planes
	
	move.l		a6,d6
;	wordwidth of source mask
	move.w		(a6)+,d5		; spf_psm_wordwidth
	move.w		d5,.sp_maskwidth(sp)
	move.w		d5,BLiTXC.w		; xcount
	move.w		(a6)+,d0		; skew
	move.b		d0,BLiTSKEW.w
	move.w		(a6)+,BLiTSYI.w		; syinc
	move.w		(a6)+,BLiTDYI.w		; dyinc

	move.l		.emsc_cmp_src,a0
	move.l		.emsc_cmp_dst,a1

	move.w		(a6)+,d0		; wordoffset
	add.w		d0,d0
	add.w		d0,a0
	subq		#2,a0
	add.w		d0,d0
	add.w		d0,d0
	add.w		d0,a1

	; link to next component, if present
	move.l		(a6)+,d7
	beq.s		.BLiT_EMCSpr_lastpsm
	add.l		d6,d7
.BLiT_EMCSpr_lastpsm:
	move.l		d7,.emsc_next_psmask			; spf_psm_nextcmp

*-------------------------------------------------------*
*	common impl. for general paths
*-------------------------------------------------------*
.BLiT_EMCSpr_Clip_4BPL_unclipped_got2:
*-------------------------------------------------------*

	move.w		.emsc_cmp_h,d4
	move.w		.sp_maskwidth(sp),d6

	altraster	#$021
		
	bra		.BLiT_EMCSpr_Clip_4BPL_unclipped_blit

.emsc_cmp_src:
	ds.l		1
.emsc_cmp_dst:
	ds.l		1	
.emsc_cmp_h:
	ds.w		1
.emsc_cmp_masklineoff:
	ds.w		1
.emsc_cmp_cplanes:
	ds.l		1
.emsc_next_psmask:
	ds.l		1
	
.BLiT_EMCSpr_4BPL_end_all:

	;move.w		#2,BLiTSYI.w	; reset for default 32w EMS/EMX
	
	popraster
	