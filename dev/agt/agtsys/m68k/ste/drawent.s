*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Entity drawing system:
*	- Walk viewport entity chain
*	- Interpret entity flags, states, functions
*	- Execute appropriate drawing routines
*-------------------------------------------------------*

	include		"config.inc"

	include		"blitter.inc"
	include		"emsprt.inc"

	include		"entity.inc"
	include		"asset.inc"

	include		"drawctx.inc"

	include		"ste/b_drawsup.inc"

	include		"prf.inc"
	
	;.opt		"~oall"
	
*-------------------------------------------------------*

within_entitydraw	set	1

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_WORLD_XMAJOR
	.globl		_BLiT_EntityDrawVisibleXM_68k
	.globl		_BLiT_EntityDeferVisibleXM_68k
	.globl		_BLiT_EntityOccludeVisibleXM_68k
	.globl		_EntityInteractVisibleXM_68k
	.globl		_gp_entity_x_head
	.globl		_gp_entity_x_aftertail
	.endif
	
	.if		^^defined AGT_CONFIG_WORLD_YMAJOR
	.globl		_BLiT_EntityDrawVisibleYM_68k
	.globl		_BLiT_EntityDeferVisibleYM_68k
	.globl		_BLiT_EntityOccludeVisibleYM_68k
	.globl		_EntityInteractVisibleYM_68k
	.globl		_gp_entity_y_head
	.globl		_gp_entity_y_aftertail
	.endif


;	.globl		_BLiT_EntityDrawLayers_68k
	.globl		_EntityTick_68k
	.globl		_EntitySpatial_68k
	.globl		_gp_entity_tick_tail
	.globl		_gp_entity_tick_head
	.globl		_gp_entity_tick_aftertail
;	.globl		_EntityLink_Tick_Ext

;	EM-optimized (but not code-generated) blitter sprite routines for <= 32-wide sprites

	.globl		_AGT_BLiT_EMSprDrawClipped
	.globl		_AGT_BLiT_EMSprDrawUnClipped
	.globl		_AGT_BLiT_EMSprInit
	.globl		_AGT_BLiT_IMSprInit
	.globl		_AGT_SlabDirtyRegion
	.globl		_AGT_BLiT_SlabInit
	
	.globl		_FNEntityDraw_EMXSlabRestore

;	safety checks

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	.globl		_AGT_Exception_IMSFrame
	.globl		_AGT_Exception_EMSFrame
	.globl		_AGT_Exception_EMHFrame
	.globl		_AGT_Exception_EMXFrame
	.globl		_AGT_Exception_SLSFrame
	.globl		_AGT_Exception_SLRFrame
	.endif
	
;	clipping window

	.globl		_OCM_occlusion_add_T
	.globl		_OCM_occlusion_sub_O

;	.globl		_g_clipwin_gpp
;	.globl		_g_clipwinq_gg

	.globl		_g_linebytes

	.globl		_g_pe_viewport

	.globl		_g_drawcontext

	.globl		slab_precalcs
		
	.if		^^defined AGT_CONFIG_DEBUGDRAW
	.globl		_g_debugdraw
	.endif


*-------------------------------------------------------*
*	Direct 68k version to support VISTRIG links
*-------------------------------------------------------*
EntityLink_Tick_68k:
*-------------------------------------------------------*
*	a0		pentity/pcurr
*	a1		pnear/psort
*	a3		*
*-------------------------------------------------------*
*	if (!psort || !(psort->f_self & EntityFlag_TICKED))
*-------------------------------------------------------*
	move.l		a1,d0
	beq.s		.not_near
.is_near:
	btst.b		#1,ent_self(a1)
	beq.s		.not_near
*-------------------------------------------------------*
.dir_tsort:
*-------------------------------------------------------*
*	if (psort->tick_priority < pcurr->tick_priority)
*-------------------------------------------------------*
	move.w		ent_tick_priority(a0),d0
	cmp.w		ent_tick_priority(a1),d0	; if (psort->tick_priority < pcurr->tick_priority)
	blt.s		.tsort_left
*-------------------------------------------------------*
.tsort_right:
*-------------------------------------------------------*
*	while (psort->pnext_tick->tick_priority < pcurr->tick_priority)
*		psort = psort->pnext_tick;	
*-------------------------------------------------------*
	move.l		a1,a3
.search_r:
	move.l		a3,a1				; psort = psort->pnext_tick;
	move.l		ent_pnext_tick(a1),a3
	cmp.w		ent_tick_priority(a3),d0
	bgt.s		.search_r
*-------------------------------------------------------*
*	// insert
*	pcurr->pnext_tick = psort->pnext_tick;
*	pcurr->pprev_tick = psort;
*	pcurr->pnext_tick->pprev_tick = pcurr;
*	psort->pnext_tick = pcurr;	
*-------------------------------------------------------*
	move.l		ent_pnext_tick(a1),a3
	move.l		a3,ent_pnext_tick(a0)
	move.l		a1,ent_pprev_tick(a0)
	move.l		a0,ent_pprev_tick(a3)
	move.l		a0,ent_pnext_tick(a1)
	rts
*-------------------------------------------------------*
.not_near:
*-------------------------------------------------------*
*	just link to end of entity chain
*-------------------------------------------------------*
	move.l		_gp_entity_tick_tail,a1
	move.w		ent_tick_priority(a0),d0
	bra.s		.tsort_left
*-------------------------------------------------------*
*	while (psort->tick_priority > pcurr->tick_priority)
*		psort = psort->pprev_tick;
*-------------------------------------------------------*
.search_l:
	move.l		ent_pprev_tick(a1),a1		; psort->pprev_tick;
.tsort_left:
	cmp.w		ent_tick_priority(a1),d0
	blt.s		.search_l
*-------------------------------------------------------*
*	// insert
*	pcurr->pnext_tick = psort->pnext_tick;
*	pcurr->pprev_tick = psort;
*	pcurr->pnext_tick->pprev_tick = pcurr;
*	psort->pnext_tick = pcurr;	
*-------------------------------------------------------*
	move.l		ent_pnext_tick(a1),a3
	move.l		a3,ent_pnext_tick(a0)
	move.l		a1,ent_pprev_tick(a0)
	move.l		a0,ent_pprev_tick(a3)
	move.l		a0,ent_pnext_tick(a1)
	rts
	


*-------------------------------------------------------*
_EntityTick_68k:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
;.savea6:		ds.l	1
.save:			ds.l	11
.return:		ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
*	locals:
*-------------------------------------------------------*
*-------------------------------------------------------*
.sp_frame_:
*-------------------------------------------------------*	
			.text
*-------------------------------------------------------*	
	movem.l		d2-d7/a2-a6,-(sp)
*-------------------------------------------------------*
	pushctxr	ent_tick,a2
*-------------------------------------------------------*
	
	; force-terminate list
	move.l		_gp_entity_tick_tail,a5
	move.l		ent_pprev_tick(a5),a4
	move.l		a4,-(sp)
	clr.l		ent_pnext_tick(a4)

*-------------------------------------------------------*
		
	subq.l		#4,sp

*-------------------------------------------------------*
	
	move.l		_gp_entity_tick_head,a6
	bra.s		.cont
	
.next:	move.l		d0,a6
	move.l		ent_fntick(a6),d0
	beq.s		.cont				; if (curr.fntick)
	move.l		d0,a0				; curr.fntick(pcurr);
	move.l		a6,(sp)
	jsr		(a0)
;	illegal
.cont:	move.l		ent_pnext_tick(a6),d0
	bne.s		.next

*-------------------------------------------------------*

.done:	addq.l		#4,sp

*-------------------------------------------------------*

	; restore list integrity
	move.l		(sp)+,a4
	move.l		_gp_entity_tick_tail,ent_pnext_tick(a4)
	
*-------------------------------------------------------*
	popctxr		a2
*-------------------------------------------------------*
	movem.l		(sp)+,d2-d7/a2-a6
*-------------------------------------------------------*
	rts



*-------------------------------------------------------*
_EntitySpatial_68k:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	3
.return:		ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
*	locals:
*-------------------------------------------------------*
*-------------------------------------------------------*
.sp_frame_:
*-------------------------------------------------------*	
			.text
*-------------------------------------------------------*	
	movem.l		a2-a4,-(sp)
*-------------------------------------------------------*
	pushctxr	ent_spatial,a2
*-------------------------------------------------------*
	.if		^^defined AGT_CONFIG_WORLD_XMAJOR	
*-------------------------------------------------------*
	move.l		_gp_entity_x_head,a0
	move.l		_gp_entity_x_aftertail,a4
*-------------------------------------------------------*
.xnext:
*-------------------------------------------------------*
	move.l		a0,a1				; pcurr->pprev_x
	move.l		ent_pnext_x(a0),a0		; pcurr
*-------------------------------------------------------*
.xresume:
*-------------------------------------------------------*
	move.w		ent_frx(a0),d0
	cmp.w		ent_frx(a1),d0
	bge.s		.xnext
*-------------------------------------------------------*
.xpushback:
*-------------------------------------------------------*
	cmp.l		a4,a0
	beq.s		.xend
*-------------------------------------------------------*
	move.l		ent_pprev_x(a0),a1
	move.l		ent_pnext_x(a0),a2
	move.l		a1,ent_pprev_x(a2)
	move.l		a2,ent_pnext_x(a1)
*-------------------------------------------------------*
;	move.w		ent_rx(a0),d0
.xscan:	move.l		ent_pprev_x(a1),a1
	cmp.w		ent_frx(a1),d0
	blt.s		.xscan
*-------------------------------------------------------*
	move.l		ent_pnext_x(a1),a3
	move.l		a3,ent_pnext_x(a0)
	move.l		a1,ent_pprev_x(a0)
	move.l		a0,ent_pprev_x(a3)
	move.l		a0,ent_pnext_x(a1)
*-------------------------------------------------------*
	move.l		a2,a0
	move.l		ent_pprev_x(a0),a1
	bra.s		.xresume
*-------------------------------------------------------*
.xend:
*-------------------------------------------------------*
	.endif
*-------------------------------------------------------*
	.if		^^defined AGT_CONFIG_WORLD_YMAJOR	
*-------------------------------------------------------*
	move.l		_gp_entity_y_head,a0
	move.l		_gp_entity_y_aftertail,a4
*-------------------------------------------------------*
.ynext:
*-------------------------------------------------------*
	move.l		a0,a1				; pcurr->pprev_x
	move.l		ent_pnext_y(a0),a0		; pcurr
*-------------------------------------------------------*
.yresume:
*-------------------------------------------------------*
	move.w		ent_fry(a0),d0
	cmp.w		ent_fry(a1),d0
	bge.s		.ynext
*-------------------------------------------------------*
.ypushback:
*-------------------------------------------------------*
	cmp.l		a4,a0
	beq.s		.yend
*-------------------------------------------------------*
	move.l		ent_pprev_y(a0),a1
	move.l		ent_pnext_y(a0),a2
	move.l		a1,ent_pprev_y(a2)
	move.l		a2,ent_pnext_y(a1)
*-------------------------------------------------------*
;	move.w		ent_ry(a0),d0
.yscan:	move.l		ent_pprev_y(a1),a1
	cmp.w		ent_fry(a1),d0
	blt.s		.yscan
*-------------------------------------------------------*
	move.l		ent_pnext_y(a1),a3
	move.l		a3,ent_pnext_y(a0)
	move.l		a1,ent_pprev_y(a0)
	move.l		a0,ent_pprev_y(a3)
	move.l		a0,ent_pnext_y(a1)
*-------------------------------------------------------*
	move.l		a2,a0
	move.l		ent_pprev_y(a0),a1
	bra.s		.yresume
*-------------------------------------------------------*
.yend:
*-------------------------------------------------------*
	.endif
*-------------------------------------------------------*
	popctxr		a2
*-------------------------------------------------------*
	movem.l		(sp)+,a2-a4
*-------------------------------------------------------*
	rts


*-------------------------------------------------------*

	include		"ste/b_slab.inc"

	.if		^^defined AGT_CONFIG_WORLD_XMAJOR	
	.if		^^defined AGT_CONFIG_ENTITYLAYERS
	include		"ste/de_xm_dp.s"
	.else
	include		"ste/de_xm.s"
	.endif
	include		"ste/de_xm_misc.s"
	.endif

	.if		^^defined AGT_CONFIG_WORLD_YMAJOR	
	.if		^^defined AGT_CONFIG_ENTITYLAYERS	
	include		"ste/de_ym_dp.s"
	.else
	include		"ste/de_ym.s"
	.endif
	include		"ste/de_ym_misc.s"
	.endif
	
	.if		^^defined AGT_CONFIG_ENTITYLAYERS	
	include		"ste/de_layers.s"
	.endif
	
	

*-------------------------------------------------------*
_FNEntityDraw_EMXSlabRestore:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
	emitframe_draw_vars			
*-------------------------------------------------------*
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	move.l		(sp)+,.tailcall+2
*-------------------------------------------------------*
	
	; could be moved to custom init, but we don't have one yet
	
	jsr		_AGT_BLiT_EMSprInit

	; access coordinates

;	move.w		ent_frx(a0),d1			; we already fetched this in preamble
	move.w		ent_fry(a0),d2

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; must be done if LOOPBACK used for VSCROLL - very likely
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; decrement 'hitflash' counter on this object


	; handle hitflash & blinky fx

	sprite_ctrl	a0,d3,d4,d7,.done
		
	; asset/framebuffer

	move.l		ent_passet(a0),a0
	
	move.l		.sp_framebuf(sp),a1

	; include raw drawing function here (configure it first)

	.if		(1)
	
enable_restore	set	0				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	move.l		lpair_first(a0),a0

	include		"ste/b_emxc_spr.s"		; body for EMX optimized blitter sprites (0<width<=32)

	.endif
		
	.if		(1)
	
	; restore step - only if restore asset is present

	move.l		.sp_savea0(sp),a0

	move.l		ent_passet(a0),a1
	move.l		lpair_second(a1),d3
	beq		.no_restore
	
	move.w		ent_frx(a0),d1
	sub.w		.sp_snapx(sp),d1
	move.w		ent_fry(a0),d2
	sub.w		.sp_snapy(sp),d2
	move.w		ent_frame(a0),d0

	move.l		.sp_framebuf(sp),a1

	; include raw restore function here (configure it first)

enable_slabrestore	set	1
preserve_usp		set	0

	move.l		d3,a0

	include		"ste/b_slabres.s"

.no_restore:

	.endif

.done:
*-------------------------------------------------------*
.tailcall:
*-------------------------------------------------------*
	jmp		$12345678
*-------------------------------------------------------*

*-------------------------------------------------------*
			.text
*-------------------------------------------------------*

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
_AGT_Exception_IMSFrame_68k:
	move.l		a0,-(sp)
	move.l		d0,-(sp)
	jsr		_AGT_Exception_IMSFrame
	; noreturn
_AGT_Exception_EMSFrame_68k:
	move.l		a0,-(sp)
	move.l		d0,-(sp)
	jsr		_AGT_Exception_EMSFrame
	; noreturn
_AGT_Exception_EMHFrame_68k:
	move.l		a0,-(sp)
	move.l		d0,-(sp)
	jsr		_AGT_Exception_EMHFrame
	; noreturn
_AGT_Exception_EMXFrame_68k:
	move.l		a0,-(sp)
	move.l		d0,-(sp)
	jsr		_AGT_Exception_EMXFrame
	; noreturn
_AGT_Exception_SLSFrame_68k:
	move.l		a0,-(sp)
	move.l		d0,-(sp)
	jsr		_AGT_Exception_SLSFrame
	; noreturn
_AGT_Exception_SLRFrame_68k:
	move.l		a0,-(sp)
	move.l		d0,-(sp)
	jsr		_AGT_Exception_SLRFrame
	; noreturn
	.endif

*-------------------------------------------------------*
