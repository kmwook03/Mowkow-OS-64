#ifndef MOWKOW64_MEMORY64_H
#define MOWKOW64_MEMORY64_H

#include <stddef.h>
#include <stdint.h>

#define MEMMAN64_FREES 4090
/*
 * 0x200000에서 올렸다(Stage 4, python_porting.md). MicroPython GC 힙이
 * 커널 .bss에 들어오면서 예전 2MiB 경계로는 여유가 250KiB밖에 남지 않았다.
 * 이 주소에 맞춰 고정된 것은 아무것도 없다(항등 사상 2MB 페이지가 어차피
 * 전 범위를 덮는다). 커널 정적 .bss가 끝나고 초기 할당기가 시작하는 자리를
 * 어디로 볼지 정하는 정책 값일 뿐이다. 512MB 시스템에서 4MiB는 넉넉하며,
 * KERNEL64_SECTORS와 같은 "넉넉하게 잡는다" 방침을 따른다(Stage 0.3).
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
