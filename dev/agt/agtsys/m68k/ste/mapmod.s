

	.globl		_SYS_MapModInit
	
	.globl		_OCM_integrate_68k
	.globl		_OCM_occlusion_add_T
	.globl		_OCM_occlusion_sub_O

;	.globl		_do_mapmod_occlusion
	.globl		_vis_mapmod_occlusion

	.globl		_g_drawcontext


	include		"drawctx.inc"
	
*------------------------------------------------------------------------------*
*	tile macros for writing different vpage layouts:
*
*	P|Q
*	---
*	R|S
*------------------------------------------------------------------------------*

	.if		^^defined AGT_CONFIG_PFLAYOUT_PQRS
	include		"ste/mapmod_pqrs16.s"
	.endif
	.if		^^defined AGT_CONFIG_PFLAYOUT_PQ
	include		"ste/mapmod_pq16.s"
	.endif
	include		"ste/mapmod_pr16.s"
	include		"ste/mapmod_pr16m.s"
	include		"ste/mapmod_pr8.s"
	include		"ste/mapmod_pr8m.s"
	.if		^^defined AGT_CONFIG_PFLAYOUT_P
	include		"ste/mapmod_p16.s"
	.endif
	
	include		"ste/ocm16.s"
	
*------------------------------------------------------------------------------*
_SYS_MapModInit:
*------------------------------------------------------------------------------*
	.if		^^defined AGT_CONFIG_PFLAYOUT_PQRS
	jsr		mapmod_setup_pqrs
	.endif
	.if		^^defined AGT_CONFIG_PFLAYOUT_PQ
	jsr		mapmod_setup_pq
	.endif
	jsr		mapmod_setup_pr16
	jsr		mapmod_setup_pr16m
	jsr		mapmod_setup_pr8
	jsr		mapmod_setup_pr8m
	.if		^^defined AGT_CONFIG_PFLAYOUT_P
	jsr		mapmod_setup_p
	.endif
	;jsr		mapmod_setup_ocm16
	jsr		ocm16_setup
	rts
	
*------------------------------------------------------------------------------*
mapmod_setup:	
*------------------------------------------------------------------------------*
	move.l		d2,-(sp)
	move.l		a0,a1
	moveq		#16-1,d2
.ol:	moveq		#16-1,d1
	move.l		4(a0),d0
	addq.l		#8,a0
.il:	move.l		d0,(a1)
	addq.l		#8,a1
	dbra		d1,.il
	dbra		d2,.ol
	move.l		(sp)+,d2
	rts


*------------------------------------------------------------------------------*
_OCM_occlusion_add_T:
*------------------------------------------------------------------------------*
*	a0		occlusion mask data
*------------------------------------------------------------------------------*
	movem.l		d0-a6,-(sp)
*------------------------------------------------------------------------------*

	; todo: optimize this stuff
	cmp.w		_g_drawcontext+dctx_guardwindow_x1,d1
	bmi.s		.nocm
	cmp.w		_g_drawcontext+dctx_guardwindow_y1,d2
	bmi.s		.nocm

	move.w		d1,d3			; fx
	lsr.w		#2,d3
	and.w		#4-1,d3			; qx
	
	move.w		d2,d4			; fy
	lsr.w		#2,d4
	and.w		#4-1,d4			; qy
	
	lsl.w		#2,d4
	add.w		d3,d4			; om 0-15 (x4:y4)
	
	add.w		d4,d4	
	add.w		(a0,d4.w),a0

	move.w		(a0)+,d7		; ocm lines	
	subq.w		#1,d7
	bmi.s		.nocm
	
	move.l		_g_drawcontext+dctx_pocm_t,a1		;_dm_dirty_oct,a1

;	todo: clipping

;	tst.w		d1
;	bmi.s		.nocm
;	tst.w		d2
;	bmi.s		.nocm

;	x offset & shift into ocm buffers

	asr.w		#4,d1			; x->tx	
	asr.w		#4,d2			; y->ty

	moveq		#16-1,d3
	and.w		d1,d3			; tx shift
	
	moveq		#-16,d5
	and.w		d1,d5
	asr.w		#3,d5			; tx offset

	add.w		d5,a1

;	y offset into ocm buffers

	move.w		_g_drawcontext+dctx_ocm_stride,d4		;_dm_ocmstride,d4
	mulu.w		d4,d2
	add.l		d2,a1
	
.yc:	move.l		(a0)+,d1		; t:o
	clr.w		d1
	lsr.l		d3,d1
	or.l		d1,(a1)
	add.w		d4,a1
	
	dbra		d7,.yc
.nocm:	
*------------------------------------------------------------------------------*
	movem.l		(sp)+,d0-a6
	rts

*------------------------------------------------------------------------------*
_OCM_occlusion_sub_O:
*------------------------------------------------------------------------------*
*	a0		occlusion mask data
*------------------------------------------------------------------------------*
	movem.l		d0-a6,-(sp)
*------------------------------------------------------------------------------*

	; todo: optimize this stuff
	cmp.w		_g_drawcontext+dctx_guardwindow_x1,d1
	bmi.s		.nocm
	cmp.w		_g_drawcontext+dctx_guardwindow_y1,d2
	bmi.s		.nocm
	
	move.w		d1,d3			; fx
	asr.w		#2,d3
	and.w		#4-1,d3			; qx
	
	move.w		d2,d4			; fy
	asr.w		#2,d4
	and.w		#4-1,d4			; qy
	
	lsl.w		#2,d4
	add.w		d3,d4			; om 0-15 (x4:y4)
	
	add.w		d4,d4	
	add.w		(a0,d4.w),a0

	move.w		(a0)+,d7		; ocm lines	
	subq.w		#1,d7
	bmi.s		.nocm

	addq		#2,a0
	
	move.l		_g_drawcontext+dctx_pocm_o,a1		;_dm_dirty_oct,a1

;	todo: clipping
;
;	tst.w		d1
;	bmi.s		.nocm
;	tst.w		d2
;	bmi.s		.nocm

;	x offset & shift into ocm buffers

	asr.w		#4,d1			; x->tx	
	asr.w		#4,d2			; y->ty

	moveq		#16-1,d3
	and.w		d1,d3			; tx shift
	
	moveq		#-16,d5
	and.w		d1,d5
	asr.w		#3,d5			; tx offset

	add.w		d5,a1

;	y offset into ocm buffers

	move.w		_g_drawcontext+dctx_ocm_stride,d4		;_dm_ocmstride,d4
	mulu.w		d4,d2
	add.w		d2,a1
	
.yc:	move.l		(a0)+,d1		; o:~
	clr.w		d1
	lsr.l		d3,d1
	not.l		d1
	and.l		d1,(a1)
	add.w		d4,a1
	
	dbra		d7,.yc
.nocm:	
*------------------------------------------------------------------------------*
	movem.l		(sp)+,d0-a6
	rts
	
	
	.if		(0)
	
*------------------------------------------------------------------------------*
_do_mapmod_occlusion:	
*------------------------------------------------------------------------------*
;	rts
	
	movem.l		d0-a6,-(sp)
	
	pushraster	#$f46

	move.l		_dm_dirty_oct,a1
	move.l		_dm_dirty_oco,a2
	move.w		_dm_ocmstride,d5
	
	move.l		_dm_dirtymask,a3
	move.w		_dm_maskstride,d4

	moveq		#0,d0
	
	move.w		_dm_map_tiles_y,d3

;	moveq		#8,d3

	subq.w		#1,d3
	

;	moveq		#32-1,d2

;	add.w		_dm_map_tiles_x,d2
;	lsr.w		#5,d2			; combine 32 tiles per op


	move.w		d5,d2			; bytes
	lsr.w		#2,d2			; words

	subq.w		#1,d2

.yc:	move.w		d2,d6

	move.l		a3,a0
	add.w		d4,a3

.xc:	movep.l		0(a0),d7		; read AA:BB:CC:DD	

	move.l		(a2),d1			; ~oco
	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d0,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,0(a0)		; write AA:BB:CC:DD	
	addq		#8,a0
	
	dbra		d6,.xc
	
	dbra		d3,.yc
	
	popraster
	
	movem.l		(sp)+,d0-a6
	rts

	.endif
	

*------------------------------------------------------------------------------*
_OCM_integrate_68k:	
*------------------------------------------------------------------------------*
;	rts
	
	movem.l		d0-a6,-(sp)
	
	pushraster	#$fff

	move.l		_dm_dirty_oct,a1
	move.l		_dm_dirty_oco,a2
	move.w		_dm_ocmstride,d5
	
	move.l		_dm_dirtymask,a3
	move.w		_dm_maskstride,d6
	

	; y size, offset
	
	move.w		_dm_pj,d2
	move.w		_dm_pj2,d4		
	sub.w		d2,d4
	ble		.nooc
	
	move.w		d2,d0
	mulu.w		d5,d0
	add.l		d0,a1
	add.l		d0,a2
	
	mulu.w		d6,d2
	add.l		d2,a3


	; x size, offset
	
	move.w		_dm_pi,d1
	move.w		_dm_pi2,d3
	
	and.w		#-16,d1	
	add.w		#16-1,d3
	and.w		#-16,d3
	
	sub.w		d1,d3
	ble		.nooc
	
;	add.w		#32-1,d3	; 32 tiles at a time
;	and.w		#-32,d3

	asr.w		#3,d1
;	lsr.w		#5,d3
	lsr.w		#4,d3
	
	add.w		d1,a1
	add.w		d1,a2
	
	add.w		d1,a3
	add.w		d1,a3
	
	move.w		d3,d0		; blocks
;	lsl.w		#2,d0
	lsl.w		#1,d0
	sub.w		d0,d5		; stride-blocks*4
;	sub.w		d0,d6		; stride-blocks*8
;	sub.w		d0,d6
	

	;

	moveq		#0,d0



	subq.w		#1,d4

	setraster	#$f46

	add.w		d3,d3
	move.w		.jt-2(pc,d3.w),d3
	jmp		.jt(pc,d3.w)
.jt:	dc.w		.jone-.jt
	dc.w		.jtwo-.jt
	dc.w		.jthree-.jt
	dc.w		.jfour-.jt
	dc.w		.jfive-.jt

	
.jone:	moveq		#-1,d3

.yc1:	movep.w		0(a3),d7		; read AA:BB

	move.w		(a2),d1			; ~oco
;	not.w		d1
	and.w		(a1),d1			; oct & ~oco
	move.w		d0,(a1)+		; erase ocm
	move.w		d3,(a2)+
	or.w		d1,d7			; combine

	movep.w		d7,0(a3)		; write AA:BB:CC:DD	
		
	add.w		d5,a1
	add.w		d5,a2
	
	add.w		d6,a3
	
	dbra		d4,.yc1
	bra		.jdone


.jtwo:	moveq		#-1,d3

.yc2:	movep.l		0(a3),d7		; read AA:BB

	move.l		(a2),d1			; ~oco
;	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d3,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,0(a3)		; write AA:BB:CC:DD	
		
	add.w		d5,a1
	add.w		d5,a2
	
	add.w		d6,a3
	
	dbra		d4,.yc2
	bra		.jdone

.jthree:
	moveq		#-1,d3

.yc3:	movep.l		0(a3),d7		; read AA:BB

	move.l		(a2),d1			; ~oco
;	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d3,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,0(a3)		; write AA:BB:CC:DD	
		
		
	movep.w		8(a3),d7		; read AA:BB

	move.w		(a2),d1			; ~oco
;	not.w		d1
	and.w		(a1),d1			; oct & ~oco
	move.w		d0,(a1)+		; erase ocm
	move.w		d3,(a2)+
	or.w		d1,d7			; combine

	movep.w		d7,8(a3)		; write AA:BB:CC:DD	
			
		
	add.w		d5,a1
	add.w		d5,a2
	
	add.w		d6,a3
	
	dbra		d4,.yc3
	bra		.jdone


.jfour:	moveq		#-1,d3

.yc4:	movep.l		0(a3),d7		; read AA:BB

	move.l		(a2),d1			; ~oco
;	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d3,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,0(a3)		; write AA:BB:CC:DD	
		
		
	movep.l		8(a3),d7		; read AA:BB

	move.l		(a2),d1			; ~oco
;	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d3,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,8(a3)		; write AA:BB:CC:DD	

		
	add.w		d5,a1
	add.w		d5,a2
	
	add.w		d6,a3
	
	dbra		d4,.yc4
	bra		.jdone


.jfive:	moveq		#-1,d3

.yc5:	movep.l		0(a3),d7		; read AA:BB

	move.l		(a2),d1			; ~oco
;	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d3,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,0(a3)		; write AA:BB:CC:DD	
		
		
	movep.l		8(a3),d7		; read AA:BB

	move.l		(a2),d1			; ~oco
;	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d3,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,8(a3)		; write AA:BB:CC:DD	
		
		
	movep.w		16(a3),d7		; read AA:BB

	move.w		(a2),d1			; ~oco
;	not.w		d1
	and.w		(a1),d1			; oct & ~oco
	move.w		d0,(a1)+		; erase ocm
	move.w		d3,(a2)+
	or.w		d1,d7			; combine

	movep.w		d7,16(a3)		; write AA:BB:CC:DD	

		
	add.w		d5,a1
	add.w		d5,a2
	
	add.w		d6,a3
	
	dbra		d4,.yc5
	bra		.jdone



	.if		(0)
	
	subq.w		#1,d3

.yc:	move.w		d3,d2

.xc:	movep.l		0(a3),d7		; read AA:BB:CC:DD	


	move.l		(a2),d1			; ~oco
	not.l		d1
	and.l		(a1),d1			; oct & ~oco
	move.l		d0,(a1)+		; erase ocm
	move.l		d0,(a2)+
	or.l		d1,d7			; combine

	movep.l		d7,0(a3)		; write AA:BB:CC:DD	
	addq		#8,a3
	
	dbra		d2,.xc
	
	add.w		d5,a1
	add.w		d5,a2
	
	add.w		d6,a3
	
	dbra		d4,.yc

	.endif
	
	
	
.jdone:
	
.nooc:
	popraster
	
	movem.l		(sp)+,d0-a6
	rts


*------------------------------------------------------------------------------*
_vis_mapmod_occlusion:	
*------------------------------------------------------------------------------*
	movem.l		d0-a6,-(sp)
	
	pushraster	#$fff

;	scroll blit against playfield fine-x

	moveq		#16-1,d1
	and.w		_dm_pi,d1
	moveq		#-1,d0
	moveq		#-1,d3	
	lsr.l		d1,d0
	lsr.w		d1,d3
	not.w		d3	
	move.b		d1,BLiTSKEW.w		; scroll
	move.l		d0,BLiTEM1.w		; scrolled endmasks
	move.w		d3,BLiTEM3.w		;
	
	
;	configure width,src/dst y increments
			
	move.w		_dm_ocmstride,d3	; ocm buffer width, bytes
	lsr.w		#1,d3			; words
	
	cmp.w		#16,d3
	ble.s		.wok
	moveq		#16,d3			; limit vis width
.wok:
	move.w		d3,BLiTXC.w
	
	subq		#1,d3			; blitter internal yinc only performed for xcount-1 words	
	add.w		d3,d3			; words->bytes

	move.w		_dm_ocmstride,d0	; source buffer stride (bytes)
	sub.w		d3,d0
	move.w		d0,BLiTSYI.w		; src yinc = source buffer stride - xcount (bytes)

	setraster	#$724

	; draw 'O' buffer first

	move.l		_dm_dstaddr,a0
	addq		#8,a0			; dest

	move.l		_dm_dirty_oco,a1	; source
;	move.w		_dm_vth,d2
	moveq		#1,d1			; colour mask = %0001
;	bsr		_vis_draw_bitfield
	moveq		#12,d7			; ~source
	bsr		_vis_draw_windowed_bitfield


	; draw 'T' buffer below it
	
	move.l		_dm_dstaddr,a0
	move.w		_dm_vth,d0
	addq		#1,d0
	mulu.w		_dm_dstlinewid,d0
	add.l		d0,a0
	addq		#8,a0
				; dest
	move.l		_dm_dirty_oct,a1	; source
;	move.w		_dm_vth,d2
	moveq		#2,d1			; colour mask = %0010
;	bsr		_vis_draw_bitfield
	moveq		#3,d7			; source
	bsr		_vis_draw_windowed_bitfield

	popraster
				
	movem.l		(sp)+,d0-a6
	rts

_vis_draw_windowed_bitfield:

	movem.l		d0-a6,-(sp)
	move.w		_dm_pj,d2
	beq.s		.n0
	bsr		_vis_draw_bitfield
.n0:	movem.l		(sp)+,d0-a6

	movem.l		d0-a6,-(sp)
	move.w		_dm_pj,d0
	move.w		d0,d3	
	mulu.w		_dm_ocmstride,d0
	mulu.w		_dm_dstlinewid,d3
	add.l		d0,a1
	add.l		d3,a0
	move.w		_dm_pj2,d2
	sub.w		_dm_pj,d2
	ble.s		.n1
	or.w		#%1100,d1	
	bsr		_vis_draw_bitfield
.n1:	movem.l		(sp)+,d0-a6

	movem.l		d0-a6,-(sp)
	move.w		_dm_pj2,d0
	move.w		d0,d3	
	mulu.w		_dm_ocmstride,d0
	mulu.w		_dm_dstlinewid,d3
	add.l		d0,a1
	add.l		d3,a0
	move.w		_dm_vth,d2
	sub.w		_dm_pj2,d2
	ble.s		.n2
	bsr		_vis_draw_bitfield
.n2:	movem.l		(sp)+,d0-a6
	
	rts

*------------------------------------------------------------------------------*
*	draw 1-plane bitfield over 4 planes with colour mask 
*	width, src/dst y increments must be preconfigured
*------------------------------------------------------------------------------*
_vis_draw_bitfield:	
*------------------------------------------------------------------------------*
	
	move.w		#2,BLiTSXI.w		; source is 1-plane
	move.w		#8,BLiTDXI.w		; dest is 4-plane interleaved

	move.b		#2,BLiTHOP.w		; disable halftone ops
	
;	dst yinc can be derived from known values (dst_linewid bytes - xcount bytes)
	
	move.w		BLiTXC.w,d3	
	subq		#1,d3			; blitter internal yinc only performed for xcount-1 words
	lsl.w		#3,d3			; in 4pln bytes (8 bytes)
	neg.w		d3
	add.w		_dm_dstlinewid,d3
	move.w		d3,BLiTDYI.w
		
;	perform blit
	
;	moveq		#7,d4
	moveq		#1,d4
	
	moveq		#4-1,d3			; planes
	
.plnc:	lsr.b		#1,d1
	scs		d0
	or.b		d7,d0
	and.b		#15,d0			; output 3(copy/src) or 15(fill) according to next colour mask bit	
	move.b		d0,BLiTLOP.w

	move.l		a1,BLiTSRC.w
	move.l		a0,BLiTDST.w
	addq		#2,a0
	
	subq.w		#1,d2
.line:	move.w		d4,BLiTYC.w
	move.b		#$c0,BLiTCTRL.w	
	dbra		d2,.line

;	move.w		d2,BLiTYC.w
;	move.b		#$80,BLiTCTRL.w		; blit nice/bus-share
;	addq		#2,a0

;.res:	bset.b		d4,BLiTCTRL.w		; restart until complete
;	.if		(use_blitter_nops)
;	nop
;	.endif
;	bne.s		.res

	dbra		d3,.plnc
	
	rts

*------------------------------------------------------------------------------*
	.data
*------------------------------------------------------------------------------*

	if	(0)
diagnostic_tile:
	rept	8
	dc.l	$AAAAAAAA,-1
	dc.l	$55555555,-1
	endr
	endif

*------------------------------------------------------------------------------*
	.bss
*------------------------------------------------------------------------------*

_dm_dirty_oct:		ds.l	1
	.globl	_dm_dirty_oct
_dm_dirty_oco:		ds.l	1
	.globl	_dm_dirty_oco
_dm_dstaddr:		ds.l	1
	.globl	_dm_dstaddr
_dm_dirtymask:		ds.l	1
	.globl	_dm_dirtymask
_dm_ptiles:		ds.l	1
	.globl	_dm_ptiles
_dm_pltiles:		ds.l	1
	.globl	_dm_pltiles
_dm_pmap:		ds.l	1
	.globl	_dm_pmap
_dm_plmap:		ds.l	1
	.globl	_dm_plmap
_dm_virtual_pagebytes:	ds.l	1
	.globl	_dm_virtual_pagebytes
_dm_visible_linebytes:	ds.w	1
	.globl	_dm_visible_linebytes
_dm_map_tiles_x:	ds.w	1
	.globl	_dm_map_tiles_x
_dm_map_tiles_y:	ds.w	1
	.globl	_dm_map_tiles_y
_dm_pi:			ds.w	1
	.globl	_dm_pi
_dm_pi2:		ds.w	1
	.globl	_dm_pi2
_dm_pj:			ds.w	1
	.globl	_dm_pj
_dm_pj2:		ds.w	1
	.globl	_dm_pj2
_dm_maskstride:		ds.w	1
	.globl	_dm_maskstride
_dm_lineskip:		ds.w	1
	.globl	_dm_lineskip
_dm_dstlinewid:		ds.w	1
	.globl	_dm_dstlinewid
_dm_ocmstride:		ds.w	1
	.globl	_dm_ocmstride
_dm_vth:		ds.w	1
	.globl	_dm_vth
	
*------------------------------------------------------------------------------*
	.text
*------------------------------------------------------------------------------*
