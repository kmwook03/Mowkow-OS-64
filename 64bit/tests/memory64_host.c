#include <arch/platform64.h>
#include <memory64.h>
#include <stddef.h>
#include <stdint.h>

#define CHECK64(condition) do { if (!(condition)) return __LINE__; } while (0)

uint64_t platform_irq_save64(void)
{
	return 0;
}

void platform_irq_restore64(uint64_t state)
{
	(void) state;
}

static int test_alignment64(void)
{
	uintptr_t result;

	CHECK64(align_up_checked64(0x1001, 0x1000, &result) == 0);
	CHECK64(result == 0x2000);
	CHECK64(align_down_checked64(0x1fff, 0x1000, &result) == 0);
	CHECK64(result == 0x1000);
	CHECK64(align_up_checked64(1, 3, &result) < 0);
	CHECK64(align_down_checked64(1, 0, &result) < 0);
	CHECK64(align_up_checked64(UINTPTR_MAX, 2, &result) < 0);
	return 0;
}

static int test_early_allocator64(void)
{
	early_alloc64_init(0x1001, 0x5001);
	CHECK64(early_alloc64(1, 0x1000) == 0x2000);
	CHECK64(early_alloc64(1, 3) == 0);
	CHECK64(early_alloc64(SIZE_MAX, 0x1000) == 0);
	CHECK64(early_alloc64(0x2000, 0x1000) == 0x3000);
	CHECK64(early_alloc64(1, 0x1000) == 0);
	return 0;
}

static int test_free_list64(void)
{
	struct MEMMAN64 man;
	uintptr_t first;
	uintptr_t middle;

	memman64_init(&man);
	CHECK64(memman64_add_pool(&man, 0x10000, 0x10000) == 0);
	CHECK64(memman64_validate(&man) == 0);
	CHECK64(memman64_total(&man) == 0x10000);

	first = memman64_alloc_4k(&man, 1);
	middle = memman64_alloc_at_4k(&man, 0x18000, 0x1000);
	CHECK64(first == 0x10000);
	CHECK64(middle == 0x18000);
	CHECK64(memman64_validate(&man) == 0);
	CHECK64(memman64_total(&man) == 0xe000);

	CHECK64(memman64_free_4k(&man, first, 1) == 0);
	CHECK64(memman64_free_4k(&man, first, 1) < 0);
	CHECK64(memman64_free_4k(&man, 0x17800, 0x1000) < 0);
	CHECK64(memman64_free_4k(&man, 0x20000, 0x1000) < 0);
	CHECK64(memman64_free_4k(&man, middle, 0x1000) == 0);
	CHECK64(memman64_validate(&man) == 0);
	CHECK64(memman64_total(&man) == 0x10000);
	return 0;
}

static int test_invalid_ranges64(void)
{
	struct MEMMAN64 man;

	memman64_init(&man);
	CHECK64(memman64_add_pool(&man, 0x1001, 0x1000) < 0);
	CHECK64(memman64_add_pool(&man, UINTPTR_MAX - 0xfff, 0x2000) < 0);
	CHECK64(memman64_add_pool(&man, 0x1000, 0x4000) == 0);
	CHECK64(memman64_alloc_4k(&man, SIZE_MAX) == 0);
	CHECK64(memman64_alloc_at_4k(&man, 0x1800, 0x1000) == 0);
	CHECK64(memman64_free(&man, 0x1000, 1) < 0);
	return 0;
}

int main(void)
{
	int status;

	status = test_alignment64();
	if (status != 0) {
		return status;
	}
	status = test_early_allocator64();
	if (status != 0) {
		return status;
	}
	status = test_free_list64();
	if (status != 0) {
		return status;
	}
	return test_invalid_ranges64();
}
