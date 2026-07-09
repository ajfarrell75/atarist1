;//****************************************************************************
* Project:	ultraDev
* Module:	UDResetter
* Code:		ultra
* Created:	28.12.2019
* Notes:	If you do not have a reset cable installed you
*			this source to preform a reset when needed
*			How does it work?
*			just call UDResetCheck periodically for example
*			in your vbl routine.
*			if the fpga state machine needs a reset the source
*			will do that for you.
*			Keep in mind you need to do the first reset and if your atari
*			crashes you need to reset it too;)
* Modified:
* Changes:
*
* (c) 2019 cream
;//****************************************************************************

UDResetCheck:
	cmp	#UDStateWaitForAtari,UDStateFPGA
	bne.s	.UDResetCheckNo
	lea	$4.w,a0
	move.l	(a0),a0
	jmp	(a0)
.UDResetCheckNo:
	rts