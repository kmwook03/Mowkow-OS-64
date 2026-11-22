#ifndef MOWKOW64_PROCESS_EXEC_FIXTURE64_H
#define MOWKOW64_PROCESS_EXEC_FIXTURE64_H

static void unix_fixture64(void)
{
	struct ELF64_EHDR *header = (void *) elf_bytes64;
	struct ELF64_PHDR *segments = (void *) (elf_bytes64 + 64);
	const uint8_t note[24] = {7, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0,
		'M', 'O', 'W', 'K', 'O', 'W', 0, 0, 1, 0, 0, 0};

	fixture64();
	header->e_phnum = 7;
	header->e_entry = 0x401000;
	segments[0] = (struct ELF64_PHDR) {1, 4, 0, 0x400000, 0, 0x1000, 0x1000, 0x1000};
	segments[1] = (struct ELF64_PHDR) {1, 5, 0x1000, 0x401000, 0, 0x1000, 0x1000, 0x1000};
	segments[2] = (struct ELF64_PHDR) {1, 6, 0x2000, 0x402000, 0, 0x1000, 0x1000, 0x1000};
	segments[3] = (struct ELF64_PHDR) {4, 4, 0x300, 0x400300, 0, 24, 24, 4};
	segments[4] = (struct ELF64_PHDR) {7, 4, 0x2100, 0x402100, 0, 8, 32, 16};
	segments[5] = (struct ELF64_PHDR) {6, 4, 64, 0x400040, 0, 392, 392, 8};
	segments[6] = (struct ELF64_PHDR) {0x6474e551, 6, 0, 0, 0, 0, 0, 16};
	memcpy(elf_bytes64 + 0x300, note, sizeof(note));
	memset(elf_bytes64 + 0x2100, 0x67, 8);
}

static int exec_fixture64(struct PROCESS64 *owner)
{
	uintptr_t base = owner->memory->heap.base;
	uint8_t *alias = (void *) owner->memory->heap_storage.kernel_address;
	uint64_t *argv = (void *) (alias + 0x100);
	uint64_t *envp = (void *) (alias + 0x120);

	memcpy(alias, "fixture", 8);
	memcpy(alias + 0x20, "hello", 6);
	memcpy(alias + 0x30, "한글", 7);
	memcpy(alias + 0x40, "LANG=ko", 8);
	argv[0] = base + 0x20; argv[1] = base + 0x30; argv[2] = 0;
	envp[0] = base + 0x40; envp[1] = 0;
	return process64_execve_user64((void *) base, (void *) (base + 0x100),
		(void *) (base + 0x120));
}

static void exec_transaction64(void)
{
	struct PROCESS64 *owner = NULL;
	struct TASK64 task = {0};
	struct PROCESS_IMAGE64 *old;
	unsigned int count = as_arch_table_count64();
	size_t baseline, empty = live_pages64;
	static uint64_t before[8][512];

	fixture64(); assert(prepare64(&owner) == 0);
	old = owner->memory;
	baseline = live_pages64; snapshot64(old->space, before);
	owner->state = PROCESS_RUNNING64; owner->thread.task = &task;
	owner->thread.saved_kernel_rsp = 0x12340;
	task.process = owner; current_task64 = &task;
	owner->files[3].used = owner->files[4].used = owner->files[4].cloexec = 1;
	irq_off64 = 1; assert(address_space_activate64(old->space) == 0); irq_off64 = 0;
	assert(process64_set_tls64(old->heap.base + 0x800) == 0);
	assert(tls_pointer64 == old->heap.base + 0x800);
	assert(process64_set_tls64(old->heap.base + 1) == -14);
	assert(process64_set_tls64(0x400000) == -14);
	/* Snapshot, all tables, ELF snapshot, image, stack and heap allocations. */
	for (unsigned int i = 0; i < count + 5; i++) {
		unix_fixture64();
		alloc_fail64 = (int) (alloc_calls64 + i);
		assert(exec_fixture64(owner) == -12);
		alloc_fail64 = -1;
		assert(owner->memory == old && active_space64 == old->space);
		assert(owner->files[3].used && owner->files[4].used);
		assert(owner->thread.saved_kernel_rsp == 0x12340 && live_pages64 == baseline);
		assert(tls_pointer64 == old->heap.base + 0x800);
		unchanged64(old->space, before);
	}
	/* Failed rollback free keeps the bank until a later exec retries. */
	unix_fixture64(); alloc_fail64 = (int) alloc_calls64 + (int) count + 4;
	free_fail64 = (int) free_calls64 + 1; free_persistent64 = 1;
	assert(exec_fixture64(owner) == -12 && owner->memory == old);
	assert(owner->images[1].space != NULL && owner->exec_storage.size != 0);
	free_persistent64 = 0; alloc_fail64 = free_fail64 = -1;
	assert(process64_release_image(&owner->images[1]) == 0);
	assert(user_backing_free64(&owner->exec_storage) == 0 && live_pages64 == baseline);
	for (unsigned int defect = 0; defect < 10; defect++) {
		struct ELF64_PHDR *segments = (void *) (elf_bytes64 + 64);

		unix_fixture64();
		switch (defect) {
		case 0: segments[0].p_offset = 0x1000; break;
		case 1: segments[4].p_filesz = 33; break;
		case 2: segments[4].p_align = 3; break;
		case 3: segments[4].p_offset = UINT64_MAX; break;
		case 4: segments[6] = segments[4]; break;
		case 5: segments[4].p_memsz = 0x100001; break;
		case 6: segments[5].p_vaddr++; break;
		case 7: segments[6].p_type = 3; break;
		case 8: segments[1].p_flags = 7; break;
		case 9: elf_bytes64[0] = 0; break;
		}
		assert(exec_fixture64(owner) == -8 && owner->memory == old);
		assert(live_pages64 == baseline);
	}
	unix_fixture64();
	assert(process64_execve_user64(NULL, NULL, NULL) == -14);
	assert(process64_execve_user64((void *) old->heap.base, (void *) UINTPTR_MAX, NULL) == -14);
	uint64_t *vector = (void *) (old->heap_storage.kernel_address + 0x1000);

	for (size_t i = 0; i < 257; i++) { vector[i] = old->heap.base; }
	assert(process64_execve_user64((void *) old->heap.base,
		(void *) (old->heap.base + 0x1000), NULL) == -7);
	assert(process64_execve_user64((void *) old->heap.base,
		NULL, (void *) (old->heap.base + 0x1000)) == -7);
	vector[0] = old->heap.base + 0x3000; vector[1] = 0;
	memset((void *) (old->heap_storage.kernel_address + 0x3000), 'a', 65536);
	assert(process64_execve_user64((void *) old->heap.base,
		(void *) (old->heap.base + 0x1000), NULL) == -7);
	assert(live_pages64 == baseline && owner->memory == old);
	assert(address_space_pin64(old->space) == 0);
	assert(exec_fixture64(owner) == -16 && owner->memory == old);
	assert(address_space_unpin64(old->space) == 0 && live_pages64 == baseline);
	struct ADDRESS_SPACE64 *occupied[3] = {0};

	for (unsigned int i = 0; i < 3; i++) { assert(address_space_create64(&occupied[i]) == 0); }
	assert(exec_fixture64(owner) == -12 && owner->memory == old);
	for (unsigned int i = 0; i < 3; i++) { assert(address_space_destroy64(&occupied[i]) == 0); }
	owner->console = (struct CONSOLE64 *) &console_tokens64[0];
	closing_console64 = owner->console;
	assert(exec_fixture64(owner) == -4 && owner->memory == old && live_pages64 == baseline);
	owner->console = NULL; closing_console64 = NULL;
	/* A root/backing retirement error after commit cannot turn success into
	 * failure or free the still-running supervisor continuation stack.
	 */
	unix_fixture64(); free_fail64 = (int) free_calls64 + 1;
	uint32_t pid = owner->pid, parent = owner->parent_pid;
	uintptr_t entry = owner->thread.entry_storage.kernel_address;

	assert(exec_fixture64(owner) == 0);
	assert(owner->memory != old && old->space != NULL && old->space->state == AS_REAP64);
	free_fail64 = -1;
	assert(process64_release_image(old) == 0);
	assert(owner->pid == pid && owner->parent_pid == parent && task.process == owner);
	assert(owner->files[3].used && !owner->files[4].used && tls_pointer64 == 0);
	assert(owner->thread.entry_storage.kernel_address == entry);
	assert(owner->thread.saved_kernel_rsp == 0x12340 && owner->memory->abi == 1);
	assert(owner->memory->tls_address == 0x402100 && owner->memory->tls_filesz == 8);
	assert(owner->memory->tls_memsz == 32 && owner->memory->tls_align == 16);
	uint64_t *stack = (void *) (owner->memory->stack_storage.kernel_address +
		owner->memory->initial_rsp - owner->memory->stack.base);
	char text[32];

	assert((owner->memory->initial_rsp & 15) == 0 && stack[0] == 2);
	assert(stack[3] == 0 && stack[5] == 0);
	assert(copy_string_from_user64(text, sizeof(text), (void *) stack[1]) == 5);
	assert(strcmp(text, "hello") == 0);
	assert(copy_string_from_user64(text, sizeof(text), (void *) stack[2]) == 6);
	assert(strcmp(text, "한글") == 0);
	assert(copy_string_from_user64(text, sizeof(text), (void *) stack[4]) == 7);
	assert(strcmp(text, "LANG=ko") == 0);
	assert(stack[6] == 3 && stack[7] == 0x400040);
	assert(stack[8] == 4 && stack[9] == 56 && stack[10] == 5 && stack[11] == 7);
	assert(stack[12] == 6 && stack[13] == 4096);
	assert(stack[34] == 31 && stack[36] == 0 && stack[37] == 0);
	assert(copy_string_from_user64(text, sizeof(text), (void *) stack[35]) == 7);
	assert(strcmp(text, "fixture") == 0);
	assert(stack[30] == 25 && copy_from_user64(text, (void *) stack[31], 16) == 0);
	/* Repeat with both banks alternating; no PID/slot/entry-stack churn. */
	baseline = live_pages64;
	for (unsigned int i = 0; i < 8; i++) {
		unix_fixture64(); assert(exec_fixture64(owner) == 0);
		assert(live_pages64 == baseline && owner->pid == pid);
	}
	irq_off64 = 1; assert(address_space_activate64(NULL) == 0); irq_off64 = 0;
	current_task64 = NULL; owner->thread.task = NULL; owner->state = PROCESS_READY64;
	assert(process64_discard_prepared(&owner) == 0 && live_pages64 == empty);
}

#endif
