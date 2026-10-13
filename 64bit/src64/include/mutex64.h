#ifndef MOWKOW64_MUTEX64_H
#define MOWKOW64_MUTEX64_H

#include <mtask64.h>
#include <stdint.h>

#define MUTEX64_ERR_BUSY    (-16)
#define MUTEX64_ERR_INVALID (-22)

/* Task-context mutex. Lock acquisition may sleep and must never run in IRQ
   context. The zero-filled state is an initialized, unlocked mutex. */
struct MUTEX64 {
	struct TASK64 *owner;
	struct TASK64 *waiters[MAX_TASKS64];
	uint32_t waiter_count;
	uint8_t locked;
};

void mutex64_init(struct MUTEX64 *mutex);
int mutex64_lock(struct MUTEX64 *mutex);
int mutex64_unlock(struct MUTEX64 *mutex);

#endif
