*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
* 	STE display module
*-------------------------------------------------------*


*-------------------------------------------------------*
	.text
*-------------------------------------------------------*

	.globl		_STE_ConfigureDisplayService
	.globl		_STE_InstallDisplayService
	.globl		_STE_RemoveDisplayService

	.globl		_g_displayservice_consumed_lines	; informs framebuffer of any difference between displaymemory start and framebuffer start
;	.globl		_g_tshift
	.globl		_g_linebytes

	.globl		_gp_active_group
	.globl		_g_vbl
	.globl		_g_fieldmask
	.globl		_min_pconsole
	.globl		_max_console
	
	.globl		vbl_sema
	.globl		_g_seek
	
	.globl		_AGT_ConsoleText_68k
	.if		^^defined ENABLE_AGT_AUDIO
	.globl		_YMSYS_VBCallback
	.endif

	.globl		_blackscreen

	.globl		_bFalcon030

*-------------------------------------------------------*
	
	include		"config.inc"

*-------------------------------------------------------*

tacr		=	$fffffa19
tbcr		=	$fffffa1b
cdcr		=	$fffffa1d
tadr		=	$fffffa1f
tbdr		=	$fffffa21
tcdr		=	$fffffa23
cddr		=	$fffffa25
iera		=	$fffffa07
ierb		=	$fffffa09
imra		=	$fffffa13
imrb		=	$fffffa15
isra		=	$fffffa0f
isrb		=	$fffffa11

*-------------------------------------------------------*
highlight_statusbar		=	0

use_auto_end_of_interrupt	=	0		; defaults to manual EOI for compatibility with other user-defined interrupts
use_blitter_compatibility	=	1		; default is to assume blitter is busy on STE, helps prevent flickery rasters
use_overscan			=	1		; enable 240 lines vs usual 200 (can be up to 274 but 240 is a good standard)
use_lower_border		=	1		; enable/disable lower border specifically, when use_overscan = 1
use_overscan_timera		=	1		; use timera instead of timerd for upper border sync

; internal timera/timerb schedule for overscan mode
displayservice_upper_delay	set	(94+4)		; safety padding of 6...3, but err towards more padding (smaller value) with blitter in use
timecritical_pad_tb		set	3-1		; TimerB padding (in scanlines)
timecritical_pad_ta		set	6-1		; TimerA padding
timecritical_pad_td		set	7-1		; TimerD padding

max_displayports	=	16

*-------------------------------------------------------*
	.abs
*-------------------------------------------------------*
;DISP_STE_200:		ds.w	1
;DISP_STE_240:		ds.w	1
;DISP_STE_256:		ds.w	1
;DISP_STE_272:		ds.w	1
;DISP_STE_136SS:		ds.w	1
;DISP_size:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	
	include		"tburn.s"

*-------------------------------------------------------*

.macro	blitoff
	.if		(use_blitter_compatibility)

	move.w		$ffff8a3c.w,-(sp)
	bclr.b		#7,$ffff8a3c.w

	.endif
	.endm

.macro	bliton
	.if		(use_blitter_compatibility)

	move.w		(sp)+,$ffff8a3c.w

	.endif
	.endm

;.macro	blitoffx	labl
;	.if		(use_blitter_compatibility)
;
;	move.b		$ffff8a3c.w,\labl+3
;	bclr.b		#7,$ffff8a3c.w
;
;	.endif
;	.endm
;
;.macro	blitonx
;	.if		(use_blitter_compatibility)
;
;	move.b		#0,$ffff8a3c.w
;	
;	.endif
;	.endm

*-------------------------------------------------------*

	include		"ste/dispcom.s"
	include		"ste/disp200.s"
	include		"ste/disp272.s"
	include		"ste/dispxs.s"
	
*-------------------------------------------------------*
*	some overscan modes burn display lines
*	which the playfield must compensate for
*-------------------------------------------------------*
_g_displayservice_consumed_lines:
*-------------------------------------------------------*
	dc.w		0				; 200
	dc.w		0				; 240	displayservice_ste240_d0_tb
	dc.w		0				; 256	displayservice_ste256_d0_tb
	dc.w		0				; 272	displayservice_ste272_d0_tb
	dc.w		0				; nickel

*-------------------------------------------------------*
_STE_ConfigureDisplayService:
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
	
;	lea		_g_displayservice_consumed_lines,a0	
;	move.w		#0,DISP_STE_200(a0)
;	move.w		#displayservice_ste240_d0_tb,DISP_STE_240(a0)
;	move.w		#displayservice_ste256_d0_tb,DISP_STE_256(a0)
;	move.w		#displayservice_ste272_d0_tb,DISP_STE_272(a0)	
;	move.w		#displayservice_ste136s_d0_tb,DISP_STE_136SS(a0)

;	move.w		#0,DISP_STF_200(a0)
;	move.w		#displayservice_ste240_d0_tb,DISP_STF_200(a0)
;	move.w		#displayservice_ste256_d0_tb,DISP_STF_200(a0)
;	move.w		#displayservice_ste272_d0_tb,DISP_STF_200(a0)


;	move.w		#0,_g_displayservice_consumed_lines
;
;	.if		(use_overscan)
;	tst.b		_bFalcon030
;	bne.s		.skip
;	move.w		#displayservice_ste240_d0_tb,_g_displayservice_consumed_lines
;.skip:
;	.endif
	
	.if		(^^defined TIMING_RASTERS | use_rasters)
	clr.w		BGRAST.w
	.endif
	
	clr.b		psilence_+3
	
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

*-------------------------------------------------------*
_STE_InstallDisplayService:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.mode:			ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
*-------------------------------------------------------*
	tst.b		displayservice_installed
	bne.s		.modify		
*-------------------------------------------------------*
	or.w		#$0700,sr
*-------------------------------------------------------*
	lea		savestate,a1
	move.l		$68.w,(a1)+
	move.l		$70.w,(a1)+
	move.l		$120.w,(a1)+
	move.l		$134.w,(a1)+
*-------------------------------------------------------*
	lea		drte,a0
	move.l		a0,$68.w			; HBL
	move.l		a0,$70.w			; VBL
	move.l		a0,$120.w			; TimerB
	move.l		a0,$134.w			; TimerA
*-------------------------------------------------------*
	and.w		#$fbff,sr
*-------------------------------------------------------*
	st		displayservice_installed
*-------------------------------------------------------*
.modify:
*-------------------------------------------------------*
	or.w		#$0700,sr
*-------------------------------------------------------*
	.if		(use_overscan_timera)
	.else
	.if		^^defined AGT_CONFIG_PROFILER
	.else
	bclr.b		#5,imrb.w			; kill TimerC when TimerD is timecritical. this stops $4ba from updating!
	.endif
	.endif
*-------------------------------------------------------*
	move.w		.mode+2(a6),d0
	lsl.w		#2,d0
	move.l		.service_table(pc,d0.w),$70.w	
;	jsr		_blackscreen
*-------------------------------------------------------*
	and.w		#$fbff,sr
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

.service_table:
	dc.l		displayservice_ste200_vbi
	dc.l		displayservice_nickel_vbi	; 240
	dc.l		displayservice_nickel_vbi	; 256
	dc.l		displayservice_ste272_vbi
	dc.l		displayservice_nickel_vbi
	
*-------------------------------------------------------*
_STE_RemoveDisplayService:
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
	tst.b		displayservice_installed
	beq.s		.no_op
*-------------------------------------------------------*
	; wait for vbl to elapse
	; note: display timers will already be scheduled
	move.w		$464.w,d0
.wvb0:	cmp.w		$464.w,d0
	beq.s		.wvb0
*-------------------------------------------------------*
	; turn off display timers and restore vectors
	or.w		#$0700,sr
	
	.if		(use_overscan)
	.if		(use_overscan_timera)
	bclr.b		#5,imra.w	; TimerA
	.else
	bclr.b		#4,imrb.w	; TimerD
	.endif	
	bclr.b		#0,imra.w	; TimerB
	.endif
	
	lea		savestate,a1
	move.l		(a1)+,$68.w
	move.l		(a1)+,$70.w
	move.l		(a1)+,$120.w
	move.l		(a1)+,$134.w
	and.w		#$fbff,sr
*-------------------------------------------------------*
	sf		displayservice_installed
*-------------------------------------------------------*
	move.w		$464.w,d0
.wvb2:	cmp.w		$464.w,d0
	beq.s		.wvb2
*-------------------------------------------------------*
.no_op:
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

drte:  	rte

*-------------------------------------------------------*



*-------------------------------------------------------*
displayservice_ste_vbi_dmasafe:
*-------------------------------------------------------*

	movem.l		d0-d7/a0-a6,-(sp)
	move.l		_gp_active_group,d0
	beq		.skip
	
	move.l		d0,a0
	moveq		#1,d0
	and.w		_g_vbl,d0
	and.w		_g_fieldmask,d0
	lsl.w		#2,d0
	move.l		(a0,d0.w),a0
	
	move.w		_g_linebytes,d1
	
	mulu.w		#29,d1
	
	move.l		(a0),d0
	lsr.l		#8,d0
	add.l		d1,d0
	
;	add.l		#188*16,d0
	move.b		d0,d1
	lsr.w		#8,d0
	move.l		d0,$ffff8200.w
	move.b		d1,$ffff820d.w
	
	move.b		4(a0),$ffff820f.w
	move.b		3(a0),$ffff8265.w

	move.l		d0,$ffff8204.w
	move.b		d1,$ffff8209.w

	move.b		4(a0),$ffff820f.w
	
	; load field palette
	
	lea		6(a0),a0
;	load palette immediately
	movem.l		(a0)+,d0-d7
	movem.l		d0-d7,$ffff8240.w

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

	movem.l		(sp)+,d0-d7/a0-a6
	subq.w		#1,_g_vbl
	
	addq.l		#1,$462.w	
	addq.l		#1,$466.w	

	bliton
	
 	rte

*-------------------------------------------------------*
displayservice_f030_vbi:
*-------------------------------------------------------*
	blitoff

	move.w		#$2700,sr

	movem.l		d0-d7/a0-a6,-(sp)
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

;	otherwise load palette immediately
	movem.l		(a0)+,d0-d7
	movem.l		d0-d7,$ffff8240.w

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

	movem.l		(sp)+,d0-d7/a0-a6
	subq.w		#1,_g_vbl

	addq.l		#1,$462.w	
	addq.l		#1,$466.w	

	bliton
	
  	rte


*-------------------------------------------------------*
_AGT_ConsoleText_68k:
*-------------------------------------------------------*
;	a0	string
;	a1	framebuffer
;	a2	*
;	d0	column
;	d1	row
;	d2	*
;	d3	*
*-------------------------------------------------------*

con_linewid	=	160

	.if		(use_debugconsole)
	
	move.w		#40,d3				; line chars remaining

	mulu.w		#con_linewid*6,d1	; line
	add.l		d1,a1

	moveq		#1,d1
	and.w		d0,d1		; 0 or 1 for even or odd char plots
	add.w		d1,a1	
	move.b		.init(pc,d1.w),d1

	and.w		#-2,d0
	add.w		d0,d0
	add.w		d0,d0
	add.w		d0,a1

	move.w		#$e0,d2
	lea		.recover(pc),a2
	bra.s		.start

.init:	dc.b		1,7
	
.next:	;cmp.w		#123-32,d0
	;bge		.invalid

	add.w		d0,d0
	move.w		.jtab(pc,d0.w),d0
	jmp		.jtab(pc,d0.w)

.recover:
	add.w		d1,a1
	eor.w		#7^1,d1

	subq.w		#1,d3			; stop at 40 chars
	beq.s		.clip

.con_recurse_:

.start:	move.w		d2,d0		
	add.b		(a0)+,d0
	bpl.s		.next
	cmp.w		d2,d0	
	bne		.special

.clip:

	; 0 = terminate
		
	.endif		;(use_debugconsole)

*-------------------------------------------------------*

	rts
	
*-------------------------------------------------------*

	.if		(use_debugconsole)

.jtab:	dc.w		.cSPC-.jtab	; space
	dc.w		.cEXC-.jtab	; !
	dc.w		.cDQU-.jtab	; "
	dc.w		.cHAS-.jtab	; #
	dc.w		.cDOL-.jtab	; $
	dc.w		.cPCT-.jtab	; %
	dc.w		.cAMP-.jtab	; &
	dc.w		.cSQU-.jtab	; '
	dc.w		.cOBR-.jtab	; (
	dc.w		.cCBR-.jtab	; )
	dc.w		.cSTR-.jtab	; *
	dc.w		.cPLS-.jtab	; +
	dc.w		.cCOM-.jtab	; ,
	dc.w		.cMIN-.jtab	; -
	dc.w		.cDOT-.jtab	; .
	dc.w		.cSLS-.jtab	; /

	dc.w		.c0-.jtab	; 0...
	dc.w		.c1-.jtab
	dc.w		.c2-.jtab
	dc.w		.c3-.jtab
	dc.w		.c4-.jtab
	dc.w		.c5-.jtab
	dc.w		.c6-.jtab
	dc.w		.c7-.jtab
	dc.w		.c8-.jtab
	dc.w		.c9-.jtab
	
	dc.w		.cCOL-.jtab	; :
	dc.w		.cSEM-.jtab	; ;
	dc.w		.cLAB-.jtab	; <
	dc.w		.cEQU-.jtab	; =
	dc.w		.cRAB-.jtab	; >
	dc.w		.cQUE-.jtab	; ?
	dc.w		.cAT-.jtab	; @

	dc.w		.cA-.jtab	; A...
	dc.w		.cB-.jtab
	dc.w		.cC-.jtab
	dc.w		.cD-.jtab
	dc.w		.cE-.jtab
	dc.w		.cF-.jtab
	dc.w		.cG-.jtab
	dc.w		.cH-.jtab
	dc.w		.cI-.jtab
	dc.w		.cJ-.jtab
	dc.w		.cK-.jtab
	dc.w		.cL-.jtab
	dc.w		.cM-.jtab
	dc.w		.cN-.jtab
	dc.w		.cO-.jtab
	dc.w		.cP-.jtab
	dc.w		.cQ-.jtab
	dc.w		.cR-.jtab
	dc.w		.cS-.jtab
	dc.w		.cT-.jtab
	dc.w		.cU-.jtab
	dc.w		.cV-.jtab
	dc.w		.cW-.jtab
	dc.w		.cX-.jtab
	dc.w		.cY-.jtab
	dc.w		.cZ-.jtab
	
	dc.w		.cOSB-.jtab	; [
	dc.w		.cSL2-.jtab	; \
	dc.w		.cCSB-.jtab	; ]
	dc.w		.cCAR-.jtab	; ^
	dc.w		.cUSC-.jtab	; _
	dc.w		.cAPO-.jtab	; `

	dc.w		.cA-.jtab	; a...
	dc.w		.cB-.jtab
	dc.w		.cC-.jtab
	dc.w		.cD-.jtab
	dc.w		.cE-.jtab
	dc.w		.cF-.jtab
	dc.w		.cG-.jtab
	dc.w		.cH-.jtab
	dc.w		.cI-.jtab
	dc.w		.cJ-.jtab
	dc.w		.cK-.jtab
	dc.w		.cL-.jtab
	dc.w		.cM-.jtab
	dc.w		.cN-.jtab
	dc.w		.cO-.jtab
	dc.w		.cP-.jtab
	dc.w		.cQ-.jtab
	dc.w		.cR-.jtab
	dc.w		.cS-.jtab
	dc.w		.cT-.jtab
	dc.w		.cU-.jtab
	dc.w		.cV-.jtab
	dc.w		.cW-.jtab
	dc.w		.cX-.jtab
	dc.w		.cY-.jtab
	dc.w		.cZ-.jtab

	dc.w		.cOCB-.jtab	; {
	dc.w		.cBAR-.jtab	; |
	dc.w		.cCCB-.jtab	; }
	dc.w		.cTIL-.jtab	; ~
	dc.w		.invalid-.jtab	; DEL


*-------------------------------------------------------*
.con_printline_:
*-------------------------------------------------------*

	tst.w		d3
	bne		.con_recurse_	
	rts

*-------------------------------------------------------*
.special_setcolumn:
*-------------------------------------------------------*
*	pad with spaces until column [c]
*-------------------------------------------------------*
		
	moveq		#0,d0
	move.b		(a0)+,d0	; new position
	sub.w		#40,d0
	add.w		d3,d0		; - current position
	ble		.start		; no change or -ve change

	; fill d0 spaces, updating d1/a1,d3
	movem.l		a0/a2,-(sp)
	lea		con_spaces_,a0
	add.w		d0,a0
	clr.b		(a0)		; temporary termination
	move.l		a0,-(sp)
	sub.w		d0,a0
	bsr		.con_printline_	; print N spaces
	move.l		(sp)+,a0
	move.b		#' ',(a0)	; restore termination
	movem.l		(sp)+,a0/a2

	bra		.start
	

	.if		(0)

;	skip without drawing spaces (not useful for console)

	move.l		a3,a1		; restore line start
			
	moveq		#1,d1
	and.w		d0,d1		; 0 or 1 for even or odd char plots
	add.w		d1,a1	
	move.b		.initc(pc,d1.w),d1

	and.w		#-2,d0
	add.w		d0,d0
	add.w		d0,d0
	add.w		d0,a1
	
	bra		.start
	
.initc:	dc.b		1,7

	.endif
	
*-------------------------------------------------------*
.special:
*-------------------------------------------------------*
	cmp.b		#10-32,d0
	beq.s		.noop
	cmp.b		#13-32,d0
	beq.s		.noop
	cmp.b		#2-32,d0
	beq.s		.special_setcolumn

*-------------------------------------------------------*
.noop:	bra		.start
*-------------------------------------------------------*
.invalid:
*-------------------------------------------------------*
	moveq		#%11111110,d0
	move.b		d0,(a1)
	move.b		d0,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		d0,con_linewid*3(a1)
	move.b		d0,con_linewid*4(a1)
	jmp		(a2)
*-------------------------------------------------------*
.cSPC:	moveq		#%00000000,d0
	move.b		d0,(a1)
	move.b		d0,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		d0,con_linewid*3(a1)
	move.b		d0,con_linewid*4(a1)
	jmp		(a2)
*-------------------------------------------------------*
.cCOL:	moveq		#%00000000,d0
	move.b		d0,(a1)
	move.b		#%00011000,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		#%00011000,con_linewid*3(a1)
	move.b		d0,con_linewid*4(a1)
	jmp		(a2)
.cSEM:	moveq		#%00000000,d0
	move.b		d0,(a1)
	move.b		#%00011000,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		#%00011000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.cLAB:	move.b		#%00000110,(a1)
	move.b		#%00011000,con_linewid*1(a1)
	move.b		#%01100000,con_linewid*2(a1)
	move.b		#%00011000,con_linewid*3(a1)
	move.b		#%00000110,con_linewid*4(a1)
	jmp		(a2)
.cEQU:	moveq		#%00000000,d0
	move.b		d0,(a1)
	move.b		#%00111100,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		#%00111100,con_linewid*3(a1)
	move.b		d0,con_linewid*4(a1)
	jmp		(a2)
.cRAB:	move.b		#%01100000,(a1)
	move.b		#%00011000,con_linewid*1(a1)
	move.b		#%00000110,con_linewid*2(a1)
	move.b		#%00011000,con_linewid*3(a1)
	move.b		#%01100000,con_linewid*4(a1)
	jmp		(a2)
.cQUE:	move.b		#%01111100,(a1)
	move.b		#%00000010,con_linewid*1(a1)
	move.b		#%00011100,con_linewid*2(a1)
	move.b		#%00000000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.cAT:	move.b		#%01111100,(a1)
	move.b		#%10011010,con_linewid*1(a1)
	move.b		#%10101010,con_linewid*2(a1)
	move.b		#%10011110,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
*-------------------------------------------------------*
.cEXC:	move.b		#%00011000,(a1)
	move.b		#%00011000,con_linewid*1(a1)
	move.b		#%00011000,con_linewid*2(a1)
	move.b		#%00000000,con_linewid*3(a1)
	move.b		#%00011000,con_linewid*4(a1)
	jmp		(a2)
.cDQU:	moveq		#%00000000,d0
	move.b		#%00101000,(a1)
	move.b		#%00101000,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		d0,con_linewid*3(a1)
	move.b		d0,con_linewid*4(a1)
	jmp		(a2)
.cHAS:	move.b		#%00101000,(a1)
	move.b		#%01111100,con_linewid*1(a1)
	move.b		#%00101000,con_linewid*2(a1)
	move.b		#%01111100,con_linewid*3(a1)
	move.b		#%00101000,con_linewid*4(a1)
	jmp		(a2)
.cDOL:	move.b		#%00111000,(a1)
	move.b		#%01010000,con_linewid*1(a1)
	move.b		#%00111000,con_linewid*2(a1)
	move.b		#%00010100,con_linewid*3(a1)
	move.b		#%00111000,con_linewid*4(a1)
	jmp		(a2)
.cPCT:	move.b		#%01100100,(a1)
	move.b		#%01101000,con_linewid*1(a1)
	move.b		#%00010000,con_linewid*2(a1)
	move.b		#%00101100,con_linewid*3(a1)
	move.b		#%01001100,con_linewid*4(a1)
	jmp		(a2)
.cAMP:	move.b		#%00110000,(a1)
	move.b		#%01001000,con_linewid*1(a1)
	move.b		#%01110000,con_linewid*2(a1)
	move.b		#%10001010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.cSQU:	moveq		#%00000000,d0
	move.b		#%00011000,(a1)
	move.b		#%00011000,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		d0,con_linewid*3(a1)
	move.b		d0,con_linewid*4(a1)
	jmp		(a2)
.cOBR:	move.b		#%00011000,(a1)
	move.b		#%00100000,con_linewid*1(a1)
	move.b		#%00100000,con_linewid*2(a1)
	move.b		#%00100000,con_linewid*3(a1)
	move.b		#%00011000,con_linewid*4(a1)
	jmp		(a2)
.cCBR:	move.b		#%00110000,(a1)
	move.b		#%00001000,con_linewid*1(a1)
	move.b		#%00001000,con_linewid*2(a1)
	move.b		#%00001000,con_linewid*3(a1)
	move.b		#%00110000,con_linewid*4(a1)
	jmp		(a2)
.cSTR:	move.b		#%01010100,(a1)
	move.b		#%00111000,con_linewid*1(a1)
	move.b		#%11111110,con_linewid*2(a1)
	move.b		#%00111000,con_linewid*3(a1)
	move.b		#%01010100,con_linewid*4(a1)
	jmp		(a2)
.cPLS:	move.b		#%00010000,(a1)
	move.b		#%00010000,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%00010000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.cCOM:	moveq		#%00000000,d0
	move.b		d0,(a1)
	move.b		d0,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		#%00011000,con_linewid*3(a1)
	move.b		#%00110000,con_linewid*4(a1)
	jmp		(a2)
.cMIN:	moveq		#%00000000,d0
	move.b		d0,(a1)
	move.b		d0,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		d0,con_linewid*3(a1)
	move.b		d0,con_linewid*4(a1)
	jmp		(a2)
.cDOT:	moveq		#%00000000,d0
	move.b		d0,(a1)
	move.b		d0,con_linewid*1(a1)
	move.b		d0,con_linewid*2(a1)
	move.b		#%00011000,con_linewid*3(a1)
	move.b		#%00011000,con_linewid*4(a1)
	jmp		(a2)
.cSLS:	move.b		#%00000100,(a1)
	move.b		#%00001000,con_linewid*1(a1)
	move.b		#%00010000,con_linewid*2(a1)
	move.b		#%00100000,con_linewid*3(a1)
	move.b		#%01000000,con_linewid*4(a1)
	jmp		(a2)	
*-------------------------------------------------------*
.cA:	move.b		#%01111100,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%11111110,con_linewid*2(a1)
	move.b		#%10000010,con_linewid*3(a1)
	move.b		#%10000010,con_linewid*4(a1)
	jmp		(a2)
.cB:	move.b		#%11111100,(a1)
	move.b		#%01000010,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%01000010,con_linewid*3(a1)
	move.b		#%11111100,con_linewid*4(a1)
	jmp		(a2)
.cC:	move.b		#%01111110,(a1)
	move.b		#%10000000,con_linewid*1(a1)
	move.b		#%10000000,con_linewid*2(a1)
	move.b		#%10000000,con_linewid*3(a1)
	move.b		#%01111110,con_linewid*4(a1)
	jmp		(a2)
.cD:	move.b		#%11111100,(a1)
	move.b		#%01000010,con_linewid*1(a1)
	move.b		#%01000010,con_linewid*2(a1)
	move.b		#%01000010,con_linewid*3(a1)
	move.b		#%11111100,con_linewid*4(a1)
	jmp		(a2)
.cE:	move.b		#%11111110,(a1)
	move.b		#%01000000,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%01000000,con_linewid*3(a1)
	move.b		#%11111110,con_linewid*4(a1)
	jmp		(a2)
.cF:	move.b		#%11111110,(a1)
	move.b		#%01000000,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%01000000,con_linewid*3(a1)
	move.b		#%11000000,con_linewid*4(a1)
	jmp		(a2)
.cG:	move.b		#%01111100,(a1)
	move.b		#%10000000,con_linewid*1(a1)
	move.b		#%10001110,con_linewid*2(a1)
	move.b		#%10000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.cH:	move.b		#%10000010,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%11111110,con_linewid*2(a1)
	move.b		#%10000010,con_linewid*3(a1)
	move.b		#%10000010,con_linewid*4(a1)
	jmp		(a2)
.cI:	move.b		#%01111100,(a1)
	move.b		#%00010000,con_linewid*1(a1)
	move.b		#%00010000,con_linewid*2(a1)
	move.b		#%00010000,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.cJ:	move.b		#%00001110,(a1)
	move.b		#%00000100,con_linewid*1(a1)
	move.b		#%00000100,con_linewid*2(a1)
	move.b		#%01000100,con_linewid*3(a1)
	move.b		#%00111000,con_linewid*4(a1)
	jmp		(a2)
.cK:	move.b		#%11000110,(a1)
	move.b		#%01001000,con_linewid*1(a1)
	move.b		#%01110000,con_linewid*2(a1)
	move.b		#%01001000,con_linewid*3(a1)
	move.b		#%11000110,con_linewid*4(a1)
	jmp		(a2)
.cL:	move.b		#%11000000,(a1)
	move.b		#%01000000,con_linewid*1(a1)
	move.b		#%01000000,con_linewid*2(a1)
	move.b		#%01000000,con_linewid*3(a1)
	move.b		#%11111110,con_linewid*4(a1)
	jmp		(a2)
.cM:	move.b		#%10000010,(a1)
	move.b		#%11000110,con_linewid*1(a1)
	move.b		#%10101010,con_linewid*2(a1)
	move.b		#%10010010,con_linewid*3(a1)
	move.b		#%10000010,con_linewid*4(a1)
	jmp		(a2)
.cN:	move.b		#%11000010,(a1)
	move.b		#%10100010,con_linewid*1(a1)
	move.b		#%10010010,con_linewid*2(a1)
	move.b		#%10001010,con_linewid*3(a1)
	move.b		#%10000110,con_linewid*4(a1)
	jmp		(a2)
.cO:	move.b		#%01111100,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%10000010,con_linewid*2(a1)
	move.b		#%10000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.cP:	move.b		#%11111100,(a1)
	move.b		#%01000010,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%01000000,con_linewid*3(a1)
	move.b		#%11000000,con_linewid*4(a1)
	jmp		(a2)
.cQ:	move.b		#%01111100,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%10001010,con_linewid*2(a1)
	move.b		#%10000110,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.cR:	move.b		#%11111100,(a1)
	move.b		#%01000010,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%01000100,con_linewid*3(a1)
	move.b		#%11000010,con_linewid*4(a1)
	jmp		(a2)
.cS:	move.b		#%01111100,(a1)
	move.b		#%10000000,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%00000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.cT:	move.b		#%11111110,(a1)
	move.b		#%00010000,con_linewid*1(a1)
	move.b		#%00010000,con_linewid*2(a1)
	move.b		#%00010000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.cU:	move.b		#%10000010,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%10000010,con_linewid*2(a1)
	move.b		#%10000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.cV:	move.b		#%10000010,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%01000100,con_linewid*2(a1)
	move.b		#%00101000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.cW:	move.b		#%10000010,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%10010010,con_linewid*2(a1)
	move.b		#%10010010,con_linewid*3(a1)
	move.b		#%01101100,con_linewid*4(a1)
	jmp		(a2)
.cX:	move.b		#%10000010,(a1)
	move.b		#%01000100,con_linewid*1(a1)
	move.b		#%00111000,con_linewid*2(a1)
	move.b		#%01000100,con_linewid*3(a1)
	move.b		#%10000010,con_linewid*4(a1)
	jmp		(a2)
.cY:	move.b		#%10000010,(a1)
	move.b		#%01000100,con_linewid*1(a1)
	move.b		#%00111000,con_linewid*2(a1)
	move.b		#%00010000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.cZ:	move.b		#%11111110,(a1)
	move.b		#%00001000,con_linewid*1(a1)
	move.b		#%00010000,con_linewid*2(a1)
	move.b		#%00100000,con_linewid*3(a1)
	move.b		#%11111110,con_linewid*4(a1)
	jmp		(a2)
*-------------------------------------------------------*
.cOSB:	move.b		#%00111000,(a1)
	move.b		#%00100000,con_linewid*1(a1)
	move.b		#%00100000,con_linewid*2(a1)
	move.b		#%00100000,con_linewid*3(a1)
	move.b		#%00111000,con_linewid*4(a1)
	jmp		(a2)
.cSL2:	move.b		#%01000000,(a1)
	move.b		#%00100000,con_linewid*1(a1)
	move.b		#%00010000,con_linewid*2(a1)
	move.b		#%00001000,con_linewid*3(a1)
	move.b		#%00000100,con_linewid*4(a1)
	jmp		(a2)
.cCSB:	move.b		#%00111000,(a1)
	move.b		#%00001000,con_linewid*1(a1)
	move.b		#%00001000,con_linewid*2(a1)
	move.b		#%00001000,con_linewid*3(a1)
	move.b		#%00111000,con_linewid*4(a1)
	jmp		(a2)
.cCAR:	move.b		#%00010000,(a1)
	move.b		#%00101000,con_linewid*1(a1)
	move.b		#%01000100,con_linewid*2(a1)
	move.b		#%00000000,con_linewid*3(a1)
	move.b		#%00000000,con_linewid*4(a1)
	jmp		(a2)
.cUSC:	move.b		#%00000000,(a1)
	move.b		#%00000000,con_linewid*1(a1)
	move.b		#%00000000,con_linewid*2(a1)
	move.b		#%00000000,con_linewid*3(a1)
	move.b		#%11111110,con_linewid*4(a1)
	jmp		(a2)
.cAPO:	move.b		#%00011000,(a1)
	move.b		#%00001000,con_linewid*1(a1)
	move.b		#%00000000,con_linewid*2(a1)
	move.b		#%00000000,con_linewid*3(a1)
	move.b		#%00000000,con_linewid*4(a1)
	jmp		(a2)
*-------------------------------------------------------*
.c0:	move.b		#%01111100,(a1)
	move.b		#%10001010,con_linewid*1(a1)
	move.b		#%10010010,con_linewid*2(a1)
	move.b		#%10100010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.c1:	move.b		#%00110000,(a1)
	move.b		#%01110000,con_linewid*1(a1)
	move.b		#%00010000,con_linewid*2(a1)
	move.b		#%00010000,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.c2:	move.b		#%01111100,(a1)
	move.b		#%00000110,con_linewid*1(a1)
	move.b		#%00111100,con_linewid*2(a1)
	move.b		#%01000000,con_linewid*3(a1)
	move.b		#%11111110,con_linewid*4(a1)
	jmp		(a2)
.c3:	move.b		#%01111100,(a1)
	move.b		#%00000010,con_linewid*1(a1)
	move.b		#%00111100,con_linewid*2(a1)
	move.b		#%00000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.c4:	move.b		#%00100010,(a1)
	move.b		#%01000010,con_linewid*1(a1)
	move.b		#%11111110,con_linewid*2(a1)
	move.b		#%00000010,con_linewid*3(a1)
	move.b		#%00000010,con_linewid*4(a1)
	jmp		(a2)
.c5:	move.b		#%11111110,(a1)
	move.b		#%10000000,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%00000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.c6:	move.b		#%01111000,(a1)
	move.b		#%10000000,con_linewid*1(a1)
	move.b		#%11111100,con_linewid*2(a1)
	move.b		#%10000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.c7:	move.b		#%11111110,(a1)
	move.b		#%00000010,con_linewid*1(a1)
	move.b		#%00001100,con_linewid*2(a1)
	move.b		#%00010000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.c8:	move.b		#%00111000,(a1)
	move.b		#%01000100,con_linewid*1(a1)
	move.b		#%01111100,con_linewid*2(a1)
	move.b		#%10000010,con_linewid*3(a1)
	move.b		#%01111100,con_linewid*4(a1)
	jmp		(a2)
.c9:	move.b		#%01111100,(a1)
	move.b		#%10000010,con_linewid*1(a1)
	move.b		#%01111110,con_linewid*2(a1)
	move.b		#%00000010,con_linewid*3(a1)
	move.b		#%00111100,con_linewid*4(a1)
	jmp		(a2)
*-------------------------------------------------------*
.cOCB:	move.b		#%00011000,(a1)
	move.b		#%00100000,con_linewid*1(a1)
	move.b		#%00100000,con_linewid*2(a1)
	move.b		#%00100000,con_linewid*3(a1)
	move.b		#%00011000,con_linewid*4(a1)
	jmp		(a2)
.cBAR:	move.b		#%00010000,(a1)
	move.b		#%00010000,con_linewid*1(a1)
	move.b		#%00000000,con_linewid*2(a1)
	move.b		#%00010000,con_linewid*3(a1)
	move.b		#%00010000,con_linewid*4(a1)
	jmp		(a2)
.cCCB:	move.b		#%00110000,(a1)
	move.b		#%00001000,con_linewid*1(a1)
	move.b		#%00001000,con_linewid*2(a1)
	move.b		#%00001000,con_linewid*3(a1)
	move.b		#%00110000,con_linewid*4(a1)
	jmp		(a2)
.cTIL:	move.b		#%00000000,(a1)
	move.b		#%00100000,con_linewid*1(a1)
	move.b		#%01010100,con_linewid*2(a1)
	move.b		#%00001000,con_linewid*3(a1)
	move.b		#%00000000,con_linewid*4(a1)
	jmp		(a2)

*-------------------------------------------------------*

	include		"con_xprn.s"
	
	.endif		; (use_debugconsole)
	
*-------------------------------------------------------*

			.if	0
			
			.abs
*-------------------------------------------------------*
th_routine:		ds.l	1
th_prev:		ds.l	1
th_next:		ds.l	1
th_priority:		ds.w	1
th_size_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	
scheduler_interrupt:

	; round-robin
	; next thread:
	; - if (prio > this)
	;   - insert sorted resume, current
	;   - switch immediate
	; - if (prio <= this)
	;     - insert sorted resume, new
	
	movem.l		d0-d2/a0/a1,-(sp)
	move.l		g_activethread,a0
	move.w		th_priority(a0),d0
	move.w		#$8000,d2			; highest priority seen so far
		
	lea		g_thread_sentinel_begin,a1
	move.l		th_next(a1),d1
	beq.s		.finish
	cmp.w		th_priority(a1),d0
	blo.s		.switch_now

	cmp.w		th_priority(a1),d2
	
	
	move.l		

.switch:	
	; 
	
.finish:
	movem.l		(sp)+,d0-d2/a0/a1
	rte

			.endif
			
*-------------------------------------------------------*
	.data
*-------------------------------------------------------*

con_palette:		dc.w	$034,$fff,$000,$000

ste_palette:		dc.l	empty
empty:			dcb.w	16,0
	
;_g_displayservice_consumed_lines:
;			dcb.b	DISP_size,0
			
;_g_tshift:		dc.w	0
_g_linebytes:		dc.w	0

_g_seek:		dc.w	0

displayservice_installed:
			dc.w	0

con_spaces_:		dcb.b	40,' '

; shared
psilence_:
	dc.l		rsilence_+255
			
*-------------------------------------------------------*
	.bss
*-------------------------------------------------------*

savestate:		ds.l	4

; shared
rsilence_:
	ds.b		(160*40)+256

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
