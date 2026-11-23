#ifndef MOWKOW64_UNIX_WAIT64_H
#define MOWKOW64_UNIX_WAIT64_H

#include <stdint.h>

/* Private futex keys are (mm, aligned user address). Task context only;
 * wait retains an mm lease until its scheduler continuation has resumed.
 */
int unix_futex_wait64(uintptr_t address, int32_t expected, uintptr_t timeout);
int unix_futex_wake64(uintptr_t address);
int unix_clock_get64(uintptr_t output);
/* Both platforms call once per 100-Hz IRQ, before scheduler selection. */
void unix_wait_tick64(uint64_t ticks);

#endif
