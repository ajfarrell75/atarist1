*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Entity processing: X major
*-------------------------------------------------------*
	
*-------------------------------------------------------*
*	Tear through entity chain in sorted order
*	deferring all visible DrawTypes
*-------------------------------------------------------*
*	Note: does not actually draw anything - sorts
*	visible items into priority buckets only.
*-------------------------------------------------------*
_BLiT_EntityDeferVisibleXM_68k:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.pctx:			ds.l	1
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
	.if		^^defined AGT_CONFIG_DEBUGDRAW
	tst.w		_g_debugdraw
	bne		_BLiT_EntityDebugVisibleXM_68k
	.endif
*-------------------------------------------------------*	
	movem.l		d2-d7/a2-a5,-(sp)
	pushctxr	ent_draw,a2
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
*	load C args to local frame
*-------------------------------------------------------*
	move.l		.pctx(a6),a4

*-------------------------------------------------------*
*	Reset layer chains
*-------------------------------------------------------*
	lea		entity_layers,a0
	.rept		draw_layers
	clr.l		(a0)+
	.endr

*-------------------------------------------------------*
*	load display chain terminating condition
*-------------------------------------------------------*
	move.l		_g_pe_viewport,a0
	move.w		ent_frx(a0),d0
	add.w		dctx_guardwindow_xs(a4),d0
	move.w		d0,.sp_pxymax_(sp)
*-------------------------------------------------------*
*	initial conditions
*-------------------------------------------------------*

	; important to begin with invalid drawtype
	; so the first drawtype will call init

	move.w		#-1,.sp_drawtype(sp)
	lea		entity_layers,a1
	
	bra		.EntityDeferVisibleXM_NONE_done

*=======================================================*
*	[drawtype] has changed - perform path switch
*-------------------------------------------------------*
.EntityDeferVisibleXM_evaluate:
*-------------------------------------------------------*
	move.w		d0,.sp_drawtype(sp)
	add.w		d0,d0
	add.w		d0,d0
	jmp		.types(pc,d0.w)
.types:	bra.w		.EntityDeferVisibleXM_NONE_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup
	bra.w		.EntityDeferVisibleXM_DEFER_setup

*=======================================================*
*	dedicated iterator for [drawtype]=ANY
*-------------------------------------------------------*
.EntityDeferVisibleXM_DEFER_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.EntityDeferVisibleXM_evaluate
*-------------------------------------------------------*
.EntityDeferVisibleXM_DEFER_go:
*-------------------------------------------------------*

	move.w		ent_drawlayer(a0),d1
	lea		(a1,d1.w),a3			; layer head
	move.l		(a3),a2
	move.l		a2,ent_layer_(a0)		; link old head after new entity
	move.l		a0,(a3)				; update head with new entity
	
*-------------------------------------------------------*
.EntityDeferVisibleXM_DEFER_done:
*-------------------------------------------------------*
	move.l		ent_pnext_x(a0),a0		; next visible entity
	move.w		ent_frx(a0),d1
	cmp.w		.sp_pxymax_(sp),d1
	blt		.EntityDeferVisibleXM_DEFER_next
	bra		.EntityDeferVisibleXM_complete
*-------------------------------------------------------*
.EntityDeferVisibleXM_DEFER_setup:
*-------------------------------------------------------*
	bra		.EntityDeferVisibleXM_DEFER_go

*=======================================================*
*	dedicated iterator for [drawtype]=NONE
*-------------------------------------------------------*
.EntityDeferVisibleXM_NONE_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.EntityDeferVisibleXM_evaluate
*-------------------------------------------------------*
.EntityDeferVisibleXM_NONE_setup:
.EntityDeferVisibleXM_NONE_go:
*-------------------------------------------------------*
	; nothing todo here - skip!
*-------------------------------------------------------*
.EntityDeferVisibleXM_NONE_done:
*-------------------------------------------------------*
	move.l		ent_pnext_x(a0),a0		; next visible entity
	move.w		ent_frx(a0),d1
	cmp.w		.sp_pxymax_(sp),d1
	blt.s		.EntityDeferVisibleXM_NONE_next
;	bra		.EntityDeferVisibleXM_complete
		
*=======================================================*
.EntityDeferVisibleXM_complete:
*-------------------------------------------------------*

	bra		_BLiT_EntityDrawLayers_68k
	