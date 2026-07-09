; nf_asmv.s - VBCC/Vasm NatFeats ASM detection code
;
; Copyright (c) 2014 by Eero Tamminen
; minor alterations by d.m.l
; 
; Mostly based on code from EmuTOS,
; Copyright (c) 2001-2013 by the EmuTOS development team
;
; This file is distributed under the GPL, version 2 or at your
; option any later version.  See doc/license.txt for details.

;	include		"rmac/bldconfig.inc"
	
;;
;; exported symbols
;;
        .globl	_nf_id
	.globl	_nf_call
        .globl	_detect_nf

;;
;; variables
;;
        .text

nf_version:
	dc.b  "NF_VERSION",0
	even

;;
;; code
;;
	.text

; NatFeats test
;
; Needs to be called from Supervisor mode,
; otherwise exception handler change bombs
_detect_nf:
;	ifd		bldcfg_cpu_clean
;	clr.l		d0
;	else
;	movem.l		d1-d7/a0-a6,-(sp)
	clr.l		d0		; assume no NatFeats available
	move.l		sp,a1
	move.l		$10.w,a0		; illegal vector
	move.l		#fail_nf,$10.w
	pea		nf_version
	subq.l		#4,sp
; Conflict with ColdFire instruction mvs.b d0,d1
	dc.w		$7300		; Jump to NATFEAT_ID
	tst.l		d0
	beq.s		fail_nf
	moveq		#1,d0		; NatFeats detected

fail_nf:
	move.l		a1,sp
	move.l		a0,$10.w	; illegal vector
;	movem.l		(sp)+,d1-d7/a0-a6
;	endc
	rts


; map native features to its ID
_nf_id:
	dc.w		$7300  ; Conflict with ColdFire instruction mvs.b d0,d1
	rts

; call native feature by its ID
_nf_call:
	dc.w		$7301  ; Conflict with ColdFire instruction mvs.b d1,d1
	rts
        
	.text
        