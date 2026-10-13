#ifndef MOWKOW64_ARCH_X86_BOOT64_H
#define MOWKOW64_ARCH_X86_BOOT64_H

#include <stdint.h>

struct BOOTINFO64;

/* x86 boot support only. COM1 is initialized before framebuffer validation;
 * its log routines neither allocate nor sleep and preserve literal CR/LF bytes.
 */
void x86_boot_diag_init64(void);
void x86_boot_log_byte64(char c);
void x86_boot_log64(const char *text);
void x86_boot_log_uint64(uint64_t value);
void x86_boot_panic64(const char *stage, int status);
int x86_boot_info_valid64(const struct BOOTINFO64 *boot_info);
void x86_boot_diag_storage64(void);
void x86_boot_diag_gui64(void);
void x86_boot_diag_keyboard64(void);
void x86_boot_diag_mouse64(void);
/* The font buffer has kernel lifetime. Call only after FAT32 is mounted. */
void x86_boot_load_font64(void);

/* Preserve the dependencies visible at the call sites in kernel64_main:
 * BSS probe after descriptors; FPU smoke after FPU init; storage after mount;
 * sheet/input smoke after memory init and before live input initialization.
 * Destructive storage cases exist only with MOWKOW64_BOOT_TESTS=1.
 */
void x86_boot_smoke_bss64(void);
void x86_boot_smoke_fpu64(void);
void x86_boot_smoke_storage64(void);
void x86_boot_smoke_gui_input64(void);

#endif
