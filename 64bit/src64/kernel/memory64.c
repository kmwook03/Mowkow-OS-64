#include <arch/platform64.h>
#ifdef __aarch64__
#include <arch/arch64.h>
#endif
#include <memory64.h>

struct MEMMAN64 memman64;

static uintptr_t early_next;
static uintptr_t early_limit;

static uint64_t memory_lock64(void)
{
	return platform_irq_save64();
}

static void memory_unlock64(uint64_t state)
{
	platform_irq_restore64(state);
}

static int is_power_of_two64(size_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static int range_end64(uintptr_t addr, size_t size, uintptr_t *end)
{
	if (end == NULL || size > UINTPTR_MAX - addr) {
		return MEMMAN64_ERR_OVERFLOW;
	}
	*end = addr + size;
	return 0;
}

int align_up_checked64(uintptr_t value, size_t alignment,
	uintptr_t *result)
{
	uintptr_t mask;

	if (result == NULL || !is_power_of_two64(alignment)) {
		return MEMMAN64_ERR_INVALID;
	}
	mask = (uintptr_t) alignment - 1;
	if (value > UINTPTR_MAX - mask) {
		return MEMMAN64_ERR_OVERFLOW;
	}
	*result = (value + mask) & ~mask;
	return 0;
}

int align_down_checked64(uintptr_t value, size_t alignment,
	uintptr_t *result)
{
	uintptr_t mask;

	if (result == NULL || !is_power_of_two64(alignment)) {
		return MEMMAN64_ERR_INVALID;
	}
	mask = (uintptr_t) alignment - 1;
	*result = value & ~mask;
	return 0;
}

uintptr_t align_up64(uintptr_t value, size_t alignment)
{
	uintptr_t result;

	if (align_up_checked64(value, alignment, &result) != 0) {
		return 0;
	}
	return result;
}

uintptr_t align_down64(uintptr_t value, size_t alignment)
{
	uintptr_t result;

	if (align_down_checked64(value, alignment, &result) != 0) {
		return 0;
	}
	return result;
}

void early_alloc64_init(uintptr_t start, uintptr_t end)
{
	if (align_up_checked64(start, MEMMAN64_PAGE_SIZE, &early_next) != 0 ||
		align_down_checked64(end, MEMMAN64_PAGE_SIZE, &early_limit) != 0 ||
		early_next >= early_limit) {
		early_next = 0;
		early_limit = 0;
	}
}

uintptr_t early_alloc64(size_t size, size_t alignment)
{
	uintptr_t addr;
	uintptr_t end;
	uintptr_t next;

	if (size == 0 || early_next == 0) {
		return 0;
	}
	if (alignment == 0) {
		alignment = MEMMAN64_PAGE_SIZE;
	}
	if (align_up_checked64(early_next, alignment, &addr) != 0 ||
		range_end64(addr, size, &end) != 0 ||
		align_up_checked64(end, MEMMAN64_PAGE_SIZE, &next) != 0 ||
		next > early_limit) {
		return 0;
	}
	early_next = next;
	return addr;
}

void memman64_init(struct MEMMAN64 *man)
{
	if (man == NULL) {
		return;
	}
	man->frees = 0;
	man->maxfrees = 0;
	man->lostsize = 0;
	man->losts = 0;
	man->pool_start = 0;
	man->pool_end = 0;
}

static uintptr_t memman64_alloc_at_4k_nolock(struct MEMMAN64 *man, uintptr_t addr,
	size_t size);

static size_t memman64_total_nolock(const struct MEMMAN64 *man)
{
	uint32_t i;
	size_t total;

	if (man == NULL) {
		return 0;
	}
	total = 0;
	for (i = 0; i < man->frees; i++) {
		if (man->free[i].size > SIZE_MAX - total) {
			return SIZE_MAX;
		}
		total += man->free[i].size;
	}
	return total;
}

static int memman64_validate_nolock(const struct MEMMAN64 *man)
{
	uint32_t i;
	uintptr_t end;
	uintptr_t previous_end;

	if (man == NULL || man->frees > MEMMAN64_FREES ||
		man->maxfrees < man->frees) {
		return MEMMAN64_ERR_INVALID;
	}
	if (man->pool_start == 0 || man->pool_end <= man->pool_start ||
		(man->pool_start & (MEMMAN64_PAGE_SIZE - 1)) != 0 ||
		(man->pool_end & (MEMMAN64_PAGE_SIZE - 1)) != 0) {
		return MEMMAN64_ERR_INVALID;
	}
	previous_end = man->pool_start;
	for (i = 0; i < man->frees; i++) {
		if (man->free[i].size == 0 ||
			(man->free[i].addr & (MEMMAN64_PAGE_SIZE - 1)) != 0 ||
			(man->free[i].size & (MEMMAN64_PAGE_SIZE - 1)) != 0 ||
			range_end64(man->free[i].addr, man->free[i].size,
				&end) != 0 ||
			man->free[i].addr < previous_end || end > man->pool_end) {
			return MEMMAN64_ERR_INVALID;
		}
		if (i > 0 && man->free[i].addr == previous_end) {
			return MEMMAN64_ERR_INVALID;
		}
		previous_end = end;
	}
	return 0;
}

int memman64_add_pool(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	uintptr_t end;

	if (man == NULL || addr == 0 || size == 0 ||
		(addr & (MEMMAN64_PAGE_SIZE - 1)) != 0 ||
		(size & (MEMMAN64_PAGE_SIZE - 1)) != 0) {
		return MEMMAN64_ERR_INVALID;
	}
	if (range_end64(addr, size, &end) != 0) {
		return MEMMAN64_ERR_OVERFLOW;
	}
	if (man->pool_start != 0 || man->pool_end != 0 || man->frees != 0) {
		return MEMMAN64_ERR_INVALID;
	}
	man->pool_start = addr;
	man->pool_end = end;
	man->free[0].addr = addr;
	man->free[0].size = size;
	man->frees = 1;
	man->maxfrees = 1;
	return 0;
}

int memman64_validate(const struct MEMMAN64 *man)
{
	uint64_t flags;
	int status;

	flags = memory_lock64();
	status = memman64_validate_nolock(man);
	memory_unlock64(flags);
	return status;
}

static uintptr_t memman64_alloc_nolock(struct MEMMAN64 *man, size_t size)
{
	uint32_t i;
	uintptr_t addr;
	uintptr_t end;

	if (man == NULL || size == 0 ||
		(size & (MEMMAN64_PAGE_SIZE - 1)) != 0) {
		return 0;
	}
	for (i = 0; i < man->frees; i++) {
		if (range_end64(man->free[i].addr, man->free[i].size,
				&end) != 0 || end > man->pool_end) {
			return 0;
		}
		if (man->free[i].size >= size) {
			addr = man->free[i].addr;
			man->free[i].addr = addr + size;
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
	uintptr_t end;
	uintptr_t previous_end;

	if (man == NULL || addr == 0 || size == 0 ||
		(addr & (MEMMAN64_PAGE_SIZE - 1)) != 0 ||
		(size & (MEMMAN64_PAGE_SIZE - 1)) != 0) {
		return MEMMAN64_ERR_INVALID;
	}
	if (range_end64(addr, size, &end) != 0) {
		return MEMMAN64_ERR_OVERFLOW;
	}
	if (addr < man->pool_start || end > man->pool_end) {
		return MEMMAN64_ERR_RANGE;
	}
	for (i = 0; i < man->frees; i++) {
		if (man->free[i].addr > addr) {
			break;
		}
	}
	if (i > 0) {
		if (range_end64(man->free[i - 1].addr,
				man->free[i - 1].size, &previous_end) != 0 ||
			previous_end > addr) {
			return MEMMAN64_ERR_INVALID;
		}
	} else {
		previous_end = 0;
	}
	if (i < man->frees && end > man->free[i].addr) {
		return MEMMAN64_ERR_INVALID;
	}
	if (i > 0 && previous_end == addr) {
		man->free[i - 1].size += size;
		if (i < man->frees && end == man->free[i].addr) {
			man->free[i - 1].size += man->free[i].size;
			man->frees--;
			for (; i < man->frees; i++) {
				man->free[i] = man->free[i + 1];
			}
		}
		return 0;
	}
	if (i < man->frees && end == man->free[i].addr) {
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
	if (man->losts < UINT32_MAX) {
		man->losts++;
	}
	if (size > SIZE_MAX - man->lostsize) {
		man->lostsize = SIZE_MAX;
	} else {
		man->lostsize += size;
	}
	return MEMMAN64_ERR_NOMEM;
}

/*
 * 프리 리스트는 재진입 불가다.
 * 태스크가 여럿이면 할당 도중 PIT가 선점해 다른 태스크가 같은 리스트를 건드릴 수 있다.
 * -> 인터럽트 막아서 해결
 * rflags를 저장/복원하는 이유는 이미 꺼져 있는 곳에서 불러도 안전하게
 * 하기 위해서다 -- io_sti로 켜 버리면 인터럽트 문맥에서 사고가 난다.
 * 규칙: 이 구간 안에서는 task_sleep64를 부르지 않는다.
 */
size_t memman64_total(const struct MEMMAN64 *man)
{
	uint64_t flags;
	size_t total;

	flags = memory_lock64();
	total = memman64_total_nolock(man);
	memory_unlock64(flags);
	return total;
}

uintptr_t memman64_alloc(struct MEMMAN64 *man, size_t size)
{
	uint64_t flags;
	uintptr_t addr;

	flags = memory_lock64();
	addr = memman64_alloc_nolock(man, size);
	memory_unlock64(flags);
	return addr;
}

int memman64_free(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	uint64_t flags;
	int status;

	flags = memory_lock64();
	status = memman64_free_nolock(man, addr, size);
	memory_unlock64(flags);
	return status;
}

uintptr_t memman64_alloc_at_4k(struct MEMMAN64 *man, uintptr_t addr, size_t size)
{
	uint64_t flags;
	uintptr_t result;

	flags = memory_lock64();
	result = memman64_alloc_at_4k_nolock(man, addr, size);
	memory_unlock64(flags);
	return result;
}

uintptr_t memman64_alloc_4k(struct MEMMAN64 *man, size_t size)
{
	uintptr_t aligned_size;

	if (size == 0 ||
		align_up_checked64((uintptr_t) size, MEMMAN64_PAGE_SIZE,
			&aligned_size) != 0) {
		return 0;
	}
	return memman64_alloc(man, (size_t) aligned_size);
}

static uintptr_t memman64_alloc_at_4k_nolock(struct MEMMAN64 *man,
	uintptr_t addr, size_t size)
{
	uint32_t i;
	uintptr_t aligned_size;
	uintptr_t end;
	uintptr_t free_start;
	uintptr_t free_end;

	if (man == NULL || addr == 0 || size == 0 ||
		(addr & (MEMMAN64_PAGE_SIZE - 1)) != 0 ||
		align_up_checked64((uintptr_t) size, MEMMAN64_PAGE_SIZE,
			&aligned_size) != 0 ||
		range_end64(addr, (size_t) aligned_size, &end) != 0 ||
		addr < man->pool_start || end > man->pool_end) {
		return 0;
	}
	size = (size_t) aligned_size;
	for (i = 0; i < man->frees; i++) {
		free_start = man->free[i].addr;
		if (range_end64(free_start, man->free[i].size, &free_end) != 0) {
			return 0;
		}
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
				if (man->maxfrees < man->frees) {
					man->maxfrees = man->frees;
				}
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
	uintptr_t aligned_size;

	if (size == 0 || (addr & (MEMMAN64_PAGE_SIZE - 1)) != 0) {
		return MEMMAN64_ERR_INVALID;
	}
	if (align_up_checked64((uintptr_t) size, MEMMAN64_PAGE_SIZE,
			&aligned_size) != 0) {
		return MEMMAN64_ERR_OVERFLOW;
	}
	return memman64_free(man, addr, (size_t) aligned_size);
}

void init_memory64(void)
{
	uintptr_t heap_end;
	uintptr_t heap_start;

#ifdef __aarch64__
	heap_start = arch64_phys_to_virt(MEMMAN64_EARLY_START);
	heap_end = arch64_phys_to_virt(MEMMAN64_EARLY_END);
#else
	heap_start = MEMMAN64_EARLY_START;
	heap_end = MEMMAN64_EARLY_END;
#endif
	early_alloc64_init(heap_start, heap_end);
	heap_start = early_alloc64(MEMMAN64_PAGE_SIZE, MEMMAN64_PAGE_SIZE);
	memman64_init(&memman64);
	if (heap_start != 0 && heap_start < heap_end) {
		memman64_add_pool(&memman64, heap_start, heap_end - heap_start);
	}
}
