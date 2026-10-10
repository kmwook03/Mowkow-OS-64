#ifndef MOWKOW64_ELF64_LOADER_H
#define MOWKOW64_ELF64_LOADER_H

#include <process64.h>

int elf64_load_process(const char *path, struct PROCESS64 *process);

/* Release is IRQ-safe and idempotent; only the current owner can release the
   fixed image window, which is outside memman64's pool. */
void elf64_release_process(struct PROCESS64 *process);

#endif
