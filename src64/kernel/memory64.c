#include <asmfunc64.h>
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

static uintptr_t memman64_alloc_at_4k_nolock(struct MEMMAN64 *man, uintptr_t addr,
	size_t size);

static size_t memman64_total_nolock(const struct MEMMAN64 *man)
{
	uint32_t i;
	size_t total;

	total = 0;
	for (i = 0; i < man->frees; i++) {
		total += man->free[i].size;
	}
	return total;
}

static uintptr_t memman64_alloc_nolock(struct MEMMAN64 *man, size_t size)
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

static int memman64_free_nolock(struct MEMMAN64 *man, uintptr_t addr, size_t size)
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

/*
 * 프리 리스트는 재진입 불가다. 태스크가 여럿이면 (콘솔마다 하나 --
 * console_plan.md) 할당 도중 PIT가 선점해 다른 태스크가 같은 리스트를
 * 건드릴 수 있다. 전환 원인이 PIT IRQ뿐이므로 인터럽트를 막으면 충분하다.
 * rflags를 저장/복원하는 이유는 이미 꺼져 있는 곳에서 불러도 안전하게
 * 하기 위해서다 -- io_sti로 켜 버리면 인터럽트 문맥에서 사고가 난다.
 * 규칙: 이 구간 안에서는 task_sleep64를 부르지 않는다.
 *
 * ponytail: 커널 전체를 덮는 자물쇠 하나. 갱신 지연이 문제되면 그때 쪼갠다.
 */
size_t memman64_total(const struct MEMMAN64 *man)
{
	uint64_t flags;
	size_t total;

	flags = io_load_rflags();
	io_cli();
	total = memman64_total_nolock(man);
	io_store_rflags(flags);
	return total;
}

uintptr_t memman64_alloc(struct MEMMAN64 *man, size_t size)
{
	uint64_t flags;
	uintptr_t addr;

	flags = io_load_rflags();
	io_cli();
	addr = memman64_alloc_nolock(man, size);
	io_store_rflags(flags);
	return addr;
}

int memman64_free(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	uint64_t flags;
	int status;

	flags = io_load_rflags();
	io_cli();
	status = memman64_free_nolock(man, addr, size);
	io_store_rflags(flags);
	return status;
}

uintptr_t memman64_alloc_at_4k(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	uint64_t flags;
	uintptr_t result;

	flags = io_load_rflags();
	io_cli();
	result = memman64_alloc_at_4k_nolock(man, addr, size);
	io_store_rflags(flags);
	return result;
}

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	size = (size_t) align_up64((uintptr_t) size, MEMMAN64_PAGE_SIZE);
	return memman64_alloc(man, size);
}

static uintptr_t memman64_alloc_at_4k_nolock(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	uint32_t i;
	uintptr_t end;
	uintptr_t free_start;
	uintptr_t free_end;

	addr = align_down64(addr, MEMMAN64_PAGE_SIZE);
	size = (size_t) align_up64((uintptr_t) size, MEMMAN64_PAGE_SIZE);
	end = addr + size;
	if (size == 0 || end <= addr) {
		return 0;
	}
	for (i = 0; i < man->frees; i++) {
		free_start = man->free[i].addr;
		free_end = free_start + man->free[i].size;
		if (free_start <= addr && end <= free_end) {
			if (free_start == addr && free_end == end) {
				man->frees--;
				for (; i < man->frees; i++) {
					man->free[i] = man->free[i + 1];
				}
			} else if (free_start == addr) {
				man->free[i].addr = end;
				man->free[i].size = free_end - end;
			} else if (free_end == end) {
				man->free[i].size = addr - free_start;
			} else if (man->frees < MEMMAN64_FREES) {
				for (uint32_t j = man->frees; j > i + 1; j--) {
					man->free[j] = man->free[j - 1];
				}
				man->frees++;
				man->free[i + 1].addr = end;
				man->free[i + 1].size = free_end - end;
				man->free[i].size = addr - free_start;
			} else {
				return 0;
			}
			return addr;
		}
	}
	return 0;
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
