*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	blitter shared-codegen sprite implementation
* 	paths: EMHQSPR 
*-------------------------------------------------------*
*	max source xsize: -
*	max source ysize: viewport clipper ysize
*	x-clipping: guardband reject only
*	y-clipping: guardband reject only
*	clearing: yes/automatic
*	occlusionmaps: no
*-------------------------------------------------------*
	
	; throw exception if frame index exceeds frame count
	sprite_sc_frame	a0,d0,_AGT_Exception_EMHFrame_68k

	pushraster	#$062


*-------------------------------------------------------*
*	Entrypoint for EMHSPR with cull-clipping only	
*-------------------------------------------------------*
.BLiT_EMHSpr_NoClip_4BPL:
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

	move.l		.sp_guardwin(sp),a2
	guard_reject	a2,d1,d3,d0,d6,.BLiT_EMHSpr_4BPL_end
	guard_reject	a2,d2,d4,d0,d6,.BLiT_EMHSpr_4BPL_end

*-------------------------------------------------------*
.BLiT_EMHSpr_NoClip_4BPL_go:
*-------------------------------------------------------*

	setraster	#$262

	; skew
	moveq		#16-1,d6
	and.w		d1,d6
	
;	access preshifted mask frame
	add.w		d6,d6
	add.w		d6,d6				; a0: spf_em_
	move.l		spf_em_psmask-8(a0,d6.w),a6	; a6: spf_emh_psm_
	move.l		a6,d6

	lea		spf_emh_psm_dstyskip(a6),a4	; baked fields for pagerestore

	lea		spf_emh_psm_bskew(a6),a6

	move.w		(a6)+,.sp_skew(sp)		; spf_emh_psm_bskew
	;move.b		d0,.emh_skew_smc+3

	move.w		(a6)+,BLiTSYI.w			; spf_emh_psm_bsyi
	move.w		(a6)+,BLiTDYI.w			; spf_emh_psm_bdyi	
	move.w		(a6)+,BLiTXC.w			; spf_emh_psm_bxc

	move.w		(a6)+,d0			; spf_emh_psm_emdata
	move.l		d6,a3
	add.w		d0,a3				; psmask+spf_emh_psm_emdata	
	;move.l		a3,.emh_emdata_smc+2
	move.l		a3,.sp_nextcmp(sp)

	move.l		(a6)+,d0			; spf_emh_psm_code
	move.l		d6,a3
	add.l		d0,a3				; psmask+spf_emh_psm_code	
	move.l		a3,.emh_call_smc+2
	
;	access colour data
	move.w		.sp_cfield4(sp),d5
	move.l		spf_em_colour-8(a0,d5.w),a0
	addq		#2,a0				; colour word-width (unused)
	move.l		a0,BLiTSRC.w			; colour data


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
	add.l		(a3,d2.w),a1	; 38c vs 56c for muls (28c excl. linetable fetch)

	.if		(enable_restore&use_sprite_pagerestore)

	move.l		a1,(a5)+			; clear addr

	move.w		(a4)+,(a5)+	; dstskip
	move.w		(a4)+,d0
	lsr.w		d0		; hack: pagerestore still expects wordwidth*2, should really be *4
	move.w		d0,(a5)+	; dstww2
	move.w		d4,(a5)+
	move.l		d5,(a5)+	
	move.l		a5,usp	

	.endif		;(enable_restore&use_sprite_pagerestore)
	

*-------------------------------------------------------*

	lea		BLiTYC.w,a0
	lea		BLiTEM1.w,a2
	lea		BLiTDST.w,a3
	lea		BLiTEM3.w,a4
	lea		BLiTCTRL.w,a5

	move.l		.sp_nextcmp(sp),a6

	move.w		.sp_linebytes(sp),d0

	move.l		#$00ffc000,d1

	move.b		.sp_skew+1(sp),d1

	move.l		#$FF000004,d2

;	IN:
;
;	d0		linebytes
;	d1		$00ffc0SS (SS = skew)
;	d2		$FF000004
;	d3		-
;	d4		-
;	d5		-
;	d6		-
;	d7		-
;	a0		BLiTYC
;	a1		dst
;	a2		BLiTEM1
;	a3		BLiTDST
;	a4		BLiTEM3
;	a5		BLiTCTRL
;	a6		emdata

.emh_call_smc:
	jsr		$123456
	
*-------------------------------------------------------*
.BLiT_EMHSpr_4BPL_end:
*-------------------------------------------------------*

	popraster

