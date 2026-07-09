*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
* 	PCS v6 blitter palette smasher
*-------------------------------------------------------*

*-----------------------------------------------------------------------------*
*	wait for first real scanline
*-----------------------------------------------------------------------------*

	move.l	a6,a1
	moveq	#1,d2
	
*-----------------------------------------------------------------------------*

;	cycles	(73)-(28+2)			; idle time...
	tburnrr	((73)-(28+2)),d5,d4
	
!	move.w	#display_lines_primary-2,d5	; +2 nops

*-----------------------------------------------------------------------------*
*	preload trailing palette (06-15) for first real physical line
*-----------------------------------------------------------------------------*

;	lea	-20(a0),a0		; dst CPU cols 06...16

;	nop
;	nop
	tburnrr	2,d5,d4
	
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)		; +28 nops


*-----------------------------------------------------------------------------*
.display_lines_loop:
*-----------------------------------------------------------------------------*
	move.l	a6,a7			; correct a7 here, can't before dbra (needs 3/4 nops)
	
*-----------------------------------------------------------------------------*
	;rept	display_lines_primary
*-----------------------------------------------------------------------------*

		move.b	d1,(a3)		; + left border
		move.b	d0,(a3)		; - left border

	move.w	d6,(a4)			; BCH: y-count
	.if	(configure_mste=0)
		nop
	.endif
	move.b	d7,(a5)			; BCH: go
		move.b	d0,(a2)		; + right border

	move.l	(a0)+,(a7)+		; CCH: 2

		move.b	d1,(a2)		; - right border
		
	move.l	(a0)+,(a7)+		; CCH: 2+8=10
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
;	move.l	a6,a7			; have only 4 nops to loop, 3/4 consumed by dbra

;	nop				; pad...
;	nop				; pad...
;	nop

*-----------------------------------------------------------------------------*
	;endr	; display_lines_primary
*-----------------------------------------------------------------------------*
	dbra	d5,.display_lines_loop
*-----------------------------------------------------------------------------*
	
*-----------------------------------------------------------------------------*
*	final line, -1 nop compensation for dbra fallthrough	
*-----------------------------------------------------------------------------*
	
		move.b	d1,(a3)		; + left border
		move.b	d0,(a3)		; - left border

	move.w	d6,(a4)			; BCH: y-count
	.if	(configure_mste=0)
		nop
	.endif
	move.b	d7,(a5)			; BCH: go
		move.b	d0,(a2)		; + right border

	move.l	(a0)+,(a1)+		; CCH: 2

		move.b	d1,(a2)		; - right border
		
	move.l	(a0)+,(a1)+		; CCH: 2+8=10
	move.l	(a0)+,(a1)+
	move.l	(a0)+,(a1)+
	move.l	(a0)+,(a1)+
	move.l	a6,a1
	
;	nop
;	nop
	tburnrr	2,d5,d4
	move.l	a6,a7			; a7 wasn't corrected due to final dbra
	
*-----------------------------------------------------------------------------*
*	special line #1
*-----------------------------------------------------------------------------*

		move.b	d1,(a3)		; + left border
		move.b	d0,(a3)		; - left border

	move.w	d6,(a4)			; BCH: y-count
	.if	(configure_mste=0)
		nop
	.endif
	move.b	d7,(a5)			; BCH: go
		move.b	d0,(a2)		; + right border

	move.l	(a0)+,(a1)+		; CCH: 2
	
		move.b	d1,(a2)		; - right border
		
	move.l	(a0)+,(a1)+		; CCH: 2+8=10
	move.l	(a0)+,(a1)+
	move.l	(a0)+,(a1)+
	move.l	(a0)+,(a1)+

		move.b	d0,(a2)		; + toggle
		
	move.w	d6,(a4)			; BCH: y-count


*-----------------------------------------------------------------------------*
*	 special line #2
*-----------------------------------------------------------------------------*

		move.b	d1,(a3)		; + left border
		move.b	d0,(a3)		; - left border
		move.b	d1,(a2)		; - toggle
		
	.if	(configure_mste=0)
		nop
	.endif
	move.b	d7,(a5)			; BCH: go
		move.b	d0,(a2)		; + right border

	move.l	(a0)+,(a7)+		; CCH: 2

		move.b	d1,(a2)		; - right border
		
	move.l	(a0)+,(a7)+		; CCH: 2+8=10
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	a6,a7

	nop				; pad...
;	nop
;	nop	
!	move.w	#display_lines_lowborder-2,d5

*-----------------------------------------------------------------------------*
.lowborder_lines_loop:
*-----------------------------------------------------------------------------*
;	rept	display_lines_lowborder-1
*-----------------------------------------------------------------------------*

		move.b	d1,(a3)		; + left border
		move.b	d0,(a3)		; - left border

	move.w	d6,(a4)			; BCH: y-count
	.if	(configure_mste=0)
		nop
	.endif
	move.b	d7,(a5)			; BCH: go
		move.b	d0,(a2)		; + right border

	move.l	(a0)+,(a7)+		; CCH: 2
	
		move.b	d1,(a2)		; - right border
		
	move.l	(a0)+,(a7)+		; CCH: 2+8=10
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	(a0)+,(a7)+
	move.l	a6,a7

;	nop				; pad...
;	nop
;	nop

	dbra	d5,.lowborder_lines_loop

*-----------------------------------------------------------------------------*
;	endr	; display_lines_lowborder
*-----------------------------------------------------------------------------*

*-----------------------------------------------------------------------------*
*	wipe palette (00-15) beyond last line
*-----------------------------------------------------------------------------*

	moveq	#0,d2
	move.l	d2,-(a7)
	move.l	d2,-(a7)
	move.l	d2,-(a7)
	move.l	d2,(a6)+
	move.l	d2,(a6)+
	move.l	d2,(a6)+
	move.l	d2,(a6)+
	move.l	d2,(a6)+

*-----------------------------------------------------------------------------*
