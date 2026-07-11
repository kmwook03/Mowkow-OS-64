bits 16
org 0x7c00

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif

stage2_addr equ 0x8000

start:
	jmp 0x0000:boot_start

boot_start:
	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	sti

	mov [boot_drive], dl

	mov si, loading_msg
	call print_string

	mov si, stage2_packet
	mov ah, 0x42
	mov dl, [boot_drive]
	int 0x13
	jc disk_error

	mov dl, [boot_drive]
	jmp 0x0000:stage2_addr

disk_error:
	mov si, disk_error_msg
	call print_string
.halt:
	hlt
	jmp .halt

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

boot_drive:
	db 0

loading_msg:
	db "Mowkow OS x86_64", 13, 10, 0
disk_error_msg:
	db "Disk read error", 13, 10, 0

align 4
stage2_packet:
	db 0x10
	db 0
	dw STAGE2_SECTORS
	dw stage2_addr
	dw 0x0000
	dd 1
	dd 0

times 510 - ($ - $$) db 0
dw 0xaa55
