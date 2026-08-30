;   naskfunc
;   TAB=4

[FORMAT "WCOFF"]        ; obj make mode
[INSTRSET   "i486p"]
[BITS   32]             ; for 32-bit
[FILE   "naskfunc.nas"] ;   info for obj

	GLOBAL	_io_hlt, _io_cli, _io_sti, _io_stihlt
	GLOBAL	_io_in8,  _io_in16,  _io_in32
	GLOBAL	_io_out8, _io_out16, _io_out32
	GLOBAL	_io_load_eflags, _io_store_eflags
	GLOBAL	_load_gdtr, _load_idtr
	GLOBAL	_load_cr0, _store_cr0
	GLOBAL	_load_tr
	GLOBAL	_asm_inthandler20, _asm_inthandler21
    GLOBAL  _asm_inthandler26
	GLOBAL	_asm_inthandler27, _asm_inthandler2c
    GLOBAL  _asm_inthandler0c, _asm_inthandler0d
	GLOBAL	_asm_end_app, _memtest_sub
	GLOBAL	_farjmp, _farcall
	GLOBAL	_asm_hrb_api, _start_app
	EXTERN	_inthandler20, _inthandler21
    EXTERN  _inthandler26
	EXTERN	_inthandler27, _inthandler2c
    EXTERN  _inthandler0c, _inthandler0d
	EXTERN	_hrb_api

[SECTION    .text]

_io_hlt:                ; void io_hlt(void);
    HLT
    RET

_io_cli:                ; void io_cli(void);
    CLI
    RET

_io_sti:                ; void io_sti(void);
    STI
    RET

_io_stihlt:             ; void io_stihlt(void);
    STI
    HLT
    RET

_io_in8:                ; int io_in8(int port);
    MOV     EDX, [ESP+4]    ; port
    MOV     EAX, 0
    IN      AL, DX
    RET

_io_in16:               ; int io_in16(int port);
    MOV     EDX, [ESP+4]    ; port
    MOV     EAX, 0
    IN      AX, DX
    RET

_io_in32:
    MOV     EDX, [ESP+4]    ; port
    IN      EAX, DX
    RET

_io_out8:
    MOV     EDX, [ESP+4]    ; port
    MOV     AL, [ESP+8]     ; data
    OUT     DX, AL
    RET

_io_out16:
    MOV     EDX, [ESP+4]
    MOV     EAX, [ESP+8]
    OUT     DX, AX
    RET

_io_out32:
    MOV     EDX, [ESP+4]
    MOV     EAX, [ESP+8]
    OUT     DX, EAX
    RET

_io_load_eflags:
    PUSHFD                 ; PUSH EFLAGS
    POP     EAX
    RET

_io_store_eflags:
    MOV     EAX, [ESP+4]
    PUSH    EAX
    POPFD                   ; POP EFLAGS
    RET

_load_gdtr:
    MOV     AX, [ESP+4]         ; limit
    MOV     [ESP+6], AX
    LGDT    [ESP+6]
    RET

_load_idtr:
    MOV     AX, [ESP+4]         ; limit
    MOV     [ESP+6], AX
    LIDT    [ESP+6]
    RET

_load_cr0:
    MOV     EAX, CR0
    RET

_store_cr0:
    MOV     EAX, [ESP+4]
    MOV     CR0, EAX
    RET

_load_tr:
    LTR     [ESP+4]     ; tr
    RET

_asm_inthandler20:
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL	_inthandler20
	POP		EAX
	POPAD
	POP		DS
	POP		ES
	IRETD

_asm_inthandler21:
    PUSH    ES
    PUSH    DS
    PUSHAD
    MOV     EAX, ESP
    PUSH    EAX
    MOV     AX, SS
    MOV     DS, AX
    MOV     ES, AX
    CALL    _inthandler21
    POP     EAX
    POPAD
    POP     DS
    POP     ES
    IRETD

_asm_inthandler26:
    PUSH    ES
    PUSH    DS
    PUSHAD
    MOV     EAX, ESP
    PUSH    EAX
    MOV     AX, SS
    MOV     DS, AX
    MOV     ES, AX
    CALL    _inthandler26
    POP     EAX
    POPAD
    POP     DS
    POP     ES
    IRETD

_asm_inthandler27:
    PUSH    ES
    PUSH    DS
    PUSHAD
    MOV     EAX, ESP
    PUSH    EAX
    MOV     AX, SS
    MOV     DS, AX
    MOV     ES, AX
    CALL    _inthandler27
    POP     EAX
    POPAD
    POP     DS
    POP     ES
    IRETD

_asm_inthandler2c:
    PUSH    ES
    PUSH    DS
    PUSHAD
    MOV     EAX, ESP
    PUSH    EAX
    MOV     AX, SS
    MOV     DS, AX
    MOV     ES, AX
    CALL    _inthandler2c
    POP     EAX
    POPAD
    POP     DS
    POP     ES
    IRETD

_asm_inthandler0c:
	STI
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL	_inthandler0c
	CMP		EAX,0
	JNE		_asm_end_app
	POP		EAX
	POPAD
	POP		DS
	POP		ES
	ADD		ESP,4			
	IRETD

_asm_inthandler0d:
	STI
	PUSH	ES
	PUSH	DS
	PUSHAD
	MOV		EAX,ESP
	PUSH	EAX
	MOV		AX,SS
	MOV		DS,AX
	MOV		ES,AX
	CALL	_inthandler0d
	CMP		EAX,0			
	JNE		_asm_end_app	
	POP		EAX
	POPAD
	POP		DS
	POP		ES
	ADD		ESP,4			
	IRETD

_memtest_sub:   ; unsigned int memtest_sub(unsigned int start, unsigned int end);
    PUSH    EDI
    PUSH    ESI
    PUSH    EBX
    MOV     ESI, 0xaa55aa55         ; pat0 = 0xaa55aa55;
    MOV     EDI, 0x55aa55aa         ; pat1 = 0x55aa55aa;
    MOV     EAX, [ESP+12+4]         ; i = start;
mts_loop:
    MOV     EBX, EAX
    ADD     EBX, 0xffc              ; p = i + 0x0ffc;
    MOV     EDX, [EBX]              ; old = *p;
    MOV     [EBX], ESI              ; *p = pat0;
    XOR     DWORD [EBX], 0xffffffff ; *p ^= 0xffffffff;
    CMP     EDI, [EBX]              ; if (*p != pat1) goto fin;
    JNE     mts_fin
    MOV     [EBX], EDX              ; *p = old;
    ADD     EAX, 0x1000             ; i += 0x1000;
    CMP     EAX, [ESP+12+8]         ; if (i <= end) goto mts_loop;
    JBE     mts_loop
    POP     EBX
    POP     ESI
    POP     EDI
    RET
mts_fin:
    MOV     [EBX], EDX              ; *p = old;
    POP     EBX
    POP     ESI
    POP     EDI
    RET

_farjmp:
    JMP     FAR [ESP+4]
    RET

_farcall:
    CALL    FAR [ESP+4]
    RET

_asm_hrb_api:
		STI
        PUSH    DS
        PUSH    ES
        PUSHAD              ; for preservation
        PUSHAD              ; give to hrb_api
        MOV     AX, SS
        MOV     DS, AX
        MOV     ES, AX
        CALL    _hrb_api
        CMP     EAX, 0
        JNE     _asm_end_app
		ADD		ESP,32
        POPAD
        POP     ES
        POP     DS
		IRETD
_asm_end_app:
        ; EAX == tss.esp0
        MOV     ESP, [EAX]
        MOV     DWORD [EAX+4], 0
        POPAD
        RET                         ; go back to cmd_app

_start_app:                                     ; void start_app(int eip, int cs, int esp, int ds, int *tss_esp0);
        PUSHAD
        ; preserve the registers for app
        MOV     EAX, [ESP+36]
        MOV     ECX, [ESP+40]
        MOV     EDX, [ESP+44]
        MOV     EBX, [ESP+48]
        MOV     EBP, [ESP+52]
        MOV     [EBP], ESP
        MOV     [EBP+4], SS
        MOV     ES, BX
        MOV     DS, BX
        MOV     FS, BX
        MOV     GS, BX
        ; stack config
        OR      ECX, 3
        OR      EBX, 3
        PUSH    EBX
        PUSH    EDX
        PUSH    ECX
        PUSH    EAX
        RETF
