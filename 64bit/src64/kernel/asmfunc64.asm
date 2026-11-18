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
global task_start64
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
extern process64_return_to_user64
extern process64_entry_stack_top

section .text
; copy_kernel(loader64.asm)은 디스크에서 늘 KERNEL_SECTORS*512바이트를 통째로 옮긴다.
; 그 범위가 .bss의 적재 주소를 넘어서기 때문에, .bss 자리에 0이 아니라
; 디스크에 있던 파일 내용이 들어온다. C 코드가 전역 변수를 건드리기 전에
; 여기서 직접 0으로 채운다.
_start:
	cli
	cld
	mov rsp, stack_top

	; SSE를 켠다. 최적화를 켜고 빌드하면 gcc가 평범한 구조체 복사에도
	; movdqa/xmm을 쓰므로, C로 들어가기 전에 켜 두지 않으면 첫 xmm 명령에서
	; 바로 예외가 난다 (init_fpu64는 너무 늦다).
	mov rax, cr0
	and ax, 0xfffb          ; EM = 0, x87 에뮬레이션 끔
	or ax, 0x0002           ; MP = 1
	mov cr0, rax
	mov rax, cr4
	or ax, 0x0600           ; OSFXSR | OSXMMEXCPT
	mov cr4, rax

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
	mov rbx, rsp
	sub rsp, 520
	and rsp, -16
	fxsave64 [rsp]
	mov [rsp + 512], rbx
	mov [rdi], rsp
	mov rsp, [rsi]
	fxrstor64 [rsp]
	mov rsp, [rsp + 512]
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx
	ret

; Fresh kernel tasks have no suspended IRQ-restore caller.
task_start64:
	sti
	sub rsp, 8
	call r12
	cli
.halt:
	hlt
	jmp .halt

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

%macro SAVE_REGS64 0
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
	cld
	mov ax, KERNEL_DATA_SEL
	mov ds, ax
	mov es, ax
	; Preserve FP before any C, and give the kernel a clean control environment.
	mov rbx, rsp
	and rsp, -16
	sub rsp, 512
	fxsave64 [rsp]
	fxrstor64 [rel clean_fp64]
	mov rdi, rbx
%endmacro

%macro RESTORE_REGS64 0
	; Only abandon a user return. Kernel IRQ frames may own locks/references.
	mov rdi, [rbx + 144]
	and edi, 3
	call process64_return_to_user64
	test eax, eax
	jnz syscall_exit_to_kernel
	fxrstor64 [rsp]
	mov rsp, rbx
	; The kernel also uses flat segments. Restore a user selector only for CPL3.
	mov ax, KERNEL_DATA_SEL
	test byte [rsp + 144], 3
	jz %%segments
	mov ax, USER_DATA_SEL
%%segments:
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
%endmacro

exception_common:
	SAVE_REGS64
	call exception_handler64
	cmp rax, 1
	je syscall_exit_to_kernel
	RESTORE_REGS64

irq_common:
	SAVE_REGS64
	call irq_handler64
	RESTORE_REGS64

syscall_common:
	SAVE_REGS64
	; Blocking and preemption use this process's own entry stack.
	sti
	call syscall_handler64
	cli
	cmp rax, 1
	je syscall_exit_to_kernel
	RESTORE_REGS64

syscall_exit_to_kernel:
	call process64_current_exit_rsp
	mov r12, rax
	call process64_current_exit_status
	mov esi, eax
	mov rdi, r12
	jmp leave_user_mode64

enter_user_mode64:
	mov r10, [rsp + 8]
	cli
	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15
	mov rbx, rsp
	mov r12, rdi
	mov r13, rsi
	mov r14, rdx
	mov r15, rcx
	sub rsp, 520
	and rsp, -16
	fxsave64 [rsp]
	mov [rsp + 512], rbx
	lea r11, [rel .kernel_resume]
	push r11
	mov [r10], rsp
	sub rsp, 8
	call process64_entry_stack_top
	mov rsp, rax
	push qword USER_DATA_SEL
	push r13
	push qword 0x202
	push qword 0x33
	push r12
	fxrstor64 [rel clean_fp64]
	mov ax, USER_DATA_SEL
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov rdi, r14
	mov rsi, r15
	xor eax, eax
	xor ebx, ebx
	xor ecx, ecx
	xor edx, edx
	xor ebp, ebp
	xor r8d, r8d
	xor r9d, r9d
	xor r10d, r10d
	xor r11d, r11d
	xor r12d, r12d
	xor r13d, r13d
	xor r14d, r14d
	xor r15d, r15d
	iretq
.kernel_resume:
	fxrstor64 [rsp]
	mov rsp, [rsp + 512]
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx
	; The C owner restores its IRQ token after switching back to the kernel root.
	ret

leave_user_mode64:
	cli
	cld
	mov ax, KERNEL_DATA_SEL
	mov ds, ax
	mov es, ax
	mov rsp, rdi
	mov eax, esi
	ret

section .rodata
align 16
clean_fp64:
	dw 0x037f
	times 22 db 0
	dd 0x1f80
	dd 0
	times 480 db 0
%if ($ - clean_fp64) != 512
	%error "FXSAVE image size"
%endif

section .bss
align 16
stack_bottom:
	resb 131072
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
