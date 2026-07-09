;///////////////////////////////////////////////////////////////
; pretty version of console text using XiA's xprn text printer
;///////////////////////////////////////////////////////////////

; dml: fit existing display metrics
max_screen_lines	=	256
screen_linebytes	=	160	;dst_linewid

; xprn_font_file_header_struct:
  .abs
  xprn_font_file_stringid:    ds.l 1
  xprn_font_file_chartabpos:  ds.l 1
  xprn_font_file_ver:         ds.b 1
  xprn_font_file_numchars:    ds.b 1
  xprn_font_file_charheight:  ds.b 1
  xprn_font_file_numplanes:   ds.b 1
  xprn_font_file_width:       ds.b 1
  xprn_font_file_inited:      ds.b 1
  .68000

;///////////////////////////////////////////////////////////////
;// xprn - font and string output engine

_xprn_print_string: .globl _xprn_print_string

; dml: temporary haxor for fixed monospaced 8x12 console font
  lea xprn_small_square_shadow_mono,a1

; In: a0.l - pointer to null-terminated string (supports $0d as newline)
;     a1.l - pointer to font file
;     a2.l - screen pointer, adjusted for x and y (i e where to start rendering chars)
;            value must be complete with bitplane offset, must be on an even 16-pixel
;            boundary in X
;     d0.w - column width in pixels (it's your responsibility this value doesn't clash
;            with the screen line width)
;     d1.w - width of screen line in bytes
;     d2.b - extra line height in pixels (0 = the font's own height, 3 = 3 extra scanlines etc)
;     d3.b - number of bitplanes in destination screen
;     d4.b - use blitter? 1 = yes, 0 = no

  ;break
  tst.b xprn_font_file_inited(a1)
  bne .inited

  bsr xprn_print_init_font

.inited:
  ; choose display routine
  ; proportional font?
  tst.b xprn_font_file_width(a1)
  beq xprn_print_string_prop_font
  cmp.b #8,xprn_font_file_width(a1)
  beq xprn_print_string_monospace_font_8px
  rts


xprn_print_string_monospace_font_8px:
  cmp.b #1,xprn_font_file_numplanes(a1)
  beq xprn_print_string_monospace_font_8px_1bpl
  cmp.b #2,xprn_font_file_numplanes(a1)
  beq xprn_print_string_monospace_font_8px_2bpl
  rts


xprn_print_string_monospace_font_8px_2bpl:
  move.l a2,xprn_line_start
  clr.l d7 ; char
  clr.l d5 ; X
  lsr.w #3,d0 ; d0 is now column width in 8px increments
  add.w d0,d0
  move.l xprn_font_file_chartabpos(a1),a6 ; a6 points to chartabpos
  lea xprn_x_pos_table_8px_width,a4
  ; Fetch char
.newchar:
  move.b (a0)+,d7 ; d7.b
  beq .alldone
  cmp.b #09,d7
  bne .nottab
  move.b #32,d7
.nottab:
  cmp.b #$0d,d7
  beq .newline
  and.w #$ff,d7
  sub.b #32,d7 ; adjust for no chars below ASCII 32
  add.w d7,d7
  add.w d7,d7
  move.l (a6,d7.w),a5
  ; Draw char, one bitplane, 8px wide mono
  add.w (a4,d5.w),a2
  move.l a2,-(sp)
  clr.l d6
  move.b xprn_font_file_charheight(a1),d6
  sub.b #1,d6
.onescanline:
    move.b (a5)+,(a2)
    move.b (a5)+,2(a2)
    add.w d1,a2
  dbra d6,.onescanline
  move.l (sp)+,a2
  add.w #2,d5
  cmp.w d0,d5
  bge .newline
  bra .newchar

.newline:
  tst.w d5 ; is x already 0?
  bne .noexit1
  ; if x==0 and last char was a $0d, done
  cmp.b #$0d,d7
  beq .newchar
.noexit1:
  clr.l d5
  clr.l d6
  move.l xprn_line_start,a2
  move.b xprn_font_file_charheight(a1),d6
  add.b d2,d6
  mulu d1,d6
  add.w d6,a2
  move.l a2,xprn_line_start
  bra .newchar

.alldone:
  rts



xprn_print_string_monospace_font_8px_1bpl:
  move.l a2,xprn_line_start
  clr.l d7 ; char
  clr.l d5 ; X
  lsr.w #3,d0 ; d0 is now column width in 8px increments
  add.w d0,d0
  move.l xprn_font_file_chartabpos(a1),a6 ; a6 points to chartabpos
  lea xprn_x_pos_table_8px_width,a4
  ; Fetch char
.newchar:
  move.b (a0)+,d7 ; d7.b
  beq .alldone
  cmp.b #09,d7
  bne .nottab
  move.b #32,d7
.nottab:
  cmp.b #$0d,d7
  beq .newline
  and.w #$ff,d7
  sub.b #32,d7 ; adjust for no chars below ASCII 32
  add.w d7,d7
  add.w d7,d7
  move.l (a6,d7.w),a5
  ; Draw char, one bitplane, 8px wide mono
  add.w (a4,d5.w),a2
  move.l a2,-(sp)
  clr.l d6
  move.b xprn_font_file_charheight(a1),d6
  sub.b #1,d6
.onescanline:
    move.b (a5)+,(a2)
    add.w d1,a2
  dbra d6,.onescanline
  move.l (sp)+,a2
  add.w #2,d5
  cmp.w d0,d5
  bge .newline
  bra .newchar

.newline:
  tst.w d5 ; is x already 0?
  bne .noexit1
  ; if x==0 and last char was a $0d, done
  cmp.b #$0d,d7
  beq .newchar
.noexit1:
  clr.l d5
  clr.l d6
  move.l xprn_line_start,a2
  move.b xprn_font_file_charheight(a1),d6
  add.b d2,d6
  mulu d1,d6
  add.w d6,a2
  move.l a2,xprn_line_start
  bra .newchar

.alldone:
  rts


xprn_print_string_prop_font:
.alldone:
  rts


xprn_line_start:  dc.l 0
xprn_x_pos_table_8px_width:
;  offs set 0
;  rept 20
;    dc.w offs
;    dc.w offs+1
;    offs set offs+8
;  endr
  dc.w 0,1
  rept 20
    dc.w 7,1
  endr


xprn_print_init_font:
	
; In: a1.l - pointer to font file
  tst.b xprn_font_file_inited(a1)
  bne .inited

  ; init stuffs
  move.l a1,d7 ; d7 is now start of file
  move.b #1,xprn_font_file_inited(a1)
  add.l d7,xprn_font_file_chartabpos(a1)
  move.l xprn_font_file_chartabpos(a1),a6 ; a6 is now start of charpostable
  clr.l d6
  move.b xprn_font_file_numchars(a1),d6
  subq #1,d6
.oneoffs:
    add.l d7,(a6)
    addq #4,a6
  dbra d6,.oneoffs

.inited:

; dml: temporary haxor - init ytable just for console (others are per-playfield!)
  bsr.s init_ytable

  rts


;// xprn - font and string output engine
;///////////////////////////////////////////////////////////////

init_ytable:
  lea ytable,a0
  clr.l d0
  move.l #max_screen_lines-1,d7
.loop:
    move.l d0,(a0)+
    add.l #screen_linebytes,d0
  dbra d7,.loop
  rts

;///////////////////////////////////////////////////////////////
  .data
;///////////////////////////////////////////////////////////////
	
xprn_small_square_shadow_mono:
  incbin "data/agtconsole.xprn"
  
;///////////////////////////////////////////////////////////////
  .bss
;///////////////////////////////////////////////////////////////
  	
ytable:
  ds.l max_screen_lines

;///////////////////////////////////////////////////////////////
  .text
;///////////////////////////////////////////////////////////////
