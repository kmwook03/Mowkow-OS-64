#ifndef MOWKOW64_ELF64_LOADER_H
#define MOWKOW64_ELF64_LOADER_H

#include <process64.h>

int elf64_load_process(const char *path, struct PROCESS64 *process);

#endif
