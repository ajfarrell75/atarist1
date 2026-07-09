
displayservice_nickel_d0_tb	=	0

nickel_codegen_space		=	2048
max_nevents			=	64
	
	

*-------------------------------------------------------*
*	draw context
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
nevent_smc:	ds.l	1
nevent_sync8:	ds.b	1
nevent_time8:	ds.b	1
nevent_rout:	ds.l	1
*-------------------------------------------------------*
nevent_size:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
 
_g_nickel_read_idx:
		.globl	_g_nickel_read_idx
		dc.w	0
_g_nickel_write_idx:
		.globl	_g_nickel_write_idx
		dc.w	0
 
*-------------------------------------------------------*
displayservice_nickel_vbi:
*-------------------------------------------------------*
	blitoff
*-------------------------------------------------------*
	move.w		#$2700,sr
*-------------------------------------------------------*
	
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


	movem.l		d0-d7/a0-a6,-(sp)

	lea		_g_nickel_read_idx,a0
	move.w		(a0)+,d0
	cmp.w		(a0),d0
	beq.s		.got_idx
	move.w		(a0),d0
	move.w		d0,-(a0)
.got_idx:
	add.w		d0,d0
	add.w		d0,d0
	lea		_g_a_nickel_eventbase,a0
	move.l		(a0,d0.w),DPNickelEventPos.w

	lea		_g_a_displayport_maps,a0
	move.l		(a0,d0.w),a2
	
	lea		_gp_active_group,a0
	move.w		(a2)+,d0
	bmi.s		.maps_done
	lea		s_a_displayports,a1
.map_next:	
	move.l		(a0,d0.w),(a1)+
	move.w		(a2)+,d0
	bpl.s		.map_next		
.maps_done:
	
;	move.l		_g_a_nickel_eventbase,DPNickelEventPos.w

	
;	lea		_gp_active_group,a0
;	lea		_g_a_displayports,a1
;	.rept		max_displayports
;	move.l		(a0)+,(a1)+
;	.endr


	; index field group
	moveq		#1,d0
	and.w		_g_vbl,d0
;	eor.w		#1,d0
	and.w		_g_fieldmask,d0
	add.w		d0,d0
	add.w		d0,d0
	move.w		d0,DPFieldIndex.w

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
;	move.l		_g_a_displayports,d0
;	beq.s		.skip
	
;	move.l		d0,a0
;	add.w		DPFieldIndex.w,a0
;	move.l		(a0),a0
	
;;	moveq		#1,d0
;;	and.w		_g_vbl,d0
;;	and.w		_g_fieldmask,d0
;;	lsl.w		#2,d0
;;	move.l		(a0,d0.w),a0

	moveq		#0,d0
	lea		psilence_+1,a0
	
;	move.b		 (a0),$ffff8201.w
;	move.b		1(a0),$ffff8203.w
;	move.b		2(a0),$ffff820d.w
	move.b		 (a0),$ffff8201.w
	move.b		1(a0),$ffff8203.w
	move.b		d0,$ffff820d.w
	
	;
	move.b		d0,$ffff820f.w
	move.b		d0,$ffff8265.w
	move.b		 (a0),$ffff8205.w
	move.b		1(a0),$ffff8207.w
	move.b		d0,$ffff8209.w
	move.b		d0,$ffff820f.w
	
;	move.b		4(a0),$ffff820f.w
;	move.b		3(a0),$ffff8265.w
;	move.b		 (a0),$ffff8205.w
;	move.b		1(a0),$ffff8207.w
;	move.b		2(a0),$ffff8209.w
;	move.b		4(a0),$ffff820f.w

	move.l		s_a_displayports,d0
	beq.s		.skip
	move.l		d0,a0
	add.w		DPFieldIndex.w,a0
	move.l		(a0),a0
	
	; load field palette
	
	lea		6(a0),a0

;	schedule palette on first real line, to achieve 256 lines
	move.l		a0,ste_palette
	
	bsr		displayservice_stexs
	
;	moveq		#0,d0
;	lea		$ffff8240.w,a0
;
;	.if		(^^defined TIMING_RASTERS | use_rasters)
;	move.w		BGRAST.w,(a0)+
;	move.w		d0,(a0)+
;	.else
;	move.l		d0,(a0)+
;	.endif
;
;	move.l		d0,(a0)+
;	move.l		d0,(a0)+
;	move.l		d0,(a0)+
;	move.l		d0,(a0)+
;	move.l		d0,(a0)+
;	move.l		d0,(a0)+
;	move.l		d0,(a0)+
	
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
displayservice_nickel_codegen_blk0beg_:
*-----------------------------------------------------------------------------*	
*	claim exclusive control
*-----------------------------------------------------------------------------*	
	.if		(use_blitter_compatibility)
	blitoff
	.endif
	move.w		#$2700,sr
*-----------------------------------------------------------------------------*
	movem.l		d0/a0/a1,-(sp)
*-----------------------------------------------------------------------------*
displayservice_nickel_codegen_blk0end_:
*-----------------------------------------------------------------------------*

	moveq		#99,d0
	lea		$ffff8240.w,a0
	
*-----------------------------------------------------------------------------*
displayservice_nickel_codegen_blk1beg_:
*-----------------------------------------------------------------------------*
	lea		tbdr.w,a1
.wl:	cmp.b		(a1),d0
	blo.s		.wl
*-----------------------------------------------------------------------------*
displayservice_nickel_codegen_blk1end_:
*-----------------------------------------------------------------------------*

	move.l		#$AABBCCDD,(a0)+

*-----------------------------------------------------------------------------*
displayservice_nickel_codegen_blk2beg_:
*-----------------------------------------------------------------------------*	
*	set up next nickel event
*-----------------------------------------------------------------------------*	
	move.l		DPNickelEventPos.w,a0	
	move.l		(a0)+,a1
	move.b		(a0)+,(a1)
	clr.b		tbcr.w					; TimerB CTRL: halt
	move.b		(a0)+,tbdr.w
	move.b		#8,tbcr.w				; TimerB CTRL: start))
	move.l		(a0)+,$120.w
	move.l		a0,DPNickelEventPos.w
*-----------------------------------------------------------------------------*	
*	cleanup
*-----------------------------------------------------------------------------*	
	movem.l		(sp)+,d0/a0/a1
*-----------------------------------------------------------------------------*
	.if		(use_blitter_compatibility)
	bliton
	.endif
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
*-----------------------------------------------------------------------------*
	rte
*-----------------------------------------------------------------------------*	
displayservice_nickel_codegen_blk2end_:
*-----------------------------------------------------------------------------*	

; shared
nickel_block_templates:
	;
	dc.l		displayservice_nickel_codegen_blk0beg_
	dc.l		displayservice_nickel_codegen_blk0end_-displayservice_nickel_codegen_blk0beg_
	; pre-sync
	dc.l		displayservice_nickel_codegen_blk1beg_
	dc.l		displayservice_nickel_codegen_blk1end_-displayservice_nickel_codegen_blk1beg_
	; post-sync
	dc.l		displayservice_nickel_codegen_blk2beg_
	dc.l		displayservice_nickel_codegen_blk2end_-displayservice_nickel_codegen_blk2beg_
	;
	dc.l		0

; shared	
_g_p_nickel_block_templates:
	.globl		_g_p_nickel_block_templates
	dc.l		nickel_block_templates
	
*-----------------------------------------------------------------------------*
displayservice_nickel_port_b:
*-----------------------------------------------------------------------------*
	.if		(use_blitter_compatibility)
	blitoff
	.endif
	move.w		#$2700,sr
*-----------------------------------------------------------------------------*

	movem.l		d0/a0/a1,-(sp)

;	move.l		DPNickelPortPos.w,a0
;	move.l		(a0)+,d0
;	bne.s		.go
;	move.l		s_a_displayports,a0
;	bra.s		.fail
;.go:	move.l		a0,DPNickelPortPos.w
;	move.l		d0,a0
;.fail:	add.w		DPFieldIndex.w,a0
;	move.l		(a0),a0

	move.l		DPNickelPort.w,a0

.smc:	moveq		#99,d0	;displayservice_nickel_ds_tb-(timecritical_pad_tb*2),d0
	lea		tbdr.w,a1
.wl:	cmp.b		(a1),d0
	blo.s		.wl

	; TODO: reorder struct for fast loading
	
	move.b		4(a0),$ffff820f.w
	move.b		3(a0),$ffff8265.w
	move.b		 (a0),$ffff8205.w
	move.b		1(a0),$ffff8207.w
	move.b		2(a0),$ffff8209.w
	move.b		4(a0),$ffff820f.w
	
.skip:
*-----------------------------------------------------------------------------*	
*	set up next nickel event
*-----------------------------------------------------------------------------*	
	move.l		DPNickelEventPos.w,a0	
	move.l		(a0)+,a1
	move.b		(a0)+,(a1)
	clr.b		tbcr.w					; TimerB CTRL: halt
	move.b		(a0)+,tbdr.w
	move.b		#8,tbcr.w				; TimerB CTRL: start))
	move.l		(a0)+,$120.w
	move.l		a0,DPNickelEventPos.w
*-----------------------------------------------------------------------------*	
*	fetch next port
*-----------------------------------------------------------------------------*	
	move.l		DPNickelPortPos.w,a0
	move.l		(a0)+,d0
	bne.s		.go
	move.l		s_a_displayports,a0
	bra.s		.fail
.go:	move.l		a0,DPNickelPortPos.w
	move.l		d0,a0
.fail:	add.w		DPFieldIndex.w,a0
	;move.l		(a0),a0
	;move.l		a0,DPNickelPort.w
	move.l		(a0),DPNickelPort.w
*-----------------------------------------------------------------------------*	
	
	movem.l		(sp)+,d0/a0/a1

*-----------------------------------------------------------------------------*

	.if		(use_blitter_compatibility)
	bliton
	.endif
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
	
	rte
	
displayservice_nickel_port_smc	set	.smc+1


*-----------------------------------------------------------------------------*
displayservice_nickel_stop_b:
*-----------------------------------------------------------------------------*
	.if		(use_blitter_compatibility)
	blitoff
	.endif
	move.w		#$2700,sr
*-----------------------------------------------------------------------------*

	movem.l		d0/a0/a1,-(sp)
	
	lea		psilence_+1,a0
	
;.smc:	moveq		#99,d0	;displayservice_nickel_ds_tb-(timecritical_pad_tb*2),d0
;	lea		tbdr.w,a1
;.wl:	cmp.b		(a1),d0
;	blo.s		.wl

	moveq		#0,d0
	
	lea		tbdr.w,a1
.smc:	cmp.b		#99,(a1)
	bhs.s		.smc

	move.b		d0,$ffff820f.w
	move.b		d0,$ffff8265.w
	move.b		(a0)+,$ffff8205.w
	move.b		(a0)+,$ffff8207.w
	move.b		d0,$ffff8209.w
	move.b		d0,$ffff820f.w
	
	lea		$ffff8240.w,a0
	move.w		(a0),d0
	swap		d0
	move.w		(a0)+,d0
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.w		d0,(a0)

;	move.l		DPNickelEventPos.w,a0	
;	move.l		(a0)+,a1
;	move.b		(a0)+,(a1)
	clr.b		tbcr.w					; TimerB CTRL: halt
;	move.b		(a0)+,tbdr.w
;	move.b		#8,tbcr.w				; TimerB CTRL: start))
;	move.l		(a0)+,$120.w
;	move.l		a0,DPNickelEventPos.w

;	addq.l		#4,.group_smc+2
	
	movem.l		(sp)+,d0/a0/a1

*-----------------------------------------------------------------------------*

	.if		(use_blitter_compatibility)
	bliton
	.endif
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
	
	rte
	
displayservice_nickel_stop_smc	set	.smc+3
;displayservice_nickel_stop_smc:
;	dc.w		0	


*-----------------------------------------------------------------------------*
displayservice_nickel_blank_b:
*-----------------------------------------------------------------------------*
	.if		(use_blitter_compatibility)
	blitoff
	.endif
	move.w		#$2700,sr
*-----------------------------------------------------------------------------*

	movem.l		d0/a0/a1,-(sp)
	
	lea		psilence_+1,a0
	
;.smc:	moveq		#99,d0	;displayservice_nickel_ds_tb-(timecritical_pad_tb*2),d0
;	lea		tbdr.w,a1
;.wl:	cmp.b		(a1),d0
;	blo.s		.wl

	moveq		#0,d0
	
	lea		tbdr.w,a1
.smc:	cmp.b		#99,(a1)
	bhs.s		.smc
		
	move.b		d0,$ffff820f.w
	move.b		d0,$ffff8265.w
	move.b		(a0)+,$ffff8205.w
	move.b		(a0)+,$ffff8207.w
	move.b		d0,$ffff8209.w
	move.b		d0,$ffff820f.w
	
.skip:
	lea		$ffff8240.w,a0
	move.w		(a0),d0
	swap		d0
	move.w		(a0)+,d0
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.l		d0,(a0)+
	move.w		d0,(a0)

*-----------------------------------------------------------------------------*	
*	set up next nickel event
*-----------------------------------------------------------------------------*	
	move.l		DPNickelEventPos.w,a0	
	move.l		(a0)+,a1
	move.b		(a0)+,(a1)
	clr.b		tbcr.w					; TimerB CTRL: halt
	move.b		(a0)+,tbdr.w
	move.b		#8,tbcr.w				; TimerB CTRL: start))
	move.l		(a0)+,$120.w
	move.l		a0,DPNickelEventPos.w
*-----------------------------------------------------------------------------*	
	
	movem.l		(sp)+,d0/a0/a1

*-----------------------------------------------------------------------------*

	.if		(use_blitter_compatibility)
	bliton
	.endif
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
	
	rte
	
displayservice_nickel_blank_smc	set	.smc+3


*-----------------------------------------------------------------------------*
displayservice_stexs:	;_first_b:
*-----------------------------------------------------------------------------*	
	
	clr.b		tbcr.w				; TimerB CTRL: halt

*-----------------------------------------------------------------------------*	
*	set up first nickel event
*-----------------------------------------------------------------------------*	
	move.l		DPNickelEventPos.w,a0
	move.l		(a0)+,a1
	move.b		(a0)+,(a1)
	bset.b		#0,iera.w				; TimerB IE: enable
	bset.b		#0,imra.w				; TimerB IM: enable
	move.b		(a0)+,tbdr.w
	move.b		#8,tbcr.w				; TimerB CTRL: start))
	move.l		(a0)+,$120.w
	move.l		a0,DPNickelEventPos.w
*-----------------------------------------------------------------------------*	

	; reset view shiftgroup
	;move.l		#s_a_displayports,DPNickelPortPos.w

	;move.l		DPNickelPortPos.w,a0
	lea		s_a_displayports,a0
	move.l		(a0)+,d0
	bne.s		.go
	move.l		s_a_displayports,a0
	bra.s		.fail
.go:	move.l		a0,DPNickelPortPos.w
	move.l		d0,a0
.fail:	add.w		DPFieldIndex.w,a0
	move.l		(a0),a0
	move.l		a0,DPNickelPort.w
	
;	move.b		#displayservice_nickel_dlb_tb-timecritical_pad_tb,tbdr.w	; TimerB DATA: count
;	move.b		#8,tbcr.w				; TimerB CTRL: start))

	
.cont:
	
	move.l		ste_palette,a0
;	.if		(use_debugconsole)
;	tst.w		_max_console
;	beq.s		.ncon
;	lea		con_palette,a0	
;.ncon:	
;	.endif
	
		
	lea		$ffff8240.w,a1	
	.if		(^^defined TIMING_RASTERS | use_rasters)
	move.w		BGRAST.w,(a1)+
	addq		#2,a0
	move.w		(a0)+,(a1)+
	.else
	move.l		(a0)+,(a1)+
	.endif
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
				
	rts

displayservice_nickel_end_b:
	.if		(use_blitter_compatibility)
	blitoff
	.endif

	clr.b		tbcr.w				; TimerB CTRL: halt

	.if		(use_blitter_compatibility)
	bliton
	.endif
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
	
	rte

displayservice_nickel_end_smc:
	dc.w		0


	.if		(use_debugconsole)

*-----------------------------------------------------------------------------*
displayservice_nickel_lowerconsole_routing:
*-----------------------------------------------------------------------------*
	
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

;	.rept		84
;	nop
;	.endr

;	.rept		28
;	tst.l		(a0)
;	.endr

	tburns		84

	move.b		d0,$ffff8209.w
	lsr.w		#8,d0
	move.l		d0,$ffff8204.w
	move.l		#$00340fff,$ffff8240.w
	clr.w		$ffff8244.w
	
	.if		^^defined AGT_CONFIG_ENABLE_DISPLAYTIMER

	move.w		#displayservice_ste272_d1_tb+1,_g_display_timer_counted
	move.l		_min_pconsole,a0
	move.l		(a0),_g_display_timer_begin
	
	.endif

	clr.b		_g_seek
	
	move.b		tbdr.w,d0
.wt2:	cmp.b		tbdr.w,d0
	beq.s		.wt2

	clr.b		tbcr.w				; TimerB CTRL: halt

	move.l		(sp)+,a0
	move.l		(sp)+,d0
	
	.if		(use_blitter_compatibility)
	bliton
	.endif
	
	.if		(0=use_auto_end_of_interrupt)
;	manual EOI
	bclr.b		#0,isra.w
	.endif
	
	rte	

	.endif ; (use_debugconsole)
	


*-----------------------------------------------------------------------------*
displayservice_nickel_border_b:
*-----------------------------------------------------------------------------*
	.if		(use_blitter_compatibility)	

	blitoff
	
	.endif
		
	move.w		#$2700,sr
	move.l		d0,-(sp)
	move.l		a0,-(sp)
		
;	insert 'padding' scans to absorb short blitter jobs - 

.smc:	moveq		#99,d0	;displayservice_nickel_dlb_tb-(timecritical_pad_tb*2),d0

	.if		(use_debugconsole)
	tst.w		_min_pconsole
	bne		displayservice_nickel_lowerconsole_routing	
	.endif

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
;	nop		; HW: almost stable

	clr.b		$ffff820a.w			; 60 Hz
;	dcb.w		5,$4e71				; ...wait...
	tburnrs		5,d0
	move.b		#2,$ffff820a.w			; 50 Hz
		

	move.l		a1,-(sp)
	
	move.l		DPNickelEventPos.w,a0
	move.l		(a0)+,a1
	move.b		(a0)+,(a1)
	clr.b		tbcr.w					; TimerB CTRL: halt
	move.b		(a0)+,tbdr.w
	move.b		#8,tbcr.w				; TimerB CTRL: start))
	move.l		(a0)+,$120.w
	move.l		a0,DPNickelEventPos.w
	
	move.l		(sp)+,a1
	
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

displayservice_nickel_border_smc	set	.smc+1

	

; shared	
_g_a_nickel_port_cfg:	
	.globl		_g_a_nickel_port_cfg
	dc.l		displayservice_nickel_port_smc,displayservice_nickel_port_b
_g_a_nickel_border_cfg:
	.globl		_g_a_nickel_border_cfg
	dc.l		displayservice_nickel_border_smc,displayservice_nickel_border_b
_g_a_nickel_blank_cfg:
	.globl		_g_a_nickel_blank_cfg
	dc.l		displayservice_nickel_blank_smc,displayservice_nickel_blank_b
_g_a_nickel_stop_cfg:
	.globl		_g_a_nickel_stop_cfg
	dc.l		displayservice_nickel_stop_smc,displayservice_nickel_stop_b
_g_a_nickel_end_cfg:
	.globl		_g_a_nickel_end_cfg
	dc.l		displayservice_nickel_end_smc,displayservice_nickel_end_b


nickel_lastline		set	0

.macro	nickel_port	absline
nickel_rel		set	\absline-nickel_lastline
	dc.l		displayservice_nickel_port_smc
	dc.b		nickel_rel-(timecritical_pad_tb*2)
	dc.b		nickel_rel-timecritical_pad_tb
	dc.l		displayservice_nickel_port_b	
nickel_lastline		set	\absline
	.endm

.macro	nickel_border
nickel_rel		set	229-nickel_lastline
	dc.l		displayservice_nickel_border_smc
	dc.b		nickel_rel-(timecritical_pad_tb*2)
	dc.b		nickel_rel-timecritical_pad_tb
	dc.l		displayservice_nickel_border_b	
nickel_lastline		set	229
	.endm

.macro	nickel_stop	absline
nickel_rel		set	\absline-nickel_lastline
	dc.l		displayservice_nickel_stop_smc
	dc.b		nickel_rel-(timecritical_pad_tb*2)
	dc.b		nickel_rel-timecritical_pad_tb
	dc.l		displayservice_nickel_stop_b	
nickel_lastline		set	\absline
	.endm

.macro	nickel_end
	dc.l		displayservice_nickel_end_smc
	dc.b		0
	dc.b		255
	dc.l		displayservice_nickel_end_b
	.endm

nickel_events_0:
	nickel_end
	ds.b		nevent_size*max_nevents
nickel_events_1:
	nickel_end
	ds.b		nevent_size*max_nevents

; todo: per-shiftgroup
;nickel_events:
;	nickel_port	0+16
;	nickel_port	72+16
;	nickel_port	96+16
;	nickel_port	184+16
;
;	nickel_border	;229
;
;	nickel_stop	256
;
;	nickel_end
;
;	ds.b		1024
	
; buffered
_g_a_nickel_eventbase:
	.globl		_g_a_nickel_eventbase
	dc.l		nickel_events_0
	dc.l		nickel_events_1

	bss
	

displayport_map_0:
	ds.w		max_displayports
displayport_map_1:
	ds.w		max_displayports
	
s_a_displayports:
	ds.l		max_displayports
	
		
; todo: per-shiftgroup
codegen_space_0:
	ds.w		(nickel_codegen_space+256)>>1
codegen_space_1:
	ds.w		(nickel_codegen_space+256)>>1

	text

; buffered
_g_a_displayport_maps:
	.globl		_g_a_displayport_maps
	dc.l		displayport_map_0
	dc.l		displayport_map_1

; todo: per-shiftgroup
_g_a_nickel_codegen_area:
	.globl		_g_a_nickel_codegen_area
	dc.l		codegen_space_0+255
	dc.l		codegen_space_1+255


