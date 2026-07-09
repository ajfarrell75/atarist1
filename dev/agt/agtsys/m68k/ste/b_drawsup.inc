
; common sprite draw control for all sprite routines
;
; [word] draw_hitflash(entity): 
;
;	flags<mb------cccccccc>
;
;	flags: draw control flags 
;		m : 0 = colour-fill (colour 15), 1 = invisible / blink at 2vbl interval
;		b : 0 = normal background restore, 1 = no background restore
;	count: effect decay counter
;		0   : no effect / effect cancelled
;		!=0 : applies effect for remaining [count] frames	
;
; examples:
;	$0004	colour-fill for 4 frames
;	$8004	blink for 4 frames
;	$00ff	filled, continuous
;	$80ff	invisible, continuous
;	$4000	normal drawing, no background restore
;	


;	version for sprite methods which support LOP (blitter logic op) for colour-fill
;
.macro	sprite_ctrl	ent,lop,temp,flags,skip

	moveq		#3,\lop
	move.w		ent_draw_hitflash(\ent),\flags	; $zz00 / $zzxx
; count check - effect active?
	neg.b		\flags
	beq.s		.nhf\~
;	illegal
; effect active - handle decay count for all effects:
	move.b		\flags,\temp
; count of $FF means don't decay
	cmp.b		#1,\flags
	beq.s		.noc\~
	subx.b		\temp,\temp			; $zz00 / $zzFF
	add.b		\temp,ent_draw_hitflash+1(\ent)
; effect check:
.noc\~:	tst.w		\flags
	bpl.s		.yhf\~
; blink:
	btst		#1,\flags
	bne.s		.nhf\~
	bra		\skip	
.yhf\~:
; flash:
	moveq		#15,\lop	
.nhf\~:
	move.b		\lop,BLiTLOP.w

	.endm


;	version for sprite methods which do not support LOP
;
.macro	sprite_ctrl_nl	ent,temp,flags,skip

	move.w		ent_draw_hitflash(\ent),\flags	; $zz00 / $zzxx
; count check - effect active?
	neg.b		\flags
	beq.s		.nhf\~
; effect active - handle decay count for all effects:
	move.b		\flags,\temp
; count of $FF means don't decay
	cmp.b		#1,\flags
	beq.s		.noc\~
	subx.b		\temp,\temp			; $zz00 / $zzFF
	add.b		\temp,ent_draw_hitflash+1(\ent)
; effect check:
.noc\~:	;tst.w		\sr1
	;bpl.s		.yhf\~
; blink:
	btst		#1,\flags
	bne.s		.nhf\~
	bra		\skip	
.yhf\~:
; flash:	
.nhf\~:
	.endm


; sprite safety check: frame within available range

.macro	sprite_sc_frame	sps,frame,except
	.if		^^defined AGT_CONFIG_SAFETY_CHECKS
;	sanity check on frame index!
	cmp.w		sps_framecount(\sps),\frame
	.if		(within_entitydraw)
	bhs		\except
	.else
	blo.s		.frameok\~
.framefault\~:
	not.w		$ffff8240.w
	bra.s		.framefault\~
.frameok\~:
	.endif		;(within_entitydraw)
	.endif		;^^defined AGT_CONFIG_SAFETY_CHECKS
	.endm


; sprite frame lookup, includes dual-field indexing if used

.macro	sprite_spf	sps,frame,tmp,spf
	moveq		#1,\tmp
	and.b		sps_flags(\sps),\tmp			; 1 = double index
	beq.s		.no_dfm\~
	add.w		\frame,\frame				;	
	and.w		.sp_mfield(sp),\tmp			; 1 = field offset
	add.w		\tmp,\frame
.no_dfm\~:
	add.w		\frame,\frame				; todo: game layer can prescale this
	add.w		\frame,\frame
	move.l		sps_frameindex(\sps,\frame.w),\spf	; access sprite frame data
	.endm


;	sprite window cliptest

;.macro	sprite_cliptest	win,x_,y_,w_,h_,temp,reject,accept
;	
;	lea		_g_clipwin_gpp,\win
;	
;	cmp.w		(\win)+,\x_
;	bmi		\reject
;
;	cmp.w		(\win)+,\y_
;	bmi		\reject
;
;	move.w		\x_,\temp
;	add.w		\w_,\temp			; xr	
;	sub.w		(\win)+,\temp			; xr - cx2
;	bgt		\reject
;
;	move.w		\y_,\temp
;	add.w		\h_,\temp			; yr	
;	sub.w		(\win)+,\temp			; yr - cy2
;	ble		\accept
;
;	bra		\reject
;	
;	.endm



	; accept if range [A] fully within window [B]
	
.macro	guard_accept	win,p_,s_,temp1,temp2,accept	

	move.w		\p_,\temp1			; x
	sub.w		(\win)+,\temp1			; x-cx
	move.w		(\win)+,\temp2			; cw
	sub.w		\s_,\temp2			; cw-w
	cmp.w		\temp2,\temp1
	bcs		\accept

	.endm
	
	; reject if range [A] partly outside window [B]
	
.macro	guard_reject	win,p_,s_,temp1,temp2,reject	

	move.w		\p_,\temp1			; x
	sub.w		(\win)+,\temp1			; x-cx
	move.w		(\win)+,\temp2			; cw
	sub.w		\s_,\temp2			; cw-w
	cmp.w		\temp2,\temp1
	bcc		\reject

	.endm


	; accept if range [A] partly within window [B]
	
.macro	vis_accept	win,p_,s_,temp1,temp2,accept	

	move.w		\p_,\temp1			; x
	add.w		\s_,\temp1			; x+w
	sub.w		(\win)+,\temp1			; -cx
	move.w		(\win)+,\temp2			; cw
	add.w		\s_,\temp2			; cw+w
	cmp.w		\temp2,\temp1
	bcs		\accept

	.endm
	
	; accept if range [A] fully outside window [B]
	
.macro	vis_reject	win,p_,s_,temp1,temp2,reject	

	move.w		\p_,\temp1			; x
	add.w		\s_,\temp1			; x+w
	sub.w		(\win)+,\temp1			; -cx
	move.w		(\win)+,\temp2			; cw
	add.w		\s_,\temp2			; cw+w
	cmp.w		\temp2,\temp1
	bcc		\reject

	.endm
	


	