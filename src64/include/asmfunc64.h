#ifndef MOWKOW64_ASMFUNC64_H
#define MOWKOW64_ASMFUNC64_H

#include <stdint.h>

void io_hlt(void);
void io_cli(void);
void io_sti(void);
void io_out8(uint16_t port, uint8_t data);
uint8_t io_in8(uint16_t port);
void load_gdtr64(uint16_t limit, uintptr_t addr);
void load_idtr64(uint16_t limit, uintptr_t addr);
void load_tr64(uint16_t selector);
void reload_segments64(uint16_t code_selector, uint16_t data_selector);
void asm_exception_default(void);

#endif
