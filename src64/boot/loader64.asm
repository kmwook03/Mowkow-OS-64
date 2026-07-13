bits 16
org 0x8000

%ifndef KERNEL_LBA
%define KERNEL_LBA 17
%endif

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif

kernel_load_real equ 0x10000
kernel_load_addr equ 0x100000
pml4_addr equ 0x70000
pdpt_addr equ 0x71000
pd_addr equ 0x72000
stack64_top equ 0x90000

start:
	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00

	mov [boot_drive], dl
	mov si, stage2_msg
	call print_string

	call read_kernel
	call collect_boot_info
	call enable_a20

	lgdt [gdt64_ptr]

	mov eax, cr0
	or eax, 1
	mov cr0, eax
	jmp code32_sel:protected_start

print_string:
	lodsb
	test al, al
	jz .done
	mov ah, 0x0e
	mov bh, 0x00
	mov bl, 0x07
	int 0x10
	jmp print_string
.done:
	ret

read_kernel:
	mov si, kernel_packet
	mov ah, 0x42
	mov dl, [boot_drive]
	int 0x13
	jc disk_error
	ret

collect_boot_info:
	mov ah, 0x02
	int 0x16
	mov [boot_info.leds], al

	mov ax, 0x4f00
	xor bx, bx
	mov es, bx
	mov di, vbe_info_buffer
	int 0x10
	ret

disk_error:
	mov si, disk_error_msg
	call print_string
.halt:
	hlt
	jmp .halt

enable_a20:
	in al, 0x92
	or al, 0x02
	out 0x92, al
	ret

boot_drive:
	db 0

boot_info:
.cyls:
	db 0
.leds:
	db 0
.vmode:
	db 3
.reserve:
	db 0
.scrnx:
	dw 80
.scrny:
	dw 25
.vram:
	dq 0xb8000

stage2_msg:
	db "Entering long mode...", 13, 10, 0
disk_error_msg:
	db "Kernel read error", 13, 10, 0

align 4
kernel_packet:
	db 0x10
	db 0
	dw KERNEL_SECTORS
	dw kernel_load_real & 0xffff
	dw kernel_load_real >> 4
	dd KERNEL_LBA
	dd 0

align 16
vbe_info_buffer:
	db "VBE2"
	times 508 db 0

gdt64:
	dq 0
	dq 0x00cf9a000000ffff
	dq 0x00cf92000000ffff
	dq 0x00af9a000000ffff
gdt64_end:

gdt64_ptr:
	dw gdt64_end - gdt64 - 1
	dd gdt64

code32_sel equ 0x08
data_sel equ 0x10
code64_sel equ 0x18

bits 32
protected_start:
	mov ax, data_sel
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov esp, stack64_top

	call copy_kernel
	call setup_page_tables
	call enter_long_mode

copy_kernel:
	cld
	mov esi, kernel_load_real
	mov edi, kernel_load_addr
	mov ecx, KERNEL_SECTORS * 512 / 4
	rep movsd
	ret

setup_page_tables:
	cld
	mov edi, pml4_addr
	xor eax, eax
	mov ecx, 4096 * 3 / 4
	rep stosd

	mov dword [pml4_addr], pdpt_addr | 0x003
	mov dword [pml4_addr + 4], 0
	mov dword [pdpt_addr], pd_addr | 0x003
	mov dword [pdpt_addr + 4], 0

	mov edi, pd_addr
	mov eax, 0x00000083
	xor edx, edx
	mov ecx, 512
.map_pd:
	mov [edi], eax
	mov [edi + 4], edx
	add eax, 0x00200000
	adc edx, 0
	add edi, 8
	loop .map_pd
	ret

enter_long_mode:
	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	mov eax, pml4_addr
	mov cr3, eax

	mov ecx, 0xc0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax

	jmp code64_sel:long_mode_start

bits 64
long_mode_start:
	mov ax, data_sel
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov fs, ax
	mov gs, ax

	mov rsp, stack64_top
	mov rdi, boot_info
	mov rax, kernel_load_addr
	call rax

.halt:
	hlt
	jmp .halt
