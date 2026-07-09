
displayservice_ste200_d1_tb	=	229
displayservice_ste200_dlc_tb	=	200

*-------------------------------------------------------*
displayservice_ste200_vbi:
*-------------------------------------------------------*
	blitoff
*-------------------------------------------------------*
	move.w		#$2700,sr
*-------------------------------------------------------*

	.if		(use_debugconsole)
	tst.w		_max_console
	beq.s		.no_top

	tst.b		DMAFlag.w
	bne		displayservice_ste_vbi_dmasafe
	
	.if		(use_overscan_timera)
	
	clr.b		tacr.w					; TimerA CTRL: halt
	bset		#5,iera.w				; TimerA IE: enable
	move.b		#displayservice_upper_delay-timecritical_pad_ta,tadr.w	; TimerA DATA: delay
	move.b		#4,tacr.w				; TimerA CTRL: start
	bset		#5,imra.w				; TimerA IM: enable

	move.l		#displayservice_common_upper_a,$134.w
	
	.else

	and.b		#$f8,cdcr.w				; TimerD CTRL: halt
	bset		#4,ierb.w				; TimerD IE: enable
	move.b		#displayservice_upper_delay-timecritical_pad_td,cddr.w	; TimerD DATA: delay
	or.b		#4,cdcr.w				; TimerD CTRL: start
	bset		#4,imrb.w				; TimerD IM: enable
	
	move.l		#displayservice_common_upper_d,$110.w

	.endif
.no_top:
	.endif

	movem.l		d0-d7/a0-a6,-(sp)

	.if		(use_debugconsole)
	move.l		_max_console,d0
	beq.s		.normal
	move.b		d0,$ffff8265.w
	move.l		d0,d1
	lsr.w		#8,d0
;	clr.b		$ffff8265.w
	move.l		d0,$ffff8200.w
	move.b		d1,$ffff820d.w
	move.b		d1,$ffff820f.w
;	move.b		#(dst_linewid-160)>>1,$ffff820f.w
	
	move.l		#$00340fff,$ffff8240.w
	clr.w		$ffff8244.w

	bsr		displayservice_common_full_setlower

	bra		.skip
	.endif ; (use_debugconsole)
	
.normal:
	move.l		_gp_active_group,d0
	beq		.skip
	
	move.l		d0,a0
	moveq		#1,d0
	and.w		_g_vbl,d0
	and.w		_g_fieldmask,d0
	lsl.w		#2,d0
	move.l		(a0,d0.w),a0
	move.b		0(a0),$ffff8201.w
	move.b		1(a0),$ffff8203.w
	move.b		2(a0),$ffff820d.w
	move.b		4(a0),$ffff820f.w
	move.b		3(a0),$ffff8265.w
	move.b		0(a0),$ffff8205.w
	move.b		1(a0),$ffff8207.w
	move.b		2(a0),$ffff8209.w
	move.b		4(a0),$ffff820f.w

	; load field palette
	
	lea		6(a0),a0
;	move.l		a0,ste_palette

	bsr		displayservice_ste200


	.if		(^^defined TIMING_RASTERS | use_rasters)
	move.w		BGRAST.w,$ffff8240.w
	.endif

.skip:
	.if		^^defined ENABLE_AGT_AUDIO
	jsr		_YMSYS_VBCallback
	.endif
	
	andi.w		#$fbff,sr

	addq.w		#1,vbl_sema
	bgt.s		.nsvc
	
	tst.w		2+VBServiceVec.w
	beq.s		.nsvc
	
	move.l		VBServiceVec.w,a0
	jsr		(a0)
	

.nsvc:	subq.w		#1,vbl_sema

		
	subq.w		#1,_g_vbl


	movem.l		(sp)+,d0-d7/a0-a6

	addq.l		#1,$462.w	
	addq.l		#1,$466.w	

	.if		(use_overscan_timera)
	.else
	.if		^^defined AGT_CONFIG_PROFILER
	.else
	addq.l		#4,$4ba.w			; simulate TimerC, since we have turned it off with TimerD timecritical
	.endif
	.endif

	move.b		#1,_g_seek

	bliton
	
	rte


*-----------------------------------------------------------------------------*
displayservice_ste200:
*-----------------------------------------------------------------------------*

	.if		(use_debugconsole)
	
	tst.w		_min_pconsole
	beq.s		.no_lower
	
	clr.b		tbcr.w					; TimerB CTRL: start))
	bset.b		#0,iera.w				; TimerB IE: enable
	bset.b		#0,imra.w				; TimerB IM: enable
	move.b		#displayservice_ste200_dlc_tb-timecritical_pad_tb,tbdr.w	; TimerB DATA: count
	move.b		#8,tbcr.w				; TimerB CTRL: start))
	move.l		#displayservice_ste200_lowerconsole_b,$120.w

.no_lower:
		
	.endif

;	load palette immediately
	movem.l		(a0)+,d0-d7
	movem.l		d0-d7,$ffff8240.w
	
	rts

*-----------------------------------------------------------------------------*
displayservice_ste200_lower_b:
*-----------------------------------------------------------------------------*
	.if		(use_blitter_compatibility)	

	blitoff
		
	.endif
		
	move.w		#$2700,sr
	move.l		d0,-(sp)
	move.l		a0,-(sp)

;	insert 'padding' scans to absorb short blitter jobs - 

	moveq		#displayservice_ste200_d1_tb-(timecritical_pad_tb*2),d0
	lea		tbdr.w,a0
.wl:	cmp.b		(a0),d0
	blo.s		.wl
	
	tburnrs		10,d0

;	nop		; pad until lower border event
;	nop
;	nop
;	nop
;	nop
;	nop
;	nop
;	nop
;
;	nop		; HW: almost stable
;	nop		; HW: stable
;;	nop		; HW: almost stable

	clr.b		$ffff820a.w			; 60 Hz
;	dcb.w		5,$4e71				; ...wait...
	tburnrs		5,d0
	move.b		#2,$ffff820a.w			; 50 Hz
		
	clr.b		tbcr.w				; TimerB CTRL: halt
	
	.if		(highlight_statusbar)
	lea		$ffff8240.w,a0
	move.l		#$06660666,d0
;	move.l		#$09990999,d0
	and.l		d0,(a0)+
	and.l		d0,(a0)+
	and.l		d0,(a0)+
	and.l		d0,(a0)+
	and.l		d0,(a0)+
	and.l		d0,(a0)+
	and.l		d0,(a0)+
	and.l		d0,(a0)+
	.endif
	
	move.l		(sp)+,a0
	move.l		(sp)+,d0
	
	clr.b		_g_seek
	
	.if		(use_blitter_compatibility)
	bliton
	.endif
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
	
	rte

	.if		(use_debugconsole)

*-----------------------------------------------------------------------------*
displayservice_ste200_lowerconsole_b:
*-----------------------------------------------------------------------------*
	.if		(use_blitter_compatibility)	

	blitoff
		
	.endif

	move.w		#$2700,sr
	move.l		d0,-(sp)
	move.l		a0,-(sp)

	moveq		#displayservice_ste200_dlc_tb-(timecritical_pad_tb*2),d0
	lea		tbdr.w,a0
.wl:	cmp.b		(a0),d0
	blo.s		.wl

;	nop
;	nop
;	nop
;	nop
;	nop
	move.l		psilence_,d0			; 5 nops
	
;	nop		; pad until lower border event
;	nop
;	nop
	move.b		d0,$ffff820f.w			; 3 nops


;	move.l		_min_pconsole,a0		; 8 nops
;	move.l		(a0),d0

	move.b		d0,$ffff8209.w			; 3 nops

;	nop		; HW: almost stable
;	nop		; HW: stable
;;	nop		; HW: almost stable
;	nop

	move.b		d0,$ffff820a.w
;	clr.b		$ffff820a.w			; 60 Hz 4 nops
;	dcb.w		5,$4e71				; ...wait...
;	dcb.w		2,$4e71				; ...wait...
	tburns		2
	move.b		d0,$ffff8265.w
	move.b		#2,$ffff820a.w			; 50 Hz
	
;	move.b		d0,$ffff8209.w
	lsr.w		#8,d0
	move.l		d0,$ffff8204.w
	
;	moveq		#0,d0
;	move.b		d0,$ffff820f.w

	move.l		_min_pconsole,a0		; 8 nops
	move.l		(a0),d0

;	.rept		28
;	tst.l		(a0)
;	.endr

	tburns		84

	move.b		d0,$ffff8209.w
	lsr.w		#8,d0
	move.l		d0,$ffff8204.w
	move.l		#$00340fff,$ffff8240.w
	clr.w		$ffff8244.w

	clr.b		tbcr.w				; TimerB CTRL: halt
	
	move.l		(sp)+,a0
	move.l		(sp)+,d0

	clr.b		_g_seek

	.if		(use_blitter_compatibility)
	bliton
	.endif
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
	
	rte

	.endif		;(use_debugconsole)
