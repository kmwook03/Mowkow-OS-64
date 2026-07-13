#include <memory64.h>

struct MEMMAN64 memman64;

static uintptr_t early_next;
static uintptr_t early_limit;

uintptr_t align_up64(uintptr_t value, size_t alignment)
{
	uintptr_t mask;

	if (alignment == 0) {
		return value;
	}
	mask = (uintptr_t) alignment - 1;
	return (value + mask) & ~mask;
}

uintptr_t align_down64(uintptr_t value, size_t alignment)
{
	uintptr_t mask;

	if (alignment == 0) {
		return value;
	}
	mask = (uintptr_t) alignment - 1;
	return value & ~mask;
}

void early_alloc64_init(uintptr_t start, uintptr_t end)
{
	early_next = align_up64(start, MEMMAN64_PAGE_SIZE);
	early_limit = align_down64(end, MEMMAN64_PAGE_SIZE);
}

uintptr_t early_alloc64(size_t size, size_t alignment)
{
	uintptr_t addr;
	uintptr_t next;

	addr = align_up64(early_next, alignment != 0 ? alignment : MEMMAN64_PAGE_SIZE);
	next = align_up64(addr + size, MEMMAN64_PAGE_SIZE);
	if (next < addr || next > early_limit) {
		return 0;
	}
	early_next = next;
	return addr;
}

void memman64_init(struct MEMMAN64 *man)
{
	man->frees = 0;
	man->maxfrees = 0;
	man->lostsize = 0;
	man->losts = 0;
}

size_t memman64_total(const struct MEMMAN64 *man)
{
	uint32_t i;
	size_t total;

	total = 0;
	for (i = 0; i < man->frees; i++) {
		total += man->free[i].size;
	}
	return total;
}

uintptr_t memman64_alloc(struct MEMMAN64 *man, size_t size)
{
	uint32_t i;
	uintptr_t addr;

	if (size == 0) {
		return 0;
	}
	for (i = 0; i < man->frees; i++) {
		if (man->free[i].size >= size) {
			addr = man->free[i].addr;
			man->free[i].addr += size;
			man->free[i].size -= size;
			if (man->free[i].size == 0) {
				man->frees--;
				for (; i < man->frees; i++) {
					man->free[i] = man->free[i + 1];
				}
			}
			return addr;
		}
	}
	return 0;
}

int memman64_free(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	uint32_t i;
	uint32_t j;

	if (size == 0) {
		return 0;
	}
	for (i = 0; i < man->frees; i++) {
		if (man->free[i].addr > addr) {
			break;
		}
	}
	if (i > 0 && man->free[i - 1].addr + man->free[i - 1].size == addr) {
		man->free[i - 1].size += size;
		if (i < man->frees && addr + size == man->free[i].addr) {
			man->free[i - 1].size += man->free[i].size;
			man->frees--;
			for (; i < man->frees; i++) {
				man->free[i] = man->free[i + 1];
			}
		}
		return 0;
	}
	if (i < man->frees && addr + size == man->free[i].addr) {
		man->free[i].addr = addr;
		man->free[i].size += size;
		return 0;
	}
	if (man->frees < MEMMAN64_FREES) {
		for (j = man->frees; j > i; j--) {
			man->free[j] = man->free[j - 1];
		}
		man->frees++;
		if (man->maxfrees < man->frees) {
			man->maxfrees = man->frees;
		}
		man->free[i].addr = addr;
		man->free[i].size = size;
		return 0;
	}
	man->losts++;
	man->lostsize += size;
	return -1;
}

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	size = (size_t) align_up64((uintptr_t) size, MEMMAN64_PAGE_SIZE);
	return memman64_alloc(man, size);
}

int memman64_free_4k(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	size = (size_t) align_up64((uintptr_t) size, MEMMAN64_PAGE_SIZE);
	return memman64_free(man, addr, size);
}

void init_memory64(void)
{
	uintptr_t heap_start;

	early_alloc64_init(MEMMAN64_EARLY_START, MEMMAN64_EARLY_END);
	heap_start = early_alloc64(MEMMAN64_PAGE_SIZE, MEMMAN64_PAGE_SIZE);
	memman64_init(&memman64);
	if (heap_start != 0 && heap_start < MEMMAN64_EARLY_END) {
		memman64_free(&memman64, heap_start, MEMMAN64_EARLY_END - heap_start);
	}
}
