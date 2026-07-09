*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Sound services
*-------------------------------------------------------*

	.if		^^defined ENABLE_AGT_AUDIO

*-------------------------------------------------------*
			.text
*-------------------------------------------------------*

use_falcon_friendly_ym	=	1
use_rasters		=	0
audio_standalone_timer	=	0

			.if	^^defined AGT_CONFIG_AUDIO_200HZ
configure_50hz		=	0
			.else
configure_50hz		=	1
			.endif


	.globl		_YMSYS_Init

	.if		(audio_standalone_timer)
	.globl		_YMSYS_Timer
	.else
	.globl		_YMSYS_Callback
	.globl		_YMSYS_VBCallback
	.endif

	.globl		_STE_WriteLMC1992

	.globl		_testcounter
	.globl		_audioframe_collisions
	.globl		_audio_mux_frame

;	.globl		_g_ym_buzztable

	.globl		_g_ymselected
	.globl		_g_remove_channels
	.globl		_g_remove_channel_count
	.globl		_g_active_physical
	.globl		_g_bmm_channel_controller_pbend
	.globl		_g_bmm_channel_controller_volume
	.globl		_g_yminstrument_index
	.globl		_g_bmm_channel_octave_map
	.globl		_g_active_channels
	.globl		_g_multiplex_proxy_index
	.globl		_g_multiplex_sentinel_index
	.globl		_g_instrument_map
	.globl		_g_bmm_channel_multiplex_map
	.globl		_g_sampleE
	.globl		_g_sampleB
	.globl		_g_samplePrio
	.globl		_g_serverlocks
	.globl		_g_front_uoactive
	.globl		_g_front_prio
	.globl		_g_back_prio
	.globl		_g_sampletranslation
	.globl		_g_bmmstream_restarteventcount
	.globl		_g_bmmstream_restartpos
	.globl		_g_bmmstream_eventcount
	.globl		_g_bmmstream_pos
	.globl		_g_bmmstream_loop
	.globl		_g_looped

	.globl		_g_ym_freqtable
	.globl		_g_ym_vibtable
	.globl		_g_combine_ctrlvol_notevel
	.globl		_g_combine_adsrenv_mixvol

	.globl		_g_soundtick
	.globl		_subframe_event_68k
	.globl		_assign_bmm_channel_68k
	.globl		_bmm_event_feeder_68k
	.globl		_fixup_procedures_68k

	.globl		_g_multiplex_sentinels
	.globl		_g_multiplex_proxies

	.globl		_g_back_uoactive

	.globl		_g_strobe
	.globl		_g_channel_freepool
	.globl		_g_channel_freepool_count
;	.globl		_g_combine_ctrlvol_notevel
;	.globl		_g_combine_adsrenv_mixvol
	.globl		_g_yminstrument_dict
	.globl		_ev_processed
	.globl		_ev_target
	.globl		_api_channel_cleanup
	.globl		_g_flushchannels

*-------------------------------------------------------*

timera		=	$134
timerb		=	$120
timerc		=	$114
timerd		=	$110

tbcr		=	$fffffa1b
cdcr		=	$fffffa1d
tbdr		=	$fffffa21
tcdr		=	$fffffa23
cddr		=	$fffffa25
iera		=	$fffffa07
ierb		=	$fffffa09
imra		=	$fffffa13
imrb		=	$fffffa15
isra		=	$fffffa0f
isrb		=	$fffffa11

*-------------------------------------------------------*
		.abs
*-------------------------------------------------------*
ins_prio:	ds.w	1
ins_penvseq:	ds.l	1
ins_envrel:	ds.w	1
ins_psplname:	ds.l	1
ins_pwavalloc:	ds.l	1
ins_pwav:	ds.l	1
ins_samplelen:	ds.w	1
;ins_pad_:	ds.w	1
*-------------------------------------------------------*
ins_size_:

*-------------------------------------------------------*
		.abs
*-------------------------------------------------------*
vch_panext:	ds.l	1
vch_paprev:	ds.l	1
*-------------------------------------------------------*
vch_penv:	ds.l	1
vch_psustain:	ds.l	1
*-------------------------------------------------------*
vch_cvol:	ds.b	1
vch_vol:	ds.b	1
vch_evol:	ds.b	1
vch_note:	ds.b	1
*-------------------------------------------------------*
vch_status_:	ds.b	1
vch_noteon:	ds.b	1
vch_chn:		ds.b	1
vch_setbuzz:	ds.b	1
;vch_ovol:	ds.b	1
*-------------------------------------------------------*
vch_vibpos:	ds.w	1
vch_vibmag:	ds.w	1
vch_pbend:	ds.w	1
vch_finenote:	ds.w	1
vch_ymi:	ds.w	1
vch_handle:	ds.w	1
vch_ymch:	ds.w	1
vch_ymchlast:	ds.w	1
vch_mplexcnt:	ds.b	1
vch_mplexcycle:	ds.b	1
vch_mplexmap:	ds.w	1
*-------------------------------------------------------*
vch_puoanext:	ds.l	1
vch_puoaprev:	ds.l	1
vch_pmplexsent:	ds.l	1
*-------------------------------------------------------*
vch_size_:

*-------------------------------------------------------*
		.abs
*-------------------------------------------------------*
ym_cmd:		ds.b	1
ym_pad0_:	ds.b	1
ym_data:	ds.b	1
ym_pad1_:	ds.b	1
*-------------------------------------------------------*
		.text
*-------------------------------------------------------*

EP_Background		=	5
pbend_bits		=	5
mperc_last_		=	54+1+2
midi_mperc_start_	=	35

			.if	(configure_50hz)
ms_per_frame		=	20
vib_stepspeed		=	40
			.else
ms_per_frame		=	5
vib_stepspeed		=	10
			.endif


max_sfx_handles		=	256
max_midi_notes		=	128
max_midi_channels	=	16
max_virtual_channels	=	48
max_physical_channels	=	3

max_handles 		=	(max_sfx_handles+(max_midi_notes*max_midi_channels))

*-------------------------------------------------------*
_YMSYS_Init:
*-------------------------------------------------------*
	movem.l		d0-d7/a0-a6,-(sp)

	move.w		#-1,_audioframe_sema

;	move.l		#.macro,$14.w
;	divu.w		$4bc.w,d0
;	bra.s		*+20
;.macro:	pea		'unes'
;	pea		'ipch'
;	pea		'sssh'


	.if		(audio_standalone_timer)

;	move.l		timerd.w,sys_timerd
;	move.l		#_YMSYS_Timer,timerd.w
;
;	bclr		#4,ierb.w
;
;	and.b		#$f8,cdcr.w
;	move.b		#192,cddr.w
;	or.b		#$5,cdcr.w
;
;	bset		#4,ierb.w
;	bset		#4,imrb.w
;	bclr		#4,isrb.w

	move.l		timerc.w,sys_timerc
	move.l		#_YMSYS_Timer,timerc.w

	bclr		#5,ierb.w

;	and.b		#$f8,cdcr.w
	and.b		#$0f,cdcr.w
	move.b		#192,cddr.w
	or.b		#(5<<4),cdcr.w

	bset		#5,ierb.w
	bset		#5,imrb.w
	bclr		#5,isrb.w

	.else


	.endif

	movem.l		(sp)+,d0-d7/a0-a6
	rts




cccc:	dc.w		0




.macro	dovolume	mr1,mr2,mr3,mr4,mr5


	; combine note volume with channel controller volume

	; todo: only required for physically active channels

	move.w		vch_cvol(\mr1),\mr4			; cvol[8]:vol[8]
	add.b		\mr4,\mr4
	lsr.w		\mr4				; m = (cvol << 7) | vol;

	; needs done on every envelope update

	move.w		vch_evol(\mr1),\mr5
	move.b		(\mr2,\mr4.w),\mr5			; evol[8]:mixvol[8]
	add.b		\mr5,\mr5
	lsr.w		\mr5				; o = (evol << 7) | mixvol;

;	moveq		#0,d1
;	move.b		vch_cvol(a0),d1
;	lsl.w		#7,d1
;
;	or.b		vch_vol(a0),d1
;
;	; combine envelope volume with channel volume
;
;	moveq		#0,d2
;	move.b		vch_evol(a0),d2
;	lsl.w		#7,d2
;
;	or.b		(a1,d1.w),d2

	move.b		(\mr3,\mr5.w),\mr5

	.endm




	.if		(audio_standalone_timer)
*-------------------------------------------------------*
_YMSYS_Timer:
*-------------------------------------------------------*
;	subq.w		#1,cccc
;	bmi.s		.ok
;	bclr		#4,isrb.w
;	rte
;.ok:	move.w		#3,cccc

	.if		(use_rasters)
	move.w		$ffff8240.w,-(sp)
	not.w		$ffff8240.w
	.endif
*-------------------------------------------------------*
	movem.l		d0-d7/a0-a6,-(sp)
*-------------------------------------------------------*
*	critical section - cannot self-interrupt
*-------------------------------------------------------*
	or.w		#$0700,sr
	move.w		_audioframe_sema,d0
	bpl		.service_busy
	addq.w		#1,_audioframe_sema
*-------------------------------------------------------*
*	yield
*-------------------------------------------------------*
;	bclr.b		#4,isrb.w
	bclr.b		#5,isrb.w
	and.w		#$f3ff,sr
*-------------------------------------------------------*
	tst.b		_g_flushchannels
	beq.s		.nfl
	sf		_g_flushchannels
	movem.l		d0-a6,-(sp)
	jsr		_api_channel_cleanup
	movem.l		(sp)+,d0-a6
.nfl:

	.else

xxx:	dc.w		0

*-------------------------------------------------------*
_YMSYS_VBCallback:
*-------------------------------------------------------*

	.if		(configure_50hz)
	.else

	and.b		#$f0,cdcr.w
	bclr		#4,ierb.w
	move.b		#193,cddr.w
	or.b		#(5<<0),cdcr.w

	bset		#4,ierb.w
	bset		#4,imrb.w

	.endif

	andi.w		#$fbff,sr

*-------------------------------------------------------*
_YMSYS_Callback:
*-------------------------------------------------------*
	addq.w		#1,_audioframe_sema
	beq.s		.service_ready
	subq.w		#1,_audioframe_sema
	rts

.service_ready:
	.if		(use_rasters)
	move.w		$ffff8240.w,-(sp)
	.endif

	.endif

*-------------------------------------------------------*
*	YM output stage
*-------------------------------------------------------*

	move.b		_g_serverlocks(pc),d0
	bne		.locked
	addq.b		#1,_g_soundtick


	.if		(use_rasters)
;	move.w		$ffff8240.w,d7
	move.w		#$700,$ffff8240.w
	.endif

	lea		_g_ymselected(pc),a0

	lea		_g_combine_ctrlvol_notevel,a2
	lea		_g_combine_adsrenv_mixvol,a3

	lea		_g_ym_vibtable,a4
	lea		_g_ym_freqtable,a5

	lea		_ym_regs,a6

	moveq		#%11111000,d7			; enable tone channels

*-------------------------------------------------------*
	moveq		#0,d3				; zero volume
	move.l		(a0)+,d0			; ChA
	beq		.no_chA

	move.l		d0,a1

;	move.w		vch_finenote(a1),d0
	moveq		#0,d0
	move.b		vch_note(a1),d0
	lsl.w		#5,d0
	add.w		vch_pbend(a1),d0


	moveq		#vib_stepspeed,d2
	add.b		vch_vibpos(a1),d2
	move.b		d2,vch_vibpos(a1)
	add.w		vch_vibmag(a1),d2

	move.w		d0,d4

	add.w		(a4,d2.w),d0

	add.w		d0,d0
;	add.w		d0,d0
	move.w		(a5,d0.w),d1			; freq

;	move.b		vch_ovol(a1),d3

	dovolume	a1,a2,a3,d0,d3

	moveq		#$20,d0
	and.b		d3,d0				; square cancellation bit
	eor.b		d0,d3
	lsr.b		#(5-0),d0
	eor.b		d0,d7				; optionally disable A square

	cmp.b		#$10,d3
	blt.s		.no_buzzA

	; NES.WWWW
	;
	; N=0	wave event - change of waveform
	; E=1	force reset envelope
	; S=1	force reset square
	; W	waveform shape

	move.b		vch_setbuzz(a1),d0
	bmi.s		.no_resetA
	tas.b		vch_setbuzz(a1)			; accept new waveform

	moveq		#%01100000,d2
	and.w		d0,d2
	bne.s		.do_resetA
	eor.b		d2,d0
	bra.s		.try_modifyBuzzA
.do_resetA:
	bclr		#5,d0
	beq.s		.no_resetSqrA
	; force-reset square wave
	move.b		#$0,_ym_reg_resetABC+ym_cmd-_ym_regs(a6)
	move.b		#$1,_ym_reg_resetABC+4+ym_cmd-_ym_regs(a6)
.no_resetSqrA:
	bclr		#6,d0
	bne.s		.do_modifyBuzzA
.try_modifyBuzzA:
	cmp.b		_ym_reg_shapeE+ym_data-_ym_regs(a6),d0
	beq.s		.no_resetA
.do_modifyBuzzA:
	; modify buzz waveform
	move.b		#$D,_ym_reg_shapeE+ym_cmd-_ym_regs(a6)
	move.b		d0,_ym_reg_shapeE+ym_data-_ym_regs(a6)

.no_resetA:


	; YM env bit #2 = saw

	and.w		#2,d0
	beq.s		.no_sawA
	add.w		#12<<5,d4
.no_sawA:

;	lea		_g_ym_buzztable,a5
	add.w		d4,d4
;	add.w		d4,d4
	move.w		(a5,d4.w),d2			; freq
;	lea		_g_ym_freqtable,a5

	addq.w		#4,d2
	lsr.w		#3,d2
;	and.w		#$0fff,d2
	move.b		d2,_ym_reg_freqE+ym_data-_ym_regs(a6)
	lsr.w		#8,d2
	move.b		d2,_ym_reg_freqE+ym_data+4-_ym_regs(a6)

.no_buzzA:
;	addq		#1,d1
	move.b		d1,_ym_reg_freqA+ym_data-_ym_regs(a6)
	lsr.w		#8,d1
	move.b		d1,_ym_reg_freqA+ym_data+4-_ym_regs(a6)

.no_chA:
	move.b		d3,_ym_reg_levelA+ym_data-_ym_regs(a6)

*-------------------------------------------------------*
	moveq		#0,d3				; zero volume
	move.l		(a0)+,d0			; ChB
	beq		.no_chB

	move.l		d0,a1

;	move.w		vch_finenote(a1),d0
	moveq		#0,d0
	move.b		vch_note(a1),d0
	lsl.w		#5,d0
	add.w		vch_pbend(a1),d0

	moveq		#vib_stepspeed,d2
	add.b		vch_vibpos(a1),d2
	move.b		d2,vch_vibpos(a1)
	add.w		vch_vibmag(a1),d2

	move.w		d0,d4

	add.w		(a4,d2.w),d0

	add.w		d0,d0
;	add.w		d0,d0
	move.w		(a5,d0.w),d1			; freq

;	move.b		vch_ovol(a1),d3

	dovolume	a1,a2,a3,d0,d3

	moveq		#$20,d0
	and.b		d3,d0				; square cancellation bit
	eor.b		d0,d3
	lsr.b		#(5-1),d0
	eor.b		d0,d7				; optionally disable B square

	cmp.b		#$10,d3
	blt.s		.no_buzzB

	; NES.WWWW
	;
	; N=0	wave event - change of waveform
	; E=1	force reset envelope
	; S=1	force reset square
	; W	waveform shape

	move.b		vch_setbuzz(a1),d0
	bmi.s		.no_resetB
	tas.b		vch_setbuzz(a1)			; accept new waveform

	moveq		#%01100000,d2
	and.w		d0,d2
	bne.s		.do_resetB
	eor.b		d2,d0
	bra.s		.try_modifyBuzzB
.do_resetB:
	bclr		#5,d0
	beq.s		.no_resetSqrB
	; force-reset square wave
	move.b		#$2,_ym_reg_resetABC+ym_cmd-_ym_regs(a6)
	move.b		#$3,_ym_reg_resetABC+4+ym_cmd-_ym_regs(a6)
.no_resetSqrB:
	bclr		#6,d0
	bne.s		.do_modifyBuzzB
.try_modifyBuzzB:
	cmp.b		_ym_reg_shapeE+ym_data-_ym_regs(a6),d0
	beq.s		.no_resetB
.do_modifyBuzzB:
	; modify buzz waveform
	move.b		#$D,_ym_reg_shapeE+ym_cmd-_ym_regs(a6)
	move.b		d0,_ym_reg_shapeE+ym_data-_ym_regs(a6)

.no_resetB:

	and.w		#2,d0
	beq.s		.no_sawB
	add.w		#12<<5,d4
.no_sawB:

;	lea		_g_ym_buzztable,a5
	add.w		d4,d4
;	add.w		d4,d4
	move.w		(a5,d4.w),d2			; freq
;	lea		_g_ym_freqtable,a5

	addq.w		#4,d2
	lsr.w		#3,d2
;	and.w		#$0fff,d2
	move.b		d2,_ym_reg_freqE+ym_data-_ym_regs(a6)
	lsr.w		#8,d2
	move.b		d2,_ym_reg_freqE+ym_data+4-_ym_regs(a6)

.no_buzzB:
	move.b		d1,_ym_reg_freqB+ym_data-_ym_regs(a6)
	lsr.w		#8,d1
	move.b		d1,_ym_reg_freqB+ym_data+4-_ym_regs(a6)

.no_chB:
	move.b		d3,_ym_reg_levelB+ym_data-_ym_regs(a6)


*-------------------------------------------------------*
	moveq		#0,d3				; zero volume
	move.l		(a0)+,d0			; ChB
	beq		.no_chC

	move.l		d0,a1

;	move.w		vch_finenote(a1),d0
	moveq		#0,d0
	move.b		vch_note(a1),d0
	lsl.w		#5,d0
	add.w		vch_pbend(a1),d0

	moveq		#vib_stepspeed,d2
	add.b		vch_vibpos(a1),d2
	move.b		d2,vch_vibpos(a1)
	add.w		vch_vibmag(a1),d2

	move.w		d0,d4

	add.w		(a4,d2.w),d0

	add.w		d0,d0
;	add.w		d0,d0
	move.w		(a5,d0.w),d1			; freq

;	move.b		vch_ovol(a1),d3

	dovolume	a1,a2,a3,d0,d3

;	todo: table to translate d3->d3/d7M

;	and.w		#$ff,d3
;	move.b		.atab(pc,d3.w),d0		; %1xxxxxxx = no buzz
;	bmi.s		.no_buzzC
;	and.b		d0,d7				; optionally disable C square
;.atab:	dc.b

	moveq		#$20,d0
	and.b		d3,d0				; square cancellation bit
	eor.b		d0,d3
	lsr.b		#(5-2),d0
	eor.b		d0,d7				; optionally disable C square

	cmp.b		#$10,d3
	blt.s		.no_buzzC

	; NES.WWWW
	;
	; N=0	wave event - change of waveform
	; E=1	force reset envelope
	; S=1	force reset square
	; W	waveform shape

	move.b		vch_setbuzz(a1),d0
	bmi.s		.no_resetC
	tas.b		vch_setbuzz(a1)			; accept new waveform

	moveq		#%01100000,d2
	and.w		d0,d2
	bne.s		.do_resetC
	eor.b		d2,d0
	bra.s		.try_modifyBuzzC
.do_resetC:
	bclr		#5,d0
	beq.s		.no_resetSqrC
	; force-reset square wave
	move.b		#$4,_ym_reg_resetABC+ym_cmd-_ym_regs(a6)
	move.b		#$5,_ym_reg_resetABC+4+ym_cmd-_ym_regs(a6)
.no_resetSqrC:
	bclr		#6,d0
	bne.s		.do_modifyBuzzC
.try_modifyBuzzC:
	cmp.b		_ym_reg_shapeE+ym_data-_ym_regs(a6),d0
	beq.s		.no_resetC
.do_modifyBuzzC:
	; modify buzz waveform
	move.b		#$D,_ym_reg_shapeE+ym_cmd-_ym_regs(a6)
	move.b		d0,_ym_reg_shapeE+ym_data-_ym_regs(a6)

.no_resetC:

	and.w		#2,d0
	beq.s		.no_sawC
	add.w		#12<<5,d4
.no_sawC:


;	lea		_g_ym_buzztable,a5
	add.w		d4,d4
;	add.w		d4,d4
	move.w		(a5,d4.w),d2			; freq
;	lea		_g_ym_freqtable,a5

	addq.w		#4,d2
	lsr.w		#3,d2
;	and.w		#$0fff,d2
	move.b		d2,_ym_reg_freqE+ym_data-_ym_regs(a6)
	lsr.w		#8,d2
	move.b		d2,_ym_reg_freqE+ym_data+4-_ym_regs(a6)

.no_buzzC:
;	subq		#1,d1
	move.b		d1,_ym_reg_freqC+ym_data-_ym_regs(a6)
	lsr.w		#8,d1
	move.b		d1,_ym_reg_freqC+ym_data+4-_ym_regs(a6)

.no_chC:
	move.b		d3,_ym_reg_levelC+ym_data-_ym_regs(a6)

*-------------------------------------------------------*

	move.b		d7,_ym_reg_mixer+ym_data-_ym_regs(a6)

*-------------------------------------------------------*
*	load all YM registers at once
*-------------------------------------------------------*
;	lea		_ym_regs,a6
;	movem.l		(a6)+,d0-d7/a0-a5



	;	cancel envelope reset
;	st		_ym_reg_shapeE+ym_cmd-_ym_regs(a6)


	lea		$ffff8800.w,a5

	.if		(use_falcon_friendly_ym)

	movem.l		(a6)+,d0-d7/a0-a4	; 1-13
	ori.w		#$0700,sr
	move.l		d0,(a5)
	move.l		d1,(a5)
	move.l		d2,(a5)
	move.l		d3,(a5)
	move.l		d4,(a5)
	move.l		d5,(a5)
	move.l		d6,(a5)
	move.l		d7,(a5)
	move.l		a0,(a5)
	move.l		a1,(a5)
	move.l		a2,(a5)
	move.l		a3,(a5)
	move.l		a4,(a5)
	move.l		(a6)+,(a5)		; 14
	move.l		(a6)+,(a5)		; 15
	andi.w		#$fbff,sr

	.else

	movem.l		(a6)+,d0-d7/a0-a4
	movem.l		d0-d7/a0-a4,(a5)
	movem.l		(a6)+,(a5)
	movem.l		(a6)+,(a5)

	.endif

	moveq		#-1,d0
	move.b		d0,_ym_reg_shapeE+ym_cmd-_ym_regs-(15*4)(a6)
	move.b		d0,_ym_reg_resetABC+ym_cmd-_ym_regs-(15*4)(a6)
	move.b		d0,_ym_reg_resetABC+4+ym_cmd-_ym_regs-(15*4)(a6)






*-------------------------------------------------------*
*	1 channel percussion
*-------------------------------------------------------*
	.if		(use_rasters)
	move.w		#$070,$ffff8240.w
	.endif

;	if (g_sampleE > g_sampleB)
;	{
;		// stop playback
;		reg8(ffff8901) = 0x00;
;		reg8(ffff8921) = 0x81;
;
;		reg8(ffff8903) = (g_sampleB >> 16) & 0xFF;
;		reg8(ffff8905) = (g_sampleB >> 8) & 0xFF;
;		reg8(ffff8907) =  g_sampleB & 0xFF;
;
;		reg8(ffff890f) = (g_sampleE >> 16) & 0xFF;
;		reg8(ffff8911) = (g_sampleE >> 8) & 0xFF;
;		reg8(ffff8913) =  g_sampleE & 0xFF;
;
;		// start playback
;		reg8(ffff8901) = 0x01;
;
;		g_sampleB = 0;
;		g_sampleE = 0;
;	}

	lea		_g_sampleE(pc),a1
	tst.w		(a1)
	beq.s		.no_perc

	move.l		(a1)+,d1
	move.l		(a1),d0

	moveq		#0,d3
	lea		$ffff8901.w,a0

	move.b		d3,(a0)
;	move.b		#$81,$20(a0)

;	movep.l		d0,$0(a0)
;	movep.l		d1,$c(a0)

	move.l		d0,d2
	lsr.w		#8,d2
	move.l		d2,2-1(a0)
	move.b		d0,7-1(a0)

	move.l		d1,d2
	lsr.w		#8,d2
	move.l		d2,14-1(a0)
	move.b		d1,19-1(a0)

	move.b		#1,(a0)

	move.l		d3,(a1)
	move.l		d3,-(a1)
.no_perc:

*-------------------------------------------------------*
*	channel multiplexing
*-------------------------------------------------------*
;	subq.w		#1,_g_strobe
;	bpl		.no_mplex
;	addq.w		#4,_g_strobe


	
	lea		_g_multiplex_sentinels,a0
	


;	moveq		#3-1,d7


.macro	multiplex

.mch\~:	move.b		vch_mplexcnt(a0),d6
	subq.b		#1,d6
	ble.s		.no_mch\~

	subq.b		#1,vch_mplexcycle(a0)
	bpl		.no_mch\~

	ext.w		d6
	lea		_mplex_cycles(pc),a2
	move.b		(a2,d6.w),d6
.okz\~:
	add.b		d6,vch_mplexcycle(a0)

	move.l		vch_panext(a0),a1
	move.b		vch_noteon(a1),d1
		
	move.l		a1,a2
.scan\~:
	move.l		vch_panext(a2),a2
	move.b		vch_noteon(a2),d2
	bne.s		.send\~
	cmp.b		d2,d1
	bne.s		.scan\~
.send\~:
	cmp.l		a1,a2
	beq.s		.no_change\~
	
	move.l		vch_paprev(a0),a3
	move.l		vch_panext(a0),a4
	move.l		a4,vch_panext(a3)
	move.l		a3,vch_paprev(a4)
	
	move.l		vch_paprev(a2),a1
	move.l		a0,vch_panext(a1)
	move.l		a0,vch_paprev(a2)
	move.l		a1,vch_paprev(a0)
	move.l		a2,vch_panext(a0)
		
.no_change\~:
	
.no_mch\~:
	.endm


;	lea		vch_size(a0),a0
;	dbra		d7,.mch

	multiplex
	lea		vch_size_(a0),a0
	multiplex
	lea		vch_size_(a0),a0
	multiplex

*-------------------------------------------------------*
.no_mplex:
*-------------------------------------------------------*
	.if		(use_rasters)
	move.w		#$007,$ffff8240.w
	.endif

;	jsr		_audio_mux_frame		; this is where the heavy work is done

;	subq.w		#1,xxx
;	bpl.s		.sk
;	addq.w		#4,xxx


	tst.b		_g_flushchannels
	beq.s		.nfl
	sf		_g_flushchannels
	movem.l		d0-a6,-(sp)
	jsr		_api_channel_cleanup
	movem.l		(sp)+,d0-a6
.nfl:
	tst.w		_g_bmmstream_eventcount
	beq.s		.no
	jsr		_bmm_event_feeder_68k
.no:

.sk:

*-------------------------------------------------------*
*	channel fx
*-------------------------------------------------------*

	.if		(use_rasters)
	move.w		#$757,$ffff8240.w
	.endif


;	ifd xxxx

	move.l		_g_front_uoactive+vch_puoanext(pc),a0

	move.l		vch_puoanext(a0),d0
	beq		.fxdone

	move.w		#((1<<(7+1))*16),d3

	.if		(configure_50hz)
	move.w		#(1<<(7+1))<<2,d4
	.else
	move.w		#(1<<(7+1)),d4
	.endif

	moveq		#0,d5
;	moveq		#1,d7				; status_complete
	moveq		#-1,d6

	lea		_g_remove_channels(pc),a4
	lea		_g_remove_channel_count(pc),a1
	move.w		(a1),d7

.fxnext:
	tst.b		vch_status_(a0)			; status_active
;	bne.s		*
	bne		.no_act




.macro	doenv

	move.l		vch_penv(a0),d1
;	beq.s		.no_env

	cmp.w		vch_psustain+2(a0),d1
	bne.s		.env_advance\~
;	cmp.l		vch_psustain(a0),d1
;	bne.s		.env_advance\~
.env_sustain\~:

;	slide vibrato upwards on [S]ustain

	cmp.w		vch_vibmag(a0),d3
	ble		.no_env
	add.w		d4,vch_vibmag(a0)
	bra		.no_env

.env_advance\~:
	move.l		d1,a3

;	.if		(configure_50hz)
;	addq		#4,a3
;	move.b		-4(a3),vch_evol(a0)
;	.else
	move.b		(a3)+,vch_evol(a0)
;	.endif

	bgt.s		.env_cont\~
.env_end\~:
	move.b		d5,vch_noteon(a0)		; 0=false

;	move.b		d7,vch_status_(a0)		; status_complete

	move.l		a0,(a4,d7.w)
	addq.w		#4,d7
	move.b		d6,vch_status_(a0)

	move.l		d5,vch_penv(a0)
	bra		.no_env
.env_cont\~:
	move.l		a3,vch_penv(a0)




	.endm





	.if		(configure_50hz)
;	doenv
;	doenv
;	doenv
	.endif
	doenv		; otherwise 200hz



;	slide vibrato back down on [A]ttack/[D]ecay/[R]elease

;	tst.w		vch_vibmag(a0)
;	ble.s		.no_env
;	sub.w		d4,vch_vibmag(a0)

;	sub.w		d4,vch_vibmag(a0)
;	bpl.s		.no_env
;	move.w		d5,vch_vibmag(a0)

	move.w		vch_vibmag(a0),d1
	ble.s		.no_upd
	sub.w		d4,d1
	move.w		d1,vch_vibmag(a0)
.no_upd:



.no_env:

.no_act:

	; detach from last YM HW channel

	move.w		vch_ymch(a0),vch_ymchlast(a0)
	move.w		d6,vch_ymch(a0)			; -1

	; next vchannel

	move.l		d0,a0
	move.l		vch_puoanext(a0),d0
	bne		.fxnext

	move.w		d7,(a1)

.fxdone:



.macro	vchremove	mr1,mr2,mr3

	lea		_g_remove_tide(pc),a3
	move.l		(\mr3),\mr2
	move.l		\mr1,(\mr2)+
	move.l		\mr2,(\mr3)

	.endm

*-------------------------------------------------------*
*	channel retirement
*-------------------------------------------------------*

;	bra		.no_cull

	cmp.w		#(max_virtual_channels-12)*4,_g_channel_freepool_count
	bge		.no_cull

	move.l		_g_back_uoactive+vch_puoaprev,a0

;	lea		_g_back_uoactive,a0
;.try2:	move.l		vch_puoaprev(a0),a0
	move.l		vch_puoaprev(a0),d0
	beq.s		.no_cull

	tst.b		vch_noteon(a0)
	bne.s		.try2
	tst.b		vch_status_(a0)
	bne.s		.no_cull

	lea		_g_remove_channel_count(pc),a3
	move.w		(a3),d7
	lea		_g_remove_channels(pc),a4
	move.l		a0,(a4,d7.w)
	addq.w		#4,d7
	st		vch_status_(a0)
	move.w		d7,(a3)

	.if		(1)

	bra.s		.cull_done

.try2:	move.l		d0,a0
	move.l		vch_puoaprev(a0),d0
	beq.s		.no_cull

	tst.b		vch_noteon(a0)
	bne.s		.try3
	tst.b		vch_status_(a0)
	bne.s		.no_cull

	lea		_g_remove_channel_count(pc),a3
	move.w		(a3),d7
	lea		_g_remove_channels(pc),a4
	move.l		a0,(a4,d7.w)
	addq.w		#4,d7
	st		vch_status_(a0)
	move.w		d7,(a3)
	bra.s		.cull_done

.try3:	move.l		d0,a0
	move.w		vch_puoaprev(a0),d0
	beq.s		.no_cull

	tst.b		vch_noteon(a0)
	bne.s		.try4
	tst.b		vch_status_(a0)
	bne.s		.no_cull

	lea		_g_remove_channel_count(pc),a3
	move.w		(a3),d7
	lea		_g_remove_channels(pc),a4
	move.l		a0,(a4,d7.w)
	addq.w		#4,d7
	st		vch_status_(a0)
	move.w		d7,(a3)
;	bra.s		.cull_done

.try4:

	.endif

.cull_done:
.no_cull:







	.if		(0)

	.if		(use_rasters)
	move.w		#$fff,$ffff8240.w
	.endif


	move.l		_g_front_uoactive+vch_puoanext(pc),a0

	move.l		vch_puoanext(a0),d0
	beq.s		.rdone

	lea		_g_remove_channels(pc),a1

	lea		_g_remove_channel_count(pc),a2
	move.w		(a2),d1
;	move.w		d1,d2
;	add.w		d2,d2
;	add.w		d2,d2
;	add.w		d2,a1
	add.w		d1,a1

	moveq		#1,d7				; status_complete
	moveq		#-1,d6				; status_inactive
.rnext:
	cmp.b		vch_status_(a0),d7
	bne.s		.rskip

	move.b		d6,vch_status_(a0)

	move.l		a0,(a1)+
;	addq.w		#1,d1
	addq.w		#4,d1

.rskip:	move.l		d0,a0
	move.l		vch_puoanext(a0),d0
	bne.s		.rnext

	move.w		d1,(a2)

.rdone:


	.endif


*-------------------------------------------------------*
*	channel cleanup
*-------------------------------------------------------*




	move.b		_g_serverlocks(pc),d7
	bne		.no_clean

	move.w		_g_remove_channel_count(pc),d7
	beq		.no_clean

	lea		_g_remove_channels(pc),a0
;	move.w		d7,d0
;	add.w		d0,d0
;	add.w		d0,d0
;	add.w		d0,a0
	add.w		d7,a0

	lea		_g_active_channels(pc),a1

	lea		_g_channel_freepool,a2

	move.w		_g_channel_freepool_count,d1
;	move.w		d1,d0
;	add.w		d0,d0
;	add.w		d0,d0
;	add.w		d0,a2
	add.w		d1,a2

;	subq.w		#1,d7
;	subq.w		#4,d7

.cnext:	move.l		-(a0),a6

	move.w		vch_handle(a6),d0
	bmi.s		.no_handle
	add.w		d0,d0
	add.w		d0,d0
	lea		(a1,d0.w),a3
	cmp.l		(a3),a6
	bne.s		.orphan				; careful not to unlink orphan handles
	clr.l		(a3)
.orphan:

.no_handle:

	move.l		a6,(a2)+
	addq.w		#4,d1

	move.w		vch_mplexmap(a6),d0
	bmi.s		.no_multiplex

	; locate proxy for this mapping

	; todo: use table of ptrs or store ptr
;	lea		_g_multiplex_proxies,a5
;	mulu.w		#vch_size_,d0
;	add.w		d0,a5
	add.w		d0,d0
	add.w		d0,d0
	lea		_g_multiplex_proxy_index(pc),a5
	move.l		(a5,d0.w),a5
	move.l		vch_pmplexsent(a5),a3

	subq.b		#1,vch_mplexcnt(a3)		; proxy->p_mplex_sentinel->mplex_count--;
	bne.s		.mplex_not_empty

.mplex_empty:

	; unlink proxy (a5) from priority list
	move.l		vch_paprev(a5),a3
	move.l		vch_panext(a5),a4
	move.l		a4,vch_panext(a3)
	move.l		a3,vch_paprev(a4)

.mplex_not_empty:

.no_multiplex:

	; unlink channel (a6) from priority or mplex list
	move.l		vch_paprev(a6),a3
	move.l		vch_panext(a6),a4
	move.l		a4,vch_panext(a3)
	move.l		a3,vch_paprev(a4)

	; unlink channel (a6) from unordered active list
	move.l		vch_puoaprev(a6),a3
	move.l		vch_puoanext(a6),a4
	move.l		a4,vch_puoanext(a3)
	move.l		a3,vch_puoaprev(a4)

;	dbra		d7,.cnext
	subq.w		#4,d7
	bne.s		.cnext

.clean_done:

	move.w		d1,_g_channel_freepool_count

	clr.w		_g_remove_channel_count

.no_clean:



*-------------------------------------------------------*
*	channel selection
*-------------------------------------------------------*

	.if		(use_rasters)
	move.w		#$050,$ffff8240.w
	.endif

;	lea		_g_combine_ctrlvol_notevel,a1
;	lea		_g_combine_adsrenv_mixvol,a2

	move.l		_g_front_prio+vch_panext(pc),d0
	lea		_g_active_physical(pc),a3
	moveq		#0,d3

.no_act0:
	move.l		d0,a0
	move.l		vch_panext(a0),d0
	beq.s		.pad_fill
.get_phy0:


;	tst.b		vch_status_(a0)
;	bne.s		.no_act0
;	dovolume
	move.l		a0,(a3)+
	addq.w		#1,d3

.no_act1:
	move.l		d0,a0
	move.l		vch_panext(a0),d0
	beq.s		.pad_fill
.get_phy1:


;	tst.b		vch_status_(a0)
;	bne.s		.no_act1
;	dovolume
	move.l		a0,(a3)+
	addq.w		#1,d3

.no_act2:


	move.l		d0,a0
	move.l		vch_panext(a0),d0
	beq.s		.pad_fill
.get_phy2:


;	tst.b		vch_status_(a0)
;	bne.s		.no_act2
;	dovolume
	move.l		a0,(a3)+
	addq.w		#1,d3

;	all 3 YM channels filled

	bra.s		.no_pad

;	fill any unused YM channels with NULL

.pad_fill:
	moveq		#3-1,d2
	sub.w		d3,d2
	bmi.s		.no_pad
	moveq		#0,d0
.pad:	move.l		d0,(a3)+
	dbra		d2,.pad

.no_pad:

.no_chn:



*-------------------------------------------------------*
*	channel prioritization
*-------------------------------------------------------*

	.if		(use_rasters)
	move.w		#$707,$ffff8240.w
	.endif


	lea		_g_ymselected+(4*2)(pc),a2

;	initialize YM channel selection

	moveq		#0,d0
	move.l		d0,(a2)				; g_ymselected[2] = 0;
	move.l		d0,-(a2)			; g_ymselected[1] = 0;
	move.l		d0,-(a2)			; g_ymselected[0] = 0;

;	initialize stack

	move.l		d0,-(sp)

;	map sticky channels


;	moveq		#3-1,d7

.macro	prio

.map\~:	move.l		(a0)+,d1			; curr = g_active_physical[pc];
	beq.s		.cont
	move.l		d1,a1				; curr

	move.l		vch_pmplexsent(a1),d1		; curr->sent
	beq.s		.norm\~

	move.l		d1,a1				; sent
	move.l		vch_panext(a1),a1		; sent->anext

.norm\~:
	move.w		vch_ymchlast(a1),d1
	bpl.s		.stky\~

	move.l		a1,-(sp)

	; todo: can expand this for up to 3 iters
	bra.s		.esc\~

.stky\~:
	move.w		d1,vch_ymch(a1)			; curr->ymch = sticky;
	move.l		a1,(a2,d1.w)			; g_ymselected[sticky] = curr;
.esc\~:
	.endm


	lea		_g_active_physical(pc),a0
	prio
	prio
	prio

.cont:

;	fill remaining channels as available

.macro	ymfill

	move.l		(sp)+,d1			; while (0 != (curr = *(--stacked)))
	beq.s		.done
.look\~:
	tst.l		-(a0)
	bne.s		.look\~
.free\~:
	move.l		d1,a1
	move.l		a1,(a0)
	move.l		a0,d0
	sub.l		a2,d0
	move.w		d0,vch_ymch(a1)

	.endm

	lea		_g_ymselected+(4*3)(pc),a0
	ymfill
	ymfill
	ymfill
	addq.l		#4,sp
.done:


.locked:
*-------------------------------------------------------*

	.if		(audio_standalone_timer)

	or.w		#$0700,sr
	and.w		#$f5ff,sr
	subq.w		#1,_audioframe_sema
*-------------------------------------------------------*
	movem.l		(sp)+,d0-d7/a0-a6
	.if		(use_rasters)
;	not.w		$ffff8240.w
	move.w		(sp)+,$ffff8240.w
	.endif
	rte

*-------------------------------------------------------*
.service_busy:
*-------------------------------------------------------*
	addq.w		#1,_audioframe_collisions
	movem.l		(sp)+,d0-d7/a0-a6
;	bclr.b		#4,isrb.w
	bclr.b		#5,isrb.w
	.if		(use_rasters)
;	not.w		$ffff8240.w
	move.w		(sp)+,$ffff8240.w
	.endif
	rte

	.else

	.if		(use_rasters)
	move.w		(sp)+,$ffff8240.w
	.endif

	subq.w		#1,_audioframe_sema
	rts

	.endif







_subframe_event_68k:
	movem.l		d2/a2,-(sp)

	move.l		_g_front_uoactive+vch_puoanext(pc),a0

	move.l		vch_puoanext(a0),d0
	beq		.evdone

	lea		_g_bmm_channel_controller_pbend(pc),a1
	lea		_g_bmm_channel_controller_volume(pc),a2

.evnext:
	tst.b		vch_status_(a0)			; status_active
	bne.s		.no_act


;	todo: better do this at YM output? only 3 channels max

	moveq		#0,d1
	move.b		vch_chn(a0),d1
	bmi.s		.no_chn

	move.b		(a2,d1.w),vch_cvol(a0)		; cvol

	add.b		d1,d1

	move.w		(a1,d1.w),vch_pbend(a0)

;	move.w		(a1,d1.w),d2			; cpbend
;
;	cmp.w		vch_pbend(a0),d2
;	beq.s		.no_chg
;	move.w		d2,vch_pbend(a0)
;	move.b		vch_note(a0),d1
;	lsl.w		#5,d1
;	add.w		d2,d1
;
;
;.no_chg:


.no_chn:

;	better do this at ym output

;	move.b		vch_note(a0),d1
;	lsl.w		#5,d1
;	add.w		vch_pbend(a0),d1
;	move.w		d1,vch_finenote(a0)


;	tst.b		vch_noteon(a0)
;	bne.s		.still_on
;	tst.w		vch_penv(a0)
;	bne.s		.unlock_sustain
;	move.b		#1,vch_status_(a0)
;	bra.s		.cnt
;.unlock_sustain:
;	clr.l		vch_psustain(a0)
;.cnt:
;.still_on:

.no_act:

	; next vchannel

	move.l		d0,a0
	move.l		vch_puoanext(a0),d0
	bne.s		.evnext


.evdone:

	movem.l		(sp)+,d2/a2
	rts




.macro	nextev
;	bra		.stream_next
	subq.w		#1,.sp_eventcount(sp)
	beq		.stream_exhausted
	ext.l		d0
	beq		.stream_continue
	bra		.stream_gotduration
	.endm

*-------------------------------------------------------*
_bmm_event_feeder_68k:
*-------------------------------------------------------*
;			.abs
*-------------------------------------------------------*
;.savea6:		ds.l	1
;.save:			ds.l	10
;.return:		ds.l	1
*-------------------------------------------------------*
*-------------------------------------------------------*
;.frame_:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.sp_duration:		ds.w	1
.sp_eventcount:		ds.w	1
.sp_evprocessed:	ds.l	1
.sp_evtarget:		ds.l	1
.sp_freepoolcount:	ds.w	1
.sp_pad_:		ds.w	1
.sp_frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*

;	not.w		$ffff8240.w

*-------------------------------------------------------*
;	movem.l		d2-d7/a2-a5,-(sp)
;	link		a6,#-.frame_
*-------------------------------------------------------*
;	move.l		a6,-(sp)
	lea		-.sp_frame_(sp),sp
*-------------------------------------------------------*
;	move.w		_g_bmmstream_eventcount,d0
;	beq		.no_events
;	move.w		d0,.sp_eventcount(sp)
	move.w		_g_channel_freepool_count,.sp_freepoolcount(sp)
	move.w		_g_bmmstream_eventcount,.sp_eventcount(sp)
	move.l		_ev_processed,.sp_evprocessed(sp)
	move.l		_ev_target,.sp_evtarget(sp)
	move.l		_g_bmmstream_pos,a0
*-------------------------------------------------------*



;		ev_target += samples_per_frame;
;
;		while (g_bmmstream_eventcount && (ev_processed < ev_target))
;		{
;			s32 duration = bmm_event_feeder_68k();
;			if ((duration == 0) && g_bmmstream_loop)
;			{
;				api_channel_cleanup();
;				dprintf("song: loop!\n");
;
;				duration = bmm_event_feeder_68k();
;			}
;
;			ev_processed += duration;
;
;			eventdirty = true;
;		}

	moveq		#ms_per_frame,d0
	add.l		d0,.sp_evtarget(sp)

	move.l		.sp_evprocessed(sp),d0
	cmp.l		.sp_evtarget(sp),d0
	bge		.frame_complete

	lea		_g_front_prio(pc),a5
*-------------------------------------------------------*
.frame_continue:
*-------------------------------------------------------*

;	clr.w		.sp_duration(sp)

*-------------------------------------------------------*
.stream_continue:
*-------------------------------------------------------*
	.if		(use_rasters)
	not.w		$ffff8240.w
	.endif

	; terminate if our event is last in this burst (i.e. nonzero time delay before next event)

;	tst.w		.sp_duration(sp)
;	bne		.stream_complete

	; fetch...

;	move.w		(a0)+,.sp_duration(sp)
	move.w		(a0)+,d0

	moveq		#0,d1
	move.b		(a0),d1				; chn[4]:ctrl[4]
	addq		#4,a0

	move.w		d1,d7
	add.w		d7,d7
	add.w		d7,d7
	jmp		.proc_fixups(pc,d7.w)
.proc_fixups:
	.rept		256
	bra.w		*
	.endr

*-------------------------------------------------------*
.proc_ctrl:
*-------------------------------------------------------*
	lsr.w		#4,d1

	move.l		a0,a1

;	moveq		#0,d4
	move.b		-(a1),d4

	bne.s		.f_pitch

;	add.w		d4,d4
;	jmp		.jctrl(pc,d4.w)
;
;.jctrl:	bra.s		.f_veloc
;	bra.s		.f_pitch
;	bra		.stream_next

.f_veloc:

	move.b		-(a1),d3
	lsr.b		#7-5,d3
	lea		_g_bmm_channel_controller_volume(pc),a1
	move.b		d3,(a1,d1.w)

;	bra		.stream_next
	nextev

.f_pitch:

	moveq		#0,d3
	move.b		-(a1),d3

	sub.w		#64,d3

;	muls.w		#(pbend_renorm>>1),d3
	move.w		d3,d6				; *(pbend_renorm>>1) = *6
	add.w		d6,d6
	add.w		d6,d3
	add.w		d3,d3

	add.w		d1,d1
	lea		_g_bmm_channel_controller_pbend(pc),a1
	move.w		d3,(a1,d1.w)

;	bra		.stream_next
	nextev


*-------------------------------------------------------*
.proc_percon:
*-------------------------------------------------------*

;	moveq		#0,d4
;	move.b		-1(a0),d4

	move.w		#mperc_last_+midi_mperc_start_,d6
	sub.b		-1(a0),d6			; ymi

	add.w		d6,d6
	add.w		d6,d6
	lea		_g_yminstrument_index(pc),a2
	move.l		(a2,d6.w),a2

	move.l		ins_pwav(a2),d7
	beq		.stream_next


	lea		_g_sampleE(pc),a1

	tst.w		(a1)
	beq.s		.perc_on

	move.b		_g_samplePrio(pc),d6
	cmp.b		ins_prio+1(a2),d6
	blt		.stream_next

.perc_on:

	moveq		#0,d6
	move.w		ins_samplelen(a2),d6
	add.l		d7,d6
	and.w		#-2,d6
;	move.l		d6,_g_sampleE
	move.l		d6,(a1)+

	addq.l		#1,d7
	and.w		#-2,d7
;	move.l		d7,_g_sampleB
	move.l		d7,(a1)+


	move.b		ins_prio+1(a2),_g_samplePrio

;	bra		.stream_next
	nextev

*-------------------------------------------------------*
.proc_percoff:
*-------------------------------------------------------*
	moveq		#0,d4
	move.b		-1(a0),d4

	move.w		#mperc_last_+midi_mperc_start_,d6
	sub.w		d4,d6				; ymi

	lea		_g_yminstrument_index(pc),a2
	add.w		d6,d6
	add.w		d6,d6
	move.l		(a2,d6.w),a2

	move.b		_g_samplePrio(pc),d6
	cmp.b		ins_prio+1(a2),d6
	blt		.stream_next

	move.b		#EP_Background,_g_samplePrio

;	bra		.stream_next
	nextev

*-------------------------------------------------------*
.proc_velchange:
*-------------------------------------------------------*

	lsr.w		#4,d1


;	do we even generate lone velocity changes as events?

;	bra.s		*


;	todo: testing for disabled channels shouldn't be needed if convertor strips them in the first place

;	lea		_g_bmm_channel_multiplex_map,a1
;	move.b		(a1,d1.w),d5			; mpmap
;	cmp.b		#8,d5				; channel is not mapped / disabled at runtime - ignore
;	bge		.stream_next

	; note control

	moveq		#0,d4
	move.b		-1(a0),d4

;	todo: test for zero note shouldn't be required at runtime

;	beq.s		*
;	beq		.stream_next			; note=0 is invalid (except for controller data)

	lea		_g_bmm_channel_octave_map(pc),a1
	add.b		(a1,d1.w),d4

;	handle management required for runtime SFX control and to track note-on state for note@midichannel polyphonic mapping

	move.w		d1,d6
	lsl.w		#7,d6
	or.b		d4,d6
;	add.w		#max_sfx_handles,d6

	add.w		d6,d6
	add.w		d6,d6
	lea		_g_active_channels+(max_sfx_handles*4)(pc),a1
	move.l		(a1,d6.w),d6			; curr

	; can't note-off if channel has been removed early, or note-on failed to allocate

;	beq.s		*
	beq		.stream_next

	; can't note-off if note has completed / channel inactive

	move.l		d6,a6
;	tst.b		vch_status_(a6)
;	bne.s		*
;	bne		.stream_next

	move.b		-2(a0),vch_vol(a6)

;	bra		.stream_next
	nextev


*-------------------------------------------------------*
.proc_noteoff:
*-------------------------------------------------------*

	lsr.w		#4,d1

;	todo: testing for disabled channels shouldn't be needed if convertor strips them in the first place

;	lea		_g_bmm_channel_multiplex_map,a1
;	move.b		(a1,d1.w),d5			; mpmap
;	cmp.b		#8,d5				; channel is not mapped / disabled at runtime - ignore
;	bge		.stream_next

	; note control

	moveq		#0,d4
	move.b		-1(a0),d4

;	todo: test for zero note shouldn't be required at runtime

;	beq.s		*
;	beq		.stream_next			; note=0 is invalid (except for controller data)

	lea		_g_bmm_channel_octave_map(pc),a1
	add.b		(a1,d1.w),d4

;	handle management required for runtime SFX control and to track note-on state for note@midichannel polyphonic mapping

	move.w		d1,d6
	lsl.w		#7,d6
	or.b		d4,d6
;	add.w		#max_sfx_handles,d6

	add.w		d6,d6
	add.w		d6,d6
	lea		_g_active_channels+(max_sfx_handles*4)(pc),a1
	move.l		(a1,d6.w),d6			; curr

	; can't note-off if channel has been removed early, or note-on failed to allocate

;	beq.s		*
	beq		.stream_next

	; can't note-off if note has completed / channel inactive

	move.l		d6,a6
;	tst.b		vch_status_(a6)
;	bne.s		*
;	bne		.stream_next

	; can't note-off if already off (but still active)

;	tst.b		vch_noteon(a6)
;	beq.s		*
;	beq		.stream_next

	sf		vch_noteon(a6)

	.if		(0)

	; if note has no envelope, it can be stopped immediately
	; otherwise it goes into [R]elease mode

	tst.w		vch_penv(a6)
	bne.s		.unlock_sustain

;	move.b		#1,vch_status_(a6)		; status_complete

	lea		_g_remove_channel_count(pc),a3
	move.w		(a3),d6
	lea		_g_remove_channels(pc),a4
	move.l		a6,(a4,d6.w)
	addq.w		#4,d6
	move.w		d6,(a3)

	st		vch_status_(a6)


	.endif

.unlock_sustain:

	clr.l		vch_psustain(a6)


	; can't de-prioritize note-off if multiplexing (taken care of by multiplexer)

;	tst.b		d5
;	lea		_g_bmm_channel_multiplex_map,a1
;	tst.b		(a1,d1.w)			; mpmap
	tst.w		vch_mplexmap(a6)
	bpl		.stream_next

	; unlink channel (a6)
	move.l		vch_paprev(a6),a3
	move.l		vch_panext(a6),a4
	move.l		a4,vch_panext(a3)
	move.l		a3,vch_paprev(a4)

	; relink channel (a6) at back
	lea		_g_back_prio(pc),a4
	move.l		vch_paprev(a4),a3
	move.l		a6,vch_panext(a3)
	move.l		a6,vch_paprev(a4)
	move.l		a3,vch_paprev(a6)
	move.l		a4,vch_panext(a6)

;	bra		.stream_next
	nextev


*-------------------------------------------------------*
.proc_noteon:
*-------------------------------------------------------*


.macro	vch_assign	mr1,mr2,mr3,mr4,mr5,mr6		;vch1, chn2*, ins3*, vel4, note5*, mpmap6

	move.b		\mr2,vch_chn(\mr1)

	move.w		\mr6,vch_mplexmap(\mr1)

	lea		_g_instrument_map(pc),a1
	move.b		(a1,\mr3.w),\mr3			; ymi = instrument_map[_instrument];
	move.w		\mr3,vch_ymi(\mr1)

	move.b		\mr5,vch_note(\mr1)

	moveq		#0,\mr5

	add.w		\mr3,\mr3
	add.w		\mr3,\mr3
	lea		_g_yminstrument_index(pc),a2
	move.l		(a2,\mr3.w),a2			; yminstrument_t *pins = &s_yminstrument_dict[ymi];

	move.l		\mr5,vch_psustain(\mr1)
	move.b		#15,vch_evol(\mr1)		; todo: check this

	move.l		ins_penvseq(a2),a3

	move.w		ins_envrel(a2),\mr3
	bmi.s		.nrel\~
	add.l		a3,\mr3
	move.l		\mr3,vch_psustain(\mr1)

.nrel\~:
	move.b		(a3)+,vch_setbuzz(\mr1)
	move.b		(a3),vch_evol(\mr1)

.nenv\~:
	move.l		a3,vch_penv(\mr1)

	move.b		\mr4,vch_vol(\mr1)
	move.b		#31,vch_cvol(\mr1)

	move.w		\mr5,vch_vibmag(\mr1)

	move.b		\mr5,vch_status_(\mr1)

	.endm


	lsr.w		#4,d1

;	todo: testing for disabled channels shouldn't be needed if convertor strips them in the first place

	lea		_g_bmm_channel_multiplex_map(pc),a1
	move.b		(a1,d1.w),d5			; mpmap
;	cmp.b		#8,d5				; channel is not mapped / disabled at runtime - ignore
;	bge		.stream_next
	ext.w		d5

;	cmp.b		#8,d5
;	beq.s		*

	; note control

	move.l		a0,a1

	moveq		#0,d4
	move.b		-(a1),d4

;	todo: test for zero note shouldn't be required at runtime

;	beq.s		*

;	beq		.stream_next			; note=0 is invalid (except for controller data)

	moveq		#0,d3
	move.b		-(a1),d3
	moveq		#0,d2
	move.b		-(a1),d2

	lea		_g_bmm_channel_octave_map(pc),a1
	add.b		(a1,d1.w),d4


;	handle management required for runtime SFX control and to track note-on state for note@midichannel polyphonic mapping

	move.w		d1,d6
	lsl.w		#7,d6
	or.b		d4,d6
;	add.w		#max_sfx_handles,d6

	add.w		d6,d6
	add.w		d6,d6
	lea		_g_active_channels+(max_sfx_handles*4)(pc),a1
	move.l		(a1,d6.w),d6			; curr
	beq		.channel_unmapped
	move.l		d6,a6

;	tst.b		vch_status_(a6)
;	bne		.channel_unmapped
;	bne.s		*

;	tst.b		vch_noteon(a6)
;	bne.s		*

.channel_mapped:

	st		vch_noteon(a6)

	vch_assign	a6,d1,d2,d3,d4,d5		; vch1, chn2*, ins3*, vel4, note5*, mpmap6

	add.w		d5,d5
	bmi.s		.mapped_on_no_proxy

	add.w		d5,d5
	lea		_g_multiplex_proxy_index(pc),a6
	move.l		(a6,d5.w),a6

.mapped_on_no_proxy:


	; unlink channel (a6)
	move.l		vch_paprev(a6),a3
	move.l		vch_panext(a6),a4
	move.l		a4,vch_panext(a3)
	move.l		a3,vch_paprev(a4)

	; relink channel (a6) at front
;	lea		_g_front_prio,a3
	move.l		vch_panext(a5),a4
	move.l		a6,vch_panext(a5)
	move.l		a6,vch_paprev(a4)
	move.l		a5,vch_paprev(a6)
	move.l		a4,vch_panext(a6)

;	bra		.stream_next
	nextev

.channel_unmapped:

	move.w		.sp_freepoolcount(sp),d6
	beq		.stream_next

	subq.w		#4,d6
	move.w		d6,.sp_freepoolcount(sp)

;	add.w		d6,d6
;	add.w		d6,d6
	lea		_g_channel_freepool,a1
	move.l		(a1,d6.w),a6			; curr

	st		vch_noteon(a6)

	move.w		d1,d6
	lsl.w		#7,d6
	or.b		d4,d6
	add.w		#max_sfx_handles,d6
	move.w		d6,vch_handle(a6)

	lea		_g_active_channels(pc),a1
	add.w		d6,d6
	add.w		d6,d6
	move.l		a6,(a1,d6.w)


	vch_assign	a6,d1,d2,d3,d4,d5		; vch1, chn2*, ins3*, vel4, note5*, mpmap6

	add.w		d5,d5
	bmi.s		.unmapped_on_no_mplex

.unmapped_on_mplex:

	add.w		d5,d5
	lea		_g_multiplex_proxy_index(pc),a1
	move.l		(a1,d5.w),a1			; pproxy [garbage, d5=32/mch=8]
	move.l		vch_pmplexsent(a1),a2		; psent

	tst.b		vch_mplexcnt(a2)
	bne.s		.prelinked

	; link proxy (a1) at front of prio list
;	lea		_g_front_prio,a3
	move.l		vch_panext(a5),a4
	move.l		a1,vch_panext(a5)
	move.l		a1,vch_paprev(a4)
	move.l		a5,vch_paprev(a1)
	move.l		a4,vch_panext(a1)

.prelinked:
	addq.b		#1,vch_mplexcnt(a2)


	; link channel (a6) at front of sentinel (a2)
;	move.l		a2,a3
	move.l		vch_panext(a2),a4
	move.l		a6,vch_panext(a2)
	move.l		a6,vch_paprev(a4)
	move.l		a2,vch_paprev(a6)
	move.l		a4,vch_panext(a6)

	bra.s		.unmapped_on_linkactive


.unmapped_on_no_mplex:

	; link channel (a6) at front of prio list
;	lea		_g_front_prio,a3
	move.l		vch_panext(a5),a4
	move.l		a6,vch_panext(a5)
	move.l		a6,vch_paprev(a4)
	move.l		a5,vch_paprev(a6)
	move.l		a4,vch_panext(a6)

;	bra.s		.unmapped_on_linkactive

.unmapped_on_linkactive:

	; link channel (a6) at front of prio list
	lea		_g_front_uoactive(pc),a3
	move.l		vch_puoanext(a3),a4
	move.l		a6,vch_puoanext(a3)
	move.l		a6,vch_puoaprev(a4)
	move.l		a3,vch_puoaprev(a6)
	move.l		a4,vch_puoanext(a6)

*-------------------------------------------------------*
.stream_next:
*-------------------------------------------------------*

	subq.w		#1,.sp_eventcount(sp)
;	bne		.stream_continue
	bne		.stream_remaining

*-------------------------------------------------------*
.stream_exhausted:
*-------------------------------------------------------*
	addq.w		#1,_g_looped

	tst.w		_g_bmmstream_loop
	beq.s		.stream_stop

*-------------------------------------------------------*
.stream_restart:
*-------------------------------------------------------*
	move.w		_g_bmmstream_restarteventcount,.sp_eventcount(sp)
	move.l		_g_bmmstream_restartpos,a0

	movem.l		d0-a6,-(sp)
	jsr		_api_channel_cleanup
	movem.l		(sp)+,d0-a6

;	prefer accurate frames - fetch from restartpos at the
;	risk of offsetting the next loop by some fraction of a frame
;	bra		.frame_continue

;	prefer consistent replay after loop/restart, at the cost
;	of dropping a small amount of time - fraction of a frame
	move.l		.sp_evtarget(sp),.sp_evprocessed(sp)
	bra		.stream_stop


*-------------------------------------------------------*
.stream_remaining:
*-------------------------------------------------------*

;	moveq		#0,d0
;	move.w		.sp_duration(sp),d0

	ext.l		d0
	beq		.stream_continue


;	bne.s		.no_loop
;	tst.w		_g_bmmstream_loop
;	beq.s		.no_loop

;	movem.l		d0-a6,-(sp)
;	jsr		_api_channel_cleanup
;	movem.l		(sp)+,d0-a6

;	bra		.frame_continue

;.no_loop:

*-------------------------------------------------------*
*	jump here after processing nonzero duration
*-------------------------------------------------------*
.stream_gotduration:
*-------------------------------------------------------*

	add.l		.sp_evprocessed(sp),d0
	move.l		d0,.sp_evprocessed(sp)

	cmp.l		.sp_evtarget(sp),d0
	blt		.frame_continue

*-------------------------------------------------------*
*	jump here if stream completed and no loop
*-------------------------------------------------------*
.stream_stop:
*-------------------------------------------------------*

	.if		(use_rasters)
	move.w		#$543,$ffff8240.w
	.endif

	movem.l		d0-d1/a0-a1,-(sp)
	jsr		_subframe_event_68k
	movem.l		(sp)+,d0-d1/a0-a1

*-------------------------------------------------------*
.frame_complete:
*-------------------------------------------------------*
	move.l		a0,_g_bmmstream_pos
	move.w		.sp_eventcount(sp),_g_bmmstream_eventcount
	move.l		.sp_evtarget(sp),_ev_target
	move.l		.sp_evprocessed(sp),_ev_processed
	move.w		.sp_freepoolcount(sp),_g_channel_freepool_count

*-------------------------------------------------------*

;	moveq		#0,d0
;	move.w		_g_bmmstate_duration,d0
;	move.w		.sp_duration(sp),d0
;	move.w		d0,_g_bmm_pending_samples

*-------------------------------------------------------*
.no_events:
*-------------------------------------------------------*
	lea		.sp_frame_(sp),sp
;	move.l		(sp)+,a6
*-------------------------------------------------------*
;	unlk		a6
;	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*

	.if		(use_rasters)
;	not.w		$ffff8240.w
	move.w		#$000,$ffff8240.w
	.endif

	rts



proc_noteon	=	.proc_noteon
proc_noteoff	=	.proc_noteoff
;proc_noteonoff	=	.proc_noteonoff
;proc_perconoff	=	.proc_perconoff
proc_percon	=	.proc_percon
proc_percoff	=	.proc_percoff
proc_velchange	=	.proc_velchange
proc_ctrl	=	.proc_ctrl
proc_none	=	.stream_next

proc_fixups	=	.proc_fixups


	.if		(0)
*-------------------------------------------------------*
_assign_bmm_channel_68k:
*-------------------------------------------------------*
			rsreset
*-------------------------------------------------------*
.savea6:		rs.l	1
.save:			rs.l	10
.return:		rs.l	1
*-------------------------------------------------------*
.pvch:			rs.l	1
.chn:			rs.l	1
.mpmap:			rs.l	1
.ins:			rs.l	1
.vel:			rs.l	1
.note:			rs.l	1
*-------------------------------------------------------*
.frame_:		rs.l	0
*-------------------------------------------------------*

*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
*-------------------------------------------------------*
	move.l		.pvch(a6),a4			; _virt_channel

	moveq		#0,d2
	move.b		.chn+3(a6),d2			; _virt_channel->chn = _bmmchannel;
	move.b		d2,vch_chn(a4)

	move.w		.mpmap+2(a6),vch_mplexmap(a4)

	moveq		#0,d0
	move.b		.ins+3(a6),d0
	lea		_g_instrument_map(pc),a1
	move.b		(a1,d0.w),d0			; ymi = instrument_map[_instrument];
	move.w		d0,vch_ymi(a4)

	move.b		.note+3(a6),d1
	bgt.s		.npos
	add.b		#12,d1
.npos:	move.b		d1,vch_note(a4)

	moveq		#0,d1

;	lea		_g_yminstrument_dict,a2
;	mulu.w		#ins_size_,d0
;	add.w		d0,a2				; yminstrument_t *pins = &s_yminstrument_dict[ymi];

	lea		_g_yminstrument_index(pc),a2
	add.w		d0,d0
	add.w		d0,d0
	move.l		(a2,d0.w),a2

	lea		_g_bmm_channel_controller_pbend(pc),a1
	add.w		d2,d2
	move.w		(a1,d2.w),vch_pbend(a4)

	move.l		d1,vch_psustain(a4)
	move.b		#15,vch_evol(a4)		; todo: check this

	move.l		ins_penvseq(a2),d2
	beq.s		.nenv

;	caution: d0 upper cleared
	move.w		ins_envrel(a2),d0
	bmi.s		.nrel
	add.l		d2,d0
	move.l		d0,vch_psustain(a4)

.nrel:	move.l		d2,a3
	move.b		(a3),vch_evol(a4)

.nenv:	move.l		d2,vch_penv(a4)

	move.b		.vel+3(a6),vch_vol(a4)
	move.b		#31,vch_cvol(a4)

	move.w		d1,vch_vibmag(a4)

	move.b		d1,vch_status_(a4)
*-------------------------------------------------------*
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts

	.endif



.macro	brafixup	v1,v2,v3
	move.l		#\v1-2,\v3
	sub.l		\v2,\v3
	move.w		\v3,2(\v2)
	addq		#4,\v2
	.endm

*-------------------------------------------------------*
_fixup_procedures_68k:
*-------------------------------------------------------*
	movem.l		d0-d7/a0-a6,-(sp)

	lea		proc_fixups(pc),a0
	moveq		#0,d7
.loop:
	; split index into chn / ctrl

	moveq		#$f,d0
	and.w		d7,d0				; ctrl
	move.w		d7,d1
	lsr.w		#4,d1				; chn


	; percussion channel has priority over controllers and notes

	cmp.w		#(10-1),d1
	bne.s		.no_perc

	; bypass any percussion events which arent note-on/note-off, since
	; controllers and volume changes don't affect our percussion

	moveq		#$8,d2				; bypass percussion controllers
	and.w		d0,d2
	bne		.no_event

	moveq		#$6,d6				; bypass velocity changes only, leaving note on/off events
	and.w		d0,d6
	beq		.no_event

;	brafixup	proc_perconoff,a0,d6
;	bra.s		.fixed

	btst		#1,d0
	beq.s		.perc_off
.perc_on:
	brafixup	proc_percon,a0,d6
	bra.s		.fixed
.perc_off:
	brafixup	proc_percoff,a0,d6
	bra.s		.fixed

.no_perc:


	; controller commands have priority over notes

	moveq		#$8,d2
	and.w		d0,d2
	beq.s		.no_ctrl

	brafixup	proc_ctrl,a0,d6

	bra.s		.fixed
.no_ctrl:


	; note on/off events

	moveq		#$6,d2
	and.w		d0,d2
	beq.s		.no_note

;	brafixup	proc_noteonoff,a0,d6
;	bra.s		.fixed

	btst		#1,d0
	beq.s		.note_off
.note_on:
	brafixup	proc_noteon,a0,d6
	bra.s		.fixed
.note_off:
	brafixup	proc_noteoff,a0,d6
	bra.s		.fixed

.no_note:

	; velocity changes only (for persisting note-on states)

	moveq		#$1,d2
	and.w		d0,d2
	beq.s		.no_vel

;	bra		*

	brafixup	proc_velchange,a0,d6

	bra.s		.fixed
.no_vel:


.no_event:

	; event is not meaningful - map it out (such cases should be prevented by convertor - we can track them here)

	brafixup	proc_none,a0,d6

.fixed:	addq.w		#1,d7
	cmp.w		#256,d7
	bne		.loop

	movem.l		(sp)+,d0-d7/a0-a6
	rts

*-------------------------------------------------------*
_STE_WriteLMC1992:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
;.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.data:			ds.l	1
*-------------------------------------------------------*
.frame_:		
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
;	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
*-------------------------------------------------------*
	lea		$ffff8924.w,a1
	lea		$ffff8922.w,a0
	move.w		#$0400,d0
	or.w		.data+2(a6),d0
	move.w		#$7ff,(a1)
	move.w		(a0),d1
	move.w		d0,(a0)
.wait:	cmp.w		(a0),d1
	bne.s		.wait
*-------------------------------------------------------*
	unlk		a6
;	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts


*-------------------------------------------------------*
_g_serverlocks:
*-------------------------------------------------------*
	dc.b		0
	even

*-------------------------------------------------------*
_g_remove_tide:
*-------------------------------------------------------*
	dc.l		_g_remove_stack

	dc.l		0
*-------------------------------------------------------*
_g_remove_stack:
*-------------------------------------------------------*
	dcb.l		max_virtual_channels,0

*-------------------------------------------------------*
_g_sampleE:
*-------------------------------------------------------*
	dc.l		0
*-------------------------------------------------------*
_g_sampleB:
*-------------------------------------------------------*
	dc.l		0
*-------------------------------------------------------*
_g_samplePrio:
*-------------------------------------------------------*
	dc.l		0	; top byte used

*-------------------------------------------------------*
_g_ymselected:
*-------------------------------------------------------*
	dcb.l		max_physical_channels,0

*-------------------------------------------------------*
_g_active_physical:
*-------------------------------------------------------*
	dcb.l		max_physical_channels,0

*-------------------------------------------------------*
_g_remove_channel_count:
*-------------------------------------------------------*
	dc.w		0

*-------------------------------------------------------*
_g_remove_channels:
*-------------------------------------------------------*
	dcb.l		max_virtual_channels,0

*-------------------------------------------------------*
_g_yminstrument_index:
*-------------------------------------------------------*
	dcb.l		256,0

*-------------------------------------------------------*
_g_bmm_channel_controller_pbend:
*-------------------------------------------------------*
	dcb.w		max_midi_channels,0

*-------------------------------------------------------*
_g_bmm_channel_controller_volume:
*-------------------------------------------------------*
	dcb.b		max_midi_channels,0

*-------------------------------------------------------*
_g_bmm_channel_octave_map:
*-------------------------------------------------------*
	dcb.b		max_midi_channels,0

*-------------------------------------------------------*
_g_active_channels:
*-------------------------------------------------------*
	dcb.l		max_handles,0

*-------------------------------------------------------*
_g_instrument_map:
*-------------------------------------------------------*
	dcb.b		256,0

*-------------------------------------------------------*
_g_multiplex_proxy_index:
*-------------------------------------------------------*
	dcb.l		3,0

*-------------------------------------------------------*
_g_multiplex_sentinel_index:
*-------------------------------------------------------*
	dcb.l		3,0

*-------------------------------------------------------*
_g_bmm_channel_multiplex_map:
*-------------------------------------------------------*
	dcb.b		16,0

*-------------------------------------------------------*
_g_front_prio:
*-------------------------------------------------------*
	dcb.b		vch_size_,0
*-------------------------------------------------------*
_g_back_prio:
*-------------------------------------------------------*
	dcb.b		vch_size_,0

*-------------------------------------------------------*
_g_front_uoactive:
*-------------------------------------------------------*
	dcb.b		vch_size_,0

_mplex_cycles:
	.if		(configure_50hz)
	dc.b		(6+1)/4				; 2
	dc.b		(5+1)/4				; 3
	dc.b		(4+1)/4				; 4
	dc.b		(4+1)/4				; 5
	dc.b		(4+1)/4				; 6
	dc.b		(3+1)/4				; 7
	dc.b		(3+1)/4				; 8
	dc.b		(3+1)/4				; 9
	dc.b		(3+1)/4				; 10
	dc.b		(3+1)/4				; 11
	dc.b		(3+1)/4				; 12
	dc.b		(2+1)/4				; 13
	dc.b		(2+1)/4				; 14
	dc.b		(2+1)/4				; 15
	dc.b		(2+1)/4				; 16
	.else
	dc.b		6				; 2
	dc.b		5				; 3
	dc.b		4				; 4
	dc.b		4				; 5
	dc.b		4				; 6
	dc.b		3				; 7
	dc.b		3				; 8
	dc.b		3				; 9
	dc.b		3				; 10
	dc.b		3				; 11
	dc.b		3				; 12
	dc.b		2				; 13
	dc.b		2				; 14
	dc.b		2				; 15
	dc.b		2				; 16
	.endif

*-------------------------------------------------------*
			.data
*-------------------------------------------------------*

_ym_regs:

_ym_reg_resetABC:
	dc.l		$0F000000			; freq A,B,C resets aliased here
	dc.l		$0F000000

_ym_reg_freqA:
	dc.l		$00000000			; freq A,B,C
	dc.l		$01000000
_ym_reg_freqB:
	dc.l		$02000000
	dc.l		$03000000
_ym_reg_freqC:
	dc.l		$04000000
	dc.l		$05000000

_ym_reg_levelA:
	dc.l		$08000000			; level A,B,C
_ym_reg_levelB:
	dc.l		$09000000
_ym_reg_levelC:
	dc.l		$0A000000

_ym_reg_freqE:
	dc.l		$0B000000			; freq E
	dc.l		$0C000000

_ym_reg_shapeE:
	dc.l		$0D000F00			; shape E

;_ym_reg_freqN:
;	dc.l		$06000000			; freq N

_ym_reg_mixer:
	dc.l		$07000000			; mixer



*-------------------------------------------------------*

_g_ym_freqtable:
	incbin		'data/YMFRQ.DAT'
_g_ym_vibtable:
	incbin		'data/YMVIB.DAT'
_g_combine_ctrlvol_notevel:
	incbin		'data/YMV1.DAT'
_g_combine_adsrenv_mixvol:
	incbin		'data/YMV2.DAT'
_g_sampletranslation:
	incbin		'data/YMST.DAT'

*-------------------------------------------------------*
			.bss
*-------------------------------------------------------*

_audioframe_sema:	ds.w		1
_audioframe_collisions:	ds.w		1
_testcounter:		ds.l		1
sys_timerc:		ds.l		1
sys_timerd:		ds.l		1

_g_soundtick:		ds.b		1
			even

*-------------------------------------------------------*
			.text
*-------------------------------------------------------*

	.endif		;^^defined ENABLE_AGT_AUDIO
