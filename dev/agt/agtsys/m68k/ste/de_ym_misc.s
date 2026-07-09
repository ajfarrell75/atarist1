	.if		^^defined AGT_CONFIG_DEBUGDRAW

*-------------------------------------------------------*
_BLiT_EntityDebugVisibleYM_68k:
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
*	locals: used by sprite routines
*-------------------------------------------------------*
;.sp_skew:		ds.w	1			; don't move this one
;.sp_maskwidth:		ds.w	1
;.sp_yoff:		ds.w	1
;.sp_yc:			ds.w	1
;.sp_yrem:		ds.w	1
;.sp_field4:		ds.w	1
;.sp_save_usp:		ds.l	1
;.sp_linetable:		ds.l	1
;.sp_nextcmp:		ds.l	1
;.sp_x:			ds.w	1
;.sp_y:			ds.w	1
*-------------------------------------------------------*
*	locals: shadow function arguments
*-------------------------------------------------------*
;.sp_framebuf:		ds.l	1
;.sp_snapx:		ds.w	1
;.sp_snapy:		ds.w	1
*-------------------------------------------------------*
*	locals: used by algorithm
*-------------------------------------------------------*
;.sp_asset:		ds.l	1
;.sp_pxymax_:		ds.l	1
;.sp_savea0:		ds.l	1
;.sp_layers:		ds.l	1
;.sp_drawtype:		ds.l	1
*-------------------------------------------------------*
.sp_frame_:		
*-------------------------------------------------------*
			.text
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

	jsr		_AGT_BLiT_IMSprInit

	bra		.STE_EntityDebugVisibleYM_RDEBUG_done

*=======================================================*
*	dedicated iterator for [drawtype]=RFILL
*-------------------------------------------------------*
.STE_EntityDebugVisibleYM_RDEBUG_next:
*-------------------------------------------------------*	
*-------------------------------------------------------*
.STE_EntityDebugVisibleYM_RDEBUG_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble
	
	; compensate for loopback buffer
	
	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; use frame number as colour
	
	move.w		ent_frame(a0),d5
	add.w		ent_id(a0),d5
	
	move.w		ent_sx(a0),d3
	move.w		ent_sy(a0),d4
	
	; decrement 'hitflash' counter on this object
	
;	move.w		ent_draw_hitflash(a0),d7	; $0000 / $FFxx
;	neg.b		d7
;	subx.b		d7,d7				; $0000 / $FFFF
;	add.b		d7,ent_draw_hitflash+1(a0)
	
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

	include		"rmac/ste/b_rfill.s"			; body for normal blitter sprites

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityDebugVisibleYM_RDEBUG_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityDebugVisibleYM_RDEBUG_next
	bra		.STE_EntityDebugVisibleYM_complete
*-------------------------------------------------------*
*	[drawtype] has suddenly changed to RFILL
*-------------------------------------------------------*
.STE_EntityDebugVisibleYM_RDEBUG_setup:
*-------------------------------------------------------*
	jsr		_AGT_BLiT_IMSprInit			; todo: init type depends on platform & spr format
	bra		.STE_EntityDebugVisibleYM_RDEBUG_go

*=======================================================*
.STE_EntityDebugVisibleYM_complete:
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
	popctxr		ent_draw,a2
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

	.endif		;^^defined AGT_CONFIG_DEBUGDRAW	

	
*-------------------------------------------------------*
_EntityInteractVisibleYM_68k:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.vxs:			ds.l	1
.vxg:			ds.l	1
.vys:			ds.l	1
.vyg:			ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
*	locals: shadow function arguments
*-------------------------------------------------------*
*-------------------------------------------------------*
*	locals: used by algorithm
*-------------------------------------------------------*
.sp_pxmax:		ds.w	1
.sp_pymax:		ds.w	1
.sp_savea0:		ds.l	1
*-------------------------------------------------------*
.sp_frame_:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*	
	movem.l		d2-d7/a2-a5,-(sp)
	pushctxr	ent_interact,a2
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
*	load C args to local frame
*-------------------------------------------------------*
	
	move.l		_g_pe_viewport,a2
	move.w		ent_frx(a2),d1
;	add.w		dctx_scissorwin_x2(a4),d1
;	sub.w		dctx_scissorwin_x1(a4),d1
	add.w		.vxs+2(a6),d1
	add.w		.vxg+2(a6),d1
	add.w		.vxg+2(a6),d1
	move.w		d1,.sp_pxmax(sp)		; pxmax
	
	move.w		ent_fry(a2),d2
;	add.w		#240+(32*2),d2
;	add.w		dctx_scissorwin_y2(a4),d2
;	sub.w		dctx_scissorwin_y1(a4),d2
;	add.w		dctx_guardy(a4),d2
;	add.w		dctx_guardy(a4),d2
	add.w		.vys+2(a6),d2
	add.w		.vyg+2(a6),d2
	add.w		.vyg+2(a6),d2
	move.w		d2,.sp_pymax(sp)		; pymax

	move.l		ent_pnext_y(a2),a0		; pcurr
	cmp.w		ent_fry(a0),d2			; (pcurr->ry < pymax)
	ble		.term_outer
*-------------------------------------------------------*
.next_outer:
*-------------------------------------------------------*
	move.w		ent_self(a0),d7
	bmi.s		.term_inner
*-------------------------------------------------------*
	.if		^^defined AGT_CONFIG_VISTRIGGER
*-------------------------------------------------------*

	btst		#10,d7
	beq.s		.no_vistrig

	eor.w		#($0400|$0200),d7		; & (~EntityFlag_VISTRIG) | EntityFlag_TICKED
	move.w		d7,ent_self(a0)

	; todo: could optimise this (slightly) with inlined version
	; but it's an infrequent trigger event so not much gain...
	;
	; d0: *
	; a0: pself
	; a1: pnear
	; a3: *
	
	move.l		_g_pe_viewport,a1
	jsr		EntityLink_Tick_68k

*-------------------------------------------------------*
	.endif
	
*-------------------------------------------------------*
.no_vistrig:
*-------------------------------------------------------*
	move.w		ent_fry(a0),d4
	add.w		ent_sy(a0),d4			; vmax = e.ry + e.sy;
	move.w		ent_frx(a0),d3
	cmp.w		d3,d1
	ble.s		.term_inner
	add.w		ent_sx(a0),d3			; hmax = e.rx + e.sx
	cmp.w		ent_frx(a2),d3
	ble.s		.term_inner

*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a1		; petest = e.pnext_y;
	cmp.w		ent_fry(a1),d4			; (petest->ry < vmax)
	ble.s		.term_inner

*-------------------------------------------------------*
.next_inner:
*-------------------------------------------------------*
	
	move.w		ent_frx(a1),d5
	cmp.w		d5,d3				; (petest->rx < hmax)
	ble.s		.no_contact
	add.w		ent_sx(a1),d5			; ((petest->rx + petest->sx) > e.rx)
	cmp.w		ent_frx(a0),d5
	ble.s		.no_contact

;	avoid interacting pre-killed objects

	move.w		ent_self(a1),d0
	bmi.s		.no_contact
	
*-------------------------------------------------------*
.do_interactions:
*-------------------------------------------------------*

	move.w		ent_interactswith(a1),d6
	and.w		d7,d6
	bne.s		.do_i0
.no_i0:
	move.w		ent_interactswith(a0),d6
	and.w		ent_self(a1),d6
	bne.s		.do_i1
.no_i1:
	
*-------------------------------------------------------*
.no_contact:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a1),a1		; petest = ptest->pnext_y;
	cmp.w		ent_fry(a1),d4
	bgt.s		.next_inner

*-------------------------------------------------------*
.term_inner:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0
	cmp.w		ent_fry(a0),d2
	bgt		.next_outer
	bra		.term_outer	

.do_i0:
	move.l		ent_fncollide(a0),d0
	beq.s		.no_i0
	move.l		d0,a6

	tst.w		ent_phiddenasset(a0)
	bne.s		.indirect_i0
	tst.w		ent_phiddenasset(a1)
	beq.s		.direct_i0
.indirect_i0:
	bsr		.test_hidden_a0_a1
	tst.w		d0
	beq.s		.no_i1
.direct_i0:

	move.w		d3,-(sp)
	move.w		d4,-(sp)

	pea		(a1)
	pea		(a0)
	jsr		(a6)
	move.l		(sp)+,a0
	move.l		(sp)+,a1

	move.w		(sp)+,d4
	move.w		(sp)+,d3

	move.l		_g_pe_viewport,a2
	move.w		.sp_pxmax(sp),d1
	move.w		.sp_pymax(sp),d2
	move.w		ent_self(a0),d7
	bra.s		.no_i0
	
.do_i1:
	move.l		ent_fncollide(a1),d0
	beq.s		.no_i1
	move.l		d0,a6

	tst.w		ent_phiddenasset(a0)
	bne.s		.indirect_i1
	tst.w		ent_phiddenasset(a1)
	beq.s		.direct_i1
.indirect_i1:
	bsr		.test_hidden_a0_a1
	tst.w		d0
	beq.s		.no_i1
.direct_i1:

	move.w		d3,-(sp)
	move.w		d4,-(sp)

	pea		(a0)
	pea		(a1)
	jsr		(a6)
	move.l		(sp)+,a1
	move.l		(sp)+,a0

	move.w		(sp)+,d4
	move.w		(sp)+,d3

	move.l		_g_pe_viewport,a2
	move.w		.sp_pxmax(sp),d1
	move.w		.sp_pymax(sp),d2
	move.w		ent_self(a0),d7
	bra		.no_i1

*-------------------------------------------------------*
.term_outer:
*-------------------------------------------------------*
	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	unlk		a6
	popctxr		a2	
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts


*-------------------------------------------------------*
.test_hidden_a0_a1:
*-------------------------------------------------------*
	
	move.l		ent_phiddenasset(a0),d0
	beq		.nha0yha1
	move.l		ent_phiddenasset(a1),d1
	beq		.yha0nha1

.yha0yha1:

	; both have hidden assets
	
	move.l		d0,a2
	move.w		ent_frame(a0),d0
	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	cmp.w		sps_framecount(a2),d0
	blo.s		._fok01
._ffault01:
	not.w		$ffff8240.w
	bra.s		._ffault01
._fok01:
	.endif		
	btst.b		#(8-8),sps_flags(a2)
	beq.s		._fndfa01
	add.w		d0,d0
._fndfa01:
	add.w		d0,d0				; todo: game layer can prescale this
	add.w		d0,d0
	move.l		sps_frameindex(a2,d0.w),a2	; access sprite frame data

	move.l		d1,a3
	move.w		ent_frame(a1),d0
	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	cmp.w		sps_framecount(a3),d0
	blo.s		._fok02
._ffault02:
	not.w		$ffff8240.w
	bra.s		._ffault02
._fok02:
	.endif
	btst.b		#(8-8),sps_flags(a3)
	beq.s		._fndfa02
	add.w		d0,d0
._fndfa02:
	add.w		d0,d0				; todo: game layer can prescale this
	add.w		d0,d0
	move.l		sps_frameindex(a3,d0.w),a3	; access sprite frame data


	move.w		ent_frx(a1),d1			;   |Bx1n
	add.w		spf_xo(a3),d1

	move.w		ent_frx(a0),d2			; Ax2n|
	add.w		spf_xo(a2),d2
	move.w		spf_w(a2),d0
;	add.w		spf_w(a2),d2
	add.w		d0,d2
	cmp.w		d2,d1
	bge.s		.cull0	
	
	add.w		spf_w(a3),d1			; Bx2n|	
;	sub.w		spf_w(a2),d2
	sub.w		d0,d2
	cmp.w		d2,d1				;    |Ax1n
	ble.s		.cull0	
	
	move.w		ent_fry(a1),d1			;   |By1n
	add.w		spf_yo(a3),d1

	move.w		ent_fry(a0),d2			; Ay2n|
	add.w		spf_yo(a2),d2
	move.w		spf_h(a2),d0
;	add.w		spf_h(a2),d2
	add.w		d0,d2
	cmp.w		d2,d1
	bge.s		.cull0	
	
	add.w		spf_h(a3),d1			; By2n|	
;	sub.w		spf_h(a2),d2
	sub.w		d0,d2
	cmp.w		d2,d1
	ble.s		.cull0


.pass0:	moveq		#-1,d0
	rts
	
.cull0:	moveq		#0,d0
	rts



.nha0yha1:

	; a1 has hidden asset

	move.l		ent_phiddenasset(a1),d0		
	move.l		d0,a3
	move.w		ent_frame(a1),d0
	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	cmp.w		sps_framecount(a3),d0
	blo.s		._fok1
._ffault1:
	not.w		$ffff8240.w
	bra.s		._ffault0
._fok1:
	.endif
	btst.b		#(8-8),sps_flags(a3)
	beq.s		._fndfa1
	add.w		d0,d0
._fndfa1:
	add.w		d0,d0				; todo: game layer can prescale this
	add.w		d0,d0
	move.l		sps_frameindex(a3,d0.w),a3	; access sprite frame data

	move.w		ent_frx(a1),d1			;   |Bx1n
	add.w		spf_xo(a3),d1
	move.w		ent_frx(a0),d2			; Ax2|
	add.w		ent_sx(a0),d2
	cmp.w		d2,d1
	bge.s		.cull1	
	
	add.w		spf_w(a3),d1			; Bx2n|	
	cmp.w		ent_frx(a0),d1			;    |Ax1
	ble.s		.cull1	
	
	move.w		ent_fry(a1),d1			;   |By1n
	add.w		spf_yo(a3),d1
	move.w		ent_fry(a0),d2			; Ay2|
	add.w		ent_sy(a0),d2
	cmp.w		d2,d1
	bge.s		.cull1	
	
	add.w		spf_h(a3),d1			; By2n|	
	cmp.w		ent_fry(a0),d1			;    |Ay1
	ble.s		.cull1	
	
.pass1:	moveq		#-1,d0
	rts
	
.cull1:	moveq		#0,d0
	rts




.yha0nha1:

	; a0 has hidden asset

	move.l		ent_phiddenasset(a0),d0		
	move.l		d0,a2
	move.w		ent_frame(a0),d0
	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	cmp.w		sps_framecount(a2),d0
	blo.s		._fok2
._ffault2:
	not.w		$ffff8240.w
	bra.s		._ffault2
._fok2:
	.endif		
	btst.b		#(8-8),sps_flags(a2)
	beq.s		._fndfa2
	add.w		d0,d0
._fndfa2:
	add.w		d0,d0				; todo: game layer can prescale this
	add.w		d0,d0
	move.l		sps_frameindex(a2,d0.w),a2	; access sprite frame data

	move.w		ent_frx(a0),d1			;   |Bx1n
	add.w		spf_xo(a2),d1
	move.w		ent_frx(a1),d2			; Ax2|
	add.w		ent_sx(a1),d2
	cmp.w		d2,d1
	bge.s		.cull2	
	
	add.w		spf_w(a2),d1			; Bx2n|	
	cmp.w		ent_frx(a1),d1			;    |Ax1
	ble.s		.cull2	
	
	move.w		ent_fry(a0),d1			;   |By1n
	add.w		spf_yo(a2),d1
	move.w		ent_fry(a1),d2			; Ay2|
	add.w		ent_sy(a1),d2
	cmp.w		d2,d1
	bge.s		.cull2
	
	add.w		spf_h(a2),d1			; By2n|	
	cmp.w		ent_fry(a1),d1			;    |Ay1
	ble.s		.cull2	
	
.pass2:	moveq		#-1,d0
	rts
	
.cull2:	moveq		#0,d0
	rts






*-------------------------------------------------------*
_BLiT_EntityOccludeVisibleYM_68k:
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
	movem.l		d2-d7/a2-a5,-(sp)
	pushctxr	ent_draw,a2
	link		a6,#-.frame_
	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
	pushraster	#$770
*-------------------------------------------------------*
*	load C args to local frame
*-------------------------------------------------------*
	move.l		.pctx(a6),a4
	move.l		a4,.sp_dctx(sp)
	lea		dctx_guardwindow(a4),a5
	move.l		a5,.sp_guardwin(sp)
	lea		dctx_scissorwindow(a4),a5
	move.l		a5,.sp_scissorwin(sp)
	;
	move.l		dctx_framebuffer(a4),.sp_framebuf(sp)
	move.l		dctx_lineidx(a4),.sp_linetable(sp)
	move.w		dctx_snapadjx(a4),.sp_snapx(sp)
	move.w		dctx_snapadjy(a4),.sp_snapy(sp)
	move.w		_g_linebytes,.sp_linebytes(sp)
;	move.w		dctx_guardx(a4),.sp_guardx(sp)
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

;	move.l		dctx_prestorestate(a4),a5
;	move.l		rstr_ptide(a5),a5
;	move.l		a5,usp

*-------------------------------------------------------*
	
	; important to begin with invalid drawtype
	; so the first drawtype will call init first

	move.w		#-1,.sp_drawtype(sp)
	
	bra		.STE_EntityOccludeVisibleYM_NONE_done


*=======================================================*
*	dedicated iterator for [drawtype]=EMXSPR
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_EMXSPR_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityOccludeVisibleYM_evaluate
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_EMXSPR_setup:
.STE_EntityOccludeVisibleYM_EMXSPR_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; decrement 'hitflash' counter on this object

;	move.w		ent_draw_hitflash(a0),d7	; $0000 / $FFxx
;	neg.b		d7
;	subx.b		d7,d7				; $0000 / $FFFF
;	add.b		d7,ent_draw_hitflash+1(a0)

	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0

	;
	
	moveq		#1,d3
	and.b		sps_flags(a0),d3
	addq		#2,d3
	lsl.w		d3,d0
;	add.w		d0,d0				; todo: game layer can prescale this
;	add.w		d0,d0
	move.l		sps_frameindex(a0,d0.w),a0	; access sprite frame data
			
	; frame fields

	move.w		(a0)+,d3			; w
	move.w		(a0)+,d4			; h
	add.w		(a0)+,d1			; +xo
	add.w		(a0)+,d2			; +yo

	move.l		.sp_guardwin(sp),a2
	guard_reject	a2,d1,d3,d0,d6,.STE_EntityOccludeVisibleYM_EMXSPR_no
	guard_reject	a2,d2,d4,d0,d6,.STE_EntityOccludeVisibleYM_EMXSPR_no

	move.w		(a0),d0
	beq.s		.STE_EntityOccludeVisibleYM_EMXSPR_no
			
	setraster	#$ff0
	
	lea		(a0,d0.w),a0		
	jsr		_OCM_occlusion_sub_O

	setraster	#$770

.STE_EntityOccludeVisibleYM_EMXSPR_no:
	
	;
	
	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_EMXSPR_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityOccludeVisibleYM_EMXSPR_next
	bra		.STE_EntityOccludeVisibleYM_complete
*-------------------------------------------------------*


*=======================================================*
*	dedicated iterator for [drawtype]=EMSPR
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_EMSPR_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityOccludeVisibleYM_evaluate
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_EMSPR_setup:
.STE_EntityOccludeVisibleYM_EMSPR_go:
*-------------------------------------------------------*

	; access coordinates

	move.w		ent_frx(a0),d1
;	move.w		ent_fry(a0),d2			; we already fetched this in preamble

	; compensate for loopback buffer

	sub.w		.sp_snapx(sp),d1		; not really needed unless LOOPBACK used on HSCROLL - kinda pointless
	sub.w		.sp_snapy(sp),d2		; must be done if LOOPBACK used for VSCROLL - very likely

	; access sprite frame

	move.w		ent_frame(a0),d0

	; decrement 'hitflash' counter on this object

;	move.w		ent_draw_hitflash(a0),d7	; $0000 / $FFxx
;	neg.b		d7
;	subx.b		d7,d7				; $0000 / $FFFF
;	add.b		d7,ent_draw_hitflash+1(a0)

	; asset/framebuffer

	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
	move.l		a0,g_dbg_curr_entity
	.endif

	move.l		a0,.sp_savea0(sp)
	move.l		ent_passet(a0),a0

	;
	
	moveq		#1,d3
	and.b		sps_flags(a0),d3
	addq		#2,d3
	lsl.w		d3,d0
;	add.w		d0,d0				; todo: game layer can prescale this
;	add.w		d0,d0
	move.l		sps_frameindex(a0,d0.w),a0	; access sprite frame data
			
	; frame fields

	move.w		(a0)+,d3			; w
	move.w		(a0)+,d4			; h
	add.w		(a0)+,d1			; +xo
	add.w		(a0)+,d2			; +yo

	move.l		.sp_guardwin(sp),a2
	guard_reject	a2,d1,d3,d0,d6,.STE_EntityOccludeVisibleYM_EMSPR_no
	guard_reject	a2,d2,d4,d0,d6,.STE_EntityOccludeVisibleYM_EMSPR_no

	move.w		(a0),d0
	beq.s		.STE_EntityOccludeVisibleYM_EMSPR_no
	
	setraster	#$ff0
	
	lea		(a0,d0.w),a0		
	jsr		_OCM_occlusion_sub_O

	setraster	#$770

.STE_EntityOccludeVisibleYM_EMSPR_no:	
	;

	move.l		.sp_savea0(sp),a0

*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_EMSPR_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt		.STE_EntityOccludeVisibleYM_EMSPR_next
	bra		.STE_EntityOccludeVisibleYM_complete
*-------------------------------------------------------*

*=======================================================*
*	[drawtype] has changed - perform path switch
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_evaluate:
*-------------------------------------------------------*
	move.w		d0,.sp_drawtype(sp)
	add.w		d0,d0
	add.w		d0,d0
	jmp		.types(pc,d0.w)
.types:	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; none
	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; IMSPR/SPRITE
	bra.w		.STE_EntityOccludeVisibleYM_EMSPR_setup
	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; EMSPRQ
	bra.w		.STE_EntityOccludeVisibleYM_EMXSPR_setup
	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; EMXSPRQ
	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; EMXSPRUR
	bra.w		.STE_EntityOccludeVisibleYM_EMSPR_setup	; EMHSPR
	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; SLAB
	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; CUSTOM
	bra.w		.STE_EntityOccludeVisibleYM_NONE_setup	; RFILL

*=======================================================*
*	dedicated iterator for [drawtype]=NONE
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_NONE_next:
*-------------------------------------------------------*
	move.w		ent_drawtype(a0),d0
	cmp.w		.sp_drawtype(sp),d0
	bne		.STE_EntityOccludeVisibleYM_evaluate
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_NONE_setup:
.STE_EntityOccludeVisibleYM_NONE_go:
*-------------------------------------------------------*
	; nothing todo here - skip!
*-------------------------------------------------------*
.STE_EntityOccludeVisibleYM_NONE_done:
*-------------------------------------------------------*
	move.l		ent_pnext_y(a0),a0		; next visible entity
	move.w		ent_fry(a0),d2
	cmp.w		.sp_pxymax_(sp),d2
	blt.s		.STE_EntityOccludeVisibleYM_NONE_next
	bra		.STE_EntityOccludeVisibleYM_complete
	
		
*=======================================================*
.STE_EntityOccludeVisibleYM_complete:
*-------------------------------------------------------*

;	move.l		usp,a5
;	move.l		a5,_drawcontext+dctx_clearstack_tide

*-------------------------------------------------------*
	popraster
*-------------------------------------------------------*

	lea		.sp_frame_(sp),sp
	move.l		(sp)+,a6
	
;	move.l		.pctx(a6),a4
;	move.l		dctx_prestorestate(a4),a4
;	move.l		usp,a5
;	move.l		a5,rstr_ptide(a4)
	
	unlk		a6
	popctxr		a2
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts
