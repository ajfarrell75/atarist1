*-------------------------------------------------------*
* macro:	tburn/tburnr 
*		burns time, in nops, wastes minimal space
*-------------------------------------------------------*
* usage:	tburns	<nops>
*		tburnrs	<nops>,<scratch_Dn>
*		tburnrr	<nops>,<scratch_Dx>,<scratch_Dy>
*		tburnm	<nops>,<addr.w>
*-------------------------------------------------------*
* tburns:	uses (sp)
* tburnrs:	uses reg & (sp), occupies less space in most cases
* tburnrr:	uses regs, occupies less space in longer burns
* tburnm:	does not touch registers or (sp)
*-------------------------------------------------------*

		; (n * 5) + 3 [5 words]
.macro		tburnr_nx5_o3	num,sregwa,sregwb
!		move.w		#(\num-1),\sregwa	; 8
.lp\~:		lsr.w		#2,\sregwb		; 10
		dbf		\sregwa,.lp\~		; 10/14
		.endm

;		; (n * 11) + 8
;.macro		tburn_nx11_o8	num
;		subq.w		#2,sp			; 8
;		move.w		#(\num * 2),(sp)	; 12
;		; odd iters = ends even
;		; even iters = ends odd
;.lp\~:		subq.w		#1,(sp)
;		bne.s		.lp\~ 			; 22/20(22:)
;		cmp.w		d0,a0			; 6(:4)		
;		addq.w		#2,sp       		; 8
;   		.endm

		; (n * 6) + 4 [5 words]
.macro		tburns_nx6_o4	num
		move.w		#\num,-(sp)		; 12
.lp\~:		subq.w		#1,(sp)
		bne.s		.lp\~ 			; 24/20
		addq.w		#2,sp       		; 8(-4)
   		.endm

		; (n * 7) + 3 [6 words]
.macro		tburnm_nx7_o3	num,addr
		move.w		#\num,/addr.w		; 16(-4)
.lp\~:		subq.w		#1,/addr.w
		bne.s		.lp\~ 			; 26/24 (28/24)
   		.endm

*-------------------------------------------------------*

;		version for zero-free-registers & stack

.macro		tburns		nops

.nrem		set		\nops

		; l:nx6(+4)
		.if		(.nrem >= (6+4))
.bdy64\~	set		(.nrem - 4)
.crs64\~	set		(.bdy64\~ / 6)
		tburns_nx6_o4	.crs64\~
.don64\~	set		(.crs64\~ * 6) + 4
.nrem		set		(.nrem - .don64\~)
		.endif


******** UNUSED <
		.if		0

		; l:nx5(+8+3)
		.if		(.nrem >= (5+(8+3)))
		move.w		d0,-(sp)		; 8 (2n)
		move.w		d7,-(sp)		; 8 (2n)
.nrem		set		(.nrem - 8)		; save/restore (8n)
.bdy5\~		set		(.nrem - 3)
.crs5\~		set		(.bdy5\~ / 5)
		tburn_nx5_o3	.crs5\~,d0,d7
.don5\~		set		(.crs5\~ * 5) + 3
.nrem		set		(.nrem - .don5\~)
		move.w		(sp)+,d7		; 8 (2n)
		move.w		(sp)+,d0		; 8 (2n)
		.endif
		
		.endif
******** UNUSED >

		; r:nx5
		.if		(.nrem >= 5)
.crs5\~		set		(.nrem / 5)
		.rept		.crs5\~
		move.l		(sp),(sp)
		.endr
.don5\~		set		(.crs5\~ * 5)
.nrem		set		(.nrem - .don5\~)
		.endif

		; r:nx3
		.if		(.nrem >= 3)
.crs3\~		set		(.nrem / 3)
		.rept		.crs3\~
		tst.l		(sp)
		.endr
.don3\~		set		(.crs3\~ * 3)
.nrem		set		(.nrem - .don3\~)
		.endif
	
		; r:nx2
		.if		(.nrem >= 2)
.crs2\~		set		(.nrem / 2)
		.rept		.crs2\~
		tst.w		(sp)
		.endr
.don2\~		set		(.crs2\~ * 2)
.nrem		set		(.nrem - .don2\~)
		.endif	
	
		; r:nx1
		.if		.nrem
		.rept		.nrem
		nop
		.endr
		.endif
		
		.endm

*-------------------------------------------------------*

;		version for 1-free-register & stack

.macro		tburnrs		nops,sreg

.nrem		set		\nops

		; note: prefer nx34 for short runs, takes less space
		; 5 words here vs (1+n) for nx34
		.if		(.nrem >= ((34*5)+1))

		; l:nx6(+4)
		.if		(.nrem >= (6+4))
.bdy64\~	set		(.nrem - 4)
.crs64\~	set		(.bdy64\~ / 6)
		tburns_nx6_o4	.crs64\~
.don64\~	set		(.crs64\~ * 6) + 4
.nrem		set		(.nrem - .don64\~)
		.endif
		
		.endif
		

******** UNUSED <
		.if		0
		
		; l:nx5(+8+3) [7 words]
		;.if		(.nrem >= (5+(4+3)))
		; note: prefer nx34 for short runs, takes less space
		; 7 words here vs (1+n) for nx34
		.if		(.nrem >= ((34*7)+1))
		move.w		d7,-(sp)		; 8 (2n)
.nrem		set		(.nrem - 4)		; save/restore (8n)
.bdy5\~		set		(.nrem - 3)
.crs5\~		set		(.bdy5\~ / 5)
		tburn_nx5_o3	.crs5\~,\sreg,d7
.don5\~		set		(.crs5\~ * 5) + 3
.nrem		set		(.nrem - .don5\~)
		move.w		(sp)+,d7		; 8 (2n)
		.endif

		.endif
******** UNUSED >

		; r:nx34(+1) [n+1 words]
		.if		(.nrem >= (34+1))
.nrem		set		(.nrem - 1)
		moveq		#1,\sreg
.bdy34\~	set		.nrem
.crs34\~	set		(.bdy34\~ / 34)	
		.rept		.crs34\~
		divu.w		\sreg,\sreg
		.endr
.don34\~	set		(.crs34\~ * 34)
.nrem		set		(.nrem - .don34\~)
		.endif
		
		; r:nx6 [n words]
		.if		(.nrem >= 6)
.crs6\~		set		(.nrem / 6)
		.rept		.crs6\~
		lsl.l		#8,\sreg
		.endr
.don6\~		set		(.crs6\~ * 6)
.nrem		set		(.nrem - .don6\~)
		.endif
		
		; r:nx5 [n words]
		.if		(.nrem >= 5)
.crs5\~		set		(.nrem / 5)
		.rept		.crs5\~
		move.l		(sp),(sp)
		.endr
.don5\~		set		(.crs5\~ * 5)
.nrem		set		(.nrem - .don5\~)
		.endif

		; r:nx4 [n words]
		.if		(.nrem >= 4)
.crs4\~		set		(.nrem / 4)
		.rept		.crs4\~
		lsl.l		#4,\sreg
		.endr
.don4\~		set		(.crs4\~ * 4)
.nrem		set		(.nrem - .don4\~)
		.endif

		; r:nx3 [n words]
		.if		(.nrem >= 3)
.crs3\~		set		(.nrem / 3)
		.rept		.crs3\~
		tst.l		(sp)
		.endr
.don3\~		set		(.crs3\~ * 3)
.nrem		set		(.nrem - .don3\~)
		.endif
	
		; r:nx2 [n words]
		.if		(.nrem >= 2)
.crs2\~		set		(.nrem / 2)
		.rept		.crs2\~
		tst.w		(sp)
		.endr
.don2\~		set		(.crs2\~ * 2)
.nrem		set		(.nrem - .don2\~)
		.endif	
		
		; r:nx1 [n words]
		.if		.nrem
		.rept		.nrem
		nop
		.endr
		.endif
		
		.endm

*-------------------------------------------------------*

;		version for 2-free-registers, no stack

.macro		tburnrr		nops,sreg,sreg2

.nrem		set		\nops

		; l:nx5(+8+3) [7 words]
		;.if		(.nrem >= (5+3))
		; note: prefer nx34 for short runs, takes less space
		; 7 words here vs (1+n) for nx34
		.if		(.nrem >= ((34*7)+1))
.bdy5\~		set		(.nrem - 3)
.crs5\~		set		(.bdy5\~ / 5)
		tburn_nx5_o3	.crs5\~,\sreg,\sreg2
.don5\~		set		(.crs5\~ * 5) + 3
.nrem		set		(.nrem - .don5\~)
		.endif
		
		; r:nx34(+1) [n+1 words]
		.if		(.nrem >= (34+1))
.nrem		set		(.nrem - 1)
		moveq		#1,\sreg
.bdy34\~	set		.nrem
.crs34\~	set		(.bdy34\~ / 34)	
		.rept		.crs34\~
		divu.w		\sreg,\sreg
		.endr
.don34\~	set		(.crs34\~ * 34)
.nrem		set		(.nrem - .don34\~)
		.endif

		; r:nx6 [n words]
		.if		(.nrem >= 6)
.crs6\~		set		(.nrem / 6)
		.rept		.crs6\~
		lsl.l		#8,\sreg
		.endr
.don6\~		set		(.crs6\~ * 6)
.nrem		set		(.nrem - .don6\~)
		.endif
		
		; r:nx5 [n words]
		.if		(.nrem >= 5)
.crs5\~		set		(.nrem / 5)
		.rept		.crs5\~
		move.l		(sp),(sp)
		.endr
.don5\~		set		(.crs5\~ * 5)
.nrem		set		(.nrem - .don5\~)
		.endif

		; r:nx4 [n words]
		.if		(.nrem >= 4)
.crs4\~		set		(.nrem / 4)
		.rept		.crs4\~
		lsl.l		#4,\sreg
		.endr
.don4\~		set		(.crs4\~ * 4)
.nrem		set		(.nrem - .don4\~)
		.endif

		; r:nx3 [n words]
		.if		(.nrem >= 3)
.crs3\~		set		(.nrem / 3)
		.rept		.crs3\~
		tst.l		(sp)
		.endr
.don3\~		set		(.crs3\~ * 3)
.nrem		set		(.nrem - .don3\~)
		.endif
	
		; r:nx2 [n words]
		.if		(.nrem >= 2)
.crs2\~		set		(.nrem / 2)
		.rept		.crs2\~
		tst.w		(sp)
		.endr
.don2\~		set		(.crs2\~ * 2)
.nrem		set		(.nrem - .don2\~)
		.endif	
	
		; r:nx1 [n words]
		.if		.nrem
		.rept		.nrem
		nop
		.endr
		.endif
		
		.endm

*-------------------------------------------------------*

;		version for zero-free-registers, via memory

.macro		tburnm		nops,addr

.nrem		set		\nops

		; l:nx7(+3)
		.if		(.nrem >= (7+3))
.bdy73\~	set		(.nrem - 3)
.crs73\~	set		(.bdy73\~ / 7)
		tburn_nx7_o3	.crs73\~,/addr
.don73\~	set		(.crs73\~ * 7) + 3
.nrem		set		(.nrem - .don73\~)
		.endif

		; r:nx5
		.if		(.nrem >= 5)
.crs5\~		set		(.nrem / 5)
		.rept		.crs5\~
		move.l		(sp),(sp)
		.endr
.don5\~		set		(.crs5\~ * 5)
.nrem		set		(.nrem - .don5\~)
		.endif

		; r:nx3
		.if		(.nrem >= 3)
.crs3\~		set		(.nrem / 3)
		.rept		.crs3\~
		tst.l		(sp)
		.endr
.don3\~		set		(.crs3\~ * 3)
.nrem		set		(.nrem - .don3\~)
		.endif
	
		; r:nx2
		.if		(.nrem >= 2)
.crs2\~		set		(.nrem / 2)
		.rept		.crs2\~
		tst.w		(sp)
		.endr
.don2\~		set		(.crs2\~ * 2)
.nrem		set		(.nrem - .don2\~)
		.endif	
	
		; r:nx1
		.if		.nrem
		.rept		.nrem
		nop
		.endr
		.endif
		
		.endm

*-------------------------------------------------------*
