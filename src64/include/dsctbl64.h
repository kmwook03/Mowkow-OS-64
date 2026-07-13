#ifndef MOWKOW64_DSCTBL64_H
#define MOWKOW64_DSCTBL64_H

#include <stdint.h>

#define GDT64_KERNEL_CODE 0x08
#define GDT64_KERNEL_DATA 0x10
#define GDT64_TSS         0x18

#define AR64_PRESENT     0x80
#define AR64_CODE        0x1a
#define AR64_DATA        0x12
#define AR64_TSS         0x89
#define AR64_INT_GATE    0x8e

struct GDTR64 {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

struct GDT64_DESCRIPTOR {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t access_right;
	uint8_t limit_high_flags;
	uint8_t base_high;
} __attribute__((packed));

struct TSS64_DESCRIPTOR {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid1;
	uint8_t access_right;
	uint8_t limit_high_flags;
	uint8_t base_mid2;
	uint32_t base_high;
	uint32_t reserved;
} __attribute__((packed));

struct IDT64_GATE {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t access_right;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t reserved;
} __attribute__((packed));

struct TSS64 {
	uint32_t reserved0;
	uint64_t rsp0;
	uint64_t rsp1;
	uint64_t rsp2;
	uint64_t reserved1;
	uint64_t ist1;
	uint64_t ist2;
	uint64_t ist3;
	uint64_t ist4;
	uint64_t ist5;
	uint64_t ist6;
	uint64_t ist7;
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t io_map_base;
} __attribute__((packed));

void init_gdtidt64(void);
void set_gdt64_desc(struct GDT64_DESCRIPTOR *sd, uint32_t limit, uint32_t base, uint8_t access, uint8_t flags);
void set_tss64_desc(struct TSS64_DESCRIPTOR *sd, uintptr_t base, uint32_t limit);
void set_idt64_gate(struct IDT64_GATE *gd, uintptr_t offset, uint16_t selector, uint8_t ist, uint8_t access);

#endif
