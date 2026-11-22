#ifndef MOWKOW64_ADDRESS_SPACE_CLONE64_H
#define MOWKOW64_ADDRESS_SPACE_CLONE64_H

static void mapping_lifetime64(void)
{
	struct ADDRESS_SPACE64 *space = NULL, *clone = NULL;
	struct USER_BACKING64 backing = {0};
	static uint64_t before[8][512];
	unsigned int calls;

	reset64(); assert(address_space_create64(&space) == 0);
	assert(user_backing_alloc64(&backing, 258 * 4096) == 0);
	assert(user_backing_pin64(NULL) == -22 && address_space_pin64(NULL) == -22);
	assert(user_backing_unpin64(&backing) == -22 && address_space_unpin64(space) == -22);
	backing.pins = UINT32_MAX;
	assert(user_backing_pin64(&backing) == -75);
	backing.pins = 0; space->pins = UINT32_MAX;
	assert(address_space_pin64(space) == -75);
	space->pins = 0;
	assert(address_space_map_backing64(space, 0x400000, &backing, SIZE_MAX, 4096, 1) == -22);
	assert(address_space_map_backing64(space, 0x400000, &backing, 0, SIZE_MAX, 1) == -22);
	assert(address_space_map_backing64(space, 0x400000, &backing, 1, 4096, 1) == -22);
	assert(backing.pins == 0 && space->vma_count == 0);
	assert(address_space_map_backing64(space, 0x400000, &backing, 0, 4096, 3) == 0);
	assert(address_space_map_backing64(space, 0x404000, &backing, 4096, 4096, 5) == 0);
	snapshot64(space, before);
	/* A merge candidate must not skip a later executable physical alias. */
	assert(address_space_map_backing64(space, 0x401000, &backing, 4096, 4096, 3) == -22);
	assert(address_space_map_backing64(space, 0x400000, &backing, 0, 4096, 3) == -17);
	unchanged64(space, before);
	assert(backing.pins == 2 && space->vma_count == 2);
	assert(address_space_pin64(space) == 0);
	calls = free_calls64;
	assert(address_space_destroy64(&space) == -16 && free_calls64 == calls);
	assert(address_space_map_backing64(space, 0x406000, &backing, 0, 4096, 1) == -16);
	assert(user_backing_free64(&backing) == -16 && backing.pins == 2);
	assert(address_space_unpin64(space) == 0);
	free_fail64 = (int) free_calls64;
	assert(address_space_destroy64(&space) == -12 && backing.pins == 2);
	assert(user_backing_free64(&backing) == -16);
	free_fail64 = -1;
	assert(address_space_destroy64(&space) == 0 && backing.pins == 0);
	assert(address_space_create64(&space) == 0);
	/* Alternating permissions prevent coalescing and exhaust the VMA budget. */
	for (unsigned int i = 0; i < 256; i++) {
		assert(address_space_map_backing64(space, 0x400000 + i * 4096,
			&backing, i * 4096, 4096, i % 2 ? 3 : 1) == 0);
	}
	snapshot64(space, before);
	assert(address_space_map_backing64(space, 0x500000, &backing, 256 * 4096, 4096, 1) == -12);
	unchanged64(space, before);
	assert(backing.pins == 256 && space->vma_count == 256);
	assert(address_space_map_backing64(space, 0x500000, &backing, 256 * 4096, 4096, 3) == 0);
	assert(backing.pins == 256 && space->vma_count == 256);
	assert(address_space_destroy64(&space) == 0 && backing.pins == 0);
	assert(address_space_create64(&space) == 0);
	assert(address_space_map64(space, 0x400000, backing.physical, 4096, 1) == 0);
	calls = alloc_calls64;
	assert(address_space_clone64(space, &clone) == -95 && clone == NULL);
	assert(alloc_calls64 == calls && space->pins == 0);
	assert(address_space_destroy64(&space) == 0);
	assert(user_backing_free64(&backing) == 0);
	assert(live_pages64 == as_arch_table_count64());
}

static struct ADDRESS_SPACE64 *copy_source64;
static struct USER_BACKING64 *copy_backing64;
static unsigned int copy_interrupts64;

static void interrupt_copy64(void)
{
	unsigned int frees = free_calls64;

	assert(!irq_off64);
	irq_off64 = 1;
	assert(address_space_activate64(NULL) == 0);
	irq_off64 = 0;
	assert(address_space_destroy64(&copy_source64) == -16);
	assert(user_backing_free64(copy_backing64) == -16 && free_calls64 == frees);
	assert(address_space_map_backing64(copy_source64, 0x406000,
		copy_backing64, 0, 4096, 1) == -16);
	irq_off64 = 1;
	assert(address_space_activate64(copy_source64) == 0);
	irq_off64 = 0;
	copy_interrupts64++;
}

static void clone_spaces64(void)
{
	struct ADDRESS_SPACE64 *source = NULL, *child = NULL;
	struct USER_BACKING64 image = {0}, data = {0};
	static uint64_t before[8][512];
	const uintptr_t addresses[] = {0x400000, 0x402000, 0x403000, 0x405000,
		0x100000000ULL, 0x100200000ULL};
	const unsigned int permissions[] = {1, 5, 3, 3, 3, 3};
	const unsigned char contents[] = {0x31, 0x31, 0x31, 0x31, 0x72, 0x72};
	unsigned int tables = as_arch_table_count64();
	size_t baseline;
	uintptr_t alias_pa, pa;
	unsigned int flags;

	reset64(); assert(address_space_create64(&source) == 0);
	assert(user_backing_alloc64(&image, 4 * 4096) == 0);
	assert(user_backing_alloc64(&data, 2 * 4096) == 0);
	memset((void *) image.kernel_address, 0x31, image.size);
	memset((void *) data.kernel_address, 0x72, data.size);
	assert(address_space_map_backing64(source, addresses[0], &image, 0, 4096, 1) == 0);
	assert(address_space_map_backing64(source, addresses[1], &image, 8192, 4096, 5) == 0);
	assert(address_space_map_backing64(source, addresses[2], &image, 12288, 4096, 3) == 0);
	assert(address_space_map_backing64(source, addresses[3], &image, 12288, 4096, 3) == 0);
	assert(address_space_map_backing64(source, addresses[4], &data, 0, 4096, 3) == 0);
	assert(address_space_map_backing64(source, addresses[5], &data, 4096, 4096, 3) == 0);
	baseline = live_pages64; snapshot64(source, before);
	irq_off64 = 1;
	assert(address_space_activate64(source) == 0);
	assert(address_space_clone64(source, &child) == -22 && child == NULL);
	irq_off64 = 0;
	copy_source64 = source; copy_backing64 = &image; copy_interrupts64 = 0;
	copy_hook64 = interrupt_copy64;
	assert(address_space_clone64(source, &child) == 0 && child != NULL);
	copy_hook64 = NULL;
	assert(copy_interrupts64 == 2 && source->pins == 0 && active_space64 == source);
	assert(live_pages64 == baseline + tables + 6);
	for (unsigned int i = 0; i < 6; i++) {
		uintptr_t parent_pa;
		unsigned int parent_flags;

		assert(walk64(child, addresses[i], &pa, &flags) == 0 && flags == permissions[i]);
		assert(walk64(source, addresses[i], &parent_pa, &parent_flags) == 0);
		assert(pa != parent_pa && parent_flags == flags);
		for (size_t j = 0; j < 4096; j++) {
			assert(*(unsigned char *) as_cpu_alias64(pa + j) == contents[i]);
		}
	}
	assert(walk64(child, 0x403000, &pa, &flags) == 0);
	assert(walk64(child, 0x405000, &alias_pa, &flags) == 0 && alias_pa == pa);
	*(unsigned char *) as_cpu_alias64(pa) = 0x98;
	assert(*(unsigned char *) (image.kernel_address + 12288) == 0x31);
	*(unsigned char *) data.kernel_address = 0x45;
	assert(walk64(child, addresses[4], &pa, &flags) == 0);
	assert(*(unsigned char *) as_cpu_alias64(pa) == 0x72);
	*(unsigned char *) data.kernel_address = 0x72;
	assert(walk64(child, 0x401000, &pa, &flags) == -1);
	assert(walk64(child, 0x404000, &pa, &flags) == -1);
	assert(walk64(child, 0x1001ff000ULL, &pa, &flags) == -1);
	assert(walk64(child, 0x100210000ULL, &pa, &flags) == -1);
#ifdef TEST_AS_AARCH64
	assert(clean_count64 == 64);
	assert(walk64(child, 0x402000, &pa, &flags) == 0);
	assert(cleaned64[0] == as_cpu_alias64(pa));
	assert(cleaned64[63] == as_cpu_alias64(pa) + 4032);
#endif
	assert(address_space_destroy64(&child) == 0 && live_pages64 == baseline);
	/* Fail every table/backing allocation, then every possible rollback free. */
	for (unsigned int allocation = 0; allocation < tables + 2; allocation++) {
		unsigned int releases = allocation;

		for (unsigned int release = 0; release <= releases; release++) {
			event_count64 = clean_count64 = 0;
			alloc_fail64 = (int) (alloc_calls64 + allocation);
			free_fail64 = release == releases ? -1 : (int) (free_calls64 + release);
			free_persistent64 = release != releases;
			assert(address_space_clone64(source, &child) == -12);
			assert(source->pins == 0 && active_space64 == source && !irq_off64);
			if (release != releases) {
				assert(child != NULL && child->state == AS_REAP64);
				irq_off64 = 1;
				assert(address_space_activate64(child) == -16);
				irq_off64 = 0;
			} else { assert(child == NULL); }
			free_persistent64 = 0; alloc_fail64 = free_fail64 = -1;
			assert(address_space_destroy64(&child) == 0 && live_pages64 == baseline);
			assert(image.pins == 4 && data.pins == 2);
			unchanged64(source, before);
		}
	}
	for (unsigned int release = 0; release < tables + 2; release++) {
		event_count64 = clean_count64 = 0;
		assert(address_space_clone64(source, &child) == 0);
		free_fail64 = (int) (free_calls64 + release);
		assert(address_space_destroy64(&child) == -12 && child != NULL);
		free_fail64 = -1;
		assert(address_space_destroy64(&child) == 0 && live_pages64 == baseline);
	}
	irq_off64 = 1; assert(address_space_activate64(NULL) == 0); irq_off64 = 0;
	assert(address_space_destroy64(&source) == 0);
	assert(user_backing_free64(&data) == 0 && user_backing_free64(&image) == 0);
	assert(live_pages64 == tables);
}

#endif
