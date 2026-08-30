; haribote-ipl
; TAB=4

CYLS	EQU		9

		ORG		0x7c00			; location which this program located in memory

; FAT12 Format
		JMP		entry
		DB		0x90
		DB		"HARIBOTE"		; Bootsector name
		DW		512				; size of 1 sector
		DB		1				; size of cluster (1 sector/cluster)
		DW		1				; numbers of reserved sectors
		DB		2				; numbers of FAT tables
		DW		224				; numbers of Entry for root directory
		DW		2880				; total numbers of sector in disk
		DB		0xf0				; midea type
		DW		9				; numbers of sector for one FAT table
		DW		18				; numbers of sector for one track
		DW		2				; numbers of head
		DD		0				; we don't use partition
		DD		2880				; drive size
		DB		0, 0, 0x29			; what..?
		DD		0xffffffff			; volume serial number
		DB		"HARIBOTEOS "			; name of disk
		DB		"FAT12   "			; name of format(8bytes)
		RESB	18				; remain 18bytes

; Program body

entry:
		MOV		AX, 0		; Register initialization
		MOV		SS, AX
		MOV		SP, 0x7c00
		MOV		DS, AX

		MOV 	AX, 0x0820
		MOV		ES, AX
		MOV		CH, 0				; cylinder 0
		MOV 	DH, 0				; head 0
		MOV 	CL, 2				; sector 2
		MOV		BX, 18*2*CYLS-1		; total sector numbers
		CALL	readfast

		; Run haribote.sys
		MOV 	BYTE [0x0ff0], CYLS
		JMP 	0xc200

error:
		MOV 	AX, 0
		MOV 	ES, AX
		MOV		SI, msg
putloop:
		MOV		AL, [SI]
		ADD		SI, 1
		CMP		AL, 0
		JE		fin
		MOV		AH, 0x0e		; character print
		MOV		BX, 15			; collor code
		INT		0x10			; video BIOS
		JMP 	putloop

fin:
		HLT					; halt CPU
		JMP 	fin

msg:
		DB		0x0a, 0x0a		; two line break
		DB		"load error"
		DB		0x0a
		DB 		0

readfast:
		; ES: read address, CH: Cylinder, DH: Head, CL: Sector, BX: Numbers of read sectors
		MOV 	AX, ES		; calculate max AL from the ES
		SHL 	AX, 3		; AH = AX / 32
		AND 	AH, 0x7f	; AH = AH % 128
		MOV 	AL, 128		; AL = 128 - AH
		SUB 	AL, AH

		MOV 	AH, BL		; store the result of MAX AL in AH
		CMP 	BH, 0		; if (BH != 0) { AH = 18; }
		JE 		.skip1
		MOV 	AH, 18
.skip1:
		CMP 	AL, AH		; if (AL > AH) { AL = AH; }
		JBE 	.skip2
		MOV 	AL, AH
.skip2:
		MOV 	AH, 19		; calculate Max AL from CL
		SUB 	AH, CL		; AH = 19 - CL
		CMP 	AL, AH		; if (AL > AH) { AL = AH; }
		JBE 	.skip3
		MOV 	AL, AH
.skip3:
		PUSH	BX
		MOV 	SI, 0		; count fail
	
retry:
		MOV 	AH, 0x02	; AH = 0x02 -> read disk
		MOV 	BX, 0
		MOV		DL, 0x00	; A drive
		PUSH	ES
		PUSH	DX
		PUSH	CX
		PUSH	AX
		INT		0x13		; call DISK BIOS
		JNC		next		; No error, jump to fin
		ADD		SI, 1
		CMP		SI, 5		; if SI >= 5, then go to error
		JAE 	error
		MOV		AH, 0x00
		MOV		DL, 0x00	; A drive
		INT		0x13		; drive reset
		POP 	AX
		POP 	CX
		POP 	DX
		POP 	ES
		JMP		retry
next:
		POP 	AX
		POP 	CX
		POP 	DX
		POP 	BX			; store ES in BX
		SHR 	BX, 5		; BX : 16bytes -> 512bytes
		MOV 	AH, 0
		ADD 	BX, AX		; BX += AL
		SHL 	BX, 5		; BX : 512bytes -> 16bytes
		MOV 	ES, BX		; ES += AL * 0x20
		POP 	BX
		SUB 	BX, AX
		JZ  	.ret
		ADD 	CL, AL		; CL += AL
		CMP 	CL, 18		; if (CL <= 18)
		JBE 	readfast	; jump to readfast
		MOV 	CL, 1
		ADD 	DH, 1
		CMP 	DH, 2		; if (DH < 2)
		JB  	readfast	; jump to readfast
		MOV 	DH, 0
		ADD 	CH, 1
		JMP 	readfast
.ret:
		RET

		RESB 	0x7dfe-$
		
		DB  	0x55, 0xaa
