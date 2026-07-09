*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Sprite clearing/background-restore mechanism
*-------------------------------------------------------*


	.globl		_g_rst_pfb
	.globl		_g_rst_pflineidx
	.globl		_g_rst_vpagebytes
	.globl		_g_rst_vpageh
	.globl		_g_rst_state

*-------------------------------------------------------*

			.if 	(use_sprite_saverestore)
spriteclear_stacksize	=	32768
			.else
spriteclear_stacksize	=	18*256
			.endif

;sprclear		macro
;	move.l		(\1)+,(\2)+
;	move.l		(\1)+,(\2)+
;	endm
	
*-------------------------------------------------------*
*	Sprite Clearing: Initialize
*-------------------------------------------------------*
;_AGT_PlayfieldRestoreInit:
*-------------------------------------------------------*
;	move.l		#_g_spriteclear_stack_0,_drawcontext+dctx_clearstack_tide
;	move.l		#_g_spriteclear_stack_1,_drawcontext+dctx_clearstack_prvtide
;	move.l		#_g_spriteclear_stack_0,_drawcontext+dctx_clearstack_cur
;	move.l		#_g_spriteclear_stack_1,_drawcontext+dctx_clearstack_prv
;	rts
		
*-------------------------------------------------------*
*	Sprite Clearing
*-------------------------------------------------------*
*	- 4 bitplanes
*-------------------------------------------------------*
;_AGT_PlayfieldRestoreReset:
*-------------------------------------------------------*
;			.abs
*-------------------------------------------------------*
;.savea6:		ds.l	1
;.save:			ds.l	10
;.return:		ds.l	1
*-------------------------------------------------------*
;.frame_:		
*-------------------------------------------------------*
;			.text
*-------------------------------------------------------*
;	movem.l		d2-d7/a2-a5,-(sp)
;	link		a6,#-.frame_
*-------------------------------------------------------*
;	move.l		_drawcontext+dctx_clearstack_prv,a5
;	move.l		_drawcontext+dctx_clearstack_cur,a4
;	clr.l		(a5)
;	clr.l		(a4)
;	move.l		a5,_drawcontext+dctx_clearstack_tide
;	move.l		a4,_drawcontext+dctx_clearstack_prvtide
*-------------------------------------------------------*
;	unlk		a6
;	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
;	rts
		

	.if		(use_sprite_pagerestore)

*-------------------------------------------------------*
*	Sprite Clearing
*-------------------------------------------------------*
*	- 4 bitplanes
*-------------------------------------------------------*
_AGT_CPU_PlayfieldRestore:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.sp_pfwork:		ds.l	1
.sp_pflineidx:		ds.l	1
.sp_pfvpagebytes:	ds.l	1
.sp_pfvpageh:		ds.w	1
*-------------------------------------------------------*
.sp_frame_:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
*-------------------------------------------------------*
	move.w		_g_rst_vpageh,-(sp)
	move.l		_g_rst_vpagebytes,-(sp)
	move.l		_g_rst_pflineidx,-(sp)
	move.l		_g_rst_pfb,-(sp)
*-------------------------------------------------------*
	move.l		_g_rst_state,a5
	move.l		rstr_ptide(a5),a5
*-------------------------------------------------------*	
	move.l		-(a5),d0
	beq		.cstop
*-------------------------------------------------------*		
.ccont:	move.l		d0,a0				; (current)++ restore context
	move.l		d0,a5				; --(prev) restore context

	move.w		(a0)+,d0			; pf-y (pixel)
	move.w		(a0)+,d1			; pf-x (addr)

	move.l		(a0)+,a1			; clear dstaddr
	move.w		(a0)+,a4			; dst skip
	move.w		(a0)+,d6			; pixelword width * 2
	move.w		(a0)+,d7			; #lines 

	move.l		.sp_pflineidx(sp),a3

;	access opposite vpage
	move.l		.sp_pfvpagebytes(sp),d4

;	by (playfield buffer-y)
	move.w		d0,d2
;	by+bys
	add.w		d7,d2
;	byerr = (by+bys)-vpageh
	sub.w		.sp_pfvpageh(sp),d2		; 288 @ 240disp
	ble.s		.inside_vpage
	
*-------------------------------------------------------*		
.lower_vpage_overlap:
*-------------------------------------------------------*		
*	sprite is at least partly overlapping lower vpage
*-------------------------------------------------------*		
*	block restore source region has been split at vpage y-wrap point
*	so we manipulate the clearing record to crop the block and retry
*	after first clearing the cropped region
*-------------------------------------------------------*		
		
	cmp.w		d7,d2
	bge.s		.lower_vpage_only

*-------------------------------------------------------*		
.vpage_split_y:
*-------------------------------------------------------*		
	
;	sprite straddles both loopback vpages - requires splitting into two clear ops

	sub.w		d2,d7				; size of upper fragment

;	crop original clearing record #lines for retry (should not split next time)
	
	move.w		d7,-(a0)

;	manipulate clearing record pointer to retry this block

	lea		4+2(a0),a5

;	adjust source line for lower fragment	

	add.w		d7,d0

;	adjust dest line for lower fragment

	add.w		d7,d7
	add.w		d7,d7
	add.l		(a3,d7.w),a1			; 28c vs 56c for muls

;	set size of lower fragment

	move.w		d2,d7	

*-------------------------------------------------------*		
.lower_vpage_only:
*-------------------------------------------------------*		
*	sprite is entirely inside lower loopback vpage	
*	opposite vpage refers backwards, not forwards
*-------------------------------------------------------*		
*	block is not split (or has been adjusted so)
*-------------------------------------------------------*		

;	workbuffer
	move.l		.sp_pfwork(sp),a0
;	+ vpage wrap
	sub.l		d4,a0
;	+ playfield line
	add.w		d0,d0
	add.w		d0,d0
	add.l		(a3,d0.w),a0
;	+ block xoffset
	add.w		d1,a0
	
	move.w		.jt(pc,d6.w),d6
	lea		.jt(pc,d6.w),a2
	jmp		(a2)
	
;	move.l		-(a5),d0
;	bne.s		.ccont

*-------------------------------------------------------*		

	bra		.cstop

.jt:	dc.w		.x0-.jt
	dc.w		.x1-.jt
	dc.w		.x2-.jt
	dc.w		.x3-.jt
	dc.w		.x4-.jt
	dc.w		.x5-.jt
	dc.w		.x6-.jt
	dc.w		.x7-.jt
	dc.w		.x8-.jt
	dc.w		.x9-.jt
	dc.w		.xA-.jt
	dc.w		.xB-.jt
	dc.w		.xC-.jt
	dc.w		.xD-.jt
	dc.w		.xE-.jt

;	todo:	
	dc.w		.xE-.jt
	dc.w		.xE-.jt
	dc.w		.xE-.jt
	
*-------------------------------------------------------*		
.inside_vpage:
*-------------------------------------------------------*		
*	block is not split (or has been adjusted so)
*-------------------------------------------------------*		

;	workbuffer
	move.l		.sp_pfwork(sp),a0
;	+ vpage wrap
	add.l		d4,a0
;	+ playfield line
	add.w		d0,d0
	add.w		d0,d0
	add.l		(a3,d0.w),a0
;	+ block xoffset
	add.w		d1,a0

	move.w		.jt(pc,d6.w),d6
	lea		.jt(pc,d6.w),a2
	jmp		(a2)

.no_draw:

	move.l		-(a5),d0
	bne		.ccont
*-------------------------------------------------------*
.cstop:
*-------------------------------------------------------*

	move.l		_g_rst_state,a4
	move.l		rstr_pstack(a4),a5
	clr.l		(a5)+
	move.l		a5,rstr_ptide(a4)

	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts
	
	; d0,d1,d2,d3,d4,d5,d6,a2,a3, a6
	
*-------------------------------------------------------*		
.xE:	subq.w		#1,d7
.xElp:	movem.l		(a0)+,d0-d6/a2
	movem.l		d0-d6/a2,(a1)
	lea		32(a1),a1
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	lea		40(a1),a1
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)	
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.xElp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.xD:	subq.w		#1,d7
.xDlp:	movem.l		(a0)+,d0-d5
	movem.l		d0-d5,(a1)
	lea		24(a1),a1
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	lea		40(a1),a1
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)	
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.xDlp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.xC:	subq.w		#1,d7
.xClp:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+	
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	lea		40(a1),a1
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)	
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.xClp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.xB:	subq.w		#1,d7
.xBlp:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	lea		40(a1),a1
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)	
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.xBlp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.xA:	subq.w		#1,d7
.xAlp:	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	lea		40(a1),a1
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)	
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.xAlp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.x9:	subq.w		#1,d7
.x9lp:	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	lea		40(a1),a1
	movem.l		(a0)+,d0-d6/a2
	movem.l		d0-d6/a2,(a1)
	add.w		a4,a0				; + src lineskip
	lea		32(a1,a4),a1			; + dst lineskip
	dbra		d7,.x9lp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.x8:	subq.w		#1,d7
.x8lp:	movem.l		(a0)+,d0-d6/a2
	movem.l		d0-d6/a2,(a1)
	lea		32(a1),a1
	movem.l		(a0)+,d0-d6/a2
	movem.l		d0-d6/a2,(a1)
	add.w		a4,a0				; + src lineskip
	lea		32(a1,a4),a1			; + dst lineskip
	dbra		d7,.x8lp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop

*-------------------------------------------------------*		
.x7:	subq.w		#1,d7
.x7lp:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.x7lp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.x6:	subq.w		#1,d7
.x6lp:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+	
	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.x6lp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.x5:	subq.w		#1,d7
.x5lp:	movem.l		(a0)+,d0-d6/a2/a3/a6
	movem.l		d0-d6/a2/a3/a6,(a1)
	add.w		a4,a0				; + src lineskip
	lea		40(a1,a4),a1			; + dst lineskip
	dbra		d7,.x5lp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop

*-------------------------------------------------------*		
.x4:	subq.w		#1,d7
.x4lp:	movem.l		(a0)+,d0-d6/a2
	movem.l		d0-d6/a2,(a1)
	add.w		a4,a0				; + src lineskip
	lea		32(a1,a4),a1			; + dst lineskip
	dbra		d7,.x4lp
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop
	
*-------------------------------------------------------*		
.x3:	move.l		a4,a3
	lea		24(a4),a4
	moveq		#4-1,d0
	and.w		d7,d0
	lsr.w		#2,d7
	add.w		d0,d0
	jmp		.x3jt(pc,d0.w)
.x3jt:	bra.s		.x30
	bra.s		.x31
	bra.s		.x32
	bra.s		.x33
.x34:	movem.l		(a0)+,d0-d5
	movem.l		d0-d5,(a1)
	add.w		a3,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x33:	movem.l		(a0)+,d0-d5
	movem.l		d0-d5,(a1)
	add.w		a3,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x32:	movem.l		(a0)+,d0-d5
	movem.l		d0-d5,(a1)
	add.w		a3,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x31:	movem.l		(a0)+,d0-d5
	movem.l		d0-d5,(a1)
	add.w		a3,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x30:	dbra		d7,.x34
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop

*-------------------------------------------------------*		
.x2:	moveq		#4-1,d0
	and.w		d7,d0
	lsr.w		#2,d7
	add.w		d0,d0
	jmp		.x2jt(pc,d0.w)
.x2jt:	bra.s		.x20
	bra.s		.x21
	bra.s		.x22
	bra.s		.x23
.x24:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x23:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x22:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x21:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x20:	dbra		d7,.x24
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop

*-------------------------------------------------------*		
.x1:	moveq		#4-1,d0
	and.w		d7,d0
	lsr.w		#2,d7
	add.w		d0,d0
	jmp		.x1jt(pc,d0.w)
.x1jt:	bra.s		.x10
	bra.s		.x11
	bra.s		.x12
	bra.s		.x13
.x14:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x13:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x12:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x11:	move.l		(a0)+,(a1)+
	move.l		(a0)+,(a1)+
	add.w		a4,a0				; + src lineskip
	add.w		a4,a1				; + dst lineskip
.x10:	dbra		d7,.x14
	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop

.x0:	move.l		-(a5),d0
	bne		.ccont
	bra		.cstop


*-------------------------------------------------------*
*	Sprite Clearing - BLiTTER version
*-------------------------------------------------------*
*	- 4 bitplanes
*-------------------------------------------------------*
_AGT_BLiT_PlayfieldRestore:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.sp_pfvpageh:		ds.w	1
*-------------------------------------------------------*
.sp_frame_:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
*-------------------------------------------------------*

	pushraster	#$510

	move.w		_g_rst_vpageh,-(sp)

*-------------------------------------------------------*

	moveq		#2,d0
	move.w		d0,BLiTSXI.w
	move.w		d0,BLiTDXI.w
	move.b		d0,BLiTHOP.w
;	move.b		#15,BLiTHOP.w
	move.b		#3,BLiTLOP.w
	clr.b		BLiTSKEW.w
	moveq		#-1,d0
	move.l		d0,BLiTEM1.w
	move.w		d0,BLiTEM3.w

*-------------------------------------------------------*

	move.l		_g_rst_vpagebytes,d6
	move.l		_g_rst_pflineidx,a3
	move.l		_g_rst_pfb,a4

	move.l		_g_rst_state,a5
	move.l		rstr_ptide(a5),a5
	
	moveq		#7,d5

	lea		BLiTCTRL.w,a2
	lea		BLiTDST.w,a6
;.wait:	btst		d5,(a2)
;	nop
;	bne.s		.wait

*-------------------------------------------------------*	
	move.l		-(a5),d0
	beq		.cstop
*-------------------------------------------------------*		
.ccont:
	altraster	#$220

	move.l		d0,a0				; (current)++ restore context
	move.l		d0,a5				; --(prev) restore context

	move.w		(a0)+,d0			; pf-y (pixel)	
	move.w		(a0)+,d1			; pf-x (addr)
	move.l		(a0)+,a1			; clear dstaddr

	moveq		#2,d2
	add.w		(a0)+,d2			; src/dst skip
	move.w		d2,BLiTSYI.w
	move.w		d2,BLiTDYI.w
	
	move.w		(a0)+,d3			; pixelword width * 2
	add.w		d3,d3
	
	move.w		(a0)+,d7			; #lines 

;	access opposite vpage
	move.l		d6,d4

;	by (playfield buffer-y)
	move.w		d0,d2
;	by+bys
	add.w		d7,d2
;	byerr = (by+bys)-vpageh
	sub.w		.sp_pfvpageh(sp),d2		; 288 @ 240disp
	ble.s		.inside_vpage

*-------------------------------------------------------*		
.lower_vpage_overlap:
*-------------------------------------------------------*		
*	sprite is at least partly overlapping lower vpage
*-------------------------------------------------------*		
*	block restore source region has been split at vpage y-wrap point
*	so we manipulate the clearing record to crop the block and retry
*	after first clearing the cropped region
*-------------------------------------------------------*		

	cmp.w		d7,d2
	bge.s		.lower_vpage_only

*-------------------------------------------------------*		
.vpage_split_y:
*-------------------------------------------------------*		
	
;	sprite straddles both loopback vpages - requires splitting into two clear ops

	sub.w		d2,d7				; size of upper fragment

;	crop original clearing record #lines for retry (should not split next time)
	
	move.w		d7,-(a0)

;	manipulate clearing record pointer to retry this block

	lea		4+2(a0),a5

;	adjust source line for lower fragment	

	add.w		d7,d0

;	adjust dest line for lower fragment

	add.w		d7,d7
	add.w		d7,d7
	add.l		(a3,d7.w),a1

;	set size of lower fragment

	move.w		d2,d7	

*-------------------------------------------------------*		
.lower_vpage_only:
*-------------------------------------------------------*		
*	sprite is entirely inside lower loopback vpage	
*	opposite vpage refers backwards, not forwards
*-------------------------------------------------------*		
*	block is not split (or has been adjusted so)
*-------------------------------------------------------*		

;	workbuffer
	move.l		a4,a0
;	+ vpage wrap
	sub.l		d4,a0
;	+ playfield line
	add.w		d0,d0
	add.w		d0,d0
	add.l		(a3,d0.w),a0
;	+ block xoffset
	add.w		d1,a0

;	these fields are affected by vpage wrapping			
	move.l		a0,BLiTSRC.w

	move.l		a1,(a6)+
	move.w		d3,(a6)+	
	move.w		d7,(a6)
	subq		#6,a6

;	bra.s		.yg1
;.yc1:	move.w		#1,BLiTYC.w
;	move.b		#$c0,(a2)
;.yg1:	dbra		d7,.yc1

	move.b		#$80,(a2)
	nop
.go1:	bset.b		d5,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.go1

	move.l		-(a5),d0
	bne.s		.ccont
	
*-------------------------------------------------------*		

	bra.s		.cstop
	
*-------------------------------------------------------*		
.inside_vpage:
*-------------------------------------------------------*		
*	block is not split (or has been adjusted so)
*-------------------------------------------------------*		

;	workbuffer
	move.l		a4,a0
;	+ vpage wrap
	add.l		d4,a0
;	+ playfield line
	add.w		d0,d0
	add.w		d0,d0
	add.l		(a3,d0.w),a0
;	+ block xoffset
	add.w		d1,a0

;	these fields are affected by vpage wrapping			
	move.l		a0,BLiTSRC.w

	move.l		a1,(a6)+
	move.w		d3,(a6)+
	move.w		d7,(a6)
	subq		#6,a6
	
;	bra.s		.yg
;.yc:	move.w		#1,BLiTYC.w
;	move.b		#$c0,(a2)
;.yg:	dbra		d7,.yc

	move.b		#$80,(a2)
	nop
.go2:	bset.b		d5,(a2)
	.if		(use_blitter_nops)
	nop
	.endif
	bne.s		.go2

.no_draw:
	
	move.l		-(a5),d0
	bne		.ccont

*-------------------------------------------------------*
.cstop:
*-------------------------------------------------------*

	popraster
	
	move.l		_g_rst_state,a4
	move.l		rstr_pstack(a4),a5
	clr.l		(a5)+
	move.l		a5,rstr_ptide(a4)
*-------------------------------------------------------*
	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

	.endif

*-------------------------------------------------------*
	.data
*-------------------------------------------------------*

;	.globl		_g_spriteclear_stack_0_bsentinel
;	.globl		_g_spriteclear_stack_0_esentinel
;	.globl		_g_spriteclear_stack_1_bsentinel
;	.globl		_g_spriteclear_stack_1_esentinel

;	todo: formalise
;	.globl		_g_frb
;	.globl		_g_pflinemod
;	.globl		_g_pflineidx
;	.globl		_g_pfvpagebytes
;	.globl		_g_pfvpageh
;	.globl		_g_pfviewy

*-------------------------------------------------------*
	.bss
*-------------------------------------------------------*
	
	.globl		_g_drawcontext
	.globl		_g_pactivedctx
	
_g_drawcontext:		ds.b		dctx_size
_g_pactivedctx:		ds.l		1

*-------------------------------------------------------*
	.data
*-------------------------------------------------------*

;_g_frb:			dc.l		0
;_g_pflinemod:		dc.l		0
;_g_pflineidx:		dc.l		0
;_g_pfvpagebytes:	dc.l		0
;_g_pfvpageh:		dc.w		0
;_g_pfviewy:		dc.w		0
	
;_drawcontext+dctx_clearstack_tide:
;	dc.l		_g_spriteclear_stack_0
;_drawcontext+dctx_clearstack_prvtide:
;	dc.l		_g_spriteclear_stack_1
;	
;_drawcontext+dctx_clearstack_cur:
;	dc.l		_g_spriteclear_stack_0
;_drawcontext+dctx_clearstack_prv:
;	dc.l		_g_spriteclear_stack_1
	

;_g_spriteclear_stack_0_bsentinel:
;	dc.l		$AAAAAAAA
;	dc.l		0				; terminator
;_g_spriteclear_stack_0:	
;	dcb.b		spriteclear_stacksize,0
;_g_spriteclear_stack_0_esentinel:
;	dc.l		$AAAAAAAA
;
;_g_spriteclear_stack_1_bsentinel:
;	dc.l		$AAAAAAAA
;	dc.l		0				; terminator
;_g_spriteclear_stack_1:	
;	dcb.b		spriteclear_stacksize,0
;	dc.l		$AAAAAAAA
;_g_spriteclear_stack_1_esentinel:
;	dc.l		$AAAAAAAA
	
*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
