#ifndef MOWKOW64_MEMORY64_H
#define MOWKOW64_MEMORY64_H

#include <stddef.h>
#include <stdint.h>

#define MEMMAN64_FREES 4090

/*
 * app64.ld는 모든 애플리케이션을 0x400000에 링크하므로, 
 * 기존 값에서는 사용자 이미지 영역이 일반 메모리 풀 내부에 위치하게 되었다. 
 * init_memory64가 메모리 풀의 첫 번째 페이지를 early_alloc64에 넘기고, 
 * 이어서 gui64_init이 다음 1MiB를 사용하면서 로더가 더 이상 0x400000 영역을 확보할 수 없게 되었고, 
 * 그 결과 모든 run 명령이 실패했다. 
 * 이제 elf64_loader.c의 [USER_IMAGE_MIN, USER_IMAGE_MAX) 영역은 
 * 구조적으로 메모리 풀 바깥에 위치하도록 되어 있다. 
 * 둘 중 하나를 변경할 경우 반드시 다른 하나도 함께 맞춰서 변경해야 한다.
 */
#define MEMMAN64_EARLY_START ((uintptr_t) 0x00800000)

/*
 * 이제 MicroPython GC 힙이 커널의 .bss에 포함되면서 
 * 기존 2MiB 경계로는 약 250KiB의 여유 공간밖에 남지 않게 되었다. 
 * 다른 어떤 부분도 이 주소에 하드코딩되어 있지 않으며(identity mapping된 2MB 페이지가 어차피 전체 범위를 커버함),
 * 이 값은 단순히 커널 정적 .bss가 끝나는 위치와 동적으로 관리되는 early allocator가 시작되는 위치를 
 * 구분하기 위한 정책상의 경계일 뿐이다. 
 * 512MB 시스템을 기준으로 4MiB는 충분히 넉넉하며, KERNEL64_SECTORS와 동일하게 
 * 여유 있게 예산을 잡는 방식을 따른 것이다.
 */
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
