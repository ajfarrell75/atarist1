*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Blitter 4 bitplane sprites:
*	- Optional auto-clearing mechanism
*	- Optional clipping paths
*	- Includes FISR/NFSR read optimizations
*	- Compatible with .ims/ems/emx files from AGTCut
*-------------------------------------------------------*

	include		"config.inc"

	include		"blitter.inc"
	include		"emsprt.inc"
	
	include		"asset.inc"

	include		"drawctx.inc"

	include		"ste/b_slab.inc"

	include		"ste/b_drawsup.inc"
	
	;.opt		"~oall"
	
*-------------------------------------------------------*

within_entitydraw	set	0

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*

; enable this for automatic clearing of sprites in
; reverse drawing order, to restore background area

	
;	general purpose sprite routines
	.globl		_AGT_BLiT_IMSprDrawClipped
	.globl		_AGT_BLiT_IMSprDrawUnClipped
	.globl		_AGT_BLiT_IMSprInit

	.globl		_AGT_MarkDirtyRectangle

;	EM-optimized sprite routines for <= 32-wide sprites

	.globl		_AGT_BLiT_EMSprDrawClipped
	.globl		_AGT_BLiT_EMSprDrawUnClipped
	.globl		_AGT_BLiT_EMSprInit
	
	.globl		_AGT_BLiT_EMSprDrawString

	.globl		_AGT_BLiT_EMXSprDrawClipped
	.globl		_AGT_BLiT_EMXSprDrawUnClipped
	.globl		_AGT_BLiT_EMXSprInit

;	slab routines for large objects
;	.globl		_STE_SlabNoClipGeneral4BPL
	.globl		_AGT_BLiT_SlabDraw
	.globl		_AGT_SlabDirtyRegion
	.globl		_AGT_BLiT_SlabSetupOnce
	.globl		_AGT_BLiT_SlabInit

	.if		(use_sprite_saverestore|use_sprite_pagerestore)
;	sprite clearing mechanism
	.globl		_AGT_PlayfieldRestoreInit
	.globl		_AGT_PlayfieldRestoreReset
	.globl		_AGT_BLiT_PlayfieldRestore
	.globl		_AGT_CPU_PlayfieldRestore
	.endif
	
;	clipping window
;	.globl		_g_clipwin_gpp
;	.globl		_g_clipwin_gpp_x1
;	.globl		_g_clipwin_gpp_y1p
;	.globl		_g_clipwin_gpp_x2
;	.globl		_g_clipwin_gpp_y2p
;	.globl		_g_clipwin_gpp_x1p
;	.globl		_g_clipwin_gpp_x2p

;	.globl		_g_clipwinq_gg
;	.globl		_g_clipwinq_gg_x
;	.globl		_g_clipwinq_gg_xs
;	.globl		_g_clipwinq_gg_y
;	.globl		_g_clipwinq_gg_ys
	
	.globl		_OCM_occlusion_add_T
	
	.globl		_g_linebytes
	
	.globl		_ascii2font

	.globl		slab_precalcs
	
*-------------------------------------------------------*
_AGT_BLiT_IMSprInit:
*-------------------------------------------------------*
	move.w		#-1,BLiTEM2.w

	move.b		#2,BLiTHOP.w			; HOP

	move.w		#8,BLiTDXI.w			; dst xinc

	; planes (5, 1st being mask) x words x lines
	move.w		#10,BLiTSXI.w			; src xinc
	
	rts



*-------------------------------------------------------*
*	Most general (and most expensive) case:
*-------------------------------------------------------*
*	- 4 bitplanes
*	- full playfield window clipping
*	- masking with explicit keycolour/mask
*-------------------------------------------------------*
_AGT_BLiT_IMSprDrawClipped:
_AGT_BLiT_IMSprDrawUnClipped:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.pctx:			ds.l	1
.psps:			ds.l	1
.sprframe:		ds.l	1
.xpos:			ds.l	1
.ypos:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
			emitframe_draw_vars
;.sp_skew:		ds.w	1
;.sp_maskwidth:		ds.w	1
;.sp_flags:		ds.w	1
;.sp_linetable:		ds.l	1
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
	move.l		.pctx(a6),a4
	
	move.l		a4,.sp_dctx(sp)
	lea		dctx_guardwindow(a4),a5
	move.l		a5,.sp_guardwin(sp)
	lea		dctx_scissorwindow(a4),a5
	move.l		a5,.sp_scissorwin(sp)
	
	move.l		dctx_framebuffer(a4),a1
	move.l		dctx_lineidx(a4),.sp_linetable(sp)

	move.w		dctx_field(a4),.sp_mfield(sp)
	move.w		dctx_field(a4),d0
	add.w		d0,d0
	add.w		d0,d0
	move.w		d0,.sp_cfield4(sp)
	move.w		_g_linebytes,.sp_linebytes(sp)
	
	move.l		.psps(a6),a0
	move.w		.xpos+2(a6),d1
	move.w		.ypos+2(a6),d2
	sub.w		dctx_snapadjy(a4),d2	

	; access sprite frame
	
	move.w		.sprframe+2(a6),d0
	moveq		#0,d7

	move.l		dctx_prestorestate(a4),a5
	move.l		rstr_ptide(a5),a5
	move.l		a5,usp

*-------------------------------------------------------*

enable_restore	set	1
enable_clipping	set	1
use_hog		set	0
	
	include 	"ste/b_spr.s"

*-------------------------------------------------------*

	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	
	move.l		.pctx(a6),a4
	move.l		dctx_prestorestate(a4),a4
	move.l		usp,a5
	move.l		a5,rstr_ptide(a4)
	
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts















*-------------------------------------------------------*
_AGT_BLiT_EMSprInit:
_AGT_BLiT_EMXSprInit:
*-------------------------------------------------------*

	move.w		#-1,BLiTEM2.w

	move.b		#2,BLiTHOP.w			; HOP
	move.b		#3,BLiTLOP.w			; LOP = SRC

	; words x planes x lines
		
	move.w		#2,BLiTSXI.w

	move.w		#8,BLiTDXI.w

	move.w		#2,BLiTSYI.w			; changed by clipping, must be reset

	rts


*-------------------------------------------------------*
*	Most general (and most expensive) case:
*-------------------------------------------------------*
*	- 4 bitplanes
*	- full playfield window clipping
*	- masking with explicit keycolour/mask
*-------------------------------------------------------*
_AGT_BLiT_EMSprDrawClipped:
_AGT_BLiT_EMSprDrawUnClipped:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.pctx:			ds.l	1
.psps:			ds.l	1
.sprframe:		ds.l	1
.xpos:			ds.l	1
.ypos:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
	emitframe_draw_vars
;.sp_skew:		ds.w	1
;.sp_maskwidth:		ds.w	1
;.sp_flags:		ds.w	1
;.sp_linetable:		ds.l	1
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
	move.l		.pctx(a6),a4
	
	move.l		a4,.sp_dctx(sp)
	lea		dctx_guardwindow(a4),a5
	move.l		a5,.sp_guardwin(sp)
	lea		dctx_scissorwindow(a4),a5
	move.l		a5,.sp_scissorwin(sp)
	
	move.l		dctx_framebuffer(a4),a1
	move.l		dctx_lineidx(a4),.sp_linetable(sp)

	move.w		dctx_field(a4),.sp_mfield(sp)
	move.w		dctx_field(a4),d0
	add.w		d0,d0
	add.w		d0,d0
	move.w		d0,.sp_cfield4(sp)
	move.w		_g_linebytes,.sp_linebytes(sp)
	
	move.l		.psps(a6),a0
	move.w		.xpos+2(a6),d1
	move.w		.ypos+2(a6),d2
	sub.w		dctx_snapadjy(a4),d2	

	; access sprite frame
	
	move.w		.sprframe+2(a6),d0
	moveq		#0,d7

	move.l		dctx_prestorestate(a4),a5
	move.l		rstr_ptide(a5),a5
	move.l		a5,usp

*-------------------------------------------------------*

enable_restore	set	1
enable_clipping	set	1

	include		"ste/b_emc_spr.s"
	
*-------------------------------------------------------*

	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	
	move.l		.pctx(a6),a4
	move.l		dctx_prestorestate(a4),a4
	move.l		usp,a5
	move.l		a5,rstr_ptide(a4)

	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts


*-------------------------------------------------------*
_AGT_BLiT_EMXSprDrawClipped:
_AGT_BLiT_EMXSprDrawUnClipped:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.pctx:			ds.l	1
.psps:			ds.l	1
.sprframe:		ds.l	1
.xpos:			ds.l	1
.ypos:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
;.sp_skew:		ds.w	1
;.sp_maskwidth:		ds.w	1
;.sp_flags:		ds.w	1
;.sp_linetable:		ds.l	1
	emitframe_draw_vars
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
	move.l		.pctx(a6),a4
	
	move.l		a4,.sp_dctx(sp)
	lea		dctx_guardwindow(a4),a5
	move.l		a5,.sp_guardwin(sp)
	lea		dctx_scissorwindow(a4),a5
	move.l		a5,.sp_scissorwin(sp)
	
	move.l		dctx_framebuffer(a4),a1
	move.l		dctx_lineidx(a4),.sp_linetable(sp)

	move.w		dctx_field(a4),.sp_mfield(sp)
	move.w		dctx_field(a4),d0
	add.w		d0,d0
	add.w		d0,d0
	move.w		d0,.sp_cfield4(sp)
	move.w		_g_linebytes,.sp_linebytes(sp)
	
	move.l		.psps(a6),a0
	move.w		.xpos+2(a6),d1
	move.w		.ypos+2(a6),d2
	sub.w		dctx_snapadjy(a4),d2	

	; access sprite frame
	
	move.w		.sprframe+2(a6),d0
	moveq		#0,d7

	move.l		dctx_prestorestate(a4),a5
	move.l		rstr_ptide(a5),a5
	move.l		a5,usp

*-------------------------------------------------------*

enable_restore	set	1
enable_clipping	set	1

	include		"ste/b_emxc_spr.s"
	
*-------------------------------------------------------*

	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	
	move.l		.pctx(a6),a4
	move.l		dctx_prestorestate(a4),a4
	move.l		usp,a5
	move.l		a5,rstr_ptide(a4)
	
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts







	.if	(0);	disabled, until made to work with dctx
*-------------------------------------------------------*
_AGT_BLiT_EMSprDrawString:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.pstr:			ds.l	1
.psps:			ds.l	1
.pdst:			ds.l	1
.plinetable:		ds.l	1
.xpos:			ds.l	1
.ypos:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
	emitframe_draw_vars
*-------------------------------------------------------*
.sp_store:		ds.l	1
.sp_psps:		ds.l	1
.sp_pdst:		ds.l	1	
*-------------------------------------------------------*
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
	jsr		_AGT_BLiT_EMSprInit
;	jsr		_AGT_BLiT_IMSprInit
*-------------------------------------------------------*	
	move.l		.pstr(a6),a0
	move.l		a0,usp
*-------------------------------------------------------*	
	move.l		.psps(a6),.sp_psps(sp)
	move.l		.pdst(a6),.sp_pdst(sp)
	move.w		.xpos+2(a6),.sp_x(sp)
	move.w		.ypos+2(a6),.sp_y(sp)
	move.l		.plinetable(a6),.sp_linetable(sp)
	
	move.w		dctx_field(a4),.sp_mfield(sp)
	move.w		dctx_field(a4),d0
	add.w		d0,d0
	add.w		d0,d0
	move.w		d0,.sp_cfield4(sp)
	move.w		_g_linebytes,.sp_linebytes(sp)
		
*-------------------------------------------------------*
	moveq		#0,d0
;	clr.w		.sp_z(sp)
	bra		.fetch
*-------------------------------------------------------*
.next:
*-------------------------------------------------------*
	move.w		.sp_x(sp),d1
	moveq		#8,d3
	add.w		d1,d3
	move.w		d3,.sp_x(sp)
	
;	btst		#3,d3
;	bne		.fetch
	
;	moveq		#15,d4
;	and.w		d3,d4
;	bne		.fetch
	
;	addq.w		#1,.sp_z(sp)
	
	
*-------------------------------------------------------*
	cmp.b		#' ',d0
	beq		.fetch
*-------------------------------------------------------*	
	move.w		.sp_y(sp),d2
	move.l		.sp_psps(sp),a0
	move.l		.sp_pdst(sp),a1
*-------------------------------------------------------*

	; access sprite frame

	lea		_ascii2font,a4
	move.b		(a4,d0.w),d0
	
	; draw cells, without background-restore per cell
	
enable_restore	set	0
enable_clipping	set	1
;use_hog		set	1

;	note: don't bother with EMX codegen optimization for fonts - too expensive. assume standard EM path.

	moveq		#0,d7

	include		"ste/b_emg_spr.s"
;	include 	"ste/b_spr.s"

	moveq		#0,d0

*-------------------------------------------------------*
.fetch:
*-------------------------------------------------------*
	move.l		usp,a3
	move.b		(a3)+,d0
	move.l		a3,usp
	bne		.next
*-------------------------------------------------------*	
.sstop:	
*-------------------------------------------------------*
	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts
	
	.endif


;	include		"rmac/ste/spr2bpl.s"


	.if		(0)	; disabled, until made to work with dctx
*-------------------------------------------------------*
*
*-------------------------------------------------------*
*
*-------------------------------------------------------*
_AGT_BLiT_SlabDraw:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.psls:			ds.l	1
.pdst:			ds.l	1
.slabframe:		ds.l	1
.xpos:			ds.l	1
.ypos:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
	emitframe_draw_vars
*-------------------------------------------------------*
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
	move.l		.psls(a6),a0
	move.l		.pdst(a6),a1
	move.w		.xpos+2(a6),d1
	move.w		.ypos+2(a6),d2
;	sub.w		_g_xpreshift,d1
*-------------------------------------------------------*
	jsr		_AGT_BLiT_SlabInit

	move.l		a1,.sp_framebuf(sp)
	move.w		.slabframe+2(a6),d0
	moveq		#0,d7
	
	
enable_slabrestore	set	0
preserve_usp		set	0
		
	include		"ste/b_slab.s"

*-------------------------------------------------------*
	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts
	
	.endif


	.if	(0)	; disabled, until made to work with dctx
*-------------------------------------------------------*
*
*-------------------------------------------------------*
*
*-------------------------------------------------------*
_AGT_SlabDirtyRegion:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.psls:			ds.l	1
.pdst:			ds.l	1
.slabframe:		ds.l	1
.xpos:			ds.l	1
.ypos:			ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
	emitframe_draw_vars
*-------------------------------------------------------*
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
	move.l		.psls(a6),a0
	move.l		.pdst(a6),a1
	move.w		.xpos+2(a6),d1
	move.w		.ypos+2(a6),d2
*-------------------------------------------------------*
	jsr		_AGT_BLiT_SlabInit
	
	move.l		_g_drawcontext+dctx_prestorestate,a4
	move.l		rstr_ptide(a4),a5
	move.l		a5,usp

	move.w		.slabframe+2(a6),d0
	moveq		#0,d7

	move.l		a1,.sp_framebuf(sp)
	
enable_slabrestore	set	1
preserve_usp		set	0
		
	include		"ste/b_slabres.s"


	move.l		_g_drawcontext+dctx_prestorestate,a4
	move.l		usp,a5
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
*
*-------------------------------------------------------*
*
*-------------------------------------------------------*
_AGT_BLiT_SlabInit:
*-------------------------------------------------------*
	move.b		#2,BLiTHOP.w			; HOP = SRC
	move.w		#-1,BLiTEM2.w			; em2
	move.w		#2,BLiTSXI.w			; src xinc
	move.w		#8,BLiTDXI.w			; dst xinc
	move.w		#2,BLiTSYI.w			; src yinc, unclipped
*-------------------------------------------------------*
	rts

*-------------------------------------------------------*
*
*-------------------------------------------------------*
*
*-------------------------------------------------------*
_AGT_BLiT_SlabSetupOnce:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
*-------------------------------------------------------*
	lea		slab_precalcs,a5	
	moveq		#0,d1
	moveq		#0,d4
*-------------------------------------------------------*
.xsl:
.xol:	movem.l		d1/d4,-(sp)
	bsr		precalculate_spandata	
	movem.l		(sp)+,d1/d4
*-------------------------------------------------------*
	move.b		spandata_skew,(a5)+
	move.b		spandata_wordwidth,(a5)+
	move.w		spandata_em1,(a5)+
	move.w		spandata_em3,(a5)+
	move.w		spandata_dyinc,(a5)+
*-------------------------------------------------------*
	addq.w		#1,d1	
	and.w		#16-1,d1
	bne.s		.xol
*-------------------------------------------------------*
	addq.w		#1,d4
	cmp.w		#256,d4
	ble.s		.xsl
*-------------------------------------------------------*
.end:	move.l		(sp)+,a6
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts


precalculate_spandata:
	move.w		d1,d2
	move.w		d1,d3

;	skew/nfsr	1
;	width		1
;	em1		2
;	em3		2
;	dyi		2
	
;	lea		ssb_data-6(a0),a3
;	move.l		a3,$ffff8a24.w			; src
	
;	word offset	
	moveq		#-16,d5
	and.w		d3,d5				; ceil16(x)
	eor.w		d5,d3				; pixel offset
	lsr.w		d5	
	lea		(a1,d5.w),a3
;	move.l		a3,$ffff8a32.w			; dst
	
;	skew
;	move.b		d3,$ffff8a3d.w
	move.b		d3,spandata_skew

	; find destination word width of shifted sprite
	
	moveq		#16-1,d5
	add.w		d4,d5				; w+round	
	moveq		#-16,d0	
	move.w		d3,d6
	add.w		d5,d6				; rshift+w+round
	and.w		d0,d6				; wceil(rshift+w)

.STE_SlabNoClipGeneral4BPL_NFSR_test:

	; NFSR test

;	or.b		#$40,$ffff8a3d.w		; BLIT: nfsr
	
	and.w		d5,d0
	sub.w		d6,d0
	beq.s		.STE_SlabNoClipGeneral4BPL_EFSR


*-------------------------------------------------------*
.STE_SlabNoClipGeneral4BPL_NFSR:
*-------------------------------------------------------*
	; NFSR
;	or.b		#$40,$ffff8a3d.w		; BLIT: nfsr
	or.b		#$40,spandata_skew

.STE_SlabNoClipGeneral4BPL_EFSR:

	; dst skip adjust by dst width	
;	move.w		_g_linebytes,a4
	lsr.w		#1,d6
;	sub.w		d6,a4

	; final height
;	move.w		d4,d7				; h

;	move.w		a3,$ffff8a22.w			; src yinc


	; shifted left-edge mask
	moveq		#-1,d0
	lsr.w		d3,d0

	; shifted right-edge mask	
	add.w		d4,d2
	subq.w		#1,d2
	and.w		#16-1,d2
	addq.w		#1,d2
	moveq		#-1,d4
	lsr.w		d2,d4
	not.w		d4
	
	;moveq		#-1,d4

	; word-width				
	lsr.w		#(4-1),d6			; >>4 *2
;	move.w		d6,$ffff8a36.w			; BLIT: width
	move.b		d6,spandata_wordwidth

	cmp.w		#1,d6
	bne.s		.multi
	and.w		d4,d0
.multi:	
;	move.w		d0,$ffff8a28.w	
;	move.w		d4,$ffff8a2c.w

	move.w		d0,spandata_em1
	move.w		d4,spandata_em3
	

*-------------------------------------------------------*
*	common to mask and colour
*-------------------------------------------------------*

;	addq.l		#8,a4
;	move.w		a4,$ffff8a30.w			; dst yinc = -dstwidth

	neg.w		d6
	asl.w		#3,d6
	addq.w		#8,d6
	addq.w		#2,d6
;	move.w		d6,$ffff8a30.w			; dst yinc = -dstwidth

	move.w		d6,spandata_dyinc
	rts


*-------------------------------------------------------*

enable_restore		set	1

*-------------------------------------------------------*
_AGT_MarkDirtyRectangle:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.pdst:			ds.l	1
.xpos:			ds.l	1
.ypos:			ds.l	1
.xs:			ds.l	1
.ys:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
*-------------------------------------------------------*
	move.l		.pdst(a6),a1
	move.w		.xpos+2(a6),d1
	move.w		.ypos+2(a6),d2
	move.w		.xs+2(a6),d3
	move.w		.ys+2(a6),d4
	
	; todo: optimize all of this
	
	add.w		d1,d3
	add.w		#16-1,d3
	and.w		#-16,d1
	and.w		#-16,d3
	sub.w		d1,d3
		
	lsr.w		#1,d3
	lsr.w		#1,d1
	
	.if		(enable_restore&(use_sprite_saverestore|use_sprite_pagerestore))
	move.l		_g_drawcontext+dctx_prestorestate,a4
	move.l		rstr_ptide(a4),a5
	move.l		a5,d5
	.endif
	
	
;	lea		160.w,a4
	move.w		_g_linebytes,a4
	sub.w		d3,a4


	move.w		d3,d6
	lsr.w		#2,d6
	
	add.w		d1,a1
	
	move.w		d2,d7
	muls.w		_g_linebytes,d7
	add.l		d7,a1
	
	.if		(enable_restore&use_sprite_pagerestore)	
	move.w		d2,(a5)+			; y
	move.w		d1,(a5)+			; xo

	move.l		a1,(a5)+			; clear addr
	move.w		a4,(a5)+			; dst skip
	move.w		d6,(a5)+			; word width*2
	move.w		d4,(a5)+			; lines
	.endif	


	.if		(enable_restore&(use_sprite_saverestore|use_sprite_pagerestore))
	move.l		d5,(a5)+
	move.l		_g_drawcontext+dctx_prestorestate,a4
	move.l		a5,rstr_ptide(a4)
	.endif

*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts


*-------------------------------------------------------*

	include		"ste/spr_clear.s"
	
*-------------------------------------------------------*
			.bss
*-------------------------------------------------------*

_ascii2font:		ds.b		256
	
; clipping window: x:guard/pixel y:pixel
;_g_clipwin_gpp:
;_g_clipwin_gpp_x1:	ds.w		1
;_g_clipwin_gpp_y1p:	ds.w		1
;_g_clipwin_gpp_x2:	ds.w		1
;_g_clipwin_gpp_y2p:	ds.w		1
;_g_clipwin_gpp_x1p:	ds.w		1
;_g_clipwin_gpp_x2p:	ds.w		1

; clipping window (quick guard reject): x:guard y:guard
;_g_clipwinq_gg:
;_g_clipwinq_gg_x:	ds.w		1
;_g_clipwinq_gg_xs:	ds.w		1
;_g_clipwinq_gg_y:	ds.w		1
;_g_clipwinq_gg_ys:	ds.w		1

*-------------------------------------------------------*

spandata_skew:		ds.b		1
spandata_wordwidth:	ds.b		1
spandata_em1:		ds.w		1
spandata_em3:		ds.w		1
spandata_dyinc:		ds.w		1

slab_precalcs:		ds.b		8*(257*16)

*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
