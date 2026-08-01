#include <asmfunc64.h>
#include <dsctbl64.h>
#include <interrupt64.h>
#include <keyboard64.h>
#include <mouse64.h>
#include <stddef.h>
#include <stdint.h>
#include <timer64.h>

#define GDT64_ENTRIES 7
#define IDT64_ENTRIES 256
#define KERNEL_STACK_SIZE 16384

static union {
	struct GDT64_DESCRIPTOR seg[GDT64_ENTRIES];
	struct {
		struct GDT64_DESCRIPTOR null_desc;
		struct GDT64_DESCRIPTOR code_desc;
		struct GDT64_DESCRIPTOR data_desc;
		struct TSS64_DESCRIPTOR tss_desc;
		struct GDT64_DESCRIPTOR user_data_desc;
		struct GDT64_DESCRIPTOR user_code_desc;
	} entry;
} gdt64 __attribute__((aligned(16)));

static struct IDT64_GATE idt64[IDT64_ENTRIES] __attribute__((aligned(16)));
static struct TSS64 tss64 __attribute__((aligned(16)));
static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

extern void asm_exception00(void);
extern void asm_exception01(void);
extern void asm_exception02(void);
extern void asm_exception03(void);
extern void asm_exception04(void);
extern void asm_exception05(void);
extern void asm_exception06(void);
extern void asm_exception07(void);
extern void asm_exception08(void);
extern void asm_exception09(void);
extern void asm_exception10(void);
extern void asm_exception11(void);
extern void asm_exception12(void);
extern void asm_exception13(void);
extern void asm_exception14(void);
extern void asm_exception15(void);
extern void asm_exception16(void);
extern void asm_exception17(void);
extern void asm_exception18(void);
extern void asm_exception19(void);
extern void asm_exception20(void);
extern void asm_exception21(void);
extern void asm_exception22(void);
extern void asm_exception23(void);
extern void asm_exception24(void);
extern void asm_exception25(void);
extern void asm_exception26(void);
extern void asm_exception27(void);
extern void asm_exception28(void);
extern void asm_exception29(void);
extern void asm_exception30(void);
extern void asm_exception31(void);
extern void asm_irq20(void);
extern void asm_irq21(void);
extern void asm_irq2c(void);
extern void asm_syscall80(void);

static void (*const exception_stubs[32])(void) = {
	asm_exception00, asm_exception01, asm_exception02, asm_exception03,
	asm_exception04, asm_exception05, asm_exception06, asm_exception07,
	asm_exception08, asm_exception09, asm_exception10, asm_exception11,
	asm_exception12, asm_exception13, asm_exception14, asm_exception15,
	asm_exception16, asm_exception17, asm_exception18, asm_exception19,
	asm_exception20, asm_exception21, asm_exception22, asm_exception23,
	asm_exception24, asm_exception25, asm_exception26, asm_exception27,
	asm_exception28, asm_exception29, asm_exception30, asm_exception31,
};

static void serial_putc(char c)
{
	while ((io_in8(0x3f8 + 5) & 0x20) == 0) {
	}
	io_out8(0x3f8, (uint8_t) c);
}

static void serial_print(const char *s)
{
	while (*s != '\0') {
		serial_putc(*s++);
	}
}

static void serial_print_hex64(uint64_t value)
{
	const char hex[] = "0123456789abcdef";
	int shift;

	for (shift = 60; shift >= 0; shift -= 4) {
		serial_putc(hex[(value >> shift) & 0x0f]);
	}
}

void exception_handler64(const struct INTERRUPT_FRAME64 *frame)
{
	serial_print("EXC vector=");
	serial_print_hex64(frame->vector);
	serial_print(" error=");
	serial_print_hex64(frame->error);
	serial_print(" rip=");
	serial_print_hex64(frame->rip);
	serial_print("\r\n");
}

void irq_handler64(const struct INTERRUPT_FRAME64 *frame)
{
	if (frame->vector == 0x20) {
		inthandler20_64();
		return;
	}
	if (frame->vector == 0x21) {
		inthandler21_64();
		return;
	}
	if (frame->vector == 0x2c) {
		inthandler2c_64();
		return;
	}
}

void set_gdt64_desc(struct GDT64_DESCRIPTOR *sd, uint32_t limit, uint32_t base, uint8_t access, uint8_t flags)
{
	sd->limit_low = (uint16_t) (limit & 0xffff);
	sd->base_low = (uint16_t) (base & 0xffff);
	sd->base_mid = (uint8_t) ((base >> 16) & 0xff);
	sd->access_right = access;
	sd->limit_high_flags = (uint8_t) (((limit >> 16) & 0x0f) | (flags & 0xf0));
	sd->base_high = (uint8_t) ((base >> 24) & 0xff);
}

void set_tss64_desc(struct TSS64_DESCRIPTOR *sd, uintptr_t base, uint32_t limit)
{
	sd->limit_low = (uint16_t) (limit & 0xffff);
	sd->base_low = (uint16_t) (base & 0xffff);
	sd->base_mid1 = (uint8_t) ((base >> 16) & 0xff);
	sd->access_right = AR64_TSS;
	sd->limit_high_flags = (uint8_t) ((limit >> 16) & 0x0f);
	sd->base_mid2 = (uint8_t) ((base >> 24) & 0xff);
	sd->base_high = (uint32_t) (base >> 32);
	sd->reserved = 0;
}

void set_idt64_gate(struct IDT64_GATE *gd, uintptr_t offset, uint16_t selector, uint8_t ist, uint8_t access)
{
	gd->offset_low = (uint16_t) (offset & 0xffff);
	gd->selector = selector;
	gd->ist = (uint8_t) (ist & 0x07);
	gd->access_right = access;
	gd->offset_mid = (uint16_t) ((offset >> 16) & 0xffff);
	gd->offset_high = (uint32_t) (offset >> 32);
	gd->reserved = 0;
}

void init_gdtidt64(void)
{
	uint16_t i;

	for (i = 0; i < GDT64_ENTRIES; i++) {
		set_gdt64_desc(&gdt64.seg[i], 0, 0, 0, 0);
	}
	for (i = 0; i < IDT64_ENTRIES; i++) {
		set_idt64_gate(&idt64[i], 0, 0, 0, 0);
	}

	set_gdt64_desc(&gdt64.entry.code_desc, 0, 0, AR64_PRESENT | AR64_CODE, 0x20);
	set_gdt64_desc(&gdt64.entry.data_desc, 0, 0, AR64_PRESENT | AR64_DATA, 0x00);
	set_gdt64_desc(&gdt64.entry.user_data_desc, 0, 0,
		AR64_PRESENT | AR64_DPL3 | AR64_DATA, 0x00);
	set_gdt64_desc(&gdt64.entry.user_code_desc, 0, 0,
		AR64_PRESENT | AR64_DPL3 | AR64_CODE, 0x20);

	tss64.rsp0 = (uintptr_t) (kernel_stack + KERNEL_STACK_SIZE);
	tss64.io_map_base = sizeof(tss64);
	set_tss64_desc(&gdt64.entry.tss_desc, (uintptr_t) &tss64, sizeof(tss64) - 1);

	for (i = 0; i < 32; i++) {
		set_idt64_gate(&idt64[i], (uintptr_t) exception_stubs[i], GDT64_KERNEL_CODE, 0, AR64_INT_GATE);
	}
	set_idt64_gate(&idt64[0x20], (uintptr_t) asm_irq20, GDT64_KERNEL_CODE, 0, AR64_INT_GATE);
	set_idt64_gate(&idt64[0x21], (uintptr_t) asm_irq21, GDT64_KERNEL_CODE, 0, AR64_INT_GATE);
	set_idt64_gate(&idt64[0x2c], (uintptr_t) asm_irq2c, GDT64_KERNEL_CODE, 0, AR64_INT_GATE);
	set_idt64_gate(&idt64[0x80], (uintptr_t) asm_syscall80, GDT64_KERNEL_CODE, 0,
		AR64_INT_GATE | AR64_DPL3);

	load_gdtr64(sizeof(gdt64) - 1, (uintptr_t) &gdt64);
	reload_segments64(GDT64_KERNEL_CODE, GDT64_KERNEL_DATA);
	load_idtr64(sizeof(idt64) - 1, (uintptr_t) idt64);
	load_tr64(GDT64_TSS);
}

void init_fpu64(void)
{
	uint64_t cr0;
	uint64_t cr4;

	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 &= ~(1ULL << 2);
	cr0 |= (1ULL << 1);
	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1ULL << 9) | (1ULL << 10);
	__asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

	__asm__ volatile("fninit");
}
