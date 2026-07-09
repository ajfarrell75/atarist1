*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Quick memory services
*-------------------------------------------------------*

	.globl	_qmemcpy
	.globl	_qmemset
	.globl	_qmemclr
	.globl	_qmemset00
	.globl	_qmemsetff

	include		"config.inc"

; --------------------------------------------------------------
_qmemcpy:
; --------------------------------------------------------------
			.abs
; --------------------------------------------------------------
.sp_return:		ds.l	1
.sp_pdst:		ds.l	1
.sp_psrc:		ds.l	1
.sp_size:		ds.l	1
; --------------------------------------------------------------
			.text
; --------------------------------------------------------------
	move.l		.sp_pdst(sp),a0
	move.l		.sp_psrc(sp),a1
	move.l		.sp_size(sp),d1

	lsr.l		#4,d1					; num 16-byte blocks total
	move.l		d1,d0
	swap		d0					; num 1mb blocks (64k * 16bytes)
	subq.w		#1,d1					; num 16-byte blocks remaining
	bcs.s		.ev1mb

.lp1mb:
.lp16b:	move.l		(a1)+,(a0)+
	move.l		(a1)+,(a0)+
	move.l		(a1)+,(a0)+
	move.l		(a1)+,(a0)+
	dbra		d1,.lp16b

.ev1mb:	subq.w		#1,d0
	bpl.s		.lp1mb

	moveq		#16-1,d1
	and.w		.sp_size+2(sp),d1
	lsl.b		#4+1,d1
	bcc.s		.n8
	move.l		(a1)+,(a0)+
	move.l		(a1)+,(a0)+
.n8:	add.b		d1,d1
	bcc.s		.n4
	move.l		(a1)+,(a0)+
.n4:	add.b		d1,d1
	bcc.s		.n2
	move.w		(a1)+,(a0)+
.n2:	add.b		d1,d1
	bcc.s		.n1
	move.b		(a1)+,(a0)+
.n1:
	move.l		.sp_pdst(sp),d0
	rts

; --------------------------------------------------------------
_qmemset:
; --------------------------------------------------------------
	move.l		d2,-(sp)
	
	move.l		d2,a1
	move.l		4+4(sp),a0

	move.b		4+8+3(sp),d1
	lsl.w		#8,d1
	move.b		4+8+3(sp),d1
	move.w		d1,d2
	swap		d2
	move.w		d1,d2

	move.l		4+12(sp),d1
	lsr.l		#4,d1
	move.l		d1,d0
	swap		d0
	subq.w		#1,d1
	bcs.s		.ev1mb

.lp1mb:
.lp16b:	move.l		d2,(a0)+
	move.l		d2,(a0)+
	move.l		d2,(a0)+
	move.l		d2,(a0)+
	dbra		d1,.lp16b

.ev1mb:	subq.w		#1,d0
	bpl.s		.lp1mb

	moveq		#16-1,d1
	and.w		4+12+2(sp),d1
	lsl.b		#4+1,d1
	bcc.s		.n8
	move.l		d2,(a0)+
	move.l		d2,(a0)+
.n8:	add.b		d1,d1
	bcc.s		.n4
	move.l		d2,(a0)+
.n4:	add.b		d1,d1
	bcc.s		.n2
	move.w		d2,(a0)+
.n2:	add.b		d1,d1
	bcc.s		.n1
	move.b		d2,(a0)+
.n1:
	move.l		a1,d2
	move.l		4+4(sp),d0

	move.l		(sp)+,d2
	rts



; --------------------------------------------------------------
_qmemclr:
;	move.l		4(sp),a0
;	move.l		8(sp),d1
;	beq.s		.no
;.lp:	clr.b		(a0)+
;	subq.l		#1,d1
;	bne.s		.lp
;.no:	rts
	
_qmemset00:
; --------------------------------------------------------------
	move.l		4(sp),a0

	move.l		8(sp),d1

;.qqq	bra.s		.qqq

	lsr.l		#4,d1		; size/16

	move.l		d1,d0
	swap		d0
	subq.w		#1,d1
	bcs.s		.ev1mb

.lp1mb:
.lp16b:	clr.l		(a0)+
	clr.l		(a0)+
	clr.l		(a0)+
	clr.l		(a0)+
	dbra		d1,.lp16b

.ev1mb:	subq.w		#1,d0
	bpl.s		.lp1mb

	moveq		#16-1,d1
	and.w		8+2(sp),d1	; s & 15

	lsl.b		#4+1,d1
	bcc.s		.n8
	clr.l		(a0)+		; 8
	clr.l		(a0)+
.n8:	add.b		d1,d1
	bcc.s		.n4
	clr.l		(a0)+		; 4
.n4:	add.b		d1,d1
	bcc.s		.n2
	clr.w		(a0)+		; 2
.n2:	add.b		d1,d1
	bcc.s		.n1
	clr.b		(a0)+		; 1
.n1:
	;move.l		4(sp),d0
	rts

; --------------------------------------------------------------
_qmemsetff:
; --------------------------------------------------------------
	move.l		d2,a1
	move.l		4(sp),a0

	moveq		#-1,d2

	move.l		8(sp),d1
	lsr.l		#4,d1
	move.l		d1,d0
	swap		d0
	subq.w		#1,d1
	bcs.s		.ev1mb

.lp1mb:
.lp16b:	move.l		d2,(a0)+
	move.l		d2,(a0)+
	move.l		d2,(a0)+
	move.l		d2,(a0)+
	dbra		d1,.lp16b

.ev1mb:	subq.w		#1,d0
	bpl.s		.lp1mb

	moveq		#16-1,d1
	and.w		8+2(sp),d1
	lsl.b		#4+1,d1
	bcc.s		.n8
	move.l		d2,(a0)+
	move.l		d2,(a0)+
.n8:	add.b		d1,d1
	bcc.s		.n4
	move.l		d2,(a0)+
.n4:	add.b		d1,d1
	bcc.s		.n2
	move.w		d2,(a0)+
.n2:	add.b		d1,d1
	bcc.s		.n1
	move.b		d2,(a0)+
.n1:
	move.l		a1,d2
	;move.l		4(sp),d0
	rts

; --------------------------------------------------------------

; --------------------------------------------------------------
___mulsi3:		.globl	___mulsi3
; --------------------------------------------------------------
	move.w		6(sp),d0
	move.l		d0,a0
	mulu.w		8(sp),d0
	move.w		10(sp),d1
	move.l		d1,a1
	mulu.w		4(sp),d1
	add.w		d1,d0
	swap		d0
	clr.w		d0
	exg.l		a0,d0
	move.l		a1,d1
	mulu.w		d1,d0
	add.l		a0,d0
	rts

; --------------------------------------------------------------
___modsi3:		.globl	___modsi3
; --------------------------------------------------------------
	move.l		(sp)+,a0
	move.l		4(sp),d1
	bpl.s		.nabs
	neg.l		4(sp)
.nabs:	move.l		(sp),d0
	pea		.ret(pc)
	bpl.s		.nabsd
	neg.l		4(sp)
	subq.l		#2,(sp)
.nabsd:	bra		___udivsi3
	neg.l		d1
.ret:	move.l		d1,d0
	jmp		(a0)

; --------------------------------------------------------------
___udivsi3:		.globl	___udivsi3
; --------------------------------------------------------------
	move.l		d2,-(sp)
	move.l		12(sp),d0
	move.l		8(sp),d1
.norm:	cmpi.l		#$10000,d0
	bcs.s		.normd
	lsr.l		#1,d0
	lsr.l		#1,d1
	bra.s		.norm
.normd:	move.w		d1,d2
	clr.w		d1
	swap		d1
	divu.w		d0,d1
	movea.l		d1,a1
	move.w		d2,d1
	divu.w		d0,d1
	move.l		a1,d0
	swap		d0
	clr.w		d0
	andi.l		#$ffff,d1
	add.l		d1,d0
	move.l		12(sp),d2
	swap		d2
	move.l		d0,d1
	mulu.w		d2,d1
	movea.l		d1,a1
	swap		d2
	move.l		d0,d1
	swap		d1
	mulu.w		d2,d1
	add.l		a1,d1
	swap		d1
	clr.w		d1
	movea.l		d2,a1
	mulu.w		d0,d2
	add.l		d1,d2
	move.l		8(sp),d1
	sub.l		d2,d1
	bcc.s		.ninc
	subq.l		#1,d0
	add.l		a1,d1
.ninc:	move.l		(sp)+,d2
	rts

; --------------------------------------------------------------
___umodsi3:		.globl	___umodsi3
; --------------------------------------------------------------
	move.l		(sp)+,a0
	bsr		___udivsi3
	move.l		d1,d0
	jmp		(a0)

; --------------------------------------------------------------
___divsi3:		.globl	___divsi3
; --------------------------------------------------------------
	move.l		4(sp),d1
	bpl.s		.nabs1
	neg.l		4(sp)
.nabs1:	move.l		8(sp),d0
	bpl.s		.nabs2
	neg.l		8(sp)
.nabs2:	eor.l		d1,d0
	bpl.s		.npop
	move.l		(sp)+,a0
	pea		.ret(pc)
.npop:	bra		___udivsi3
.ret:	neg.l		d0
	jmp		(a0)
	
; --------------------------------------------------------------
_putchar:		.globl	_putchar
; --------------------------------------------------------------
	move.w		4+2(sp),d1
	movem.l		d2/a2,-(sp)
	move.w		d1,-(sp)
	move.w		#2,-(sp)
	move.w		#3,-(sp)
	trap		#13
	addq.l		#6,sp
	moveq		#0,d0
	movem.l		(sp)+,d2/a2
	rts
	
	END
	
