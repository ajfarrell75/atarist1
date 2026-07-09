
	
*------------------------------------------------------------------------------*



.macro	ocm_block_inl


	move.w		d1,d0
	and.w		d3,d0
	beq.s		.blank\~	; early shortcut empty patterns

	lea		(a6,d0.w),a5

	.if		(use_timecritical_rasters)
	moveq		#8,d0		; draw half tiles
	.else
	moveq		#16,d0		; draw whole tiles
	.endif

	
	pea		.cont\~(pc)
	move.l		(a5)+,-(sp)
	move.l		(a5),a5
	jmp		(a5)

;	move.l		(a5)+,.bb\~+2
;	move.l		(a5)+,.aa\~+2
;.aa\~:	jsr		$12345678
;.bb\~:	jsr		$12345678
;	bra.s		.cont\~
	
.blank\~:
	lea		-8*4(a0),a0
	lea		-8*8(a4),a4
.cont\~:	
	.endm
	

	
*------------------------------------------------------------------------------*
*	refresh only dirty tiles within rectangle pi,pj->pi2,pj2
*------------------------------------------------------------------------------*
_OCM16_refresh_68k:	.globl	_OCM16_refresh_68k
*------------------------------------------------------------------------------*

	movem.l		d0-a6,-(sp)
	
	pushraster	#$fff

	move.w		#2,BLiTSXI.w
	move.w		#2,BLiTDXI.w
	move.w		#2,BLiTSYI.w
	move.w		_dm_dstlinewid,d0
	subq		#8-2,d0
	move.w		d0,BLiTDYI.w
	;move.w		#(dst_linewid-8)+2,BLiTDYI.w	
	move.l		#-1,BLiTEM1.w
	move.w		#-1,BLiTEM3.w
	move.w		#4,BLiTXC.w
	move.b		#2,BLiTHOP.w
	move.b		#3,BLiTLOP.w
;	move.b		#15,BLiTLOP.w
	clr.b		BLiTSKEW.w
	
	;bra		.done

*********************************************************
* todo: set up parameters properly from call context
*********************************************************

	move.l		_dm_dirtymask,a1
	move.l		_dm_dstaddr,a4
	move.l		_dm_ptiles,d6
	move.w		_dm_visible_linebytes,d7
;	move.l		_dm_virtual_pagebytes,d6
	moveq		#0,d2
	move.w		_dm_map_tiles_x,d2
	lsl.l		#2,d2
	
	lea		BLiTYC.w,a2
	lea		BLiTCTRL.w,a3
	
;	bra		*
	
;	move.w		d7,a6
;	add.w		d7,a6

;	move.w		_dm_pj,d0
;	cmp.w		_dm_pj2,d0
;	bne.s		.okok1
;	setraster	#$fff
;.okok1:


	; check for degenerate rectangle (should never happen, if the C code is right)
;	move.w		_dm_pi2,d5
;	sub.w		_dm_pi,d5
;	ble		.done

	; find rectangle right edge (in 8bit blocks)
;	moveq		#8-1,d5
;	add.w		_dm_pi2,d5
;	and.w		#-8,d5				; rectangle right edge (in 8bit blocks)

	; find dirty mask buffer starting position (right edge, 1 8bit block = 1 word, upper byte)
;;	add.w		d5,a1
;;	add.w		d5,a1

	; find rectangle left edge (in 8bit blocks)
;	moveq		#-8,d3
;	and.w		_dm_pi,d3			; rectangle left edge (in 8bit blocks)

;	sub.w		d3,d5				; rectangle width (in 8bit blocks)


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
	
	; find rectangle height

	move.w		_dm_pj2,d4
	sub.w		_dm_pj,d4
	subq.w		#1,d4
	bmi		.done

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
;	cmp.w		#5,d3
;	ble.s		.noxclip
;	moveq		#5,d3
;.noxclip:

	cmp.w		#5,d3
	ble.s		.okok
.noknok:
	not.b		$ffff8240.w
	bra.s		.noknok
.okok:

	moveq		#$c0,d7

	add.w		d3,d3
	move.w		.jtab-2(pc,d3.w),d1
	
	lea		.jt(pc),a6
	
	setraster	#$64f
	
	jmp		.jtab(pc,d1.w)
.jtab:	dc.w		.start1-.jtab
	dc.w		.start2-.jtab
	dc.w		.start3-.jtab
	dc.w		.start4-.jtab
	dc.w		.start5-.jtab

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

.start5:
	move.w		_dm_lineskip,.smc5d+2
	move.w		_dm_maskstride,.smc5+2


;	independent left & right masks
	
	moveq		#8-1,d1
	and.w		_dm_pi,d1
	moveq		#-1,d5
	lsr.l		d1,d5

	move.b		d0,.smc5m+1

	move.w		#$07f8,d1
	
;	moveq		#-1,d0
;	moveq		#-1,d5
	
;	setraster	#$777

.ylp5:

	; check row

	movep.l		0(a1),d3			; AA:BB:CC:DD <-[AA:BB:CC:DD]
	move.l		d3,d0
	and.l		d5,d3
	beq		.next5a
	eor.l		d3,d0
	movep.l		d0,0(a1)

	; dirty row

	lea		-8*8*1(a4),a4
	lea		-8*4*1(a0),a0

	rol.l		#3,d3			; AB:BC:CD:DA
	ocm_block_inl
	ror.l		#8,d3			; DA:AB:BC:CD
	ocm_block_inl
	lsr.l		#8,d3			; 00:DA:AB:BC
	ocm_block_inl
	lsr.l		#8,d3			; 00:00:DA:AB
	ocm_block_inl

	lea		8*8*5(a4),a4
	lea		8*4*5(a0),a0

.next5a:
	; check row


	move.b		8(a1),d0		; xx:xx:xx:CC
.smc5m:	moveq		#$33,d3
	and.b		d0,d3
	beq.s		.next5b
	eor.b		d3,d0
	move.b		d0,8(a1)

	; dirty row

	lsl.w		#3,d3
	ocm_block_inl

	lea		8*8*1(a4),a4
	lea		8*4*1(a0),a0

.next5b:

	; next Y

.smc5d:	lea		1234(a4),a4
.smc5:	lea		1234(a1),a1
	add.w		d2,a0
	dbra		d4,.ylp5
	bra		.done

*------------------------------------------------------------------------------*

.start4:
	move.w		_dm_lineskip,.smc4d+2
	move.w		_dm_maskstride,.smc4+2
	move.w		#$07f8,d1


;	combine masks       e.g. 0007FF80

	and.l		d0,d5

.ylp4:

	; check row

	movep.l		0(a1),d3		; AA:BB:CC:DD <-[AA:BB:CC:DD]
	move.l		d3,d0
	and.l		d5,d3
	beq		.next4
	eor.l		d3,d0
	movep.l		d0,0(a1)

	; dirty row
	
	rol.l		#3,d3			; AB:BC:C[D:DA]
	ocm_block_inl
	ror.l		#8,d3			; DA:AB:B[C:CD]
	ocm_block_inl
	lsr.l		#8,d3			; 00:DA:A[B:BC]
	ocm_block_inl
	lsr.l		#8,d3			; 00:00:D[A:AB]
	ocm_block_inl

	lea		8*8*4(a4),a4
	lea		8*4*4(a0),a0

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
	move.w		#$07f8,d1
	
;	combine masks       e.g. 0007FF80

	and.l		d0,d5

.ylp3:

	; check row

	movep.l		-2(a1),d3		; xx:AA:BB:CC <-[AA:BB:CC:xx]
	move.l		d3,d0
	and.l		d5,d3
	beq.s		.next3
	eor.l		d3,d0
	movep.l		d0,-2(a1)

	; dirty row

	lsl.l		#3,d3
	ocm_block_inl
	lsr.l		#8,d3			; 00:xx:AA:BB
	ocm_block_inl
	lsr.l		#8,d3			; 00:00:xx:AA
	ocm_block_inl

	lea		8*8*3(a4),a4
	lea		8*4*3(a0),a0

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
	move.w		#$07f8,d1

;	combine masks       e.g. 0007FF80

	and.w		d0,d5
.ylp2:

	; check row

	movep.w		0(a1),d3		; xx:xx:BB:CC
	move.w		d3,d0
	and.w		d5,d3
	beq.s		.next2
	eor.w		d3,d0
	movep.w		d0,0(a1)

	; dirty row

	lsl.l		#3,d3
	ocm_block_inl
	lsr.l		#8,d3			; xx:xx:00:BB
	ocm_block_inl

	lea		8*8*2(a4),a4
	lea		8*4*2(a0),a0

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
	move.w		#$07f8,d1

;	combine masks       e.g. 0007FF80

	and.w		d0,d5
.ylp1:

	; check row

	move.b		(a1),d3			; xx:xx:xx:CC
	move.b		d3,d0
	and.b		d5,d3
	beq.s		.next1
	eor.b		d3,d0
	move.b		d0,(a1)

	; dirty row

	lsl.w		#3,d3
	ocm_block_inl

	lea		8*8*1(a4),a4
	lea		8*4*1(a0),a0
	
.next1:

	; next Y

.smc1d:	lea		1234(a4),a4
.smc1:	lea		1234(a1),a1
	add.w		d2,a0
	dbra		d4,.ylp1
	bra		.done


*------------------------------------------------------------------------------*

.macro	jocmtile_16	offs,dst,byc,bgo,bctl,tsrc,mapsrc,tilesrc

	; address tile source

	move.l		\tilesrc,\tsrc		; tile library
	add.l		\offs*4(\mapsrc),\tsrc	; map index
	move.l		\tsrc,BLiTSRC.w

	lea		\offs*8(\dst),\tsrc	
	move.l		\tsrc,BLiTDST.w
	
	move.w		\byc,(a2)		;BLiTYC.w
	move.b		\bgo,(\bctl)
	.if		(use_timecritical_rasters)
	move.w		\byc,(a2)		;BLiTYC.w
	move.b		\bgo,(\bctl)
	.endif
	
	.endm
	
	.if		(0)
	
*------------------------------------------------------------------------------*
.do_block:
*------------------------------------------------------------------------------*
*	schedule 2 patterns of 4 bits & execute
*------------------------------------------------------------------------------*
	move.w		#$07f8,d0
	and.w		d3,d0
	beq.s		.blank
	lea		.jt(pc,d0.w),a5
	move.l		(a5)+,-(sp)
	move.l		(a5),a3
	jsr		(a3)
	rts
.blank:	lea		-8*4(a0),a0
	lea		-8*8(a4),a4
	rts

*------------------------------------------------------------------------------*
.do_block_2:
*------------------------------------------------------------------------------*
*	schedule 2 patterns of 4 bits & execute
*------------------------------------------------------------------------------*
	lea		.jt(pc,d0.w),a5

	moveq		#16,d0
	
	move.l		(a5)+,-(sp)
	move.l		(a5),a3
	jmp		(a3)

	.endif

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
	jocmtile_16	-4,a4,d0,d7,a3,a5,a0,d6
.do_block_0111:
	jocmtile_16	-3,a4,d0,d7,a3,a5,a0,d6
.do_block_0011:
	jocmtile_16	-2,a4,d0,d7,a3,a5,a0,d6
.do_block_0001:
	jocmtile_16	-1,a4,d0,d7,a3,a5,a0,d6
	lea		-4*4(a0),a0
	lea		-4*8(a4),a4	
	rts

.do_block_1110:
	jocmtile_16	-2,a4,d0,d7,a3,a5,a0,d6
.do_block_1100:
	jocmtile_16	-3,a4,d0,d7,a3,a5,a0,d6
.do_block_1000:
	jocmtile_16	-4,a4,d0,d7,a3,a5,a0,d6
.do_block_0000:
	lea		-4*4(a0),a0
	lea		-4*8(a4),a4	
	rts

.do_block_0110:
	jocmtile_16	-2,a4,d0,d7,a3,a5,a0,d6
.do_block_0100:
	jocmtile_16	-3,a4,d0,d7,a3,a5,a0,d6
	lea		-4*4(a0),a0
	lea		-4*8(a4),a4	
	rts
	
.do_block_1011:
	jocmtile_16	-2,a4,d0,d7,a3,a5,a0,d6
.do_block_1001:
	jocmtile_16	-1,a4,d0,d7,a3,a5,a0,d6
	jocmtile_16	-4,a4,d0,d7,a3,a5,a0,d6
	lea		-4*4(a0),a0
	lea		-4*8(a4),a4	
	rts
		
.do_block_1101:
	jocmtile_16	-4,a4,d0,d7,a3,a5,a0,d6
.do_block_0101:
	jocmtile_16	-3,a4,d0,d7,a3,a5,a0,d6
	jocmtile_16	-1,a4,d0,d7,a3,a5,a0,d6
	lea		-4*4(a0),a0
	lea		-4*8(a4),a4	
	rts
	
.do_block_1010:
	jocmtile_16	-4,a4,d0,d7,a3,a5,a0,d6
.do_block_0010:
	jocmtile_16	-2,a4,d0,d7,a3,a5,a0,d6
	lea		-4*4(a0),a0
	lea		-4*8(a4),a4	
	rts
	
*------------------------------------------------------------------------------*

pairtable_ocm16		=	.jt

*------------------------------------------------------------------------------*
ocm16_setup:
*------------------------------------------------------------------------------*
	lea		pairtable_ocm16,a0
	bra		mapmod_setup
	
*------------------------------------------------------------------------------*
