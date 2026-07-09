********************************************************************************
*
*  void d_lz77(a0.l *lz77data, a1.l *dest)
*
* Very! fast lz77 decompression routine
* 68000 version
*
********************************************************************************

	.if		(^^defined AGT_CONFIG_PACKER_LZ77 | ^^defined AGT_CONFIG_PACKER_ANY)



			.globl	_DeLz77_68k
			
*-------------------------------------------------------*
_DeLz77_68k:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	3
.return:		ds.l	1
*-------------------------------------------------------*
.psrc:			ds.l	1
.pdst:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d3/a2,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
*-------------------------------------------------------*
	move.l		.psrc(a6),a0
	move.l		.pdst(a6),a1
*-------------------------------------------------------*
	move.w		$ffff8240.w,-(sp)
	bsr		d_lz77
	move.w		(sp)+,$ffff8240.w
*-------------------------------------------------------*
	move.l		(sp)+,a6
	unlk		a6
	movem.l		(sp)+,d2-d3/a2
*-------------------------------------------------------*
	rts	
	
	

		.text
		
d_lz77:
		addq.l	#4,a0		; Skip original length
		bra.s	.loadtag

.literal:
		.rept	8
		move.b	(a0)+,(a1)+     ; Copy 8 bytes literal string
		.endr
         
.loadtag:	move.b	(a0)+,d0	; Load compression TAG
		beq.s	.literal	; 8 bytes literal string?


		moveq.l	#8-1,d1         ; Process TAG per byte/string
.search:
		add.b	d0,d0		; TAG <<= 1
		bcs.s	.compressed

		move.b  (a0)+,(a1)+     ; Copy another literal byte
		dbra	d1,.search

		bra.s	.loadtag


.compressed:
		moveq.l	#0,d2
		move.b  (a0)+,d2        ; Load compression specifier
		beq.s	.break		; End of stream, exit

		moveq.l	#$0f,d3		; Mask out stringlength
		and.l	d2,d3

		lsl.w	#4,d2		; Compute string location
		move.b	(a0)+,d2
		movea.l	a1,a2
		suba.l	d2,a2


		add.w	d3,d3		; Jump into unrolled string copy loop
		neg.w	d3
		jmp     .unroll(pc,d3.w)

		.rept	15
		move.b	(a2)+,(a1)+
		.endr
		
.unroll:		move.b	(a2)+,(a1)+
		move.b	(a2)+,(a1)+

		dbra	d1,.search

		bra.s	.loadtag

.break:		rts

		.endif ; (^^defined AGT_CONFIG_PACKER_LZ77 | ^^defined AGT_CONFIG_PACKER_ANY)
