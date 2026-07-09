
*------------------------------------------------------------------------------*

.macro	tile_pr8	offs,dst,tmp2,dstpgw,dstpgh,tdst,tdstv,tsrc

	; handle odd/even offset for movep fill

; operate in +ve space
;obyte	set		((\offs+256)&1)
;oword	set		((((\offs+256)&~1)-256)*4)
;offset	set		(obyte+oword)

	lea		(((\offs+256)&1)+((((\offs+256)&~1)-256)*4))(\dst),\tdst
	lea		(((\offs+256)&1)+((((\offs+256)&~1)-256)*4))(\dst,\dstpgh.l),\tdstv
	
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*0(\tdst)
	movep.l		\tmp2,dst_linewid*0(\tdstv)
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*1(\tdst)
	movep.l		\tmp2,dst_linewid*1(\tdstv)
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*2(\tdst)
	movep.l		\tmp2,dst_linewid*2(\tdstv)
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*3(\tdst)
	movep.l		\tmp2,dst_linewid*3(\tdstv)
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*4(\tdst)
	movep.l		\tmp2,dst_linewid*4(\tdstv)
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*5(\tdst)
	movep.l		\tmp2,dst_linewid*5(\tdstv)
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*6(\tdst)
	movep.l		\tmp2,dst_linewid*6(\tdstv)
	move.l		(\tsrc)+,\tmp2
	movep.l		\tmp2,dst_linewid*7(\tdst)
	movep.l		\tmp2,dst_linewid*7(\tdstv)

	.endm


*------------------------------------------------------------------------------*
*	refresh only dirty tiles within rectangle pi,pj->pi2,pj2
*	note: called either 1,2 or 4 times per frame dependent on layout & pos
*------------------------------------------------------------------------------*
_do_dirtyrect_pr8:	.globl	_do_dirtyrect_pr8
*------------------------------------------------------------------------------*

	movem.l		d0-a6,-(sp)

	pushraster	#$64f
	
;	bra		.done

*********************************************************
* todo: set up parameters properly from call context
*********************************************************

	move.l		_dm_dirtymask,a1
	move.l		_dm_dstaddr,a4
	move.l		_dm_ptiles,a2
	move.w		_dm_visible_linebytes,d7
	move.l		_dm_virtual_pagebytes,d6
	moveq		#0,d2
	move.w		_dm_map_tiles_x,d2
	lsl.l		#2,d2
	
;	bra		*
	
;	move.w		d7,a6
;	add.w		d7,a6


	; check for degenerate rectangle (should never happen, if the C code is right)
;	move.w		_dm_pi2,d5
;	sub.w		_dm_pi,d5
;	ble		.done

	; find rectangle right edge (in 8bit blocks)
	moveq		#8-1,d5
	add.w		_dm_pi2,d5
	and.w		#-8,d5				; rectangle right edge (in 8bit blocks)

	; find dirty mask buffer starting position (right edge, 1 8bit block = 1 word, upper byte)
;	add.w		d5,a1
;	add.w		d5,a1

	; find rectangle left edge (in 8bit blocks)
	moveq		#-8,d3
	and.w		_dm_pi,d3			; rectangle left edge (in 8bit blocks)

	sub.w		d3,d5				; rectangle width (in 8bit blocks)


	; generate mask d5 for EM1:EM2:EM3 based on 8bit block offset and rectangle width

;	10000000	1
;	11000000	2
;	11100000	3
;	11110000	4
;	11111000	5
;	11111100	6
;	11111110	7
;	11111111	0

;	find rectangle width in blocks

	moveq		#8-1,d3
	add.w		_dm_pi2,d3
	moveq		#-8,d4
	and.w		d4,d3
	and.w		_dm_pi,d4
	sub.w		d4,d3

;	generate right mask e.g. FFFFFF80

	move.w		_dm_pi2,d5
	subq.w		#1,d5
	and.w		#8-1,d5
	addq.w		#1,d5
	moveq		#-1,d0
	clr.b		d0
	asr.l		d5,d0

;	generate left mask  e.g. 0007FFFF

	moveq		#8-1,d4
	and.w		_dm_pi,d4
;	move.w		rectblocks,d5

;	move.w		d3,d5
;	subq		#4,d5		; start of register is 4 'blocks' to the left
;	lsl.w		#3,d5
;	sub.w		d5,d4

	sub.w		d3,d4
	add.w		#4*8,d4

	moveq		#-1,d5
	lsr.l		d4,d5

;	combine masks       e.g. 0007FF80

	and.l		d0,d5

	; find rectangle height

	move.w		_dm_pj2,d4
	sub.w		_dm_pj,d4
	subq.w		#1,d4

;	d0 : *
;	d1 : map source
;	d2 : map x stride
;	d3 : rectangle width in ceil(rx2)-floor(rx1) blocks
;	d4 : ycount-1
;	d5 : rectangle mask, 8bit-aligned
;	d6 : dstpgh
;	d7 : *

;	a0 : *
;	a1 : dirty mask source, word-aligned bytes
;	a2 : tilesrc
;	a3 : *
;	a4 : dst
;	a5 : *
;	a6 : dst base
;	usp: *

	move.l		_dm_pmap,a0

	; select 1,2,3-wide fetch from rectangle width in 8bit blocks

	lsr.w		#3,d3
	
	; todo: this is a hack for large guardbands!
	cmp.w		#4,d3
	ble.s		.noxclip
	moveq		#4,d3
.noxclip:
	
	add.w		d3,d3
	
	move.w		.jtab-2(pc,d3.w),d0
	jmp		.jtab(pc,d0.w)
.jtab:	dc.w		.start1-.jtab
	dc.w		.start2-.jtab
	dc.w		.start3-.jtab
	dc.w		.start4-.jtab

.done:
	popraster
	
	movem.l		(sp)+,d0-a6
	rts

*------------------------------------------------------------------------------*

	; extract 8-24 relevant bits for rectangle right edge (right-justified)
	; note: visible region normally 20+1 tiles wide max so 21 bits max
	; todo: replace bsr's with dedicated code per case

;	d0 : *
;	d1 : map source
;	d2 : map x stride
;	d3 : dirty mask source stride in bytes / * dirty mask data
;	d4 : ycount-1
;	d5 : rectangle mask, 8bit-aligned
;	d6 : dstpgh
;	d7 : dstpgw

;	a0 : *
;	a1 : dirty mask source, word-aligned bytes
;	a2 : tilesrc
;	a3 : *
;	a4 : dst
;	a5 : *
;	a6 : dst base

*------------------------------------------------------------------------------*

.start4:
	move.w		_dm_lineskip,.smc4d+2
	move.w		_dm_maskstride,.smc4+2

.ylp4:

	; check row

	movep.l		0(a1),d3			; AA:BB:CC:DD <-[AA:BB:CC:DD]
	move.l		d3,d0
	and.l		d5,d3
	beq.s		.next4
	eor.l		d3,d0
	movep.l		d0,0(a1)

	; dirty row

	.if		^^defined TIMING_RASTERS
	move.w		#$707,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif

	rol.l		#3,d3			; AB:BC:CD:DA
	bsr		.do_block
	ror.l		#8,d3			; DA:AB:BC:CD
	bsr		.do_block
	lsr.l		#8,d3			; 00:DA:AB:BC
	bsr		.do_block
	lsr.l		#8,d3			; 00:00:DA:AB
	bsr		.do_block

	lea		8*4*4(a4),a4
	lea		8*4*4(a0),a0

	.if		^^defined TIMING_RASTERS
	move.w		#$753,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif

.next4:

	; next Y

.smc4d:	lea		1234(a4),a4
.smc4:	lea		1234(a1),a1
	add.w		d2,a0
	dbra		d4,.ylp4
	bra		.done

*------------------------------------------------------------------------------*

.start3:
	move.w		_dm_lineskip,.smc3d+2
	move.w		_dm_maskstride,.smc3+2

.ylp3:

	; check row

	movep.l		-2(a1),d3		; xx:AA:BB:CC <-[AA:BB:CC:xx]
	move.l		d3,d0
	and.l		d5,d3
	beq.s		.next3
	eor.l		d3,d0
	movep.l		d0,-2(a1)

	; dirty row

	.if		^^defined TIMING_RASTERS
	move.w		#$707,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif

	lsl.l		#3,d3
	bsr		.do_block
	lsr.l		#8,d3			; 00:xx:AA:BB
	bsr		.do_block
	lsr.l		#8,d3			; 00:00:xx:AA
	bsr		.do_block

	lea		8*4*3(a4),a4
	lea		8*4*3(a0),a0

	.if		^^defined TIMING_RASTERS
	move.w		#$753,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif

.next3:

	; next Y

.smc3d:	lea		1234(a4),a4
.smc3:	lea		1234(a1),a1
	add.w		d2,a0
	dbra		d4,.ylp3
	bra		.done

*------------------------------------------------------------------------------*

.start2:
	move.w		_dm_lineskip,.smc2d+2
	move.w		_dm_maskstride,.smc2+2
.ylp2:

	; check row

	movep.w		0(a1),d3		; xx:xx:BB:CC
	move.w		d3,d0
	and.w		d5,d3
	beq.s		.next2
	eor.w		d3,d0
	movep.w		d0,0(a1)

	; dirty row

	.if		^^defined TIMING_RASTERS
	move.w		#$707,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif

	lsl.l		#3,d3
	bsr		.do_block
	lsr.l		#8,d3			; xx:xx:00:BB
	bsr		.do_block

	lea		8*4*2(a4),a4
	lea		8*4*2(a0),a0

	.if		^^defined TIMING_RASTERS
	move.w		#$753,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif

.next2:

	; next Y

.smc2d:	lea		1234(a4),a4
.smc2:	lea		1234(a1),a1
	add.w		d2,a0
	dbra		d4,.ylp2
	bra		.done

*------------------------------------------------------------------------------*

.start1:
	move.w		_dm_lineskip,.smc1d+2
	move.w		_dm_maskstride,.smc1+2
.ylp1:

	; check row

	move.b		(a1),d3			; xx:xx:xx:CC
	move.b		d3,d0
	and.b		d5,d3
	beq.s		.next1
	eor.b		d3,d0
	move.b		d0,(a1)

	; dirty row

	.if		^^defined TIMING_RASTERS
	move.w		#$707,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif

	lsl.w		#3,d3
	bsr		.do_block

	lea		8*4*1(a4),a4
	lea		8*4*1(a0),a0

	.if		^^defined TIMING_RASTERS
	move.w		#$753,d0
	move.w		d0,$64.w
	move.w		d0,$ffff8240.w
	.endif
	
.next1:

	; next Y

.smc1d:	lea		1234(a4),a4
.smc1:	lea		1234(a1),a1
	add.w		d2,a0
	dbra		d4,.ylp1
	bra		.done


*------------------------------------------------------------------------------*

.macro	jtile_pr8	offs,dst,tmp2,dstpgw,dstpgh,tdst,tdstv,tsrc,mapsrc,tilesrc

	; address tile source

	move.l		\tilesrc,\tsrc		; tile library
	add.l		\offs*4(\mapsrc),\tsrc	; map index

	; plot tile to relevant vpage(s)

	tile_pr8	\offs,\dst,\tmp2,\dstpgw,\dstpgh,\tdst,\tdstv,\tsrc

	.endm
	
*------------------------------------------------------------------------------*
.do_block:
*------------------------------------------------------------------------------*
*	schedule 2 patterns of 4 bits & execute
*------------------------------------------------------------------------------*
	move.w		#$07f8,d0
	and.w		d3,d0
	lea		.jt(pc,d0.w),a5
	move.l		(a5)+,-(sp)
	move.l		(a5),a3
	jmp		(a3)

.jt:
	rept		16
	dc.l		.do_block_0000,.do_block_0000
	dc.l		.do_block_0000,.do_block_0001
	dc.l		.do_block_0000,.do_block_0010
	dc.l		.do_block_0000,.do_block_0011
	dc.l		.do_block_0000,.do_block_0100
	dc.l		.do_block_0000,.do_block_0101
	dc.l		.do_block_0000,.do_block_0110
	dc.l		.do_block_0000,.do_block_0111
	dc.l		.do_block_0000,.do_block_1000
	dc.l		.do_block_0000,.do_block_1001
	dc.l		.do_block_0000,.do_block_1010
	dc.l		.do_block_0000,.do_block_1011
	dc.l		.do_block_0000,.do_block_1100
	dc.l		.do_block_0000,.do_block_1101
	dc.l		.do_block_0000,.do_block_1110
	dc.l		.do_block_0000,.do_block_1111
	endr

*------------------------------------------------------------------------------*
*	cascade tiles into patterns of 4 bits
*------------------------------------------------------------------------------*

.do_block_1111:
	jtile_pr8	-4,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_0111:
	jtile_pr8	-3,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_0011:
	jtile_pr8	-2,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_0001:
	jtile_pr8	-1,a4,d0,d7,d6,a3,a6,a5,a0,a2
	lea		-4*4(a0),a0
	lea		-4*4(a4),a4	
	rts

.do_block_1110:
	jtile_pr8	-2,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_1100:
	jtile_pr8	-3,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_1000:
	jtile_pr8	-4,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_0000:
	lea		-4*4(a0),a0
	lea		-4*4(a4),a4	
	rts

.do_block_0110:
	jtile_pr8	-2,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_0100:
	jtile_pr8	-3,a4,d0,d7,d6,a3,a6,a5,a0,a2
	lea		-4*4(a0),a0
	lea		-4*4(a4),a4	
	rts
	
.do_block_1011:
	jtile_pr8	-2,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_1001:
	jtile_pr8	-1,a4,d0,d7,d6,a3,a6,a5,a0,a2
	jtile_pr8	-4,a4,d0,d7,d6,a3,a6,a5,a0,a2
	lea		-4*4(a0),a0
	lea		-4*4(a4),a4	
	rts
		
.do_block_1101:
	jtile_pr8	-4,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_0101:
	jtile_pr8	-3,a4,d0,d7,d6,a3,a6,a5,a0,a2
	jtile_pr8	-1,a4,d0,d7,d6,a3,a6,a5,a0,a2
	lea		-4*4(a0),a0
	lea		-4*4(a4),a4	
	rts
	
.do_block_1010:
	jtile_pr8	-4,a4,d0,d7,d6,a3,a6,a5,a0,a2
.do_block_0010:
	jtile_pr8	-2,a4,d0,d7,d6,a3,a6,a5,a0,a2
	lea		-4*4(a0),a0
	lea		-4*4(a4),a4	
	rts
	
*------------------------------------------------------------------------------*

pairtable_pr8		=	.jt

*------------------------------------------------------------------------------*
mapmod_setup_pr8:
*------------------------------------------------------------------------------*
	lea		pairtable_pr8,a0
	bra		mapmod_setup
	
*------------------------------------------------------------------------------*
