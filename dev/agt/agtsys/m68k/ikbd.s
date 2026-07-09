*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	IKBD input services
*-------------------------------------------------------*

	.globl		_AGT_InstallInputService
	.globl		_AGT_RemoveInputService
	
	.globl		_ST_FlushInput			; empty ikbd hw buffer
	.globl		_ST_EnableInput			; enable just keyboard and mouse
	.globl		_ST_DisableInput		; disable all input
	.globl		_ST_ResumeInput			; restore ikbd, joy and mouse reporting
	.globl		_ST_ResetInput			; restore to power-up state

	.globl		_key_states
	.globl		_debounced_key_states
	.globl		_debounced_key_presses
	.globl		_debounced_key_releases

	.globl		_key_eventhead
	.globl		_key_eventtail
	.globl		_key_eventring
	.globl		_msclick_eventhead
	.globl		_msclick_eventtail
	.globl		_msclick_eventring
	.globl		_joy0_eventhead
	.globl		_joy0_eventtail
	.globl		_joy0_eventring
	.globl		_joy1_eventhead
	.globl		_joy1_eventtail
	.globl		_joy1_eventring

	.globl		_joy0
	.globl		_joy1
	.globl		_buttons
	.globl		_mouse_dx
	.globl		_mouse_dy
	
	.globl		_max_console
	.globl		_min_pconsole
	.globl		_console_maxbase
	.globl		_console_minbase


*-------------------------------------------------------*
	
	include		"config.inc"

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
		
		
ikbd		=	$118		; keyboard vector

key_ctl		=	$fffffc00	; keyboard control register
key_dat		=	$fffffc02	; keyboard data register
;isrb		=	$fffffa11
;imrb		=	$fffffa15

timerb		=	$120
timerc		=	$114

tbcr		=	$fffffa1b
cdcr		=	$fffffa1d
tbdr		=	$fffffa21
tcdr		=	$fffffa23
iera		=	$fffffa07
ierb		=	$fffffa09
imra		=	$fffffa13
imrb		=	$fffffa15
isra		=	$fffffa0f
isrb		=	$fffffa11

imb_ikbd	=	$40

*-------------------------------------------------------*
*	Install IKBD handling service
*-------------------------------------------------------*
_AGT_InstallInputService:
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
*-------------------------------------------------------*
	tst.b		ikbd_on
	bne		.done
	
	lea		_key_states,a0
	lea		_debounced_key_states,a1
	moveq		#128/4-1,d7
.clear_states:
	clr.l		(a0)+
	clr.l		(a1)+
	clr.l		(a1)+
	dbra		d7,.clear_states

	clr.w		_key_eventhead
	clr.w		_msclick_eventhead
	clr.w		_joy0_eventhead
	clr.w		_joy1_eventhead

	clr.w		_key_eventtail
	clr.w		_msclick_eventtail
	clr.w		_joy0_eventtail
	clr.w		_joy1_eventtail

	move.w		#34,-(a7)
	trap		#14
	addq.w		#2,a7
	move.l		d0,a0
	move.l		28(a0),sys_midi

	bsr		_ST_FlushInput
	
	ori.w		#$0700,sr
	
	move.l		ikbd.w,sys_ikbd
	move.l		#ikbd_handler,ikbd.w
		
	bset.b		#6,ierb.w
	bset.b		#6,imrb.w

	andi.w		#$fbff,sr

;	bsr		_ST_ResetInput
	bsr		_ST_EnableInput
	
	st		ikbd_on
*-------------------------------------------------------*
.done:	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts
	
*-------------------------------------------------------*
*	Remove IKBD handling service
*-------------------------------------------------------*
_AGT_RemoveInputService:
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
*-------------------------------------------------------*
	tst.b		ikbd_on
	beq.s		.done

	bsr		_ST_DisableInput

	ori.w		#$0700,sr

	move.l		sys_ikbd,ikbd.w

	bclr.b		#6,ierb.w
	bclr.b		#6,imrb.w
	
	andi.w		#$fbff,sr	

	sf		ikbd_on
	
*-------------------------------------------------------*
.done:	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts


*-------------------------------------------------------*
_ST_FlushInput:
*-------------------------------------------------------*
	lea		key_ctl.w,a0
	bra.s		.read
.next:	btst		#0,(a0)
	bne.s		.read
	subq.w		#1,d0
	bne.s		.next
	bra.s		.end
.read:	move.b		2(a0),d0
	move.w		#512,d0
	bra.s		.next
.end:	rts

*-------------------------------------------------------*
_ST_EnableInput:
*-------------------------------------------------------*
	bsr		_ST_FlushInput
	lea		enable_ikbd_seq(pc),a0
.next:	move.b		(a0)+,d0
	bmi.s		.end
	bsr		write_acia
	bra.s		.next
.end:	bsr		_ST_FlushInput
	rts

*-------------------------------------------------------*
_ST_DisableInput:
*-------------------------------------------------------*
	bsr		_ST_FlushInput
	lea		disable_ikbd_seq(pc),a0
.next:	move.b		(a0)+,d0
	bmi.s		.end
	bsr		write_acia
	bra.s		.next
.end:	bsr		_ST_FlushInput
	rts

*-------------------------------------------------------*
_ST_ResetInput:
*-------------------------------------------------------*
	bsr		_ST_FlushInput
	lea		reset_ikbd_seq(pc),a0
.next:	move.b		(a0)+,d0
	bmi.s		.end
	bsr		write_acia
	bra.s		.next
.end:	; wait 300ms+ after reset
	moveq		#64,d0
	add.w		$4bc.w,d0
.wt:	cmp.w		$4bc.w,d0
	bpl.s		.wt
	rts

*-------------------------------------------------------*
_ST_ResumeInput:
*-------------------------------------------------------*
	bsr		_ST_FlushInput
	lea		resume_ikbd_seq(pc),a0
.next:	move.b		(a0)+,d0
	bmi.s		.end
	bsr		write_acia
	bra.s		.next
.end:	bsr		_ST_FlushInput
	rts

*-------------------------------------------------------*
write_acia:	
*-------------------------------------------------------*
.wait:	btst		#1,key_ctl.w
	beq.s		.wait
	move.b		d0,key_dat.w
	rts

*-------------------------------------------------------*

disable_ikbd_seq:	dc.b	$13,$1a,$12,-1			; reporting off, joystick events off, mouse events off 
enable_ikbd_seq:	dc.b	$11,$14,$12,-1			; resume reporting, joystick events on, mouse events off
resume_ikbd_seq:	dc.b	$11,$14,$08,-1			; resume reporting, joystick events on, mouse events on
reset_ikbd_seq:		dc.b	$80,$01,-1
			even

	
*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
ikbd_error:
*-------------------------------------------------------*
	move.l		#i_primary,ikbd.w
*-------------------------------------------------------*	
.iklp:	;move.w		#$00f,$ffff8240.w
	;move.l		#$000000ff,$ffff9800.w
	;move.w		#$000,$ffff8240.w
	;move.l		#$00000000,$ffff9800.w
*-------------------------------------------------------*
	btst		#0,key_ctl.w
	beq.s		.done
	tst.b		key_dat.w
	bra.s		.iklp
*-------------------------------------------------------*
.done:	rts


*-------------------------------------------------------*
baaad:
*-------------------------------------------------------*
;	not.l		$ffff9800.w
	not.w		$ffff8240.w
	bra.s		baaad
		
*-------------------------------------------------------*
*	IKBD packet handler
*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
ikbd_handler:
*-------------------------------------------------------*

*-------------------------------------------------------*
i_primary:
*-------------------------------------------------------*
	move.w		d0,-(sp)
	move.w		d1,-(sp)
*-------------------------------------------------------*
	clr.w		d0
	move.b		key_dat.w,d0
	move.w		d0,d1
	lsl.w		#2,d1
	jmp		.table_start(pc,d1.w)

*-------------------------------------------------------*
.table_start:
*-------------------------------------------------------*
	.rept		2
*-------------------------------------------------------*
	bra.w		.unmapped	;00
	bra.w		.keys		;01	ESC
	bra.w		.keys		;02	1
	bra.w		.keys		;03	2
	bra.w		.keys		;04	3
	bra.w		.keys		;05	4
	bra.w		.keys		;06	5
	bra.w		.keys		;07	6
	bra.w		.keys		;08	7
	bra.w		.keys		;09	8
	bra.w		.keys		;0a	9
	bra.w		.keys		;0b	0
	bra.w		.keys		;0c	-
	bra.w		.keys		;0d	+
	bra.w		.keys		;0e	BS

	.if		(use_debugconsole)
	bra.w		.console_key
	.else
	bra.w		.keys		;0f	TAB
	.endif
	
	bra.w		.keys		;10	Q
	bra.w		.keys		;11	W
	bra.w		.keys		;12	E
	bra.w		.keys		;13	R
	bra.w		.keys		;14	T
	bra.w		.keys		;15	Y
	bra.w		.keys		;16	U
	bra.w		.keys		;17	I
	bra.w		.keys		;18	O
	bra.w		.keys		;19	P
	bra.w		.keys		;1a	[
	bra.w		.keys		;1b	]
	bra.w		.keys		;1c	RETURN
	bra.w		.keys		;1d	CTRL
	bra.w		.keys		;1e	A
	bra.w		.keys		;1f	S
	bra.w		.keys		;20	D
	bra.w		.keys		;21	F
	bra.w		.keys		;22	G
	bra.w		.keys		;23	H
	bra.w		.keys		;24	J
	bra.w		.keys		;25	K
	bra.w		.keys		;26	L
	bra.w		.keys		;27	;
	bra.w		.keys		;28	'
	bra.w		.keys		;29	`
	bra.w		.keys		;2a	LSHFT
	bra.w		.keys		;2b	#
	bra.w		.keys		;2c	Z
	bra.w		.keys		;2d	X
	bra.w		.keys		;2e	C
	bra.w		.keys		;2f	V
	bra.w		.keys		;30	B
	bra.w		.keys		;31	N
	bra.w		.keys		;32	M
	bra.w		.keys		;33	,
	bra.w		.keys		;34	.
	bra.w		.keys		;35	/
	bra.w		.keys		;36	RSHFT
	bra.w		.unmapped	;37
	bra.w		.keys		;38	ALT
	bra.w		.keys		;39	SPACE
	bra.w		.keys		;3a	CAPS
	.if		use_unsafe_function_keys
	bra.w		.keys		;3b		(x)F1
	bra.w		.keys		;3c		(x)F2
	bra.w		.keys		;3d		(x)F3
	.else
	bra.w		.unmapped	;3b		(x)F1
	bra.w		.unmapped	;3c		(x)F2
	bra.w		.unmapped	;3d		(x)F3
	.endif
	bra.w		.keys		;3e	F4
	bra.w		.keys		;3f	F5
	bra.w		.keys		;40	F6
	bra.w		.keys		;41	F7
	bra.w		.keys		;42	F8
	bra.w		.keys		;43	F9
	bra.w		.keys		;44	F10
	bra.w		.unmapped	;45
	bra.w		.unmapped	;46
	bra.w		.keys		;47	CLR
	bra.w		.keys		;48	UP
	bra.w		.unmapped	;49
	bra.w		.keys		;4a	[-]
	bra.w		.keys		;4b	LEFT
	bra.w		.unmapped	;4c	
	bra.w		.keys		;4d	RIGHT
	bra.w		.keys		;4e	[+]
	bra.w		.unmapped	;4f
	bra.w		.keys		;50	DOWN
	bra.w		.unmapped	;51	
	bra.w		.keys		;52	INS
	bra.w		.keys		;53	DEL
	bra.w		.unmapped	;54	
	bra.w		.unmapped	;55
	bra.w		.unmapped	;56
	bra.w		.unmapped	;57
	bra.w		.unmapped	;58
	bra.w		.unmapped	;59
	bra.w		.unmapped	;5a
	bra.w		.unmapped	;5b
	bra.w		.unmapped	;5c
	bra.w		.unmapped	;5d
	bra.w		.unmapped	;5e
	bra.w		.unmapped	;5f
	bra.w		.keys		;60	\
	bra.w		.keys		;61	UNDO
	bra.w		.keys		;62	HELP
	bra.w		.keys		;63	[(]
	bra.w		.keys		;64	[)]
	bra.w		.keys		;65	[/]
	bra.w		.keys		;66	[*]
	bra.w		.keys		;67	[7]
	bra.w		.keys		;68	[8]
	bra.w		.keys		;69	[9]
	bra.w		.keys		;6a	[4]
	bra.w		.keys		;6b	[5]
	bra.w		.keys		;6c	[6]
	bra.w		.keys		;6d	[1]
	bra.w		.keys		;6e	[2]
	bra.w		.keys		;6f	[3]
	bra.w		.keys		;70	[0]
	bra.w		.keys		;71	[.]
	bra.w		.keys		;72	[ENTER]
	bra.w		.unmapped	;73
	bra.w		.unmapped	;74
	bra.w		.unmapped	;75
	bra.w		.unmapped	;76
	bra.w		.unmapped	;77
*-------------------------------------------------------*	
	bra.w		.mouse		;78
	bra.w		.mouse		;79
	bra.w		.mouse		;7a
	bra.w		.mouse		;7b
	bra.w		.fault		;7c
	bra.w		.fault		;7d
	bra.w		.joy0		;7e
	bra.w		.joy1		;7f
*-------------------------------------------------------*	
	.endr
*-------------------------------------------------------*	
	
	.if		(0)
;	bra.w		.unmapped	;78
;	bra.w		.unmapped	;79
;	bra.w		.unmapped	;7a
;	bra.w		.unmapped	;7b
;	bra.w		.unmapped	;7c
;	bra.w		.unmapped	;7d
;	bra.w		.unmapped	;7e
;	bra.w		.unmapped	;7f
*-------------------------------------------------------*	
	bra.w		.unmapped	;80
	bra.w		.keys		;81
	bra.w		.keys		;82
	bra.w		.keys		;83
	bra.w		.keys		;84
	bra.w		.keys		;85
	bra.w		.keys		;86
	bra.w		.keys		;87
	bra.w		.keys		;88
	bra.w		.keys		;89
	bra.w		.keys		;8a
	bra.w		.keys		;8b
	bra.w		.keys		;8c
	bra.w		.keys		;8d
	bra.w		.keys		;8e
	bra.w		.keys		;8f
	bra.w		.keys		;90
	bra.w		.keys		;91
	bra.w		.keys		;92
	bra.w		.keys		;93
	bra.w		.keys		;94
	bra.w		.keys		;95
	bra.w		.keys		;96
	bra.w		.keys		;97
	bra.w		.keys		;98
	bra.w		.keys		;99
	bra.w		.keys		;9a
	bra.w		.keys		;9b
	bra.w		.keys		;9c
	bra.w		.keys		;9d
	bra.w		.keys		;9e
	bra.w		.keys		;9f
	bra.w		.keys		;a0
	bra.w		.keys		;a1
	bra.w		.keys		;a2
	bra.w		.keys		;a3
	bra.w		.keys		;a4
	bra.w		.keys		;a5
	bra.w		.keys		;a6
	bra.w		.keys		;a7
	bra.w		.keys		;a8
	bra.w		.keys		;a9
	bra.w		.keys		;aa
	bra.w		.keys		;ab
	bra.w		.keys		;ac
	bra.w		.keys		;ad
	bra.w		.keys		;ae
	bra.w		.keys		;af
	bra.w		.keys		;b0
	bra.w		.keys		;b1
	bra.w		.keys		;b2
	bra.w		.keys		;b3
	bra.w		.keys		;b4
	bra.w		.keys		;b5
	bra.w		.keys		;b6
	bra.w		.unmapped	;b7
	bra.w		.keys		;b8
	bra.w		.keys		;b9
	bra.w		.keys		;ba
	bra.w		.unmapped	;bb
	bra.w		.unmapped	;bc
	bra.w		.unmapped	;bd
	bra.w		.keys		;be
	bra.w		.keys		;bf
	bra.w		.keys		;c0
	bra.w		.keys		;c1
	bra.w		.keys		;c2
	bra.w		.keys		;c3
	bra.w		.keys		;c4
	bra.w		.unmapped	;c5
	bra.w		.unmapped	;c6
	bra.w		.keys		;c7
	bra.w		.keys		;c8
	bra.w		.unmapped	;c9
	bra.w		.keys		;ca
	bra.w		.keys		;cb
	bra.w		.unmapped	;cc
	bra.w		.keys		;cd
	bra.w		.keys		;ce
	bra.w		.unmapped	;cf
	bra.w		.keys		;d0
	bra.w		.unmapped	;d1
	bra.w		.keys		;d2
	bra.w		.keys		;d3
	bra.w		.unmapped	;d4
	bra.w		.unmapped	;5d
	bra.w		.unmapped	;d6
	bra.w		.unmapped	;d7
	bra.w		.unmapped	;d8
	bra.w		.unmapped	;d9
	bra.w		.unmapped	;da
	bra.w		.unmapped	;db
	bra.w		.unmapped	;dc
	bra.w		.unmapped	;dd
	bra.w		.unmapped	;de
	bra.w		.unmapped	;df
	bra.w		.keys		;e0
	bra.w		.keys		;e1
	bra.w		.keys		;e2
	bra.w		.keys		;e3
	bra.w		.keys		;e4
	bra.w		.keys		;e5
	bra.w		.keys		;e6
	bra.w		.keys		;e7
	bra.w		.keys		;e8
	bra.w		.keys		;e9
	bra.w		.keys		;ea
	bra.w		.keys		;eb
	bra.w		.keys		;ec
	bra.w		.keys		;ed
	bra.w		.keys		;ee
	bra.w		.keys		;ef
	bra.w		.keys		;f0
	bra.w		.keys		;f1
	bra.w		.keys		;f2
	bra.w		.unmapped	;f3
	bra.w		.unmapped	;f4
	bra.w		.unmapped	;f5
	bra.w		.unmapped	;f6
	bra.w		.unmapped	;f7
*-------------------------------------------------------*	
	bra.w		.mouse		;f8
	bra.w		.mouse		;f9
	bra.w		.mouse		;fa
	bra.w		.mouse		;fb
	bra.w		.fault		;fc
	bra.w		.fault		;fd
	bra.w		.joy0		;fe
	bra.w		.joy1		;ff
*-------------------------------------------------------*	
	.endif
	
	.if		(use_debugconsole)
*-------------------------------------------------------*	
*	special keys (TAB,SHIFT+TAB) activate debug-con
*-------------------------------------------------------*	
.console_key:
*-------------------------------------------------------*	
	btst		#7,d0
	bne.s		.cc_up
.cc_dn:
	; if LSHFT held, do nothing on DOWN
	tst.b		_key_states+$2a
	bne		.keys
.con_on:
;	move.l		_console_base,_min_pconsole
	move.l		_console_maxbase,_max_console
	; continue to process TAB key normally	
	bra		.keys
.cc_up:	
	; if LSHFT up, disable console
	tst.b		_key_states+$2a
	beq.s		.con_off	
.con_tog:
	; if LSHFT held, toggle on UP
	tst.l		_min_pconsole
;	beq.s		.con_on
	beq.s		.tog_on
.tog_off:
	clr.l		_min_pconsole
	bra		.keys
.tog_on:	
	move.l		#_console_minbase,_min_pconsole
	bra		.keys
.con_off:
	; continue to process TAB key normally	
	clr.l		_max_console
	bra		.keys
	.endif		;(use_debugconsole)

*-------------------------------------------------------*
.fault:
*-------------------------------------------------------*
	; should only get here if the IKBD reports an
	; unsupported state, or if an overflow occurs
	not.w		$ffff8240.w
	bra		.fault

*-------------------------------------------------------*
.unmapped:
*-------------------------------------------------------*
	move.b		d0,bad_scancode
	bsr		ikbd_error
*-------------------------------------------------------*
	move.w		(sp)+,d0
	move.b		#~imb_ikbd,isrb.w
	rte
	    
*-------------------------------------------------------*
.keys:
*-------------------------------------------------------*
	move.b		#~imb_ikbd,isrb.w			; start yielding to interrupts immediately
	andi.w		#$fbff,sr
*-------------------------------------------------------*
*	record the event in a dedicated ring so we 
*	can get out of here ASAP and interpret later
*-------------------------------------------------------*
	move.l		a0,-(sp)

	move.w		_key_eventhead,d1
	lea		_key_eventring,a0
	move.b		d0,(a0,d1.w)
	addq.b		#1,d1
	move.w		d1,_key_eventhead

	; also track press/release state separately
	
	and.w		#$ff,d0
	lea		_debounced_key_states,a0
	st		(a0,d0.w)	
;	lea		_key_states,a0
	bclr		#7,d0
	seq		-128(a0,d0.w)


	move.l		(sp)+,a0
*-------------------------------------------------------*
	move.w		(sp)+,d1
	move.w		(sp)+,d0
;	move.b		#~imb_ikbd,isrb.w
	rte
	
*-------------------------------------------------------*
.joy0:
*-------------------------------------------------------*
	move.l		#i_joy0,ikbd.w
*-------------------------------------------------------*
	move.w		(sp)+,d1
	move.w		(sp)+,d0
	move.b		#~imb_ikbd,isrb.w
	rte

*-------------------------------------------------------*
.joy1:
*-------------------------------------------------------*
	move.l		#i_joy1,ikbd.w
*-------------------------------------------------------*
	move.w		(sp)+,d1
	move.w		(sp)+,d0
	move.b		#~imb_ikbd,isrb.w
	rte

*-------------------------------------------------------*
.mouse:
*-------------------------------------------------------*
	move.l		#i_mouse_dx,ikbd.w
*-------------------------------------------------------*
	move.b		#~imb_ikbd,isrb.w			; start yielding to interrupts immediately
	andi.w		#$fbff,sr
*-------------------------------------------------------*	
	and.w		#3,d0
	cmp.w		_buttons,d0
	beq.s		.no_change
*-------------------------------------------------------*
	move.w		d0,_buttons
*-------------------------------------------------------*
*	record the event in a dedicated ring so we 
*	can get out of here ASAP and interpret later
*-------------------------------------------------------*
	move.l		a0,-(sp)
	
	move.w		_msclick_eventhead,d1
	lea		_msclick_eventring,a0
	move.b		d0,(a0,d1.w)
	addq.b		#1,d1
	move.w		d1,_msclick_eventhead
	
	move.l		(sp)+,a0	
*-------------------------------------------------------*
.no_change:
*-------------------------------------------------------*
	move.w		(sp)+,d1
	move.w		(sp)+,d0
;	move.b		#~imb_ikbd,isrb.w
	rte
	

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
i_mouse_dx:
*-------------------------------------------------------*
	move.l		#i_mouse_dy,ikbd.w
	move.w		d0,-(sp)
	move.b		key_dat.w,d0
*-------------------------------------------------------*
	move.b		#~imb_ikbd,isrb.w			; start yielding to interrupts immediately		
	andi.w		#$fbff,sr
*-------------------------------------------------------*
	.if		bldcfg_use_mouse
	ext.w		d0
	add.w		d0,_mouse_dx
	.endif
*-------------------------------------------------------*
	move.w		(sp)+,d0
;	move.b		#~imb_ikbd,isrb.w
	rte
	
*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
i_mouse_dy:
*-------------------------------------------------------*
	move.l		#i_primary,ikbd.w
	move.w		d0,-(sp)
	move.b		key_dat.w,d0
*-------------------------------------------------------*
	move.b		#~imb_ikbd,isrb.w			; start yielding to interrupts immediately				
	andi.w		#$fbff,sr
*-------------------------------------------------------*
	.if		bldcfg_use_mouse
	ext.w		d0
	add.w		d0,_mouse_dy
	.endif
*-------------------------------------------------------*
	move.w		(sp)+,d0
;	move.b		#~imb_ikbd,isrb.w
	rte

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
i_joy0:
*-------------------------------------------------------*
	move.l		#i_primary,ikbd.w
	move.w		d0,-(sp)
	move.b		key_dat.w,d0
*-------------------------------------------------------*
	move.b		#~imb_ikbd,isrb.w			; start yielding to interrupts immediately				
	andi.w		#$fbff,sr
*-------------------------------------------------------*
	move.b		d0,_joy0	
*-------------------------------------------------------*
*	record the event in a dedicated ring so we 
*	can get out of here ASAP and interpret later
*-------------------------------------------------------*
	move.l		a0,-(sp)
	move.w		d1,-(sp)
	
	move.w		_joy0_eventhead,d1
	lea		_joy0_eventring,a0
	move.b		d0,(a0,d1.w)
	addq.b		#1,d1
	move.w		d1,_joy0_eventhead
	
	move.w		(sp)+,d1
	move.l		(sp)+,a0
*-------------------------------------------------------*
	move.w		(sp)+,d0
;	move.b		#~imb_ikbd,isrb.w
	rte
	
*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
i_joy1:
*-------------------------------------------------------*
	move.l		#i_primary,ikbd.w
	move.w		d0,-(sp)	
	move.b		key_dat.w,d0
*-------------------------------------------------------*
	move.b		#~imb_ikbd,isrb.w			; start yielding to interrupts immediately				
	andi.w		#$fbff,sr
*-------------------------------------------------------*
	move.b		d0,_joy1	
*-------------------------------------------------------*
*	record the event in a dedicated ring so we 
*	can get out of here ASAP and interpret later
*-------------------------------------------------------*
	move.l		a0,-(sp)
	move.w		d1,-(sp)
	
	move.w		_joy1_eventhead,d1
	lea		_joy1_eventring,a0
	move.b		d0,(a0,d1.w)
	addq.b		#1,d1
	move.w		d1,_joy1_eventhead
	
	move.w		(sp)+,d1
	move.l		(sp)+,a0
*-------------------------------------------------------*
	move.w		(sp)+,d0
;	move.b		#~imb_ikbd,isrb.w
	rte
		


*-------------------------------------------------------*
*	Flush TOS keyboard buffer
*-------------------------------------------------------*
empty_buffer:
*-------------------------------------------------------*
.bk:	move.w		#11,-(sp)
	trap		#1
	addq		#2,sp
	tst.w		d0
	beq.s		.ot
	move.w		#7,-(sp)
	trap		#1
	addq		#2,sp
	bra.s		.bk
.ot:	rts

*-------------------------------------------------------*
			.data
*-------------------------------------------------------*

_key_eventhead:		dc.w	0
_msclick_eventhead:	dc.w	0
_joy0_eventhead:	dc.w	0
_joy1_eventhead:	dc.w	0

_key_eventtail:		dc.w	0
_msclick_eventtail:	dc.w	0
_joy0_eventtail:	dc.w	0
_joy1_eventtail:	dc.w	0

*-------------------------------------------------------*
			.bss
*-------------------------------------------------------*

sys_midi:		ds.l	1			; GEMDOS MIDI interupt handler
sys_ikbd:		ds.l	1
sys_mouse:		ds.l	1

*-------------------------------------------------------*

_key_states:		ds.b	128			; key press/release states
_debounced_key_states:	ds.b	256			; separated key press/release states

_debounced_key_presses	=	_debounced_key_states
_debounced_key_releases	=	_debounced_key_states+128

_key_eventring:		ds.b	256			; key press/release events in sequence
_msclick_eventring:	ds.b	256			; mouse click/release events in sequence
_joy0_eventring:	ds.b	256			; joy0 events in sequence
_joy1_eventring:	ds.b	256			; joy1 events in sequence

*-------------------------------------------------------*

_joy0:			ds.b	1
_joy1:			ds.b	1
_buttons:		ds.w	1
_mouse_dx:		ds.w	1
_mouse_dy:		ds.w	1

*-------------------------------------------------------*

ikbd_on:		ds.b	1
bad_scancode:		ds.b	1
			even

*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
