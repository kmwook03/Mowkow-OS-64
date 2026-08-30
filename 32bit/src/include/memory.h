// memory.c
#ifndef _MEMORY_H_
#define _MEMORY_H_

#define MEMMAN_FREES  4090       // 빈 공간 블록 최대 개수
#define MEMMAN_ADDR   0x003c0000 // 메모리 관리자 구조체 주소

struct FREEINFO {                // 빈 공간 블록 정보 구조체
    unsigned int addr, size;
};

struct MEMMAN {
    int frees, maxfrees, lostsize, losts;
    struct FREEINFO free[MEMMAN_FREES];     // 빈 공간 리스트
};

unsigned int memtest(unsigned int start, unsigned int end);
void memman_init(struct MEMMAN *man);
unsigned int memman_total(struct MEMMAN *man);
unsigned int memman_alloc(struct MEMMAN *man, unsigned int size);
int memman_free(struct MEMMAN *man, unsigned int addr, unsigned int size);
unsigned int memman_alloc_4k(struct MEMMAN *man, unsigned int size);
int memman_free_4k(struct MEMMAN *man, unsigned int addr, unsigned int size);

#endif // _MEMORY_H_
