*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	System services
*-------------------------------------------------------*

*-------------------------------------------------------*
	
	include		"config.inc"

*-------------------------------------------------------*

timera		=	$134
timerb		=	$120
timerc		=	$114
timerd		=	$110

tacr		=	$fffffa19
tbcr		=	$fffffa1b
cdcr		=	$fffffa1d
tbdr		=	$fffffa21
tcdr		=	$fffffa23
cddr		=	$fffffa25
iera		=	$fffffa07
ierb		=	$fffffa09
imra		=	$fffffa13
imrb		=	$fffffa15
isra		=	$fffffa0f
isrb		=	$fffffa11

cookie_jar	=	$5a0


			.if	^^defined AGT_CONFIG_AUDIO_200HZ
configure_50hz		=	0
			.else
configure_50hz		=	1
			.endif

	.globl		_bSTF	
	.globl		_bSTE	
	.globl		_bMegaSTE
	.globl		_bFalcon030
	.globl		_bTT030	

*-------------------------------------------------------*

	.globl		_SYS_Configure
	.globl		_SYS_Save
	.globl		_SYS_Claim
	.globl		_SYS_Restore
	
	.globl		_ST_FlushInput
	.globl		_ST_ResetInput
	.globl		_ST_DisableInput
	.globl		_ST_ResumeInput
	
	.if		^^defined ENABLE_AGT_AUDIO
	.globl		_YMSYS_Callback
	.globl		_YMSYS_VBCallback
	.endif

	.globl		vbl_sema
	
	.globl		_blackscreen

*-------------------------------------------------------*

	include		"prf.s"

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*

*-------------------------------------------------------*
_SYS_Configure:
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
;	todo: get machine type etc.

	move.w		$464.w,d0
.wvb0:	cmp.w		$464.w,d0
	beq.s		.wvb0

;	or.w		#$0700,sr
;	move.b		$ffff8265.w,d0
;	not.b		$ffff8265.w
;	cmp.b		$ffff8265.w,d0
;	sne		bSTE
;	move.b		d0,$ffff8265.w
;	and.w		#$fbff,sr
*-------------------------------------------------------*
*	detect machine type
*-------------------------------------------------------*

	move.l		#'_MCH',d0
	bsr		cookie_search
	tst.l		d0
	sne		_bSTF
	bne		.dunno
	move.l		4(a0),d0
	seq		_bSTF

	move.l		#$ffff0000,d1
	and.l		d0,d1
	cmp.l		#$00010000,d1
	seq		_bSTE
	cmp.l		#$00010010,d0
	seq		_bMegaSTE
	
	cmp.l		#$00020000,d0
	seq		_bTT030
	cmp.l		#$00030000,d0
	seq		_bFalcon030
		
;	tst.b		_bFalcon030
;	bne		*

;	tst.b		bFalcon030
;	beq.s		.not_f030
;	move.b		#%00000000,ffff8007.w
;.not_f030:

;	move.l		8.w,a3
;	move.l		#.cont,8.w
;        move.l		sp,a2
;	clr.b		$ffff8e21.w
;.cont:  move.l		a2,sp
;        move.l		a3,8.w


	tst.b		_bFalcon030
	beq.s		.nf030
	moveq		#0,d0
;	movec		d0,cacr
.nf030:
*-------------------------------------------------------*
*	8mhz switch down MegaSTE
*-------------------------------------------------------*
	tst.b		_bMegaSTE
	beq.s		.not_mste
	clr.b		$ffff8e21.w
.not_mste:

.dunno:
*-------------------------------------------------------*
	.if		^^defined AGT_CONFIG_PROFILER
	jsr		_AGT_Sys_ProfilerInit
	.endif
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

*-------------------------------------------------------*
_SYS_Save:
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
	or.w		#$0700,sr
*-------------------------------------------------------*
	lea		savestate_store,a5
*-------------------------------------------------------*
*	palette
*-------------------------------------------------------*
	lea		32+$ffff8240.w,a0
	move.l		-(a0),-(a5)
	move.l		-(a0),-(a5)
	move.l		-(a0),-(a5)
	move.l		-(a0),-(a5)
	move.l		-(a0),-(a5)
	move.l		-(a0),-(a5)
	move.l		-(a0),-(a5)
	move.l		-(a0),-(a5)
*-------------------------------------------------------*
*	screenbase
*-------------------------------------------------------*
 	move.w		#3,-(sp)			; log
	trap		#14
	move.l		d0,-(a5)
 	move.w		#2,-(sp)			; phys
	trap		#14
	move.l		d0,-(a5)
	addq.l    	#4,sp
	
;	todo: save/restore palette
	
*-------------------------------------------------------*
*	blitter
*-------------------------------------------------------*
	tst.b		_bSTF
	bne.s		.no_blit
	lea		$ffff8a3c.w,a0
	moveq		#(($ff8a3c-$ff8a02)>>1)-1,d0
.lblt:	move.w		-(a0),-(a5)
	dbra		d0,.lblt
.no_blit:
*-------------------------------------------------------*
*	video
*-------------------------------------------------------*	
	tst.b		_bSTF
	bne.s		.nste
	move.b		$ffff8265.w,-(a5)
	move.b		$ffff820e.w,-(a5)
.nste:	move.b		$ffff820a.w,-(a5)
	move.b		$ffff8260.w,-(a5)
*-------------------------------------------------------*
*	vectors
*-------------------------------------------------------*
	lea		$140.w,a0
	moveq		#(($140-$8)>>2)-1,d0
.vecs:	move.l		-(a0),-(a5)
	dbra		d0,.vecs
*-------------------------------------------------------*
*	MFP state
*-------------------------------------------------------*
	move.b		d0,-(a5)
	move.b		iera.w,-(a5)
	move.b		imra.w,-(a5)
	move.b		ierb.w,-(a5)
	move.b		imrb.w,-(a5)
	move.b		cdcr.w,-(a5)
	move.b		tbcr.w,-(a5)
	move.b		tacr.w,-(a5)
*-------------------------------------------------------*
	and.w		#$fbff,sr
*-------------------------------------------------------*
	move.l		a5,savestate_restorepoint
	cmp.l		#savestate_restore_max,a5
.lock:	blt.s		.lock
*-------------------------------------------------------*	
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

*-------------------------------------------------------*
_SYS_Claim:
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
*	Halt all input reporting via ACIA
*-------------------------------------------------------*
	jsr		_ST_DisableInput

*-------------------------------------------------------*

	move.l		$462.w,d0
.wtv:	cmp.l		$462.w,d0
	beq.s		.wtv

*-------------------------------------------------------*
	or.w		#$0700,sr
*-------------------------------------------------------*

	clr.l		VBServiceVec.w
	clr.b		DMAFlag.w

*-------------------------------------------------------*
*	Mega STE
*-------------------------------------------------------*

; from Paranoid/Paradox:
;
;  ! The Mega STE has a register to control speed and cache
;    status:
;     $FFFF8E21  - - - -  - - X X
;     
;     Bit 0 controls the clockspeed (0 = 8 MHz, 1 = 16 MHz),
;     the upper bit controls the cache (0 = Cache off, 1 = cache on).
;     Some docs say that all upper 15 bits of $FFFF8E20 need to
;     be set to "1" to turn the cache on.
;     Writing to this register in anything but a Mega STE will
;     most probably lead to a crash, so be sure to check the
;     Cookie Jar for _MCH to estimate wether this is a Mega STE
;     or not (Upper word is 1 for STE, lower word is 0x0010 for
;     Mega STE, 0x0000 for anything else).	


*-------------------------------------------------------*
*	display basics
*-------------------------------------------------------*
	
	clr.b		$ffff8265.w
	clr.b		$ffff820f.w	
	clr.b		$ffff8260.w
	move.b		#2,$ffff820a.w
	
	jsr		_blackscreen

*-------------------------------------------------------*
*	vectors
*-------------------------------------------------------*
	lea		drte,a0
	move.l		a0,$68.w			; HBL
	move.l		#dummy_vbl,$70.w		; VBL
;	move.l		#dvbl,$70.w

	if		^^defined ENABLE_AGT_AUDIO
	if		(configure_50hz)
	else
	move.l		#ym_td,$110.w			; TimerD
	endif
	endif

;	move.l		#dummy_tc,$114.w		; TimerC
	move.l		a0,$118.w			; IKBD/MIDI ACIA
	move.l		a0,$120.w			; TimerB
	move.l		a0,$134.w			; TimerA
	move.l		a0,$13c.w			; GPI7

*-------------------------------------------------------*
*	MFP state
*-------------------------------------------------------*
	clr.b		$fffffa07.w			; IEA (Timer-A & B)
	clr.b		$fffffa13.w			; IMA (Timer-A & B)
	clr.b		$fffffa09.w			; IEB (Timer-C & D)
	clr.b		$fffffa15.w			; IMB (Timer-C & D)

*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_PROFILER
	move.l		#profiler_tc,timerc.w
	.else
	move.l		#timer_tc,timerc.w
	.endif

	and.b		#%00001111,cdcr.w

	.if		^^defined AGT_CONFIG_PROFILER
	move.b		#77,tcdr.w		; 2457600/77 = 31916 (/64 = 498Hz)
	.else
	move.b		#192,tcdr.w		; 2457600/192 = 12800Hz
	.endif

	or.b		#(5<<4),cdcr.w		; 12800/64 = 200Hz

	bset		#5,imrb.w
	bset		#5,ierb.w
		
*-------------------------------------------------------*
	and.w		#$fbff,sr
*-------------------------------------------------------*	
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

drte:	rte

;loader_rerouter:
;	ori.w		#$0700,sr
;	andi.w		#$fbff,sr
;loader_reroute:
;	jmp		$12345678

	
dvbl:	movem.l		d0-a6,-(sp)
;	move.w		$ffff8240.w,-(sp)
		

;	bra		*

	addq.w		#1,vbl_sema
	bgt.s		.nsvc
	tst.w		2+VBServiceVec.w
	beq.s		.nsvc

	
;	move.w		#$770,$ffff8240.w
	

	
	move.l		VBServiceVec.w,a0
	jsr		(a0)

.nsvc:	subq.w		#1,vbl_sema

;	move.w		(sp)+,$ffff8240.w
	movem.l		(sp)+,d0-a6

dummy_vbl:
	addq.l		#1,$462.w
	addq.l		#1,$466.w
	rte
	
*-------------------------------------------------------*
timer_tc:	
*-------------------------------------------------------*
;dummy_tc:
	bclr		#5,isrb.w
	addq.l		#1,$4ba.w
	rte
	
	.if		^^defined AGT_CONFIG_PROFILER
*-------------------------------------------------------*
profiler_tc:	
*-------------------------------------------------------*
;	todo: 32k align, or could be 256b align if < 64 items (!)
	move.w		d0,-(sp)
	move.l		a0,-(sp)
	move.w		prf_ctx.w,d0
	add.w		d0,d0
	add.w		d0,d0
	lea		prf_contexts(pc,d0.w),a0
	addq.l		#1,(a0)
	move.l		(sp)+,a0
	move.w		(sp)+,d0
*-------------------------------------------------------*
	addq.l		#1,_tm_clock
	addq.l		#1,$4ba.w
*-------------------------------------------------------*
	bclr		#5,isrb.w
	rte
	
prf_contexts:		dcb.l	prf_context_max+1,0
	
	.endif
	
	if		^^defined ENABLE_AGT_AUDIO	

ym_td:
;	move.w		$ffff8240.w,-(sp)
;	move.w		#-1,$ffff8240.w
	
	bclr		#4,isrb.w
;	ori.w		#$0700,sr
	andi.w		#$fbff,sr
		
	.if		^^defined ENABLE_AGT_AUDIO
	movem.l		d0-a6,-(sp)
	jsr		_YMSYS_Callback
	movem.l		(sp)+,d0-a6
	.endif

	;and.b		#$f0,cdcr.w
	
;	move.w		(sp)+,$ffff8240.w
	rte
	
	endif


*-------------------------------------------------------*
_SYS_Restore:
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
	jsr		_ST_FlushInput
	jsr		_ST_DisableInput
*-------------------------------------------------------*
	or.w		#$0700,sr
	move.l		savestate_restorepoint,a5
*-------------------------------------------------------*
*	MFP state
*-------------------------------------------------------*
	clr.b		iera.w
	clr.b		ierb.w
	move.b		(a5)+,tacr.w
	move.b		(a5)+,tbcr.w
	move.b		(a5)+,cdcr.w
	move.b		(a5)+,imrb.w
	move.b		(a5)+,ierb.w
	move.b		(a5)+,imra.w
	move.b		(a5)+,iera.w	
	move.b		(a5)+,d0
*-------------------------------------------------------*
*	vectors
*-------------------------------------------------------*
	lea		$8.w,a0
	moveq		#(($140-$8)>>2)-1,d0
.vecs:	move.l		(a5)+,(a0)+
	dbra		d0,.vecs
*-------------------------------------------------------*
*	video
*-------------------------------------------------------*
	move.b		(a5)+,$ffff8260.w
	move.b		(a5)+,$ffff820a.w
	tst.b		_bSTF
	bne.s		.nste
	move.b		(a5)+,$ffff820e.w
	move.b		(a5)+,$ffff8265.w
.nste:
*-------------------------------------------------------*
*	blitter
*-------------------------------------------------------*
	tst.b		_bSTF
	bne.s		.no_blit
	lea		$ffff8a02.w,a0
	moveq		#(($ff8a3c-$ff8a02)>>1)-1,d0
.lblt:	move.w		(a5)+,(a0)+
	dbra		d0,.lblt
.no_blit:
*-------------------------------------------------------*
*	screenbase
*-------------------------------------------------------*
	move.l		(a5)+,d0
	lsr.w		#8,d0
	move.l		d0,$ffff8200.w
	move.l		(a5)+,d0
*-------------------------------------------------------*
*	palette
*-------------------------------------------------------*
	lea		$ffff8240.w,a0
	move.l		(a5)+,(a0)+
	move.l		(a5)+,(a0)+
	move.l		(a5)+,(a0)+
	move.l		(a5)+,(a0)+
	move.l		(a5)+,(a0)+
	move.l		(a5)+,(a0)+
	move.l		(a5)+,(a0)+
	move.l		(a5)+,(a0)+
*-------------------------------------------------------*
	and.w		#$fbff,sr
*-------------------------------------------------------*
*	Resume input reporting via ACIA
*-------------------------------------------------------*
;	jsr		_ST_ResetInput
	jsr		_ST_ResumeInput
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

*-------------------------------------------------------*
_blackscreen:
*-------------------------------------------------------*
	moveq		#0,d0
	moveq		#0,d1
	moveq		#0,d2
	moveq		#0,d3
	moveq		#0,d4
	moveq		#0,d5
	moveq		#0,d6
	moveq		#0,d7
	movem.l		d0-d7,$ffff8240.w
	rts
		

*---------------------------------------------------------------*
cookie_search:
*---------------------------------------------------------------*
	move.l		cookie_jar.w,a0
.loop:	move.l		(a0),d1
	beq.s		.fail
	cmp.l		d0,d1
	beq.s		.find
	addq.l		#8,a0
	bra.s		.loop
.find:	moveq		#0,d0
	bra.s		.end
.fail:	moveq		#-1,d0
.end:	rts

*-------------------------------------------------------*
	.data
*-------------------------------------------------------*

vbl_sema:		dc.w	-1
	
*-------------------------------------------------------*
	.bss
*-------------------------------------------------------*

_tm_clock:		ds.l	1
			
savestate_restorepoint:	ds.l	1

savestate_restore_max:	ds.l	2			; MFP
			ds.l	(($140-$8)>>2)		; vectors
			ds.l	1			; display
			ds.w	29			; blitter
			ds.l	2			; screen base
			ds.l	8			; palette
savestate_store:
			ds.b	256
			
_bSTF:			ds.b	1	
_bSTE:			ds.b	1	
_bMegaSTE:		ds.b	1		
_bFalcon030:		ds.b	1		
_bTT030:		ds.b	1	
			even
						
*-------------------------------------------------------*
	.text
*-------------------------------------------------------*

