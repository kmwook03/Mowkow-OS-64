#ifndef MOWKOW64_MEMORY64_H
#define MOWKOW64_MEMORY64_H

#include <stddef.h>
#include <stdint.h>

#define MEMMAN64_FREES 4090
#define MEMMAN64_EARLY_START ((uintptr_t) 0x00200000)
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
