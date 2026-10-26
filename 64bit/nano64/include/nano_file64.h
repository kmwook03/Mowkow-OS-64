#ifndef MOWKOW64_NANO_FILE64_H
#define MOWKOW64_NANO_FILE64_H

#include <stddef.h>
#include "nano_errno64.h"

#define NANO_EOF64 (-1)
#define NANO_STREAMS64 8
#define NANO_FILE_LIMIT64 65536

struct NANO_FILE64;
const char *nano_file_path64(const char *path);
int nano_snapshot64(struct NANO_FILE64 *stream);

struct NANO_FILE64;
extern struct NANO_FILE64 *nano_stdout64;
extern struct NANO_FILE64 *nano_stderr64;

/* Private nano libc boundary, single application execution context only.
 * No hosted FILE layout or global libc errno is shared with the CRT/VM.
 * Local validation and SYS_FILE use explicit errno values. Ambiguous legacy
 * write/close errors map to EIO (5), never inferred ENOSPC/permission errors.
 */
/* Only r/rb/w/wb are supported. Text and binary preserve identical bytes.
 * Update, append and exclusive modes fail with ENOSYS before opening a file.
 * fopen reserves a stream before O_TRUNC can mutate a file. fdopen neither
 * opens nor truncates: success transfers an existing fd >= 3 to the stream;
 * failure leaves it with the caller. Duplicate stream ownership is rejected.
 * Do not access the fd separately until the stream has been closed.
 */
struct NANO_FILE64 *nano_fopen64(const char *path, const char *mode);
struct NANO_FILE64 *nano_fdopen64(int fd, const char *mode);

/* fread/fwrite return whole elements, including after a partial failure.
 * Zero-sized requests do no I/O. Overflow is rejected before buffer access.
 * Reads have a private 512-byte cache; writes retry positive short counts
 * without buffering. A zero write is EIO; a zero read is EOF. Checked
 * FILE_READ64 distinguishes pre-EOF storage failure from EOF. nano_snapshot64
 * stages at most 64 KiB and verifies exact bytes and EOF before publication.
 * Errors are sticky and block further I/O until clearerr; EOF is independent.
 */
size_t nano_fread64(void *data, size_t size, size_t count,
	struct NANO_FILE64 *stream);
size_t nano_fwrite64(const void *data, size_t size, size_t count,
	struct NANO_FILE64 *stream);
int nano_getc64(struct NANO_FILE64 *stream);
int nano_putc64(int byte, struct NANO_FILE64 *stream);
int nano_ferror64(struct NANO_FILE64 *stream);
int nano_feof64(struct NANO_FILE64 *stream);
void nano_clearerr64(struct NANO_FILE64 *stream);
int nano_fileno64(struct NANO_FILE64 *stream);

/* No pending output exists: fflush checks stream errors, not disk durability.
 * NULL flushes all output streams. POSIX fsync uses FILE_SYNC64 separately.
 * fclose releases the stream only after a successful close syscall. A close
 * failure retains ownership for retry. Earlier I/O failure is reported even
 * when close succeeds (then the stream is released, as with libc fclose).
 * Closed handles are invalid; a reused pool address cannot detect stale use.
 */
int nano_fflush64(struct NANO_FILE64 *stream);
int nano_fclose64(struct NANO_FILE64 *stream);

#endif
