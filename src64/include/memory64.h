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
 *
 * Raised again to 0x800000 (console_plan.md step 1.5): app64.ld links every
 * app at 0x400000, so the old value put the user image window inside the
 * general pool. init_memory64 handed the pool's first page to early_alloc64
 * and gui64_init took the next megabyte, after which the loader could never
 * claim 0x400000 and every `run` failed. The window [USER_IMAGE_MIN,
 * USER_IMAGE_MAX) in elf64_loader.c is now outside the pool by construction;
 * keep the two in step if either moves.
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
