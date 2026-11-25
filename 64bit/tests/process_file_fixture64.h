#ifndef MOWKOW64_PROCESS_FILE_FIXTURE64_H
#define MOWKOW64_PROCESS_FILE_FIXTURE64_H

static void file_duplicates64(void)
{
	struct PROCESS64 process = {0};
	int fd = -1;

	install_file_fixture64(&process, 3, 7, 1);
	assert(process_file_dup64(&process, 3, 4, 0, &fd) == 0 && fd == 4);
	struct UNIX_FILE64 *shared = process.files[3].shared;

	assert(shared && shared->references == 2);
	assert(process.files[4].shared == shared && !process.files[4].cloexec);
	assert(shared->handle.pos == 7);
	shared->handle.pos = 19;
	assert(process.files[4].shared->handle.pos == 19);
	assert(process_file_dup264(&process, 3, 3) == 0);
	assert(process.files[3].cloexec && shared->references == 2);
	assert(process_file_dup264(&process, 3, 4) == 0 && shared->references == 2);
	assert(process_file_dup64(&process, 3, 6, 1, &fd) == 0 && fd == 6);
	assert(process.files[6].cloexec);
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == 0 && fd == 5);
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == 0 && fd == 7);
	fd = 99;
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == -24 && fd == 99);
	assert(shared->references == 5);
	process_files_close64(&process, 1);
	assert(!process.files[3].used && !process.files[6].used);
	assert(shared->references == 3 && process.files[4].used);

	/* Replace a different OFD and leave another alias of the old one alive. */
	install_file_fixture64(&process, 3, 41, 0);
	assert(process_file_dup264(&process, 3, 6) == 0);
	struct UNIX_FILE64 *other = process.files[3].shared;

	assert(other != shared && other->references == 2);
	process.files[6].cloexec = 1;
	assert(process_file_dup264(&process, 4, 6) == 0);
	assert(!process.files[6].cloexec && process.files[6].shared == shared);
	assert(other->references == 1 && other->handle.pos == 41);
	assert(shared->references == 4 && shared->handle.pos == 19);
	assert(process_file_dup264(&process, 4, 3) == 0);
	assert(other->references == 0 && shared->references == 5);

	/* Bad source and exhausted reference count cannot close the target. */
	assert(process_file_dup264(&process, -1, 3) == -9);
	assert(process_file_dup264(&process, 4, PROCESS64_MAX_FILES) == -9);
	shared->references = ~0U;
	assert(process_file_dup264(&process, 4, 3) == -75);
	assert(process.files[3].shared == shared && shared->references == ~0U);
	shared->references = 5;
	process_files_close64(&process, 0);
	assert(shared->references == 0);
	assert(process_file_dup264(&process, 3, 3) == -9);
	fd = 99;
	assert(process_file_dup64(&process, 3, 3, 0, &fd) == -9 && fd == 99);
	assert(process_file_dup64(&process, 3, 2, 0, &fd) == -22);
	assert(process_file_dup64(&process, 3, 3, 2, &fd) == -22);
	assert(process_file_dup64(&process, 3, 3, 0, NULL) == -22);
	assert(process_file_dup264(NULL, 3, 4) == -22);
	assert(!irq_off64);
}

static void file_cloexec64(void)
{
	struct PROCESS64 parent = {0}, child = {0};
	int value = 99;

	install_file_fixture64(&parent, 3, 17, 0);
	assert(process_file_dup264(&parent, 3, 4) == 0);
	assert(process_file_set_cloexec64(&parent, 3, 1) == 0);
	assert(process_file_inherit64(&parent, &child) == 0);
	struct UNIX_FILE64 *shared = parent.files[3].shared;

	assert(shared->references == 4);
	assert(process_file_get_cloexec64(&child, 3, &value) == 0 && value == 1);
	assert(process_file_set_cloexec64(&child, 3, 0) == 0);
	assert(process_file_set_cloexec64(&child, 4, 1) == 0);
	assert(process_file_get_cloexec64(&parent, 3, &value) == 0 && value == 1);
	assert(process_file_get_cloexec64(&parent, 4, &value) == 0 && value == 0);
	assert(process_file_get_cloexec64(&child, 3, &value) == 0 && value == 0);
	assert(process_file_set_cloexec64(&child, 4, 2) == -22);
	assert(process_file_get_cloexec64(&child, 4, &value) == 0 && value == 1);
	assert(shared->references == 4 && shared->handle.pos == 17);
	process_files_close64(&child, 1);
	process_files_close64(&parent, 1);
	assert(child.files[3].used && !child.files[4].used);
	assert(!parent.files[3].used && parent.files[4].used);
	assert(shared->references == 2);

	value = 99;
	const int invalid[] = {-1, 0, 2, 3, PROCESS64_MAX_FILES};

	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		assert(process_file_get_cloexec64(&parent, invalid[i], &value) == -9);
		assert(value == 99);
		assert(process_file_set_cloexec64(&parent, invalid[i], 1) == -9);
	}
	assert(process_file_get_cloexec64(NULL, 4, &value) == -22 && value == 99);
	assert(process_file_get_cloexec64(&parent, 4, NULL) == -22);
	assert(process_file_set_cloexec64(NULL, 4, 1) == -22);
	assert(process_file_set_cloexec64(&parent, 4, -1) == -22);
	assert(process_file_get_cloexec64(&parent, 4, &value) == 0 && value == 0);
	process_files_close64(&parent, 0);
	process_files_close64(&child, 0);
	assert(shared->references == 0 && !irq_off64);
}

static void stdio_cloexec64(void)
{
	struct PROCESS64 parent = {0}, child = {0};
	int value = 99;

	assert(process_file_get_cloexec64(&parent, 0, &value) == -9 && value == 99);
	assert(process_file_set_cloexec64(&parent, 0, 1) == -9);
	parent.console = (struct CONSOLE64 *) &console_tokens64[0];
	child.console = parent.console;
	for (int fd = 0; fd < 3; fd++) {
		assert(process_file_get_cloexec64(&parent, fd, &value) == 0 && value == 0);
		assert(process_file_set_cloexec64(&parent, fd, 1) == 0);
	}
	assert(process_file_set_cloexec64(&parent, 1, 0) == 0);
	parent.closed_stdio = 4;
	assert(process_file_inherit64(&parent, &child) == 0);
	assert(child.closed_stdio == 4);
	assert(process_file_get_cloexec64(&child, 0, &value) == 0 && value == 1);
	assert(process_file_set_cloexec64(&child, 0, 0) == 0);
	assert(process_file_set_cloexec64(&child, 1, 1) == 0);
	value = 99;
	assert(process_file_get_cloexec64(&child, 2, &value) == -9 && value == 99);
	assert(process_file_set_cloexec64(&child, 2, 0) == -9);
	process_files_close64(&parent, 1);
	process_files_close64(&child, 1);
	assert(parent.closed_stdio == 5 && child.closed_stdio == 6);
	assert(process_file_get_cloexec64(&parent, 1, &value) == 0 && value == 0);
	assert(process_file_get_cloexec64(&child, 0, &value) == 0 && value == 0);
	process_files_close64(&child, 1);
	assert(child.closed_stdio == 6);
	process_files_close64(&parent, 0);
	process_files_close64(&child, 0);
	assert(parent.closed_stdio == 7 && child.closed_stdio == 7);
	for (int fd = 0; fd < 3; fd++) {
		assert(!child.files[fd].cloexec && child.files[fd].shared == NULL);
	}
	assert(!irq_off64);
}

static void file_pool_exhaustion64(void)
{
	struct PROCESS64 process = {0};
	struct PROCESS64_FILE held[PROCESS64_MAX * (PROCESS64_MAX_FILES - 3)] = {0};
	int fd = 99;

	/* Fill the actual fixed OFD pool, keeping each reference for cleanup. */
	uint64_t irq = platform_irq_save64();

	for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
		held[i].used = 1;
		held[i].shared = allocate_file64();
		assert(held[i].shared != NULL);
	}
	process.files[3] = held[0]; process.files[4] = held[1];
	memset(&held[0], 0, 2 * sizeof(held[0]));
	platform_irq_restore64(irq);
	process.files[3].shared->handle.pos = 17;
	process.files[4].shared->handle.pos = 23;
	process.files[4].cloexec = 1;
	/* Dup needs no new OFD, even with every pool entry reserved. */
	assert(process_file_dup64(&process, 3, 5, 0, &fd) == 0 && fd == 5);
	irq = platform_irq_save64();
	assert(allocate_file64() == NULL);
	platform_irq_restore64(irq);
	assert(process_file_dup264(&process, 3, 4) == 0);
	assert(process.files[4].shared->handle.pos == 17 && !process.files[4].cloexec);
	assert(process.files[3].shared->references == 3);
	/* Replacing the last reference to the old target makes one slot reusable. */
	irq = platform_irq_save64();
	held[0].shared = allocate_file64();
	assert(held[0].shared != NULL && held[0].shared->handle.pos == 0);
	platform_irq_restore64(irq);
	process_files_close64(&process, 0);
	for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
		process_file_close64(&held[i]);
	}
	for (size_t i = 0; i < sizeof(open_files64) / sizeof(open_files64[0]); i++) {
		assert(open_files64[i].references == 0);
	}
	assert(!irq_off64);
}

#endif
