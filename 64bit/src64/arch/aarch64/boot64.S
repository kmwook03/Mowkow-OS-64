/* Raspberry Pi 5 firmware entry: 0x80000, AArch64, normally at EL2. */
	.section .text.boot, "ax"
	.align 4
	.global _start
	.type _start, %function
	.extern aarch64_main
	.extern aarch64_high_main
	.extern arch64_panic_blink

_start:
	/* Keep the firmware DTB pointer while the exception level changes. */
	mov	x19, x0

	/* Firmware uses PSCI, but reject an accidentally started secondary core. */
	mrs	x1, mpidr_el1
	and	x2, x1, #0xffffff
	ubfx	x3, x1, #32, #8
	orr	x2, x2, x3
	cbnz	x2, .Lpark

	adrp	x1, __stack_top
	add	x1, x1, :lo12:__stack_top
	mov	sp, x1

	mrs	x1, CurrentEL
	lsr	x1, x1, #2
	cmp	x1, #2
	b.eq	.Lfrom_el2
	cmp	x1, #1
	b.eq	.Lat_el1

	/* Unexpected entry EL: blink panic code 1 from the current level. */
	mov	w0, #1
	bl	arch64_panic_blink
	b	.Lpark

.Lfrom_el2:
	/* EL1 executes AArch64 and may read the physical counter. */
	mrs	x1, hcr_el2
	orr	x1, x1, #(1 << 31)
	msr	hcr_el2, x1
	mrs	x1, cnthctl_el2
	orr	x1, x1, #3
	msr	cnthctl_el2, x1
	msr	cntvoff_el2, xzr

	/* Preserve reset state except for explicitly disabling MMU and caches. */
	mrs	x1, sctlr_el1
	bic	x1, x1, #(1 << 0)
	bic	x1, x1, #(1 << 2)
	bic	x1, x1, #(1 << 12)
	msr	sctlr_el1, x1
	isb

	adrp	x1, __stack_top
	add	x1, x1, :lo12:__stack_top
	msr	sp_el1, x1
	adr	x1, .Lat_el1
	msr	elr_el2, x1
	mov	x1, #0x3c5		/* EL1h, D/A/I/F masked */
	msr	spsr_el2, x1
	eret

.Lat_el1:
	msr	daifset, #0xf
	mrs	x1, cpacr_el1
	orr	x1, x1, #(3 << 20)	/* FPEN: permit FP/SIMD at EL0 and EL1. */
	msr	cpacr_el1, x1
	isb

	adrp	x1, __stack_top
	add	x1, x1, :lo12:__stack_top
	mov	sp, x1

	/* The flat image carries no bytes for NOBITS, so clear BSS ourselves. */
	adrp	x1, __bss_start
	add	x1, x1, :lo12:__bss_start
	adrp	x2, __bss_end
	add	x2, x2, :lo12:__bss_end
.Lbss_loop:
	cmp	x1, x2
	b.hs	.Lbss_done
	str	xzr, [x1], #8
	b	.Lbss_loop
.Lbss_done:

	adrp	x1, boot_dtb_ptr
	add	x1, x1, :lo12:boot_dtb_ptr
	str	x19, [x1]
	dsb	sy
	isb
	bl	aarch64_main

.Lpark:
	wfe
	b	.Lpark

	.size _start, . - _start

	/* Discard the low C call chain, move SP into the TTBR1 direct map and
	   continue at the high alias of aarch64_high_main. This never returns. */
	.global arch64_enter_high
	.type arch64_enter_high, %function
arch64_enter_high:
	movz	x0, #0xff80, lsl #32
	movk	x0, #0xffff, lsl #48
	adrp	x1, __stack_top
	add	x1, x1, :lo12:__stack_top
	add	x1, x1, x0
	mov	sp, x1
	adrp	x1, aarch64_high_main
	add	x1, x1, :lo12:aarch64_high_main
	add	x1, x1, x0
	mov	x29, xzr
	mov	x30, xzr
	dsb	sy
	isb
	br	x1

	.size arch64_enter_high, . - arch64_enter_high

	.section .bss, "aw", %nobits
	.align 3
	.global boot_dtb_ptr
boot_dtb_ptr:
	.skip 8

	.section .note.GNU-stack, "", %progbits
