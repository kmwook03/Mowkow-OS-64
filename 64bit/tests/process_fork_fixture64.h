#ifndef MOWKOW64_PROCESS_FORK_FIXTURE64_H
#define MOWKOW64_PROCESS_FORK_FIXTURE64_H

static void signal_boundaries64(struct PROCESS64 *parent)
{
	uintptr_t stack = parent->memory->stack.base + parent->memory->stack.size;
	_Alignas(16) unsigned char fp[512];

	memset(fp, 0x6b, sizeof(fp));
#ifdef TEST_AS_AARCH64
	struct ARCH64_EXCEPTION_FRAME frame = {.elr = 0x401100, .sp_el0 = stack};

	frame.x[0] = 0x7788; frame.q[3] = 0x9988; frame.fpcr = 0x400000;
#else
	struct INTERRUPT_FRAME64 frame = {.rip = 0x401100, .rsp = stack,
		.cs = 0x33, .ss = 0x2b, .rflags = 0x202, .rax = 0x7788, .r15 = 0x9988};
#endif
	capture_native64 = 1;
	assert(process64_signal_set64(parent->memory->heap.base, 0x401200) == -14);
	assert(process64_signal_set64(0x401100, 0) == -22);
	assert(process64_signal_set64(0x401100, 0x401200) == 0);
	assert(process64_signal_user64(UNIX64_SYS_SIGNAL_RETURN, &frame, fp) == -22);
	parent->started = 1;
	wake_fail64 = 1;
	assert(process64_signal_send64(parent->pid) == 0);
	assert(process64_signal_pending64(parent));
	int wakes = waiter_wakes64;

	wake_fail64 = 0; process64_wake_tick64();
	assert(waiter_wakes64 > wakes);
	/* A kernel return must leave pending delivery untouched. */
#ifdef TEST_AS_AARCH64
	frame.spsr = 5;
#else
	frame.cs = 8;
#endif
	assert(process64_native_return64(&frame, fp) == 0 && parent->signal_pending);
#ifdef TEST_AS_AARCH64
	frame.spsr = 0;
#else
	frame.cs = 0x33;
#endif
	assert(process64_native_return64(&frame, fp) == 0 && parent->signal_active);
	assert(!parent->signal_pending);
	assert(process64_signal_user64(UNIX64_SYS_SIGNAL_RAISE, &frame, fp) == -16);
	assert(process64_signal_send64(parent->pid) == 0);
	assert(!process64_signal_pending64(parent));
#ifdef TEST_AS_AARCH64
	frame.sp_el0 -= 16;
#else
	frame.rsp -= 16;
#endif
	assert(process64_signal_user64(UNIX64_SYS_SIGNAL_RETURN, &frame, fp) == -22);
	assert(parent->signal_active);
#ifdef TEST_AS_AARCH64
	frame.sp_el0 = parent->signal_return_sp;
	frame.q[3] = 0; frame.fpcr = 0;
#else
	frame.rsp = parent->signal_return_sp;
	memset(fp, 0, sizeof(fp)); frame.r15 = 0;
#endif
	assert(process64_signal_user64(UNIX64_SYS_SIGNAL_RETURN, &frame, fp) == 0);
	assert(!parent->signal_active && process64_signal_pending64(parent));
	assert(tls_pointer64 == 0x12345000);
#ifdef TEST_AS_AARCH64
	assert(frame.x[0] == 0x7788 && frame.q[3] == 0x9988 && frame.fpcr == 0x400000);
	assert(frame.sp_el0 == stack);
	frame.sp_el0 = parent->memory->stack.base;
#else
	assert(frame.rax == 0x7788 && frame.r15 == 0x9988 && frame.rsp == stack);
	for (size_t i = 0; i < sizeof(fp); i++) { assert(fp[i] == 0x6b); }
	frame.rsp = parent->memory->stack.base;
#endif
	assert(process64_signal_user64(UNIX64_SYS_SIGNAL_RAISE, &frame, fp) == -14);
	assert(!parent->signal_active);
	assert(process64_signal_set64(0, 0) == 0 && !parent->signal_pending);
	parent->started = 0; capture_native64 = 0;
}

static void fork_transaction64(void)
{
	struct PROCESS64 *parent = NULL;
	struct TASK64 task = {0};
	struct USER_CONTEXT64 context = {.tls = 0x12345000};
	size_t empty = live_pages64;
	int64_t pid;

	fixture64(); assert(prepare64(&parent) == 0);
	parent->state = PROCESS_RUNNING64; parent->thread.task = &task;
	task.process = parent; current_task64 = &task;
	irq_off64 = 1; assert(address_space_activate64(parent->memory->space) == 0); irq_off64 = 0;
	unix_fixture64(); assert(exec_fixture64(parent) == 0);
	install_file_fixture64(parent, 3, 7, 0);
	install_file_fixture64(parent, 4, 0, 1);
	parent->closed_stdio = 4;
	parent->memory->anon_pages[0] = 1;
	*(uint64_t *) parent->memory->heap_backing = 0x12345678;
	size_t base = live_pages64;
	unsigned int allocations = alloc_calls64;

	pid = process64_fork_user64(&context, NULL);
	assert(pid > 1);
	allocations = alloc_calls64 - allocations;
	struct PROCESS64 *child = process_by_pid64((uint32_t) pid);

	assert(child && child->started && child->parent_pid == parent->pid);
	assert(child->thread.fork_context.tls == context.tls);
	assert(child->files[3].shared == parent->files[3].shared);
	assert(child->files[3].shared->references == 2 && child->files[4].cloexec);
	assert(child->closed_stdio == 4 && child->memory->anon_pages[0] == 1);
	assert(child->memory->heap_backing != parent->memory->heap_backing);
	assert(*(uint64_t *) child->memory->heap_backing == 0x12345678);
	*(uint64_t *) child->memory->heap_backing = 9;
	assert(*(uint64_t *) parent->memory->heap_backing == 0x12345678);
	child->files[3].shared->handle.pos = 19;
	assert(parent->files[3].shared->handle.pos == 19);
	assert(process64_wait_user64(pid, (void *) parent->memory->heap.base, 1) == 0);
	assert(*(uint64_t *) parent->memory->heap_backing == 0x12345678);
	/* Model a detached, parked runner; real task switching is tested in QEMU. */
	child->state = PROCESS_REAP64; child->wait_status = 43 << 8;
	child->reap_pending = 1;
	free_fail64 = (int) free_calls64; free_persistent64 = 1;
	process64_collect();
	assert(child->state == PROCESS_REAP64 && child->fork_task != NULL);
	free_fail64 = -1; free_persistent64 = 0;
	process64_collect();
	assert(child->state == PROCESS_ZOMBIE64 && child->fork_task == NULL);
	assert(live_pages64 == base && parent->files[3].shared->references == 1);
	assert(process64_wait_user64(pid, (void *) 0x401000, 0) == -14);
	assert(child->state == PROCESS_ZOMBIE64);
	assert(process64_wait_user64(pid, (void *) parent->memory->heap.base, 0) == pid);
	assert(*(int32_t *) parent->memory->heap_backing == 43 << 8);
	assert(process64_wait_user64(pid, NULL, 0) == -10);
	assert(process64_wait_user64(1LL << 32, NULL, 0) == -22);
	assert(process64_wait_user64(-1, NULL, 1ULL << 32) == -22);
	for (unsigned int i = 0; i < allocations; i++) {
		alloc_fail64 = (int) (alloc_calls64 + i);
		assert(process64_fork_user64(&context, NULL) == -12);
		alloc_fail64 = -1; process64_collect();
		assert(live_pages64 == base && parent->files[3].shared->references == 1);
	}
	alloc_fail64 = (int) alloc_calls64 + (int) allocations - 1;
	free_fail64 = (int) free_calls64; free_persistent64 = 1;
	assert(process64_fork_user64(&context, NULL) == -12);
	assert(live_pages64 > base);
	alloc_fail64 = free_fail64 = -1; free_persistent64 = 0;
	process64_collect(); assert(live_pages64 == base);
	fork_entry_fail64 = 1;
	assert(process64_fork_user64(&context, NULL) == -12);
	fork_entry_fail64 = 0; fork_admit_fail64 = 1;
	assert(process64_fork_user64(&context, NULL) == -12);
	fork_admit_fail64 = 0;
	assert(live_pages64 == base && parent->files[3].shared->references == 1);
	signal_boundaries64(parent);
	irq_off64 = 1; assert(address_space_activate64(NULL) == 0); irq_off64 = 0;
	task.process = NULL; parent->thread.task = NULL; parent->state = PROCESS_READY64;
	assert(process64_discard_prepared(&parent) == 0 && live_pages64 == empty);
	current_task64 = NULL;
}

#endif
