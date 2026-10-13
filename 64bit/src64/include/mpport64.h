#ifndef MOWKOW64_MPPORT64_H
#define MOWKOW64_MPPORT64_H

#include <stddef.h>
#include <stdint.h>

/* Task-context entries share one persistent VM. Claim/release are IRQ-safe;
 * rejected entries leave the owner untouched. Initialization and execution run
 * under NLR, and cleanup clears REPL/argv before release. Failed first init
 * discards partial VM roots/heap ownership for retry; successful VM state stays.
 */
void mpport_repl(void);
void mpport_run_file(const char *path);

/* `머꼬` 명령. arg가 NULL이면 REPL, 아니면 그 파일을 돌린다. */
void mpport_run_mowkow(const char *arg);

/* Initialized VM and active NLR required. Missing/open failure returns NULL;
 * exact read returns a GC allocation and byte count, including a 1-byte empty
 * allocation. Invalid arguments raise EINVAL; read mismatch frees the buffer
 * before EIO. out_size is zero on failure when non-NULL. Caller owns the buffer
 * and must m_del it or transfer to a reader with free_len=max(size, 1).
 * fd64_open cannot distinguish a missing file from other open failures.
 */
uint8_t *mpport_load_file(const char *path, size_t *out_size);

/* 콘솔 명령이 넘긴 인자를 넣고 뺀다. mowio.argv()가 이것을 읽는다.
   NULL이면 인자가 없다는 뜻이다. */
void mpport_set_argv(const char *arg);
const char *mpport_argv(void);

/* Architecture-neutral 100 Hz clock used by the Python HAL and mowio. */
uint64_t mpport_ticks_100hz(void);

#endif
