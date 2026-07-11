#include <bootinfo64.h>
#include <dsctbl64.h>
#include <stdint.h>

static void out8(uint16_t port, uint8_t value)
{
	__asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port)
{
	uint8_t value;

	__asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static void serial_init(void)
{
	out8(0x3f8 + 1, 0x00);
	out8(0x3f8 + 3, 0x80);
	out8(0x3f8 + 0, 0x03);
	out8(0x3f8 + 1, 0x00);
	out8(0x3f8 + 3, 0x03);
	out8(0x3f8 + 2, 0xc7);
	out8(0x3f8 + 4, 0x0b);
}

static void serial_putc(char c)
{
	while ((in8(0x3f8 + 5) & 0x20) == 0) {
	}
	out8(0x3f8, (uint8_t) c);
}

static void serial_print(const char *s)
{
	while (*s != '\0') {
		serial_putc(*s++);
	}
}

void kernel64_main(const struct BOOTINFO64 *boot_info)
{
	volatile uint16_t *vga = (volatile uint16_t *) boot_info->vram;
	const char *message = "Mowkow OS x86_64";
	uint16_t i;

	serial_init();
	init_gdtidt64();
	serial_print("Mowkow OS x86_64 kernel64_main\r\n");

	for (i = 0; message[i] != '\0'; i++) {
		vga[i] = (uint16_t) message[i] | 0x0f00;
	}

	for (;;) {
		__asm__ __volatile__("hlt");
	}
}
