#ifndef MOWKOW64_ELF64_LOADER_H
#define MOWKOW64_ELF64_LOADER_H

#include <process64.h>

int elf64_load_process(const char *path, struct PROCESS64 *process);

/* Load into an empty inactive space through owned backing aliases. On failure,
 * the caller destroys the space before freeing any retained backing/snapshot.
 * Does not claim or modify the serialized legacy image window.
 */
int elf64_prepare_image(const char *path, struct PROCESS64 *process);

/* Release is IRQ-safe and idempotent; only the current owner can release the
   fixed image window, which is outside memman64's pool. */
void elf64_release_process(struct PROCESS64 *process);

#endif
