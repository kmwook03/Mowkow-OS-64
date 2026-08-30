[FORMAT "WCOFF"]
[INSTRSET "i486p"]
[BITS 32]
[FILE "api028.nas"]

		GLOBAL	_api_fwrite

[SECTION .text]

_api_fwrite:    ; void api_fwrite(char *buf, int maxsize, int fhandle);
	PUSH	EBX
	MOV		EDX, 28
	MOV		EBX, [ESP+8]			; buf
    MOV     ECX, [ESP+12]           ; maxsize
    MOV     EAX, [ESP+16]           ; fhandle
	INT		0x40
	POP		EBX
	RET    
