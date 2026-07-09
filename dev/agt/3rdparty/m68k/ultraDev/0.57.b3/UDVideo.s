************************************************************
* Project:	ultraDev
* Module:	UDVideo
* Code:		ultra
* Created:	22.12.2019
* Notes:	Service routines for the debug screen video "chip"
* Modified:
* Changes:
*
* (c) 2019 cream
************************************************************

;//****************************************************************************
UDVidSetBGColor:
 ; -> d0 0-7 color
	and	#7,d0
	add	d0,d0
	add	d0,d0

	lea	UDVideoReg1(pc),a0
	move	(a0),d1
	and	#%11100011,d1
	or	d0,d1
	move	d1,(a0)
	add	d1,d1

	move	d1,d0
	bsr	UDVidSetReg1
	rts

;//****************************************************************************

	.globl	_UDVidSetVideoMode
_UDVidSetVideoMode:

UDVidSetVideoMode:

 ; -> d0
 ; 0 = 40 char mode
 ; 1 = 16 char double mode
 ; 2 = 80 char mode
 ; 3 = 80 char mode and double mode defaults to 80 char mode

;video reg
;0		doubleMode
;1		multicolor (unused)
;2-4	background color
;5		80 colums
;543210
;8cccmd
	move	d0,d1
	and	#2,d1
	lsl	#4,d1
	and	#1,d0
	or	d1,d0
	lea	UDVideoReg1(pc),a0
	move	(a0),d1
	and	#%11011110,d1
	or	d0,d1
	move	d1,(a0)
	add	d1,d1

	
	move	d1,d0
	bsr	UDVidSetReg1
	rts

;//****************************************************************************
UDVidGetVGAY:
; <- d0 current raster beam y position
	move	UDRegVGAY,d0
	rts

;//****************************************************************************
UDVidGetVGAX:
; <- d0 current raster beam x position
	move	UDRegVGAY,d0
	rts

;//****************************************************************************
UDVidGetVBLFlag:
; <- d0
; 0 = currently in vbl
; 1 = not ;)
	move	UDRegVBLFlag,d0
	rts

;//****************************************************************************
UDVidWaitVbl:
	bsr	UDVidGetVBLFlag
	tst	d0
	bne.s	UDVidWaitVbl

.UDVidWaitVbl2:
	bsr	UDVidGetVBLFlag
	tst	d0
	beq.s	.UDVidWaitVbl2
	rts
	
;//****************************************************************************
UDVidSetReg1:
 	move	UDRegWriteToReg1,d0
	lea	$fb0000,a0
	move	(a0,d1),d0
	rts

;//****************************************************************************
 UDVideoReg1:	dc.w	0
