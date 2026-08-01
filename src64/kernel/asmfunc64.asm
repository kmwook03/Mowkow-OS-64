bits 64

%define KERNEL_DATA_SEL 0x10
%define USER_DATA_SEL   0x2b

global _start
global io_hlt
global io_cli
global io_sti
global io_stihlt
global io_out8
global io_in8
global io_load_rflags
global io_store_rflags
global load_gdtr64
global load_idtr64
global load_tr64
global reload_segments64
global context_switch64
global asm_exception_default
global asm_exception00
global asm_exception01
global asm_exception02
global asm_exception03
global asm_exception04
global asm_exception05
global asm_exception06
global asm_exception07
global asm_exception08
global asm_exception09
global asm_exception10
global asm_exception11
global asm_exception12
global asm_exception13
global asm_exception14
global asm_exception15
global asm_exception16
global asm_exception17
global asm_exception18
global asm_exception19
global asm_exception20
global asm_exception21
global asm_exception22
global asm_exception23
global asm_exception24
global asm_exception25
global asm_exception26
global asm_exception27
global asm_exception28
global asm_exception29
global asm_exception30
global asm_exception31
global asm_irq20
global asm_irq21
global asm_irq2c
global asm_syscall80
global stack_bottom
extern _bss_start
extern _bss_end
global enter_user_mode64
global leave_user_mode64
extern kernel64_main
extern exception_handler64
extern irq_handler64
extern syscall_handler64
extern process64_current_exit_rsp
extern process64_current_exit_status

section .text
; copy_kernel (loader64.asm) always copies a fixed KERNEL_SECTORS*512-byte
; block from disk, which now reaches past .bss's LMA and fills it with
; whatever file data sits there on disk instead of zeros. Zero it explicitly
; before any C code touches globals.
_start:
	cli
	cld
	mov rsp, stack_top
	mov r12, rdi
	mov rdi, _bss_start
	mov rcx, _bss_end
	sub rcx, rdi
	xor eax, eax
	rep stosb
	mov rdi, r12
	call kernel64_main

.halt:
	hlt
	jmp .halt

io_hlt:
	hlt
	ret

io_cli:
	cli
	ret

io_sti:
	sti
	ret

io_stihlt:
	sti
	hlt
	ret

io_out8:
	mov dx, di
	mov al, sil
	out dx, al
	ret

io_in8:
	mov dx, di
	xor eax, eax
	in al, dx
	ret

io_load_rflags:
	pushfq
	pop rax
	ret

io_store_rflags:
	push rdi
	popfq
	ret

load_gdtr64:
	sub rsp, 16
	mov [rsp], di
	mov [rsp + 2], rsi
	lgdt [rsp]
	add rsp, 16
	ret

load_idtr64:
	sub rsp, 16
	mov [rsp], di
	mov [rsp + 2], rsi
	lidt [rsp]
	add rsp, 16
	ret

load_tr64:
	ltr di
	ret

reload_segments64:
	push rdi
	lea rax, [rel .reload_cs]
	push rax
	retfq
.reload_cs:
	mov ax, si
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov fs, ax
	mov gs, ax
	ret

context_switch64:
	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15
	mov [rdi], rsp
	mov rsp, [rsi]
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx
	ret

asm_exception_default:
	iretq

%macro EXCEPTION_NOERR 2
asm_exception%1:
	push qword 0
	push qword %2
	jmp exception_common
%endmacro

%macro EXCEPTION_ERR 2
asm_exception%1:
	push qword %2
	jmp exception_common
%endmacro

EXCEPTION_NOERR 00, 0
EXCEPTION_NOERR 01, 1
EXCEPTION_NOERR 02, 2
EXCEPTION_NOERR 03, 3
EXCEPTION_NOERR 04, 4
EXCEPTION_NOERR 05, 5
EXCEPTION_NOERR 06, 6
EXCEPTION_NOERR 07, 7
EXCEPTION_ERR   08, 8
EXCEPTION_NOERR 09, 9
EXCEPTION_ERR   10, 10
EXCEPTION_ERR   11, 11
EXCEPTION_ERR   12, 12
EXCEPTION_ERR   13, 13
EXCEPTION_ERR   14, 14
EXCEPTION_NOERR 15, 15
EXCEPTION_NOERR 16, 16
EXCEPTION_ERR   17, 17
EXCEPTION_NOERR 18, 18
EXCEPTION_NOERR 19, 19
EXCEPTION_NOERR 20, 20
EXCEPTION_ERR   21, 21
EXCEPTION_NOERR 22, 22
EXCEPTION_NOERR 23, 23
EXCEPTION_NOERR 24, 24
EXCEPTION_NOERR 25, 25
EXCEPTION_NOERR 26, 26
EXCEPTION_NOERR 27, 27
EXCEPTION_NOERR 28, 28
EXCEPTION_ERR   29, 29
EXCEPTION_ERR   30, 30
EXCEPTION_NOERR 31, 31

%macro IRQ_STUB 2
asm_irq%1:
	push qword 0
	push qword %2
	jmp irq_common
%endmacro

IRQ_STUB 20, 0x20
IRQ_STUB 21, 0x21
IRQ_STUB 2c, 0x2c

asm_syscall80:
	push qword 0
	push qword 0x80
	jmp syscall_common

exception_common:
	push rax
	push rbx
	push rcx
	push rdx
	push rbp
	push rsi
	push rdi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov rdi, rsp
	call exception_handler64

	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdi
	pop rsi
	pop rbp
	pop rdx
	pop rcx
	pop rbx
	pop rax
	add rsp, 16
	iretq

irq_common:
	push rax
	push rbx
	push rcx
	push rdx
	push rbp
	push rsi
	push rdi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov rdi, rsp
	call irq_handler64

	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdi
	pop rsi
	pop rbp
	pop rdx
	pop rcx
	pop rbx
	pop rax
	add rsp, 16
	iretq

syscall_common:
	push rax
	push rbx
	push rcx
	push rdx
	push rbp
	push rsi
	push rdi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov ax, KERNEL_DATA_SEL
	mov ds, ax
	mov es, ax

	mov rdi, rsp
	call syscall_handler64
	cmp rax, 1
	je syscall_exit_to_kernel

	; restore the user data selectors before popping rax: doing it after
	; clobbers the low 16 bits of every syscall return value with 0x2b.
	mov ax, USER_DATA_SEL
	mov ds, ax
	mov es, ax

	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdi
	pop rsi
	pop rbp
	pop rdx
	pop rcx
	pop rbx
	pop rax
	add rsp, 16
	iretq

syscall_exit_to_kernel:
	call process64_current_exit_rsp
	push rax
	call process64_current_exit_status
	mov esi, eax
	pop rdi
	mov rsp, rdi
	mov eax, esi
	ret

enter_user_mode64:
	mov r10, [rsp + 8]
	cli
	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15
	lea r11, [rel .kernel_resume]
	push r11
	mov [r10], rsp
	mov rax, rdi
	mov r11, rsi
	mov rdi, rdx
	mov rsi, rcx
	mov dx, r9w
	mov ds, dx
	mov es, dx
	push r9
	push r11
	pushfq
	pop rcx
	or rcx, 0x200
	push rcx
	push r8
	push rax
	iretq
.kernel_resume:
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx
	sti
	ret

leave_user_mode64:
	mov rsp, rdi
	mov eax, esi
	ret

section .bss
align 16
stack_bottom:
	resb 131072
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
