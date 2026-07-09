
	include "UDDefines.s"
	include "UDVideo.s"
	include "UDResetter.s"
	
************************************************************
* Project:	ultraDev
* Module:	UDDebugScreen
* Code:		ultra
* Created:	22.12.2019
* Notes:	Service routines for debug screen printing
* Modified:
* Changes:	dml/agtools (minimal routines for console)
*
* (c) 2019 cream
************************************************************


****************************************************************************
	.globl	_UDDbgScrClear
_UDDbgScrClear:

; clears the screen and sets all colors to white
; ->d0 clear value in ascii


	lea	$fb0000,a0
	and	#$ff,d0
	sub	#$20,d0
	move	UDRegWriteToScreenPos,d1
	move	(a0),d1			;set position to 0

	move	UDRegWriteToScreen,d1
	or	#$700,d0
	add	d0,d0

	; adjust size for 80c mode
	move.w	#(UDScrCharsX*UDScrCharsY)-1,d1
	move.w	UDVideoReg1(pc),d2
	and	#%00100000,d2
	beq.s	.n80c
	move.w	#(2*UDScrCharsX*UDScrCharsY80)-1,d1
.n80c:
		
.UDDbgScrClearLoop:
	move.w	(a0,d0),d2		;write char
	dbf	d1,.UDDbgScrClearLoop
	rts	

****************************************************************************
	.globl	_UDDbgScrSetPosition
_UDDbgScrSetPosition:
; sets the printing position
; ->d0 x
; ->d1 y
	movem.l	d0-d2/a0,-(a7)

	mulu	#UDScrCharsX,d1
	lea	UDVideoReg1(pc),a0
	move	(a0),d2
	and	#%00100000,d2
	beq.s	.UDDbgScrSetPositionNo80
	add	d1,d1
.UDDbgScrSetPositionNo80:

	add	d0,d1
	add	d1,d1

	move	UDRegWriteToScreenPos,d0
	lea	$fb0000,a0
	move	(a0,d1),d0
	movem.l	(a7)+,d0-d2/a0
	rts

****************************************************************************
	.globl	_UDDbgScrPrintDirect
_UDDbgScrPrintDirect:
	
; prints a 0 DbgScrinated string in the given color
; handles returns with 10. if the printed text reachs the screen bottom 
; it will be printed outside.
; 01,<color> will set the color of the text 
; ->a0 text
; ->d0 x
; ->d1 y
; ->d2 color
	
	move.l	a6,-(a7)
	bsr	_UDDbgScrSetPosition
	
	and	#7,d2
	lsl	#8,d2
	move	UDRegWriteToScreen,d3
	lea	$fb0000,a6

.UDDbgScrPrintLoop:
	move.b	(a0)+,d2
	beq	.UDDbgScrPrintEnd

	cmp.b	#10,d2			; handle returns
	bne.s	.UDDbgScrPrintNoReturn
	addq	#1,d1
	bsr	_UDDbgScrSetPosition
	move	UDRegWriteToScreen,d3
	bra.s	.UDDbgScrPrintLoop
.UDDbgScrPrintNoReturn:

	cmp.b	#2,d2			; handle set column
	bne.s	.UDDbgScrPrintNoSetC
	move.b	(a0)+,d0
	bsr	_UDDbgScrSetPosition
	move	UDRegWriteToScreen,d3
	bra.s	.UDDbgScrPrintLoop
.UDDbgScrPrintNoSetC:

	cmp.b	#1,d2			;handle colors
	bne.s	.UDDbgScrNoCol
	moveq	#0,d2
	move.b	(a0)+,d2
	lsl	#8,d2
	bra.s	.UDDbgScrPrintLoop

.UDDbgScrNoCol:

	cmp.b	#12,d2			;handle clear
	bne.s	.UDDbgScrNoClear

	movem.l	d0-d2/a0,-(sp)
	bsr	_UDDbgScrClear
	movem.l	(sp)+,d0-d2/a0

	bra.s	.UDDbgScrPrintLoop

.UDDbgScrNoClear:
	sub.b	#$20,d2
	move	d2,d3
	add	d3,d3
	move	(a6,d3),d3		;write char to screen

	bra	.UDDbgScrPrintLoop

.UDDbgScrPrintEnd:
	move.l	(a7)+,a6
	rts


