bits 16
org 0x7c00

; every BPB value below comes from tools/mkfat32_64.py via the Makefile
%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif
%ifndef STAGE2_LBA
%define STAGE2_LBA 8
%endif
%ifndef TOTAL_SECTORS
%define TOTAL_SECTORS 131072
%endif
%ifndef RESERVED_SECTORS
%define RESERVED_SECTORS 1024
%endif
%ifndef FAT_COUNT
%define FAT_COUNT 2
%endif
%ifndef SECTORS_PER_FAT
%define SECTORS_PER_FAT 1001
%endif
%ifndef ROOT_CLUSTER
%define ROOT_CLUSTER 2
%endif
%ifndef FSINFO_LBA
%define FSINFO_LBA 1
%endif
%ifndef BACKUP_BOOT_LBA
%define BACKUP_BOOT_LBA 6
%endif
%ifndef VOLUME_ID
%define VOLUME_ID 0x646b776d
%endif

stage2_addr equ 0x8000

start:
	jmp short after_bpb
	nop

oem_name:                       ; 0x03
	db "MOWKOW64"
bytes_per_sector:               ; 0x0b
	dw 512
sectors_per_cluster:            ; 0x0d
	db 1
reserved_sectors:               ; 0x0e
	dw RESERVED_SECTORS
fat_count:                      ; 0x10
	db FAT_COUNT
root_entries:                   ; 0x11 FAT32: 항상 0
	dw 0
total_sectors16:                ; 0x13 FAT32: 항상 0
	dw 0
media_type:                     ; 0x15 고정 디스크
	db 0xf8
sectors_per_fat16:              ; 0x16 FAT32: 항상 0
	dw 0
sectors_per_track:              ; 0x18
	dw 63
heads:                          ; 0x1a
	dw 255
hidden_sectors:                 ; 0x1c
	dd 0
total_sectors32:                ; 0x20
	dd TOTAL_SECTORS
sectors_per_fat32:              ; 0x24
	dd SECTORS_PER_FAT
ext_flags:                      ; 0x28 FAT 사본 모두 미러링
	dw 0
filesystem_version:             ; 0x2a
	dw 0
root_cluster:                   ; 0x2c
	dd ROOT_CLUSTER
fsinfo_sector:                  ; 0x30
	dw FSINFO_LBA
backup_boot_sector:             ; 0x32
	dw BACKUP_BOOT_LBA
reserved2:                      ; 0x34
	times 12 db 0
drive_number:                   ; 0x40
	db 0x80
reserved1:                      ; 0x41
	db 0
boot_signature:                 ; 0x42
	db 0x29
volume_id:                      ; 0x43
	dd VOLUME_ID
volume_label:                   ; 0x47
	db "MOWKOW64   "
filesystem_type:                ; 0x52
	db "FAT32   "

after_bpb:
	jmp 0x0000:boot_start

boot_start:
	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00

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
	dd STAGE2_LBA
	dd 0

times 510 - ($ - $$) db 0
dw 0xaa55
