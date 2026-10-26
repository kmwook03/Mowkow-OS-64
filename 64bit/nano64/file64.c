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
	unsigned char buffer64[NANO_READ_BUFFER64];
};

static struct NANO_FILE64 streams64[NANO_STREAMS64];
int nano_errno64;

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
	fd = open(path, writing ? O_CREAT | O_TRUNC : 0);
	if (fd < 0) {
		nano_errno64 = NANO_EIO64;
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
	while (done < total && !stream->eof64) {
		if (stream->next64 == stream->end64) {
			long got = read(stream->fd64, stream->buffer64,
				sizeof(stream->buffer64));

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
		return result;
	}
	if (!valid64(stream)) { return NANO_EOF64; }
	return stream->error64 != 0 ? fail64(stream, stream->error64) : 0;
}

int nano_fclose64(struct NANO_FILE64 *stream)
{
	int error;

	if (!valid64(stream)) { return NANO_EOF64; }
	if (close(stream->fd64) != 0) { return fail64(stream, NANO_EIO64); }
	error = stream->error64;
	stream->used64 = 0;
	if (error != 0) {
		nano_errno64 = error;
		return NANO_EOF64;
	}
	return 0;
}
