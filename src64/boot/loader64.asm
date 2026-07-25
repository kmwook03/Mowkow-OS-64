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
pd0_addr equ 0x72000
pd1_addr equ 0x73000
pd2_addr equ 0x74000
pd3_addr equ 0x75000
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

; BIOS int 13h AH=42h caps a single extended-read request at 128 sectors
; (64 KiB) on this BIOS -- confirmed empirically, request silently fails
; past that. Loop in <=128-sector chunks to support any KERNEL_SECTORS size.
read_kernel:
	mov cx, KERNEL_SECTORS
	mov bx, kernel_load_real >> 4
	mov eax, KERNEL_LBA
.loop:
	mov word [kernel_packet_off], 0
	mov [kernel_packet_seg], bx
	mov [kernel_packet_lba], eax
	cmp cx, 128
	jbe .last_chunk
	mov word [kernel_packet_count], 128
	jmp .do_read
.last_chunk:
	mov [kernel_packet_count], cx
.do_read:
	mov si, kernel_packet
	mov ah, 0x42
	mov dl, [boot_drive]
	int 0x13
	jc disk_error
	movzx edx, word [kernel_packet_count]
	sub cx, dx
	add eax, edx
	shl edx, 5
	add bx, dx
	test cx, cx
	jnz .loop
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
	cmp ax, 0x004f
	jne .text_mode

	mov ax, 0x4f01
	mov cx, 0x0103
	xor bx, bx
	mov es, bx
	mov di, vbe_mode_info
	int 0x10
	cmp ax, 0x004f
	jne .text_mode

	mov ax, 0x4f02
	mov bx, 0x4103
	int 0x10
	cmp ax, 0x004f
	jne .text_mode

	mov ax, [vbe_mode_info + 18]
	mov [boot_info.scrnx], ax
	mov ax, [vbe_mode_info + 20]
	mov [boot_info.scrny], ax
	mov ax, [vbe_mode_info + 50]
	test ax, ax
	jnz .store_stride
	mov ax, [vbe_mode_info + 16]
.store_stride:
	mov word [boot_info.bytes_per_scanline], ax
	mov al, [vbe_mode_info + 25]
	mov [boot_info.bpp], al
	mov al, [vbe_mode_info + 27]
	mov [boot_info.framebuffer_type], al
	mov eax, [vbe_mode_info + 40]
	mov [boot_info.vram], eax
	mov dword [boot_info.vram + 4], 0
.text_mode:
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
.bytes_per_scanline:
	dw 160
.bpp:
	db 16
.framebuffer_type:
	db 0
.reserved2:
	dd 0
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
kernel_packet_count:
	dw KERNEL_SECTORS
kernel_packet_off:
	dw kernel_load_real & 0xffff
kernel_packet_seg:
	dw kernel_load_real >> 4
kernel_packet_lba:
	dd KERNEL_LBA
	dd 0

align 16
vbe_info_buffer:
	db "VBE2"
	times 508 db 0

align 16
vbe_mode_info:
	times 256 db 0

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
	mov ecx, 4096 * 6 / 4
	rep stosd

	mov dword [pml4_addr], pdpt_addr | 0x007
	mov dword [pml4_addr + 4], 0

	mov dword [pdpt_addr], pd0_addr | 0x007
	mov dword [pdpt_addr + 4], 0
	mov dword [pdpt_addr + 8], pd1_addr | 0x007
	mov dword [pdpt_addr + 12], 0
	mov dword [pdpt_addr + 16], pd2_addr | 0x007
	mov dword [pdpt_addr + 20], 0
	mov dword [pdpt_addr + 24], pd3_addr | 0x007
	mov dword [pdpt_addr + 28], 0

	mov edi, pd0_addr
	mov eax, 0x00000087
	xor edx, edx
	mov ecx, 512 * 4
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
