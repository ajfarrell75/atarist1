;-------------------------------------------------------------------------------
;  n2e_d.ash -- NRV2E decompression in 68000 assembly
;
;  This file is part of the UCL data compression library.
;
;  Copyright (C) 1996-2004 Markus Franz Xaver Johannes Oberhumer
;  All Rights Reserved.
;
;  The UCL library is free software; you can redistribute it and/or
;  modify it under the terms of the GNU General Public License as
;  published by the Free Software Foundation; either version 2 of
;  the License, or (at your option) any later version.
;
;  The UCL library is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with the UCL library; see the file COPYING.
;  If not, write to the Free Software Foundation, Inc.,
;  59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
;
;  Markus F.X.J. Oberhumer
;  <markus@oberhumer.com>
;  http://www.oberhumer.com/opensource/ucl/
;
;-------------------------------------------------------------------------------

; ------------- DECOMPRESSION -------------

; decompress from a0 to a1
;   note: must preserve d4 and a5-a7

;
; On entry:
;   a0  src pointer
;   a1  dest pointer
;
; On exit:
;   d1.l = 0x00008000
;   d2.l = 0
;
; Register usage:
;   a3  m_pos
;
;   d0  bit buffer
;   d1  m_off
;   d2  m_len
;   d5  last_m_off
;
;   d6  constant: -$500
;   d7  constant: 0
;
;
; Notes:
;   we have max_match = 65535, so we can use word arithmetics on d2
;   we have max_offset < 2**23, so we can use partial word arithmetics on d1
;

	.if		(^^defined AGT_CONFIG_PACKER_PK2E | ^^defined AGT_CONFIG_PACKER_ANY)


			.globl	_DePack2e_68k
			
*-------------------------------------------------------*
_DePack2e_68k:
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
.savea6:		ds.l	1
.save:			ds.l	10
.return:		ds.l	1
*-------------------------------------------------------*
.psrc:			ds.l	1
.pdst:			ds.l	1
*-------------------------------------------------------*
.frame_:
*-------------------------------------------------------*
			.text
*-------------------------------------------------------*
	movem.l		d2-d7/a2-a5,-(sp)
	link		a6,#-.frame_
	move.l		a6,-(sp)
*-------------------------------------------------------*
	move.l		.psrc(a6),a0
	move.l		.pdst(a6),a1
*-------------------------------------------------------*
	move.w		$ffff8240.w,-(sp)
	bsr		depack2e
	move.w		(sp)+,$ffff8240.w
*-------------------------------------------------------*
	move.l		(sp)+,a6
	unlk		a6
	movem.l		(sp)+,d2-d7/a2-a5
*-------------------------------------------------------*
	rts	
	
	
	
; ------------- constants & macros -------------
depack2e:
                moveq   #-$50,D6        ;   0xffffffb0
                lsl.w   #4,D6           ;   << 4

                moveq   #0,D7
                moveq   #-1,D5          ; last_off = -1

; init d0 with high bit set
;;move.b  #$80,d0               ; init d0.b for FILLBYTES
                moveq   #-128,D0        ; d0.b = $80
                bra.s   decompr_start


; ------------- DECOMPRESSION -------------

decompr_literal:
                move.b  (A0)+,(A1)+

decompr_start:
decompr_loop:
		addq.w	#8,$ffff8240.w
		
; optimization: carry is clear -> we know that bits are available
;   cost literal:   4 +  8 + 10
;   cost match:     4 + 10
;   cost fillbits:  4 +  8 +  8
                add.b   D0,D0
                bcc.s   decompr_match
                bne.s   decompr_literal
                move.b  (A0)+,D0
                addx.b  D0,D0
                bcs.s   decompr_literal


decompr_match:
                moveq   #1,D1
                moveq   #0,D2
decompr_l1:
                add.b   D0,D0
                bne.s   _l00
                move.b  (A0)+,D0
                addx.b  D0,D0
_l00:
                addx.w  D1,D1
; optimization: carry is clear -> we know that bits are available
;   cost loop continue:  4 + 10
;   cost loop break:    4 +  8 + 10
;   cost fillbits:      4 +  8 +  8
                add.b   D0,D0
                bcc.s   Lcon0
                bne.s   decompr_break1
                move.b  (A0)+,D0
                addx.b  D0,D0
                bcs.s   decompr_break1
Lcon0:
                subq.w  #1,D1
                add.b   D0,D0
                bne.s   _l01
                move.b  (A0)+,D0
                addx.b  D0,D0
_l01:
                addx.w  D1,D1
                bpl.s   decompr_l1
                bra.s   decompr_end
decompr_break1:
                subq.w  #3,D1
                bcs.s   decompr_prev_dist ; last m_off
                lsl.l   #8,D1
                move.b  (A0)+,D1
                not.l   D1
                asr.l   #1,D1
                bcc.s   decompr_get_mlen2

decompr_get_mlen1:
                add.b   D0,D0
                bne.s   _l02
                move.b  (A0)+,D0
                addx.b  D0,D0
_l02:
                addx.w  D2,D2
                bra.s   decompr_got_mlen
decompr_prev_dist:
                move.l  D5,D1
                add.b   D0,D0
                bne.s   _l03
                move.b  (A0)+,D0
                addx.b  D0,D0
_l03:
                bcs.s   decompr_get_mlen1
decompr_get_mlen2:
                addq.w  #1,D2
                add.b   D0,D0
                bne.s   _l04
                move.b  (A0)+,D0
                addx.b  D0,D0
_l04:
                bcs.s   decompr_get_mlen1

decompr_l2:
                add.b   D0,D0
                bne.s   _l05
                move.b  (A0)+,D0
                addx.b  D0,D0
_l05:
                addx.w  D2,D2
; optimization: carry is clear -> we know that bits are available
;   cost loop continue:  4 + 10
;   cost loop break:    4 +  8 + 10
;   cost fillbits:      4 +  8 +  8
                add.b   D0,D0
                bcc.s   decompr_l2
                bne.s   Lbreak0
                move.b  (A0)+,D0
                addx.b  D0,D0
                bcc.s   decompr_l2
Lbreak0:
                addq.w  #2,D2



decompr_got_mlen:
                move.l  D1,D5
                lea     0(A1,D1.l),A3

; must use sub as cmp doesn't affect the X flag
                sub.l   D6,D1
                addx.w  D7,D2

; TODO: partly unroll this loop; could use some magic with d7 for address
;       computations, then compute a nice `jmp yyy(pc,dx.w)'

;   cost for any m_len:   12 + 22 * (m_len - 1) + 4
;     38, 60, 82, 104, 126, 148, 170, 192, 214, 236
                move.b  (A3)+,(A1)+     ; 12
Lcopy0:         move.b  (A3)+,(A1)+     ; 12
                dbra    D2,Lcopy0       ; 10 / 14

                bra     decompr_loop
decompr_end:
                rts
; vi:ts=8:et
                ;END

		.endif ; (^^defined AGT_CONFIG_PACKER_PK2E | ^^defined AGT_CONFIG_PACKER_ANY)
