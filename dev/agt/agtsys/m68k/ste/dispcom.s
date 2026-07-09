

displayservice_full_lower_d1_tb	=	229

*-----------------------------------------------------------------------------*

;	336 pixel mode: maybe useful for vscroll-only cases but incompatible with hscroll
;	would then need to replace shift loader to avoid touching these every vbl
;	move.b		#1,$ffff8265.w
;	clr.b		$ffff8264.w

*-----------------------------------------------------------------------------*
displayservice_common_full_setlower:
*-----------------------------------------------------------------------------*

	clr.b		tbcr.w					; TimerB CTRL: halt

	bset.b		#0,iera.w				; TimerB IE: enable
	bset.b		#0,imra.w				; TimerB IM: enable
	move.b		#displayservice_full_lower_d1_tb-timecritical_pad_tb,tbdr.w		; TimerB DATA: count
	move.b		#8,tbcr.w				; TimerB CTRL: start))

	move.l		#displayservice_common_full_lower_b,$120.w
				
	rts

*-----------------------------------------------------------------------------*

	.if		(use_overscan_timera)

*-----------------------------------------------------------------------------*
displayservice_common_upper_a:
*-----------------------------------------------------------------------------*
	blitoff
*-----------------------------------------------------------------------------*
	movem.l		d0/a0/a1,-(sp)
*-----------------------------------------------------------------------------*
*	critical: re-synchronize with free running TimerA data before
*	starting to count hblanks as TimerA may be delayed by BLITs and
*	we may not have landed on the correct line...
*-----------------------------------------------------------------------------*
	
	lea		$ffff820a.w,a1	

	lea		tadr.w,a0
	moveq		#displayservice_upper_delay-(timecritical_pad_ta*2),d0
.tasyn:	cmp.b		(a0),d0
	blo.s		.tasyn
*-----------------------------------------------------------------------------*
	
;	moveq		#0,d0
	
	move.w		#$2100,sr				; enable HBL
	stop		#$2100					; wait next HBL
	move.w		#$2700,sr				; lockout

	tburnrs		93,d0
;	tburns		94
	;dcb.w		94,$4e71
	
;	open top border

;	move.b		d0,(a1)	;$ffff820a.w
	clr.b		(a1)
	lsl.l		#8,d0
;	tburnrs		6,d0
	move.w		a1,(a1)	;$ffff820a.w

	move.b		d0,tacr.w				; TimerA CTRL: halt

	.if		^^defined AGT_CONFIG_ENABLE_DISPLAYTIMER

	lea		$ffff8202.w,a0
	movep.l		1(a0),d0
	and.l		#$00ffffff,d0	
	move.l		d0,_g_display_timer_begin
	clr.w		_g_display_timer_counted
	addq.w		#1,_g_display_timer_ticks
	
	.endif	
	
	movem.l		(sp)+,d0/a0/a1

	bliton
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#5,isra.w
	.endif
		
	rte

	.else

*-----------------------------------------------------------------------------*
displayservice_common_upper_d:
*-----------------------------------------------------------------------------*
	blitoff
*-----------------------------------------------------------------------------*
	move.w		#$2700,sr
*-----------------------------------------------------------------------------*
	movem.l		d0/a0/a1,-(sp)
*-----------------------------------------------------------------------------*
*	critical: re-synchronize with free running TimerA data before
*	starting to count hblanks as TimerA may be delayed by BLITs and
*	we may not have landed on the correct line...
*-----------------------------------------------------------------------------*

	lea		$ffff820a.w,a1	

	lea		cddr.w,a0
	moveq		#displayservice_upper_delay-(timecritical_pad_td*2),d0
.tasyn:	cmp.b		(a0),d0
	blo.s		.tasyn
*-----------------------------------------------------------------------------*
	
;	moveq		#0,d0
	
	move.w		#$2100,sr				; enable HBL
	stop		#$2100					; wait next HBL
	move.w		#$2700,sr				; lockout

	tburnrs		93,d0
;	tburns		94
	;dcb.w		94,$4e71

;	open top border

;	move.b		d0,(a1)	;$ffff820a.w
	clr.b		(a1)
	lsl.l		#8,d0
;	tburnrs		6,d0
	move.w		a1,(a1)	;$ffff820a.w

	and.b		#$f8,cdcr.w				; TimerD CTRL: halt

	.if		^^defined AGT_CONFIG_ENABLE_DISPLAYTIMER

	lea		$ffff8202.w,a0
	movep.l		1(a0),d0
	and.l		#$00ffffff,d0	
	move.l		d0,_g_display_timer_begin
	clr.w		_g_display_timer_counted
	addq.w		#1,_g_display_timer_ticks
	
	.endif

	movem.l		(sp)+,d0/a0/a1

	bliton
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#4,isrb.w
	.endif
		
	rte
	
	.endif ; (use_overscan_timera)


*-----------------------------------------------------------------------------*
displayservice_common_full_lower_b:
*-----------------------------------------------------------------------------*

	.if		(use_blitter_compatibility)	

	blitoff
	
	.endif
		
	move.w		#$2700,sr
	move.l		d0,-(sp)
	move.l		a0,-(sp)
		
	moveq		#displayservice_full_lower_d1_tb-(timecritical_pad_tb*2),d0

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

;	nop		; HW: almost stable
;	nop		; HW: stable
;;	nop		; HW: almost stable

	clr.b		$ffff820a.w			; 60 Hz
;	dcb.w		5,$4e71				; ...wait...
	tburnrs		5,d0
	move.b		#2,$ffff820a.w			; 50 Hz

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


	.if		^^defined AGT_CONFIG_ENABLE_DISPLAYTIMER

*-----------------------------------------------------------------------------*	
	.globl		_STE_GetDisplayTimer
*-----------------------------------------------------------------------------*
_STE_GetDisplayTimer:
*-----------------------------------------------------------------------------*		
	lea		$ffff8202.w,a0
	movep.l		1(a0),d0
	and.l		#$00ffffff,d0	

	sub.l		_g_display_timer_begin,d0
	
	moveq		#0,d1
	move.b		$ffff820f,d1
	add.w		d1,d1
	add.w		#160,d1

	divu.w		d1,d0
	add.w		_g_display_timer_counted,d0
	ext.l		d0
	
	rts
	
*-----------------------------------------------------------------------------*
	.globl		_STE_WaitDisplayTimerStart
*-----------------------------------------------------------------------------*
_STE_WaitDisplayTimerStart:
*-----------------------------------------------------------------------------*
	move.w		_g_display_timer_ticks,d0
.wt:	cmp.w		_g_display_timer_ticks,d0
	beq.s		.wt
	rts

*-----------------------------------------------------------------------------*
	.bss
*-----------------------------------------------------------------------------*
	
_g_display_timer_begin:
	ds.l		1
_g_display_timer_counted:
	ds.w		1
_g_display_timer_ticks:
	ds.w		1

*-----------------------------------------------------------------------------*
	.text
*-----------------------------------------------------------------------------*
	
	.endif