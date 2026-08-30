[FORMAT "WCOFF"]
[INSTRSET "i486p"]
[BITS 32]
[FILE "api003.nas"]

    GLOBAL  _api_putstr_len

[SECTION .text]

_api_putstr_len:	; void api_putstr_len(char *s, int l);
	PUSH	EBX
	MOV		EDX,3
	MOV		EBX,[ESP+ 8]	; s
	MOV		ECX,[ESP+12]	; l
	INT		0x40
	POP		EBX
	RET