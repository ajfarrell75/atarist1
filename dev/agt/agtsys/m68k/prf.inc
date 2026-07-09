*=======================================================*
*	builtin profiler contexts
*=======================================================*

prf_ctx			=	$40
ctxstack		=	$44
prf_context_max		=	32
prf_context_depth	=	32

*-------------------------------------------------------*

; interrupt-safe
.macro	pushctx		name
	.if		^^defined AGT_CONFIG_PROFILER
	move.l		a0,-(sp)
	ori.w		#$0700,sr
	move.l		ctxstack.w,a0
	move.w		prf_ctx.w,-(a0)
	.if		^^defined prf_\{name}
	move.w		#prf_\{name},prf_ctx.w
	.endif	
	move.l		a0,ctxstack.w
	andi.w		#$fbff,sr
	move.l		(sp)+,a0
	.endif
	.endm

; interrupt-safe
.macro	popctx
	.if		^^defined AGT_CONFIG_PROFILER
	move.l		a0,-(sp)
	ori.w		#$0700,sr
	move.l		ctxstack.w,a0	
	move.w		(a0)+,prf_ctx.w
	move.l		a0,ctxstack.w
	andi.w		#$fbff,sr
	move.l		(sp)+,a0
	.endif
	.endm
	
; interrupt-safe
.macro	pushctxr	name,treg
	.if		^^defined AGT_CONFIG_PROFILER
	ori.w		#$0700,sr
	move.l		ctxstack.w,\treg
	move.w		prf_ctx.w,-(\treg)
	.if		^^defined prf_\{name}
	move.w		#prf_\{name},prf_ctx.w
	.endif
	move.l		\treg,ctxstack.w
	andi.w		#$fbff,sr
	.endif
	.endm

; interrupt-safe
.macro	popctxr		treg
	.if		^^defined AGT_CONFIG_PROFILER
	ori.w		#$0700,sr
	move.l		ctxstack.w,\treg
	move.w		(\treg)+,prf_ctx.w
	move.l		\treg,ctxstack.w	
	andi.w		#$fbff,sr
	.endif
	.endm
	
; interrupt-safe	
.macro	setctx		name
	.if		^^defined AGT_CONFIG_PROFILER
	.if		^^defined prf_\{name}
	move.w		#prf_\{name},prf_ctx.w
	.endif
	.endif
	.endm
	
*-------------------------------------------------------*

; these definitions are only required for asm-side definitions used by lowest level components
; most of the profiling definitions are on the C side

*-------------------------------------------------------*

		.abs
e_prf_text:	ds.l	1
e_prf_id:	ds.l	1
e_prf_size_:
		.text
		
.macro		prf_context_def	str,name
;		.text
prf_\{name}	set	prf_next_
prf_next_	set	prf_\{name}+1
;		dc.l	prf_\{name}_t,prf_\{name}
;		.data
;prf_\{name}_t:	dc.b	\str,0
		.endm	
	
*-------------------------------------------------------*


prf_next_			set	0
;prf_context_refs:

	prf_context_def		"undefined...",undefined
	prf_context_def		"-profiling- ",profiling
	prf_context_def		"main:loop   ",mainloop
	prf_context_def		"main:idle   ",idle
	prf_context_def		"arena:misc  ",arena_misc
	prf_context_def		"arena:bgrst ",arena_bgrestore
	prf_context_def		"arena:fill  ",arena_fill
	prf_context_def		"ent:tick    ",ent_tick
	prf_context_def		"ent:spatial ",ent_spatial
	prf_context_def		"ent:interact",ent_interact
	prf_context_def		"ent:links   ",ent_links
	prf_context_def		"ent:draw    ",ent_draw

prf_context_num			set	prf_next_

*-------------------------------------------------------*

	.data
	.even
	.text
	.even
	
*-------------------------------------------------------*
	