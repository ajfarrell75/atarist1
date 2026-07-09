*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	EMSPR/(clipped)EMXSPR sprite macros
*-------------------------------------------------------*

.macro	scan_32wc	; 14b

	; load plane count
	move.w		d2,(a0)
	; load em1-em3
	move.w		(a6)+,(a2)			; 3 reduced to 2 or 1 wide
	move.w		(a6)+,(a4)
	addq.l		#2,a6
	; load src/dst
	move.l		a1,(a3)
	; go
	move.b		d1,(a5)
	;
	add.w		d3,a1
	
	.endm

.macro	scan_2wc	; 12b

	; load plane count
	move.w		d2,(a0)
	; load em1/em3
	move.w		(a6)+,(a2)			; 2 reduced to 1 wide
	addq.w		#2,a6
;	move.w		(a6)+,(a4)
	; load src/dst
	move.l		a1,(a3)
	; go
	move.b		d1,(a5)
	;
	add.w		d3,a1

	.endm

.macro	scan_3w		; 12b

	; load plane count
	move.w		d2,(a0)
	; load em1-em3
	move.l		(a6)+,(a2)
	move.w		(a6)+,(a4)
	; load src/dst
	move.l		a1,(a3)
	; go
	move.b		d1,(a5)
	;
	add.w		d3,a1
	
	.endm

.macro	scan_1w		; 10b

	; load plane count
	move.w		d2,(a0)
	; load em1
	move.w		(a6)+,(a2)
	; load src/dst
	move.l		a1,(a3)
	; go
	move.b		d1,(a5)
	;
	add.w		d3,a1

	.endm

.macro	scan_2w		; 12b

	; load plane count
	move.w		d2,(a0)
	; load em1/em3
	move.w		(a6)+,(a2)
	move.w		(a6)+,(a4)
	; load src/dst
	move.l		a1,(a3)
	; go
	move.b		d1,(a5)
	;
	add.w		d3,a1

	.endm
