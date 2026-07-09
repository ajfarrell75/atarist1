usingVASM		equ	0

************************************************************
;// write destinations
UDRegWriteToScreen	equ $fa4100
UDRegWriteToScreenPos	equ $fa4102
UDRegWriteToFontMemory	equ $fa4104
UDRegWriteToFontAdr	equ $fa4106
UDRegWriteToFontAdrIncr	equ $fa4108

UDRegWriteToReg1	equ $fa4200

************************************************************
UDStateFPGA		equ $fa4006
UDCartridgeRamPos	equ $fa4038


************************************************************
;// debug screen vga registers
UDRegVBLFlag		equ $fa400c
UDRegVGAX		equ $fa400e
UDRegVGAY		equ $fa4010


UDCartDetect1		equ $fa4012
UDCartDetect2		equ $fa4014
UDFPGAVersion		equ $fa4016

UDScrCharsX		equ 40
UDScrCharsY		equ 32
UDScrCharsY80		equ 25

************************************************************
;// drive image registers

;// drive image write destinations
UDRegWriteToSectorLow	equ	$fa4400
UDRegWriteToSectorHigh	equ	$fa4402
UDRegWriteToSectorCount	equ	$fa4404
UDRegWriteToSectorBuffer equ	$fa4406

UDRegWriteToHdvBackup	equ	$fa4408

UDDISectorNumber	equ	$fa4018
UDDIState		equ	$fa401a
UDDIStateBusy		equ	1

UDDIErrorFlags		equ	$fa401c
UDDISectorSize		equ	$fa401e
UDDIReadSectorFlag	equ	$fa4020

UDDIFatSize		equ	$fa4024
UDDIDirSize		equ	$fa4026
UDDICurSector		equ	$fa4028

UDDIDriveNumber		equ	$fa402a

UDDIHdvBpb		equ $fa402c
UDDIHdvRW		equ $fa4030
UDDIHdvMedia		equ $fa4034
UDKeyStroke		equ $fa4054

UIDISectorBuffer	equ	$fa8000
************************************************************
;// needed fpga state machine values
UDStateWaitForAtari	equ	27

************************************************************
;// system stuff
hdv_bpb		equ	$472
hdv_rw		equ	$476
hdv_media	equ	$47e
drvbits 	equ	$4c2
buflData	equ	$4b2
buflFatDir	equ	$4b6
membot		equ	$432
memtop		equ	$436
phystop		equ	$42e
bootdev		equ	$446
