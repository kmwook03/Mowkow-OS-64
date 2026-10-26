#include <mowos.h>
#include <stdint.h>

#include "include/nano_file64.h"

#define NANO_READ_BUFFER64 512

struct NANO_FILE64 {
	int used64;
	int fd64;
	int writing64;
	int error64;
	int eof64;
	size_t next64;
	size_t end64;
	unsigned char *snapshot64;
	size_t snapshot_size64;
	size_t snapshot_at64;
	unsigned char buffer64[NANO_READ_BUFFER64];
};

static struct NANO_FILE64 streams64[NANO_STREAMS64];
/* Fixed console descriptors have process lifetime and cannot be fclose'd. */
static struct NANO_FILE64 output_streams64[2] = {
	{ .used64 = 1, .fd64 = 1, .writing64 = 1 },
	{ .used64 = 1, .fd64 = 2, .writing64 = 1 },
};
struct NANO_FILE64 *nano_stdout64 = &output_streams64[0];
struct NANO_FILE64 *nano_stderr64 = &output_streams64[1];
int nano_errno64;

/* Flat FAT root only; never discard parent or subdirectory components. */
const char *nano_file_path64(const char *path)
{
	if (path == NULL || *path == 0) { nano_errno64 = NANO_EINVAL64; return NULL; }
	if (*path == '/') { path++; }
	while (path[0] == '.' && path[1] == '/') { path += 2; }
	if (strcmp(path, ".") == 0 || *path == 0) { return "/"; }
	size_t length = 0;

	while (path[length]) {
		if (path[length] == '/') { nano_errno64 = NANO_ENOTDIR64; return NULL; }
		if (++length >= FILE_NAME_CAPACITY64) { nano_errno64 = NANO_ENAMETOOLONG64; return NULL; }
	}
	return path;
}

static int mode64(const char *mode)
{
	if (mode == NULL || mode[0] == '\0') {
		nano_errno64 = NANO_EINVAL64;
		return -1;
	}
	if ((mode[0] == 'r' || mode[0] == 'w') &&
		(mode[1] == '\0' || (mode[1] == 'b' && mode[2] == '\0'))) {
		return mode[0] == 'w';
	}
	/* In particular, never downgrade append/update/exclusive to truncate. */
	nano_errno64 = NANO_ENOSYS64;
	return -1;
}

static struct NANO_FILE64 *available64(void)
{
	for (size_t i = 0; i < NANO_STREAMS64; i++) {
		if (!streams64[i].used64) {
			return &streams64[i];
		}
	}
	nano_errno64 = NANO_EMFILE64;
	return NULL;
}

static int valid64(struct NANO_FILE64 *stream)
{
	if (stream == &output_streams64[0] || stream == &output_streams64[1]) { return 1; }
	/* Compare opaque handles before dereferencing an untrusted pointer. */
	for (size_t i = 0; i < NANO_STREAMS64; i++) {
		if (stream == &streams64[i] && streams64[i].used64) {
			return 1;
		}
	}
	nano_errno64 = NANO_EBADF64;
	return 0;
}

static int fail64(struct NANO_FILE64 *stream, int error)
{
	if (stream->error64 == 0) {
		stream->error64 = error;
	}
	nano_errno64 = stream->error64;
	return NANO_EOF64;
}

static void bind64(struct NANO_FILE64 *stream, int fd, int writing)
{
	memset(stream, 0, sizeof(*stream));
	stream->fd64 = fd;
	stream->writing64 = writing;
	stream->used64 = 1;
}

struct NANO_FILE64 *nano_fopen64(const char *path, const char *mode)
{
	int writing = mode64(mode);
	struct NANO_FILE64 *stream;
	int fd;

	if (writing < 0) { return NULL; }
	if (path == NULL || path[0] == '\0') {
		nano_errno64 = NANO_EINVAL64;
		return NULL;
	}
	stream = available64();
	if (stream == NULL) { return NULL; }
	const char *name = nano_file_path64(path);

	if (name == NULL) { return NULL; }
	fd = (int) file_call64(FILE_OPEN64, (unsigned long) name,
		writing ? FILE_CREATE64 | FILE_TRUNCATE64 | FILE_WRITING64 : 0, 0);
	if (fd < 0) {
		nano_errno64 = -fd;
		return NULL;
	}
	bind64(stream, fd, writing);
	return stream;
}

struct NANO_FILE64 *nano_fdopen64(int fd, const char *mode)
{
	int writing = mode64(mode);
	struct NANO_FILE64 *stream;

	if (writing < 0) { return NULL; }
	if (fd < 3) {
		nano_errno64 = NANO_EBADF64;
		return NULL;
	}
	for (size_t i = 0; i < NANO_STREAMS64; i++) {
		if (streams64[i].used64 && streams64[i].fd64 == fd) {
			nano_errno64 = NANO_EINVAL64;
			return NULL;
		}
	}
	stream = available64();
	if (stream != NULL) { bind64(stream, fd, writing); }
	return stream;
}

static int request64(struct NANO_FILE64 *stream, const void *data,
	size_t size, size_t count, int writing)
{
	if (!valid64(stream)) { return 0; }
	if (size == 0 || count == 0) { return 0; }
	if (stream->error64 != 0) {
		nano_errno64 = stream->error64;
		return 0;
	}
	if (stream->writing64 != writing) {
		fail64(stream, NANO_EBADF64);
		return 0;
	}
	if (data == NULL) {
		fail64(stream, NANO_EINVAL64);
		return 0;
	}
	/* Bound both the syscall's signed count and pointer endpoint first. */
	if (count > (size_t) __LONG_MAX__ / size ||
		size * count > UINTPTR_MAX - (uintptr_t) data) {
		fail64(stream, NANO_EOVERFLOW64);
		return 0;
	}
	return 1;
}

size_t nano_fread64(void *data, size_t size, size_t count,
	struct NANO_FILE64 *stream)
{
	unsigned char *out = data;
	size_t done = 0;
	size_t total;

	if (!request64(stream, data, size, count, 0)) { return 0; }
	total = size * count;
	if (stream->snapshot64 != NULL) {
		size_t take = stream->snapshot_size64 - stream->snapshot_at64;

		if (take > total) { take = total; }
		memcpy(out, stream->snapshot64 + stream->snapshot_at64, take);
		stream->snapshot_at64 += take;
		if (take < total) { stream->eof64 = 1; }
		return take / size;
	}
	while (done < total && !stream->eof64) {
		if (stream->next64 == stream->end64) {
			long got = file_call64(FILE_READ64, (unsigned long) stream->fd64,
				(unsigned long) stream->buffer64, sizeof(stream->buffer64));

			if (got < 0 || (size_t) got > sizeof(stream->buffer64)) {
				fail64(stream, NANO_EIO64);
				break;
			}
			if (got == 0) {
				stream->eof64 = 1;
				break;
			}
			stream->next64 = 0;
			stream->end64 = (size_t) got;
		}
		size_t take = stream->end64 - stream->next64;

		if (take > total - done) { take = total - done; }
		memcpy(out + done, stream->buffer64 + stream->next64, take);
		stream->next64 += take;
		done += take;
	}
	return done / size;
}

size_t nano_fwrite64(const void *data, size_t size, size_t count,
	struct NANO_FILE64 *stream)
{
	const unsigned char *in = data;
	size_t done = 0;
	size_t total;

	if (!request64(stream, data, size, count, 1)) { return 0; }
	total = size * count;
	while (done < total) {
		long wrote = write(stream->fd64, in + done, total - done);

		if (wrote <= 0 || (size_t) wrote > total - done) {
			fail64(stream, NANO_EIO64);
			break;
		}
		done += (size_t) wrote;
	}
	return done / size;
}

int nano_getc64(struct NANO_FILE64 *stream)
{
	unsigned char byte;

	return nano_fread64(&byte, 1, 1, stream) == 1 ? byte : NANO_EOF64;
}

int nano_putc64(int byte, struct NANO_FILE64 *stream)
{
	unsigned char value = (unsigned char) byte;

	return nano_fwrite64(&value, 1, 1, stream) == 1 ? value : NANO_EOF64;
}

int nano_ferror64(struct NANO_FILE64 *stream)
{
	return valid64(stream) ? stream->error64 != 0 : 1;
}

int nano_feof64(struct NANO_FILE64 *stream)
{
	return valid64(stream) ? stream->eof64 : 0;
}

void nano_clearerr64(struct NANO_FILE64 *stream)
{
	if (valid64(stream)) {
		stream->error64 = 0;
		stream->eof64 = 0;
	}
}

int nano_fileno64(struct NANO_FILE64 *stream)
{
	return valid64(stream) ? stream->fd64 : -1;
}

int nano_fflush64(struct NANO_FILE64 *stream)
{
	if (stream == NULL) {
		int result = 0;

		for (size_t i = 0; i < NANO_STREAMS64; i++) {
			if (streams64[i].used64 && streams64[i].writing64 &&
				nano_fflush64(&streams64[i]) != 0) {
				result = NANO_EOF64;
			}
		}
		for (size_t i = 0; i < 2; i++) {
			if (nano_fflush64(&output_streams64[i]) != 0) { result = NANO_EOF64; }
		}
		return result;
	}
	if (!valid64(stream)) { return NANO_EOF64; }
	return stream->error64 != 0 ? fail64(stream, stream->error64) : 0;
}

int nano_fclose64(struct NANO_FILE64 *stream)
{
	int error;

	if (!valid64(stream)) { return NANO_EOF64; }
	if (stream == nano_stdout64 || stream == nano_stderr64) {
		nano_errno64 = NANO_EBADF64;
		return NANO_EOF64;
	}
	if (close(stream->fd64) != 0) { return fail64(stream, NANO_EIO64); }
	error = stream->error64;
	free(stream->snapshot64);
	stream->snapshot64 = NULL;
	stream->used64 = 0;
	if (error != 0) {
		nano_errno64 = error;
		return NANO_EOF64;
	}
	return 0;
}

/* Stage an exact, bounded byte snapshot before upstream read_file can publish
 * lines or undo state. A failed read never leaves a truncated editable buffer.
 */
int nano_snapshot64(struct NANO_FILE64 *stream)
{
	struct FILE_INFO64 info;
	unsigned char *bytes;
	size_t done = 0;
	long status;

	if (!valid64(stream)) { return -1; }
	if (stream->writing64 || stream->next64 || stream->end64 || stream->snapshot64) {
		fail64(stream, NANO_EINVAL64); return -1;
	}
	status = file_call64(FILE_FSTAT64, (unsigned long) stream->fd64,
		(unsigned long) &info, 0);
	if (status < 0) { fail64(stream, (int) -status); return -1; }
	if (info.size64 > NANO_FILE_LIMIT64) { fail64(stream, NANO_EOVERFLOW64); return -1; }
	bytes = malloc(info.size64 ? info.size64 : 1);
	if (bytes == NULL) { fail64(stream, NANO_ENOMEM64); return -1; }
	while (done < info.size64) {
		status = file_call64(FILE_READ64, (unsigned long) stream->fd64,
			(unsigned long) (bytes + done), info.size64 - done);
		if (status <= 0 || (size_t) status > info.size64 - done) { goto failed; }
		done += (size_t) status;
	}
	/* Reject growth, stale metadata and any error at the expected EOF. */
	unsigned char extra;

	status = file_call64(FILE_READ64, (unsigned long) stream->fd64,
		(unsigned long) &extra, 1);
	if (status != 0) { goto failed; }
	struct FILE_INFO64 after;

	status = file_call64(FILE_FSTAT64, (unsigned long) stream->fd64,
		(unsigned long) &after, 0);
	if (status < 0 || memcmp(&after, &info, sizeof(info)) != 0) { goto failed; }
	stream->snapshot64 = bytes;
	stream->snapshot_size64 = done;
	return 0;
failed:
	free(bytes);
	fail64(stream, NANO_EIO64);
	return -1;
}
