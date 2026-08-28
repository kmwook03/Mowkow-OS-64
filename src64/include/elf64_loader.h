#ifndef MOWKOW64_ELF64_LOADER_H
#define MOWKOW64_ELF64_LOADER_H

#include <process64.h>

int elf64_load_process(const char *path, struct PROCESS64 *process);

/* 이미지 창은 하나뿐이라 프로세스도 한 번에 하나다. 프로세스가 끝나면
   소유권을 돌려준다 (이미지 메모리는 풀 밖이라 memman이 아니다). */
void elf64_release_process(struct PROCESS64 *process);

#endif
