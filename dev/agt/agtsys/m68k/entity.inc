*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Entity structurea
*-------------------------------------------------------*

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*


*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
ent_pnext_tick:		ds.l	1
ent_pprev_tick:		ds.l	1
			.if	^^defined AGT_CONFIG_WORLD_XMAJOR
ent_pnext_x:		ds.l	1
ent_pprev_x:		ds.l	1
			.endif
			.if	^^defined AGT_CONFIG_WORLD_YMAJOR
ent_pnext_y:		ds.l	1
ent_pprev_y:		ds.l	1
			.endif
ent_pparent:		ds.l	1
ent_ptrack:		ds.l	1
ent_layer_:		ds.l	1
ent_fncustomdraw:	ds.l	1
*-------------------------------------------------------*
ent_id:			ds.w	1
ent_type:		ds.w	1
ent_rev_self:		ds.w	1
ent_rev_parent:		ds.w	1
ent_rev_track:		ds.w	1
ent_rev_user:		ds.w	1
*-------------------------------------------------------*
ent_fwx:		ds.l	1
ent_fwy:		ds.l	1
ent_fwz:		ds.l	1
ent_fvz:		ds.l	1
*-------------------------------------------------------*
ent_fntick:		ds.l	1
ent_fncollide:		ds.l	1
ent_passet:		ds.l	1
ent_phiddenasset:	ds.l	1
ent_frx:		ds.l	1
ent_fry:		ds.l	1
ent_fvx:		ds.l	1
ent_fvy:		ds.l	1
*-------------------------------------------------------*
ent_sx:			ds.w	1
ent_sy:			ds.w	1
ent_ox:			ds.w	1
ent_oy:			ds.w	1
*-------------------------------------------------------*
ent_tick_priority:	ds.w	1
ent_draw_hitflash:	ds.w	1
ent_health:		ds.w	1
ent_damage:		ds.w	1
*-------------------------------------------------------*
ent_frame:		ds.w	1
ent_counter:		ds.w	1
ent_drawtype:		ds.w	1
ent_drawlayer:		ds.w	1
*-------------------------------------------------------*
ent_self:		ds.w	1
ent_interactswith:	ds.w	1
*-------------------------------------------------------*
ent_header_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*

EntityDraw_NONE		=	0
EntityDraw_SPRITE	=	1
EntityDraw_EMSPR	=	2
EntityDraw_EMSPRQ	=	3
EntityDraw_EMXSPR	=	4
EntityDraw_EMXSPRQ	=	5
EntityDraw_EMXSPRUR	=	6
EntityDraw_EMHSPR	=	7
EntityDraw_SLAB		=	8
EntityDraw_CUSTOM	=	9
EntityDraw_RFILL	=	10
