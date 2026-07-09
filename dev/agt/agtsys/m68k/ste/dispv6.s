*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
* 	PCS v6 display module
*-------------------------------------------------------*

	.globl	_STE_PCSV6_Setup416x272
	.globl	_STE_PCSV6_Show416x272
	.globl	_STE_PCSV6_begin_
	.globl	_STE_PCSV6_end_
;	.globl	_g_pcs_scrolldelay

*-----------------------------------------------------------------------------*

	include		"tburn.s"
	
*-----------------------------------------------------------------------------*

enable_vscroll		set	0
hotswap_machine		set	0
configure_mste		set	0

*-----------------------------------------------------------------------------*

display_lines_primary	=	227	; #lines above lower border
display_lines_lowborder	=	44	; #lines in bottom border

display_offset		=	160	; display offset to first real bitmap data
display_linebytes	=	224	; #bytes per overscanned line

display_bch_linewords	=	14*3	; palette words per blitter-channel line
display_cch_linewords	=	10	; palette words per (normal) cpu-channel line

display_lines		=	display_lines_primary+1+display_lines_lowborder

;disp_code_size		=	8898

*-----------------------------------------------------------------------------*
pcsv6_view:
*-----------------------------------------------------------------------------*

	move.l	pcsv6_view_period,d0
	beq.s	.no_timer
	add.l	$462.w,d0
.no_timer:
	move.l	d0,pcsv6_view_timeout

*-----------------------------------------------------------------------------*
*	start display
*-----------------------------------------------------------------------------*

	bsr	pcsv6_savestate

*-----------------------------------------------------------------------------*
*	wait for next vblank
*-----------------------------------------------------------------------------*
	move.l	$462.w,d0	; _vbclock
.waitv:	cmp.l	$462.w,d0
	beq.s	.waitv

	bsr	pcsv6_openview

*-----------------------------------------------------------------------------*
*	idle... wait for spacebar
*-----------------------------------------------------------------------------*
main_loop:	
*-----------------------------------------------------------------------------*
	move.l	pcsv6_view_timeout,d0
	beq.s	.notm
	cmp.l	$462.w,d0	
	bmi.s	.stop
.notm:	btst	#0,$fffffc00.w
	beq.s	main_loop
	move.b	$fffffc02.w,d0
;	cmp.b	#$01,d0
;	beq.s	.exit
	cmp.b	#$39,d0
	bne.s	main_loop
.hold:	cmp.b	$fffffc02.w,d0
	beq.s	.hold

*-----------------------------------------------------------------------------*
*	restore display & exit	
*-----------------------------------------------------------------------------*
.stop:	bsr	pcsv6_restorestate
	rts


*-----------------------------------------------------------------------------*
pcsv6_savestate:
*-----------------------------------------------------------------------------*
	move	#$2700,sr
	lea	pcsv6_recovery_state,a0
	
	; vectors
	move.l	usp,a1
	move.l	a1,(a0)+
	move.l	$68.w,(a0)+	
	move.l	$70.w,(a0)+
	move.l	$134.w,(a0)+

	; display
	move.b	$ffff820a.w,(a0)+
	move.b	$ffff8260.w,(a0)+
	move.b	$ffff820d.w,(a0)+
	move.b	$ffff8201.w,(a0)+
	move.b	$ffff8203.w,(a0)+
	move.b	$ffff8205.w,(a0)+
	move.b	$ffff8265.w,(a0)+
	clr.b	(a0)+
	movem.l	$ffff8240.w,d0-d7
	movem.l	d0-d7,(a0)
	lea	16*2(a0),a0

	; blitter
	move.w	$ffff8a20.w,(a0)+	; sxinc
	move.w	$ffff8a22.w,(a0)+	; syinc
	move.w	$ffff8a28.w,(a0)+	; em1
	move.w	$ffff8a2a.w,(a0)+	; em2
	move.w	$ffff8a2c.w,(a0)+	; em3
	move.w	$ffff8a2e.w,(a0)+	; dxinc
	move.w	$ffff8a30.w,(a0)+	; dyinc
	move.w	$ffff8a36.w,(a0)+	; xsize

	move.b	$ffff8a3a.w,(a0)+	; hop
	move.b	$ffff8a3b.w,(a0)+	; lop
	move.b	$ffff8a3d.w,(a0)+	; skew
	clr.b	(a0)+

	; mfp
	move.b	$fffffa07.w,(a0)+
	move.b	$fffffa09.w,(a0)+
	move.b	$fffffa13.w,(a0)+
	move.b	$fffffa19.w,(a0)+

	move.w	#$2300,sr
	rts

*-----------------------------------------------------------------------------*
pcsv6_restorestate:
*-----------------------------------------------------------------------------*
	move	#$2700,sr
	lea	pcsv6_recovery_state,a0
	
	; vectors
	move.l	(a0)+,a1
	move.l	a1,usp
	move.l	(a0)+,$68.w	
	move.l	(a0)+,$70.w
	move.l	(a0)+,$134.w

	; display
	move.b	(a0)+,$ffff820a.w
	move.b	(a0)+,$ffff8260.w
	move.b	(a0)+,$ffff820d.w
	move.b	(a0)+,$ffff8201.w
	move.b	(a0)+,$ffff8203.w
	move.b	(a0)+,$ffff8205.w
	move.b	(a0)+,$ffff8265.w
	clr.b	(a0)+
	movem.l	(a0)+,d0-d7

	; when integrated into engine only - not for viewer with clean exit
	moveq	#0,d0
	moveq	#0,d1
	moveq	#0,d2
	moveq	#0,d3
	moveq	#0,d4
	moveq	#0,d5
	moveq	#0,d6
	moveq	#0,d7

	movem.l	d0-d7,$ffff8240.w
	; blitter
	move.w	(a0)+,$ffff8a20.w	; sxinc
	move.w	(a0)+,$ffff8a22.w	; syinc
	move.w	(a0)+,$ffff8a28.w	; em1
	move.w	(a0)+,$ffff8a2a.w	; em2
	move.w	(a0)+,$ffff8a2c.w	; em3
	move.w	(a0)+,$ffff8a2e.w	; dxinc
	move.w	(a0)+,$ffff8a30.w	; dyinc
	move.w	(a0)+,$ffff8a36.w	; xsize

	move.b	(a0)+,$ffff8a3a.w	; hop
	move.b	(a0)+,$ffff8a3b.w	; lop
	move.b	(a0)+,$ffff8a3d.w	; skew
	clr.b	(a0)+

	; mfp
	move.b	(a0)+,$fffffa07.w
	move.b	(a0)+,$fffffa09.w
	move.b	(a0)+,$fffffa13.w
	move.b	(a0)+,$fffffa19.w

	move.w	#$2300,sr
	rts



*-----------------------------------------------------------------------------*
pcsv6_openview:
*-----------------------------------------------------------------------------*
	move	#$2700,sr
*-----------------------------------------------------------------------------*

	; display
	move.b	#2,$ffff820a.w
	clr.b	$ffff8260.w
	clr.b	$ffff820d.w
	clr.b	$ffff8265.w
	
	; blitter	
	move.w	#2,$ffff8a20.w		; sxinc
	move.w	#2,$ffff8a22.w		; syinc
	move.w	#-1,$ffff8a28.w		; em1
	move.w	#-1,$ffff8a2a.w		; em2
	move.w	#-1,$ffff8a2c.w		; em3
	move.w	#2,$ffff8a2e.w		; dxinc
	move.w	#-26,$ffff8a30.w	; dyinc
	move.w	#14,$ffff8a36.w		; xsize
	move.b	#2,$ffff8a3a.w		; hop
	move.b	#3,$ffff8a3b.w		; lop
	move.b	#0,$ffff8a3d.w		; skew
	move.l	#$ffff8240,$ffff8a32.w	; daddr
	
	; mfp & vectors
	clr.b	$fffffa07.w
	clr.b	$fffffa09.w

	move.l	#pcsv6_hbl,$68.w
	move.l	#pcsv6_vbl,$70.w

	move.l	#pcsv6_timera_ste,$134.w
	
	bset	#5,$fffffa07.w
	bset	#5,$fffffa13.w
	clr.b	$fffffa19.w

*-----------------------------------------------------------------------------*
	move.w	#$2300,sr
	rts

*-------------------------------------------------------*
_STE_PCSV6_Setup416x272:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.numfields:		ds.l	1
.numlines:		ds.l	1
.pimg_fields:		ds.l	1
.pbpal_fields:		ds.l	1
.pcpal_fields:		ds.l	1
.scrl_delay:		ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
*-------------------------------------------------------*

	move.w		.numlines+2(a6),pcsv6_num_lines

	move.l		.pimg_fields(a6),a0
	move.l		.pbpal_fields(a6),a1
	move.l		.pcpal_fields(a6),a2
	move.w		.scrl_delay+2(a6),pcsv6_scroll_delay
	
	lea		pcsv6_displayfield_tab,a3
	lea		pcsv6_bch_palette_tab,a4
	lea		pcsv6_cch_palette_tab,a5
		
	move.w		.numfields+2(a6),d7
	move.w		d7,pcsv6_num_fields
	subq.w		#1,d7
.load:	move.l		(a0)+,(a3)+
	move.l		(a1)+,(a4)+
	move.l		(a2)+,(a5)+
	dbra		d7,.load
	
;	clr.l		bch_scroll_next
;	clr.l		bch_scroll
;	clr.l		cch_scroll_next
;	clr.l		cch_scroll
;	clr.l		pcs_scroll
;	clr.w		scroll_line	
;	clr.b		dir	

	bsr		pcsv6_reset_scroll

*-----------------------------------------------------------------------------*
*	initialize sequence with first field
*-----------------------------------------------------------------------------*
	clr.w		pcsv6_field_active
*-----------------------------------------------------------------------------*
*	generate field sequence
*-----------------------------------------------------------------------------*
	lea		pcsv6_field_sequence,a0
	clr.l		(a0)
	clr.l		4(a0)
	moveq		#1,d0
.prep:	cmp.w		pcsv6_num_fields,d0
	beq.s		.pdon
	move.w		d0,(a0)+
	addq.w		#1,d0
	bra.s		.prep
.pdon:		
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

*-------------------------------------------------------*
_STE_PCSV6_Show416x272:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
*-------------------------------------------------------*
	bsr		pcsv6_view
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

pcsv6_palette:		dc.l	0	;cpu_channel0
	
*-----------------------------------------------------------------------------*
pcsv6_field_active:	dc.w	0
*-----------------------------------------------------------------------------*
pcsv6_field_sequence:	dc.w	0
			dc.w	0
			dc.w	0
			dc.w	0
				
*-----------------------------------------------------------------------------*
pcsv6_displayfield_tab:
*-----------------------------------------------------------------------------*
			dc.l	0
			dc.l	0
			dc.l	0
			dc.l	0

*-----------------------------------------------------------------------------*
pcsv6_bch_palette_tab:
*-----------------------------------------------------------------------------*
			dc.l	0
			dc.l	0
			dc.l	0
			dc.l	0

*-----------------------------------------------------------------------------*
pcsv6_cch_palette_tab:
*-----------------------------------------------------------------------------*
			dc.l	0
			dc.l	0
			dc.l	0
			dc.l	0

*-----------------------------------------------------------------------------*
pcsv6_vbl:	
*-----------------------------------------------------------------------------*
*	start timera ASAP	
*-----------------------------------------------------------------------------*
	clr.b	$fffffa19.w
	move.b	#97,$fffffa1f.w
	move.b	#4,$fffffa19.w
		
*-----------------------------------------------------------------------------*
	addq.l	#1,$462.w

*-----------------------------------------------------------------------------*
*	cycle fields, load video base, clear BG colour
*-----------------------------------------------------------------------------*
	move.l	d0,-(sp)	
	move.l	a0,-(sp)	
*-----------------------------------------------------------------------------*
	move.w	pcsv6_field_active(pc),d0
	add.w	d0,d0
	move.w	pcsv6_field_sequence(pc,d0.w),pcsv6_field_active
	add.w	d0,d0
	move.l	pcsv6_bch_palette_tab(pc,d0.w),a0
	add.l	pcsv6_bch_scroll,a0
	move.l	a0,$ffff8a24.w
	move.l	pcsv6_cch_palette_tab(pc,d0.w),a0
	add.l	pcsv6_cch_scroll,a0
	move.l	a0,pcsv6_palette
	move.l	pcsv6_displayfield_tab(pc,d0.w),d0
	add.l	pcsv6_pcs_scroll,d0
	ror.w	#8,d0
	move.l	d0,$ffff8200.w
	ror.w	#8,d0
	move.b	d0,$ffff820d.w
	clr.w	$ffff8240.w
	

	if	(enable_vscroll)
	cmp.w	#max_displayed_lines,pcsv6_num_lines
	ble	.endscrl
	
	subq.w	#1,pcsv6_scroll_delay
	bpl	.endscrl
	
	move.l	pcsv6_bch_scroll_next,pcsv6_bch_scroll
	move.l	pcsv6_cch_scroll_next,pcsv6_cch_scroll
	tst.b	pcsv6_scroll_dir
	beq.s	.down

.up:	sub.l	#display_bch_linewords*2,pcsv6_bch_scroll_next
	sub.l	#display_cch_linewords*2,pcsv6_cch_scroll_next
	sub.l	#display_linebytes,pcsv6_pcs_scroll
	subq.w	#1,pcsv6_scroll_line
	bne.s	.endscrl
	sf	pcsv6_scroll_dir
	bra.s	.endscrl

.down:	add.l	#display_bch_linewords*2,pcsv6_bch_scroll_next
	add.l	#display_cch_linewords*2,pcsv6_cch_scroll_next
	add.l	#display_linebytes,pcsv6_pcs_scroll
	addq.w	#1,pcsv6_scroll_line
	move.w	pcsv6_num_lines,d0
	sub.w	#display_lines,d0
	cmp.w	pcsv6_scroll_line,d0
	bne.s	.endscrl
	st	pcsv6_scroll_dir
.endscrl:	
	
	endif
		
*-----------------------------------------------------------------------------*
	move.l	(sp)+,a0
	move.l	(sp)+,d0
*-----------------------------------------------------------------------------*	
	rte

	
*-------------------------------------------------------*
pcsv6_hbl:
*-------------------------------------------------------*
	rte


.macro	cycles	num
;	rept	\1/2
;	or.l	d0,d0
;	endr
;	rept	\1%2
;	nop
;	endr
	.rept	\num
	nop
	.endr
	.endm

*-----------------------------------------------------------------------------*
pcsv6_timera_ste:
*-----------------------------------------------------------------------------*
	movem.l	d0-a6,-(sp)
*-----------------------------------------------------------------------------*
;	clr.b	$fffffa19.w
	bclr.b	#5,$fffffa0f.w
*-----------------------------------------------------------------------------*

	; make first line invisible (overscan corrupted bitmap)
	
	lea	$ffff8240.w,a6
	moveq	#0,d0
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+

	; line 0 is visible and non-empty in vscroll PCS images but only single palette active on this line
	; line 0 is visible but empty in non-vscroll PCS images
	
	lea	$ffff820a.w,a2
	lea	$ffff8260.w,a3
	lea	$ffff8a38.w,a4
	lea	$ffff8a3c.w,a5

	; !!! hack for 10 CPU cols
	move.l	pcsv6_palette(pc),a0	; src CPU cols 06...16
	lea	$ffff824c.w,a6		; dst CPU cols 06...16

	moveq	#-64,d7
	moveq	#3,d6
	moveq	#0,d0
	moveq	#2,d1
			
*-----------------------------------------------------------------------------*
*	wait for HBL
*-----------------------------------------------------------------------------*
	move.w	#$2100,sr
	stop	#$2100


*-----------------------------------------------------------------------------*
*	the fun begins here...
*-----------------------------------------------------------------------------*
.pcsv6_displayroutine:
*-----------------------------------------------------------------------------*
	move.w	#$2700,sr
	clr.b	$fffffa19.w

	move.l	a7,usp
	lea	$ffff824c.w,a7		; dst CPU cols 06...16
		
*-----------------------------------------------------------------------------*
*	top border removal
*-----------------------------------------------------------------------------*

;	cycles	86
	tburnrr	86,d2,d3
	
	clr.b	$ffff820a.w

;	cycles	9
	tburnrr	9,d2,d3

	move.b	#2,$ffff820a.w

*-----------------------------------------------------------------------------*
*	hardsync
*-----------------------------------------------------------------------------*

;	original non-scrolling version
;
;	lea	$ffff8209.w,a1
;	moveq	#128-1,d5
;.hsync:	tst.b	(a1)
;	beq.s	.hsync
;	move.b	(a1),d2
;	sub.b	d2,d5
;	lsr.l	d5,d5

;	STE scrolling version

	lea	$ffff8209.w,a1
	move.b	(a1),d2
.hsync:	cmp.b	(a1),d2
	beq.s	.hsync
	sub.b	(a1),d2
	lsr.l	d2,d2

*-----------------------------------------------------------------------------*

configure_mste	set	0

	; statically defined code
	include	"ste/b416c52.s"
	
*-----------------------------------------------------------------------------*

	move.l	usp,a7	
	movem.l	(sp)+,d0-a6
	rte

*-----------------------------------------------------------------------------*
pcsv6_timera_mste:
*-----------------------------------------------------------------------------*
	movem.l	d0-a6,-(sp)
*-----------------------------------------------------------------------------*
;	clr.b	$fffffa19.w
	bclr.b	#5,$fffffa0f.w
*-----------------------------------------------------------------------------*

	; make first line invisible (overscan corrupted bitmap)
	
	lea	$ffff8240.w,a6
	moveq	#0,d0
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+
	move.l	d0,(a6)+

	; line 0 is visible and non-empty in vscroll PCS images but only single palette active on this line
	; line 0 is visible but empty in non-vscroll PCS images
	
	lea	$ffff820a.w,a2
	lea	$ffff8260.w,a3
	lea	$ffff8a38.w,a4
	lea	$ffff8a3c.w,a5

	; !!! hack for 10 CPU cols
	move.l	pcsv6_palette(pc),a0	; src CPU cols 06...16
	lea	$ffff824c.w,a6		; dst CPU cols 06...16

	moveq	#-64,d7
	moveq	#3,d6
	moveq	#0,d0
	moveq	#2,d1
			
*-----------------------------------------------------------------------------*
*	wait for HBL
*-----------------------------------------------------------------------------*
	move.w	#$2100,sr
	stop	#$2100


*-----------------------------------------------------------------------------*
*	the fun begins here...
*-----------------------------------------------------------------------------*
.pcsv6_displayroutine:
*-----------------------------------------------------------------------------*
	move.w	#$2700,sr
	clr.b	$fffffa19.w

	move.l	a7,usp
	lea	$ffff824c.w,a7		; dst CPU cols 06...16
		
*-----------------------------------------------------------------------------*
*	top border removal
*-----------------------------------------------------------------------------*

;	cycles	86
	tburnrr	86,d2,d3
		
	clr.b	$ffff820a.w

;	cycles	9
	tburnrr	9,d2,d3
	
	move.b	#2,$ffff820a.w

*-----------------------------------------------------------------------------*
*	hardsync
*-----------------------------------------------------------------------------*

;	original non-scrolling version
;
;	lea	$ffff8209.w,a1
;	moveq	#128-1,d5
;.hsync:	tst.b	(a1)
;	beq.s	.hsync
;	move.b	(a1),d2
;	sub.b	d2,d5
;	lsr.l	d5,d5

;	STE scrolling version

	lea	$ffff8209.w,a1
	move.b	(a1),d2
.hsync:	cmp.b	(a1),d2
	beq.s	.hsync
	sub.b	(a1),d2
	lsr.l	d2,d2

*-----------------------------------------------------------------------------*

configure_mste	set	1

	; statically defined code
	include	"ste/b416c52.s"
	
*-----------------------------------------------------------------------------*

	move.l	usp,a7	
	movem.l	(sp)+,d0-a6
	rte

*-----------------------------------------------------------------------------*
pcsv6_reset_scroll:
*-----------------------------------------------------------------------------*
	clr.l		pcsv6_pcs_scroll
	clr.l		pcsv6_bch_scroll
	clr.l		pcsv6_cch_scroll
	clr.l		pcsv6_bch_scroll_next
	clr.l		pcsv6_cch_scroll_next
	;
	clr.w		pcsv6_scroll_dir
	clr.w		pcsv6_scroll_line
	clr.w		pcsv6_scroll_delay
	rts


*=============================================================================*
			.data
*=============================================================================*

pcsv6_mch_active:	dc.w	1	; ste

pcsv6_view_period:	dc.l	0
pcsv6_view_timeout:	dc.l	0

pcsv6_num_fields:	dc.w	0
pcsv6_num_lines:	dc.w	0

pcsv6_pcs_scroll:	dc.l	0
pcsv6_bch_scroll:	dc.l	0
pcsv6_cch_scroll:	dc.l	0
pcsv6_bch_scroll_next:	dc.l	0
pcsv6_cch_scroll_next:	dc.l	0

pcsv6_scroll_dir:	dc.w	0
pcsv6_scroll_line:	dc.w	0
pcsv6_scroll_delay:	dc.w	0

*=============================================================================*
			.bss
*=============================================================================*

; TODO: size
pcsv6_recovery_state:	ds.b	1024

*=============================================================================*
			.text
*=============================================================================*
