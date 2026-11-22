#ifndef MOWKOW64_ELF64_LOADER_H
#define MOWKOW64_ELF64_LOADER_H

#include <process64.h>

/* Load into an empty inactive space through owned backing aliases. On failure,
 * the caller destroys the space before freeing any retained backing/snapshot.
 * Does not activate a root or write through user virtual addresses.
 */
int elf64_prepare_image(const char *path, struct PROCESS64 *process);
/* Explicit ABI preparation for exec; never infer the entry convention. */
int elf64_prepare_image_abi(const char *path, struct PROCESS_IMAGE64 *image, uint32_t abi);

#endif
