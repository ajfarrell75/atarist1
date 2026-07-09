
*-------------------------------------------------------*
		
	.globl		_AGT_Sys_ProfilerInit
	.globl		_AGT_Sys_ProfilerReset
	.globl		_AGT_Sys_ProfilerDoReport
	.globl		_AGT_Sys_ConsoleRefresh

	.if		^^defined AGT_CONFIG_PROFILER

	.globl		_agtprintf
	.globl		_agtprintf_coded
	.globl		_gp_prf_context_refs
	.globl		_g_prf_context_num
	.globl		_g_lock_consolerefresh
	.globl		_g_concode
	
	include		"prf.inc"

	.endif

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
		
*-------------------------------------------------------*
_AGT_Sys_ProfilerReset:
*-------------------------------------------------------*
	.if		^^defined AGT_CONFIG_PROFILER
	movem.l		d0-a6,-(sp)
	move.w		#$2700,sr

	clr.w		prf_ctx.w

	move.w		_g_prf_context_num+2,d7
	ble.s		.none
	subq.w		#1,d7

	lea		prf_contexts,a0
.rst:	clr.l		(a0)+
	dbra		d7,.rst
	
.none:	move.l		#1,_tm_clock	
	
	setctx		undefined

	move.w		#$2300,sr
	movem.l		(sp)+,d0-a6
	.endif
	rts

*-------------------------------------------------------*
_AGT_Sys_ProfilerInit:
*-------------------------------------------------------*
	.if		^^defined AGT_CONFIG_PROFILER
	movem.l		d0-a6,-(sp)
	move.w		#$2700,sr

	bsr		_AGT_Sys_ProfilerReset
	
	move.l		#ctxstack_data_end,ctxstack.w

	move.w		#$2300,sr
	movem.l		(sp)+,d0-a6
	.endif
	rts
	
amort:	dc.l		0

*-------------------------------------------------------*
_AGT_Sys_ProfilerDoReport:
*-------------------------------------------------------*
	.if		^^defined AGT_CONFIG_PROFILER
	movem.l		d0-a6,-(sp)

	pushctxr	profiling,a0
	
	move.b		#2,_g_concode
	
	addq.w		#1,_g_lock_consolerefresh
	
	move.w		(((15*4)+4)+2)(sp),print_line_count_
			
	lea		prf_contexts,a0
	move.l		prf_profiling*4(a0),amort	; incidence [a]
	
;	pea		txtcrlf
;	jsr		_agtprintf
;	addq.l		#4,sp	
	pea		txtreport
	jsr		_agtprintf
	addq.l		#4,sp
;	pea		txtcrlf
;	jsr		_agtprintf
;	addq.l		#4,sp	
;	lea		4*3(sp),sp

	clr.b		first_
		
	move.w		_g_prf_context_num+2,d7
	ble		.none
	subq.w		#1,d7

;	move.w		#6,print_line_count_

.prf:
	move.w		d7,-(sp)

	lea		stringspace,a0
	clr.b		(a0)
	move.l		a0,numtext_ptr
	
	tst.w		print_line_count_
	ble.s		.n2
	
	lea		txtcrlf,a0
	bsr		push_string
	lea		txtpercent,a0
	bsr		push_string
.n2:
	
;	lea		prf_context_refs,a6
	move.l		_gp_prf_context_refs,a6
	lsl.w		#3,d7			; e_prf_size_
	add.w		d7,a6

	tst.w		print_line_count_
	ble.s		.ignore

	move.l		e_prf_id(a6),d0		; context index
	cmp.w		#prf_profiling,d0
	beq		.ignore
	
	lea		prf_contexts,a0
	add.w		d0,d0
	add.w		d0,d0
	move.l		(a0,d0.w),d0		; AAAA:aaaa
	swap		d0
	clr.w		d0
	move.l		_tm_clock,d1
	sub.l		amort,d1

.try:	move.l		d1,d2
	and.l		#$ffff0000,d2
	beq.s		.nnrm
	lsr.l		d0
	lsr.l		d1
	bra.s		.try

.nnrm:
	
	divu.w		d1,d0
	mulu.w		#100,d0
	
	bsr		pushreal2
		
;	lea		txtspace,a0
	lea		txtalign,a0
	bsr		push_string

	move.l		e_prf_text(a6),a0	; context name
	bsr		push_string

	move.l		numtext_ptr,a0

	pea		(a0)
	jsr		_agtprintf_coded
	addq.l		#4,sp

.ignore:	
	move.w		(sp)+,d7

	tas.b		first_				; can't sort last entry (first iter)
	beq.s		.nswp

	lea		prf_contexts,a0
	move.l		e_prf_id(a6),d0			; context index [a]
	add.w		d0,d0
	add.w		d0,d0
	move.l		(a0,d0.w),d1			; incidence [a]
	move.l		e_prf_id+e_prf_size_(a6),d0	; context index [b]
	add.w		d0,d0
	add.w		d0,d0
	move.l		(a0,d0.w),d2			; incidence [b]
	cmp.l		d1,d2
	bge.s		.nswp				; sort references according to incidence

	move.l		e_prf_text(a6),d0
	move.l		e_prf_text+e_prf_size_(a6),e_prf_text(a6)
	move.l		d0,e_prf_text+e_prf_size_(a6)

	move.l		e_prf_id(a6),d0
	move.l		e_prf_id+e_prf_size_(a6),e_prf_id(a6)
	move.l		d0,e_prf_id+e_prf_size_(a6)
.nswp:
	subq.w		#1,print_line_count_
	
	dbra		d7,.prf
	
.none:
	subq.w		#1,_g_lock_consolerefresh

	jsr		_AGT_Sys_ConsoleRefresh

	popctxr		a0
	
	movem.l		(sp)+,d0-a6
	.endif
	rts


	.if		^^defined AGT_CONFIG_PROFILER

*-------------------------------------------------------*
push_string:
*-------------------------------------------------------*
	move.l		numtext_ptr,a1
	moveq		#-1,d0
.lp:	addq.l		#1,d0
	tst.b		(a0)+
	bne.s		.lp
	subq.l		#1,a0
	bra.s		.go
.cop:	move.b		-(a0),-(a1)
.go:	dbra		d0,.cop
	move.l		a1,numtext_ptr
	rts
	
*-------------------------------------------------------*
pushreal4:
*-------------------------------------------------------*
;	d0.l 	= 	16:16 number
	move.l		d0,-(sp)
	mulu.w		#10000,d0
	moveq		#4,d7
	bra.s		pushreal_
*-------------------------------------------------------*
pushreal3:
*-------------------------------------------------------*
;	d0.l 	= 	16:16 number
	move.l		d0,-(sp)
	mulu.w		#1000,d0
	moveq		#3,d7
	bra.s		pushreal_
*-------------------------------------------------------*
pushreal2:
*-------------------------------------------------------*
;	d0.l 	= 	16:16 number
	move.l		d0,-(sp)
	mulu.w		#100,d0
	moveq		#2,d7
	bra.s		pushreal_
*-------------------------------------------------------*
pushreal1:
*-------------------------------------------------------*
;	d0.l 	= 	16:16 number
	move.l		d0,-(sp)
	mulu.w		#100,d0
	moveq		#1,d7
;	bra.s		pushreal_
	
pushreal_:
	;
	; emit fraction
	;
	swap		d0
	ext.l		d0
	bsr		push_decimal_lz
	lea		txtpoint,a0
	bsr		push_string
	move.l		(sp)+,d0
	;
	; emit integer
	;
	swap		d0
	ext.l		d0
	moveq		#1,d7
	bra		push_decimal_lz
	
*-------------------------------------------------------*
push_decimal_lz_signed:
*-------------------------------------------------------*
	tst.l		d0
	bpl.s		push_decimal_lz
	neg.l		d0
	bsr		push_decimal_lz
	move.b		#'-',-(a0)
	move.l		a0,numtext_ptr
	rts
	
*-------------------------------------------------------*
push_decimal_lz:
*-------------------------------------------------------*
;	d0.w 	= 	number
	move.l		numtext_ptr,a0
	move.l		a0,a1
	moveq		#'0',d3
	move.l		d3,d1
	moveq		#10,d4
	tst.l		d0
	beq.s		.zero
.loop:
;	moveq		#0,d2
;	divu.l		d4,d2:d0
	and.l		#$0000ffff,d0
	divu.w		d4,d0
	move.l		d0,d1
	swap		d1
	add.w		d3,d1
.zero:	move.b		d1,-(a0)
	subq.l		#1,d7
	bgt.s		.loop
	tst.w		d0
	bne.s		.loop
	move.l		a0,numtext_ptr
	rts
	
profiler_print:
	rts
	
*-------------------------------------------------------*
				.data
*-------------------------------------------------------*
	
numtext_ptr:			dc.l	0
prf_depth:			dc.l	0

ctxstack_data:			dcb.w	prf_context_depth,0
ctxstack_data_end:

txtcrlf:			dc.b	13,10,0	
txtreport:			dc.b	10,"Profiler:",10,0	
txtspace:			dc.b	" ",0	
txtalign:			dc.b	2,20,0
txtpercent:			dc.b	"%",0
txtpoint:			dc.b	".",0
				even
				
*-------------------------------------------------------*				
				.bss
*-------------------------------------------------------*				
				
				ds.b	256
stringspace:			ds.b	1
				even
print_line_count_:		ds.w	1
first_:				ds.b	1
				even
				
*-------------------------------------------------------*				
				.text
*-------------------------------------------------------*				
				
	.endif
	