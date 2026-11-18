#ifndef MOWKOW64_SHUTDOWN64_H
#define MOWKOW64_SHUTDOWN64_H

struct CONSOLE64;

/* Task context with IRQs enabled; returns only if shutdown was cancelled.
 * Serializes requests, drains storage with IRQs enabled, then stops scheduling.
 * Application-private buffers (including unsaved editor text) are not saved.
 */
void shutdown64(struct CONSOLE64 *con);

#endif
