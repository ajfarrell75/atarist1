*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Drawcontext structure
*-------------------------------------------------------*

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*

*-------------------------------------------------------*
*	draw context
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
dctx_framebuffer:	ds.l	1
dctx_lineidx:		ds.l	1
dctx_linemod:		ds.l	1
*-------------------------------------------------------*
dctx_vpagebytes:	ds.l	1
dctx_vpageh:		ds.w	1
;dctx_viewy:		ds.w	1
dctx_snapadjx:		ds.w	1
dctx_snapadjy:		ds.w	1
dctx_vpagex:		ds.w	1
dctx_vpagey:		ds.w	1
dctx_guardx:		ds.w	1
dctx_guardy:		ds.w	1
*-------------------------------------------------------*
dctx_prestorestate:	ds.l	1
dctx_pocm_t:		ds.l	1
dctx_pocm_o:		ds.l	1
dctx_ocm_stride:	ds.w	1
*-------------------------------------------------------*
dctx_field:		ds.w	1
;dctx_mfield:		ds.w	1
*-------------------------------------------------------*
dctx_guardwindow:
dctx_guardwindow_x1:	ds.w	1
dctx_guardwindow_xs:	ds.w	1
dctx_guardwindow_y1:	ds.w	1
dctx_guardwindow_ys:	ds.w	1
dctx_guardwindow_x2:	ds.w	1
dctx_guardwindow_y2:	ds.w	1
*-------------------------------------------------------*
dctx_scissorwindow:
dctx_scissorwin_x1p:	ds.w	1
dctx_scissorwin_xs:	ds.w	1
dctx_scissorwin_y1:	ds.w	1
dctx_scissorwin_ys:	ds.w	1
dctx_scissorwin_x2p:	ds.w	1
dctx_scissorwin_y2:	ds.w	1
*-------------------------------------------------------*
dctx_size:		
*-------------------------------------------------------*

*-------------------------------------------------------*
*	bgrestore context
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
rstr_pstack:		ds.l	1
rstr_ptide:		ds.l	1
*-------------------------------------------------------*


*-------------------------------------------------------*
*	bgrestore frame
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
bgrframe_dsty:		ds.w	1	; in pixels
bgrframe_dstxoff:	ds.w	1	; in bytes
bgrframe_dstaddr:	ds.l	1	; includes dstxoff
bgrframe_dstskip:	ds.w	1
bgrframe_ww2:		ds.w	1
bgrframe_lines:		ds.w	1
bgrframe_storage:	; pixel storage, if save-restore mode
bgrframe_backlink:	ds.l	1
bgrframe_size:		; frame size, if page-restore mode
*-------------------------------------------------------*


*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
clip_win:	
;
clip_win_x:		ds.w	1
clip_win_xs:		ds.w	1
clip_win_y:		ds.w	1
clip_win_ys:		ds.w	1
;
clip_win_x2:		ds.w	1
clip_win_y2:		ds.w	1
;
clip_win_size_:
*-------------------------------------------------------*

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*




*-------------------------------------------------------*
*	common var stackframe used by drawing routines
*-------------------------------------------------------*
.macro	emitframe_draw_vars
*-------------------------------------------------------*
*	r/w fields used by sprite routines
*-------------------------------------------------------*
.sp_skew:		ds.w	1	; fragile optimization: don't move this one
.sp_maskwidth:		ds.w	1	; width of EM? mask, in words
.sp_yoff:		ds.w	1	; cached yoffset
.sp_yc:			ds.w	1	; cached yclip (top)
.sp_yc2:		ds.w	1	; cached yclip (bot)
.sp_yrem:		ds.w	1	; y remainder, sprite clearing rectangle split
.sp_mfield:		ds.w	1	; colour field index offset (dualfield sprites)
.sp_cfield4:		ds.w	1	; mfield<<2
.sp_xadj:		ds.w	1	; guardx adjust, to keep sls/slr xcoords +ve
.sp_save_usp:		ds.l	1	; temp store
.sp_nextcmp:		ds.l	1	; next 32w component in a multi-component EMS
.sp_x:			ds.w	1	; cached x,y coords
.sp_y:			ds.w	1	;
.sp_pad_:		ds.w	1	; <long align>
*-------------------------------------------------------*
*	r/o fields, frequently used, shadowed for speed
*-------------------------------------------------------*
.sp_framebuf:		ds.l	1	; active framebuffer base @ playfield (0,0) - ignoring guard region
.sp_guardwin:		ds.l	1	; guardband reject window
.sp_scissorwin:		ds.l	1	; scissor/clip window
.sp_linetable:		ds.l	1	; cached lineoffset index
.sp_snapx:		ds.w	1	; pf vpage snapping offsets
.sp_snapy:		ds.w	1	;
*-------------------------------------------------------*
*	r/w fields, used by entity iterator
*-------------------------------------------------------*
.sp_savea0:		ds.l	1	; scratch
.sp_drawtype:		ds.l	1	; active drawtype
.sp_asset:		ds.l	1	; asset data
*-------------------------------------------------------*
*	const fields, set before entity iteration 
*-------------------------------------------------------*
.sp_pxymax_:		ds.w	1	; viewport iteration x or y limit
.sp_linebytes:		ds.w	1	; cached scanline stride in bytes
.sp_layers:		ds.l	1	; layer array
.sp_dctx:		ds.l	1	; drawcontext
*-------------------------------------------------------*
	.endm
*-------------------------------------------------------*


	