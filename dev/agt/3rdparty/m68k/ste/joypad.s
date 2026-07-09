;
; Jagpad Routine
; reading Jagpad A & B
; from some code found on the web
;
; (c) 2007 by Simon Sunnyboy / Paradize
; minor adaptations by dml
;
; Returns :-
; pad0 = c1numpad,c1midbuts,c1firebuts,c1joypad
; pad1 = c2numpad,c2midbuts,c2firebuts,c2joypad
; contain bitwise representation in the form
; 1 4 7 * 3 6 9 # 2 5 8 0 o p c b a r l d u
; where o=option, p=pause, a/b/c=fire, r/l/d/u=directions. and rest are the phone pad.
;

hw_jpadbut	=	$ffff9200
hw_jpadjoy	=	$ffff9202
	
	.globl 		_AGT_SampleJoyPads

	.globl 		_joypad0
	.globl 		_joypad1


; jagpad return values after call

	data
	
_jpads:

_joypad0:               
	dc.l 		0
_joypad1:               
	dc.l 		0

; local variables
c1numpad:
	dc.l		0
c2numpad:           
	dc.l		0

	text
	
_AGT_SampleJoyPads:
	movem.l		d0-a2,-(sp)
	clr.l   	d0
	clr.l   	d1
	clr.l   	d2
	clr.l   	d3
	clr.l   	d4
	clr.l   	d5
	clr.l   	d6
	clr.l   	d7
	move.l 		d0,a0
	lea     	c1numpad,a1
	lea     	c2numpad,a2
	move.l  	d0,(a1)
	move.l  	d0,(a2)

* read row 1

	move.w		#$ffee,d0
	bsr    	 	.read_controller
	
	move.w  	d1,d0
	andi.w  	#1,d0
	move.b  	d0,d3
	        	
	move.w  	d1,d0
	andi.w  	#2,d0
	lsr.w   	#1,d0
	move.b  	d0,d4
	        	
	move.w  	d1,d0
	andi.w  	#4,d0
	lsr.w   	#2,d0
	move.b  	d0,d6
	        	
	move.w  	d1,d0
	andi.w  	#8,d0
	lsr.w   	#3,d0
	move.b  	d0,d7
	        	
	move.w  	d2,d0
	andi.w  	#$0f00,d0
	lsr.w   	#8,d0
	move.b  	d0,d5
	        	
	move.w  	d2,d0
	andi.w  	#$f000,d0
	lsr.w   	#8,d0
	lsr.w   	#4,d0
	movea.l 	d0,a0

* read row 2

        move.w  	#$ffdd,d0
        bsr     	.read_controller
                	
        move.w  	d1,d0
        andi.w  	#2,d0
        or.b    	d0,d4
                	
        move.w  	d1,d0
        andi.w  	#8,d0
        lsr.w   	#2,d0
        or.b    	d0,d7
                	
        move.w  	d2,d0
        andi.w  	#$0f00,d0
        lsr.w   	#8,d0
        move.w  	d0,(a1)
                	
        move.w  	d2,d0
        andi.w  	#$f000,d0
        lsr.w   	#8,d0
        lsr.w   	#4,d0
        move.w  	d0,(a2)

* read row 3

        move.w  	#$ffbb,d0
        bsr     	.read_controller
                	
        move.w  	d1,d0
        andi.w  	#2,d0
        lsl.w   	#1,d0
        or.b    	d0,d4
                	
        move.w  	d1,d0
        andi.w  	#8,d0
        lsr.w   	#1,d0
        or.b    	d0,d7
                	
        move.w  	d2,d0
        andi.w  	#$0f00,d0
        lsr.w   	#4,d0
        or.l    	d0,(a1)
                	
        move.w  	d2,d0
        andi.w  	#$f000,d0
        lsr.w   	#8,d0
        or.l    	d0,(a2)

* read row 4

        move.w  	#$ff77,d0
        bsr     	.read_controller
                	
        move.w  	d1,d0
        andi.w  	#2,d0
        or.b    	d0,d3
                	
        move.w  	d1,d0
        andi.w  	#8,d0
        lsr.w   	#2,d0
        or.b    	d0,d6
                	
        move.w  	d2,d0
        andi.w  	#$0f00,d0
        or.l    	d0,(a1)
                	
        move.w  	d2,d0
        andi.w  	#$f000,d0
        lsr.w   	#4,d0
        or.l    	d0,(a2)
                	
        lsl.l   	#7,d3
        lsl.l   	#4,d4
        or.l    	d4,d3
        or.l    	d5,d3
                	
        lsl.l   	#7,d6
        lsl.l   	#4,d7
        or.l    	d7,d6
        move.l  	a0,d2
        or.l    	d2,d6
                	
        move.l  	(a1),d2         ; d2 = c1numpad
        lsl.l   	#5,d2
        move.l  	(a2),d5         ; d5 = c2numpad
        lsl.l   	#5,d5
                	
        or.l    	d2,d3
        or.l    	d5,d6
                	
        move.l  	d3,d4
        andi.l  	#$01e00000,d4
        lsr.l   	#4,d4
        andi.l  	#$0001ffff,d3
        or.l    	d4,d3
                	
        move.l  	d6,d4
        andi.l  	#$01e00000,d4
        lsr.l   	#4,d4
        andi.l  	#$0001ffff,d6
        or.l    	d4,d6

	lea     	_jpads,a0
	move.l  	d3,(a0)+
	move.l  	d6,(a0)+
	
	movem.l		(sp)+,d0-a2
	rts

.read_controller:
	move.w  	d0,hw_jpadjoy.w
	move.w  	hw_jpadbut.w,d1
	move.w  	hw_jpadjoy.w,d2	
	andi.w  	#$000f,d1
	andi.w  	#$ff00,d2
	not.w   	d1
	not.w   	d2
	rts
	
