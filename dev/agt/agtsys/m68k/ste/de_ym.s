*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Entity processing: Y major
*-------------------------------------------------------*
	
*-------------------------------------------------------*
*	Tear through entity chain in sorted order
*	drawing with appropriate [drawtype] calls
*-------------------------------------------------------*
*	Note: implements statechange minimization so
*	different draw functions get a chance to
*	call their own init functions, but only when
*	the [drawtype] changes between entities.
*-------------------------------------------------------*
_BLiT_EntityDrawVisibleYM_68k:
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
	bne		_BLiT_EntityDebugVisibleYM_68k
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
	move.l		a4,.sp_dctx(sp)
	lea		dctx_guardwindow(a4),a5
	move.l		a5,.sp_guardwin(sp)
	lea		dctx_scissorwindow(a4),a5
	move.l		a5,.sp_scissorwin(sp)
	move.l		dctx_framebuffer(a4),.sp_framebuf(sp)
	move.l		dctx_lineidx(a4),.sp_linetable(sp)
	move.w		dctx_snapadjx(a4),.sp_snapx(sp)
	move.w		dctx_snapadjy(a4),.sp_snapy(sp)	
	move.w		dctx_field(a4),.sp_mfield(sp)
	move.w		dctx_field(a4),d0
	add.w		d0,d0
	add.w		d0,d0
	move.w		d0,.sp_cfield4(sp)
	move.w		_g_linebytes,.sp_linebytes(sp)
;	move.l		.pdst(a6),.sp_framebuf(sp)	; todo: should be part of a framebuffer context, not separate arg
;	move.w		.snapx+2(a6),.sp_snapx(sp)	; todo: ditto
;	move.w		.snapy+2(a6),.sp_snapy(sp)	; todo: ditto
;	move.l		.plinetable(a6),.sp_linetable(sp)
*-------------------------------------------------------*
*	load display chain terminating condition
*-------------------------------------------------------*
	move.l		_g_pe_viewport,a0
	move.w		ent_fry(a0),d0
	add.w		dctx_guardwindow_ys(a4),d0
;	add.w		dctx_scissorwin_y2(a4),d0
;	sub.w		dctx_scissorwin_y1(a4),d0
;	add.w		dctx_guardy(a4),d0
	move.w		d0,.sp_pxymax_(sp)
*-------------------------------------------------------*
*	initial conditions
*-------------------------------------------------------*

	move.l		dctx_prestorestate(a4),a5
	move.l		rstr_ptide(a5),a5
	move.l		a5,usp

*-------------------------------------------------------*
	
	; important to begin with invalid drawtype
	; so the first drawtype will call init first

	move.w		#-1,.sp_drawtype(sp)
	
	bra		.STE_EntityDrawVisibleYM_EMXSPRQ_done



*=======================================================*
*	dedicated iterator for [drawtype]=EMXSPRQ
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRQ_next:
*-------------------------------------------------------*

	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRQ_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; no sprite draw control on Q path

	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1

	; include raw drawing function here (configure it first)

enable_restore	set	1				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_emxg_spr.s"		; body for EMX optimized blitter sprites (0<width<=32)

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRQ_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_EMXSPRQ_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to EMXSPRQ
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRQ_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_EMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_EMXSPRQ_go




*=======================================================*
*	dedicated iterator for [drawtype]=EMXSPR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPR_next:
*-------------------------------------------------------*

	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPR_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; handle hitflash & blinky fx
	
	sprite_ctrl	a0,d3,d4,d7,.STE_EntityDrawVisibleYM_EMXSPR_done

	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1

	; include raw drawing function here (configure it first)

enable_restore	set	1				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_emxc_spr.s"		; body for EMX optimized blitter sprites (0<width<=32)

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPR_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_EMXSPR_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to EMXSPR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPR_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_EMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_EMXSPR_go

*=======================================================*
*	dedicated iterator for [drawtype]=EMHSPR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMHSPR_next:
*-------------------------------------------------------*

	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMHSPR_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; handle hitflash & blinky fx
	
	sprite_ctrl	a0,d3,d4,d7,.STE_EntityDrawVisibleYM_EMHSPR_done

	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1

	; include raw drawing function here (configure it first)

enable_restore	set	1				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_emhq_spr.s"		; body for EMH optimized blitter sprites (0<width<=32)

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMHSPR_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_EMHSPR_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to EMHSPR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMHSPR_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_EMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_EMHSPR_go

*=======================================================*
*	dedicated iterator for [drawtype]=EMXSPRUR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRUR_next:
*-------------------------------------------------------*

	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRUR_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; handle hitflash & blinky fx
	
	sprite_ctrl	a0,d3,d4,d7,.STE_EntityDrawVisibleYM_EMXSPRUR_done


	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1

	move.l		lpair_first(a0),a0
	
	; include raw drawing function here (configure it first)

enable_restore	set	0				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_emxc_nr_spr.s"	; body for EMX optimized blitter sprites (0<width<=32)

	; restore step - only if restore asset is present

	move.l		.sp_savea0(sp),a0

	move.l		ent_passet(a0),a1
	move.l		lpair_second(a1),d3
	beq		.STE_EntityDrawVisibleYM_EMXSPRUR_no_restore
	
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

	include		"ste/b_emxres.s"

.STE_EntityDrawVisibleYM_EMXSPRUR_no_restore:

	move.l		.sp_savea0(sp),a0


*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRUR_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_EMXSPRUR_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to EMXSPRUR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMXSPRUR_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_EMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_EMXSPRUR_go


*=======================================================*
*	dedicated iterator for [drawtype]=EMSPRQ
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPRQ_next:
*-------------------------------------------------------*

	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPRQ_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; no hitflash draw control on Q path

	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1

	; include raw drawing function here (configure it first)

enable_restore	set	1				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_emg_spr.s"		; body for EMX optimized blitter sprites (0<width<=32)

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPRQ_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_EMSPRQ_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to EMSPRQ
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPRQ_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_EMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_EMSPRQ_go



*=======================================================*
*	dedicated iterator for [drawtype]=EMSPR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPR_next:
*-------------------------------------------------------*

	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPR_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; handle hitflash & blinky fx
	
	sprite_ctrl	a0,d3,d4,d7,.STE_EntityDrawVisibleYM_EMSPR_done

	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1

	; include raw drawing function here (configure it first)

enable_restore	set	1				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_emc_spr.s"		; body for EMX optimized blitter sprites (0<width<=32)

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPR_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_EMSPR_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to EMSPR
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_EMSPR_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_EMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_EMSPR_go



*=======================================================*
*	dedicated iterator for [drawtype]=SPRITE
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SPRITE_next:
*-------------------------------------------------------*

	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate
	
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SPRITE_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble
	
	; compensate for loopback buffer
	
	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame
	
	move.w		ent_frame(a0),d0

	; handle blinky fx
	
	sprite_ctrl_nl	a0,d4,d7,.STE_EntityDrawVisibleYM_SPRITE_done

	
	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1
		
	; include raw drawing function here (configure it first)
	
enable_restore	set	1				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_spr.s"			; body for normal blitter sprites

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SPRITE_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_SPRITE_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to SPRITE
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SPRITE_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_IMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_SPRITE_go



*=======================================================*
*	dedicated iterator for [drawtype]=SLAB
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SLAB_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SLAB_go:
*-------------------------------------------------------*

	; access coordinates
	
	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble
	
	; compensate for loopback buffer
	
	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame
	
	move.w		ent_frame(a0),d0

	; handle hitflash & blinky fx
	
	sprite_ctrl	a0,d3,d4,d7,.STE_EntityDrawVisibleYM_SLAB_done
	
	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1
		
	; include raw drawing function here (configure it first)
	
enable_slabrestore	set	0
preserve_usp		set	1

	move.l		lpair_first(a0),a0
	
	include		"ste/b_slabopt.s"
	
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

	move.l		.sp_savea0(sp),a0
	
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SLAB_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_SLAB_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to SPRITE
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_SLAB_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_SlabInit			
	bra		.STE_EntityDrawVisibleYM_SLAB_go



*=======================================================*
*	dedicated iterator for [drawtype]=CUSTOM
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_CUSTOM_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_CUSTOM_go:
*-------------------------------------------------------*

	; handle blinky fx

	sprite_ctrl_nl	a0,d4,d7,.STE_EntityDrawVisibleYM_CUSTOM_done

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)

	move.l		ent_fncustomdraw(a0),a6
	jsr		(a6)

	move.l		.sp_savea0(sp),a0
	
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_CUSTOM_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_CUSTOM_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_CUSTOM_setup:
*-------------------------------------------------------*
;	move.l		ent_fndrawinit(a0),a6		; todo
;	jsr		(a6)
	bra		.STE_EntityDrawVisibleYM_CUSTOM_go
	
*=======================================================*
*	[drawtype] has changed - perform path switch
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_evaluate:
*-------------------------------------------------------*
	move.w		d0,.sp_drawtype(sp)
	add.w		d0,d0
	add.w		d0,d0
	jmp		.types(pc,d0.w)
.types:	bra.w		.STE_EntityDrawVisibleYM_NONE_setup
	bra.w		.STE_EntityDrawVisibleYM_SPRITE_setup
	bra.w		.STE_EntityDrawVisibleYM_EMSPR_setup
	bra.w		.STE_EntityDrawVisibleYM_EMSPRQ_setup
	bra.w		.STE_EntityDrawVisibleYM_EMXSPR_setup
	bra.w		.STE_EntityDrawVisibleYM_EMXSPRQ_setup
	bra.w		.STE_EntityDrawVisibleYM_EMXSPRUR_setup
	bra.w		.STE_EntityDrawVisibleYM_EMHSPR_setup
	bra.w		.STE_EntityDrawVisibleYM_SLAB_setup
	bra.w		.STE_EntityDrawVisibleYM_CUSTOM_setup
	bra.w		.STE_EntityDrawVisibleYM_RFILL_setup



*=======================================================*
*	dedicated iterator for [drawtype]=RFILL
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_RFILL_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate
	
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_RFILL_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble
	
	; compensate for loopback buffer
	
	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; handle blinky fx

	sprite_ctrl_nl	a0,d4,d7,.STE_EntityDrawVisibleYM_RFILL_done

	; use frame number as colour
	
	move.w		ent_frame(a0),d5
	
	move.w		ent_sx(a0),d3
	move.w		ent_sy(a0),d4
	
	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0
	move.l		.sp_framebuf(sp),a1
		
	; include raw drawing function here (configure it first)
	
enable_restore	set	1				; enable sprite-restore
enable_clipping	set	1				; enable full xy clipping

	include		"ste/b_rfill.s"			; body for normal blitter sprites

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_RFILL_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDrawVisibleYM_RFILL_next
	bra		.STE_EntityDrawVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to RFILL
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_RFILL_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_IMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDrawVisibleYM_RFILL_go

	
*=======================================================*
*	dedicated iterator for [drawtype]=NONE
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_NONE_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityDrawVisibleYM_evaluate
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_NONE_setup:
.STE_EntityDrawVisibleYM_NONE_go:
*-------------------------------------------------------*
	; nothing todo here - skip!
*-------------------------------------------------------*
.STE_EntityDrawVisibleYM_NONE_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt.s		.STE_EntityDrawVisibleYM_NONE_next
	bra		.STE_EntityDrawVisibleYM_complete

		
*=======================================================*
.STE_EntityDrawVisibleYM_complete:
*-------------------------------------------------------*

;	move.l		usp,a5
;	move.l		a5,_g_drawcontext+dctx_clearstack_tide

*-------------------------------------------------------*

	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6

	move.l		.pctx(a6),a4
	move.l		dctx_prestorestate(a4),a4
	move.l		usp,a5
	move.l		a5,rstr_ptide(a4)
	
	unlk		a6
	popctxr		a2
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts
