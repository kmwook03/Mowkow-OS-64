#ifndef MOWKOW64_MEMORY64_H
#define MOWKOW64_MEMORY64_H

#include <stddef.h>
#include <stdint.h>

#define MEMMAN64_FREES 4090
/*
 * Raised from 0x200000 (Stage 4, python_porting.md): with the MicroPython
 * GC heap now baked into the kernel's .bss, the old 2MiB boundary left
 * only ~250KiB of headroom for it. Nothing else is hardcoded against this
 * address (identity-mapped 2MB pages cover the whole range regardless) --
 * it's purely a policy line for where kernel-static .bss ends and the
 * dynamically-managed early allocator begins. 4MiB is generous against a
 * 512MB system, matching the same "budget generously" approach as
 * KERNEL64_SECTORS (Stage 0.3).
 */
/*
 * 0x00800000에서 시작하는 이유: 유저 이미지 창 [0x400000, 0x800000)은
 * 모든 app64 실행 파일이 링크되는 고정 주소다(app64/app64.ld). 예전처럼
 * 힙 아레나가 0x400000에서 시작하면 fd64_init의 디스크 캐시가 그 창의
 * 바닥을 먼저 가져가 버려서, 어떤 앱도 적재되지 못한다.
 * 페이즈 1에서 프로세스마다 PML4를 갖게 되면 이 칸막이는 사라진다.
 */
#define MEMMAN64_EARLY_START ((uintptr_t) 0x00800000)
#define MEMMAN64_EARLY_END   ((uintptr_t) 0x20000000)
#define MEMMAN64_PAGE_SIZE   ((size_t) 0x1000)

struct FREEINFO64 {
	uintptr_t addr;
	size_t size;
};

struct MEMMAN64 {
	uint32_t frees;
	uint32_t maxfrees;
	size_t lostsize;
	uint32_t losts;
	struct FREEINFO64 free[MEMMAN64_FREES];
};

extern struct MEMMAN64 memman64;

uintptr_t align_up64(uintptr_t value, size_t alignment);
uintptr_t align_down64(uintptr_t value, size_t alignment);
void early_alloc64_init(uintptr_t start, uintptr_t end);
uintptr_t early_alloc64(size_t size, size_t alignment);
void memman64_init(struct MEMMAN64 *man);
size_t memman64_total(const struct MEMMAN64 *man);
uintptr_t memman64_alloc(struct MEMMAN64 *man, size_t size);
int memman64_free(struct MEMMAN64 *man, uintptr_t addr, size_t size);
uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size);
uintptr_t memman64_alloc_at_4k(struct MEMMAN64 *man, uintptr_t addr, size_t size);
int memman64_free_4k(struct MEMMAN64 *man, uintptr_t addr, size_t size);
void init_memory64(void);

#endif
