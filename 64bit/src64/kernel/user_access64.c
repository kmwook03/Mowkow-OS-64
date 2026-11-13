#include <kstring64.h>
#include <process64.h>
#include <user_access64.h>

static int copy_user64(void *kernel, uintptr_t user, size_t size, int output)
{
	struct PROCESS64 *process = process64_current();
	unsigned char *bytes = kernel;

	if (kernel == NULL) { return -22; }
	if (!(output ? process64_user_write_valid((void *) user, size) :
		process64_user_range_valid((const void *) user, size))) { return -14; }
	if (size > UINTPTR_MAX - (uintptr_t) kernel) { return -22; }
	while (size != 0) {
		struct USER_PAGE64 page;
		size_t count = USER_PAGE_SIZE64 - (user & (USER_PAGE_SIZE64 - 1));

		if (count > size) { count = size; }
		/* Preflight above proved every leaf; the current owner cannot unmap
		 * or exit while its syscall is suspended. No user VA is dereferenced.
		 */
		if (address_space_query64(process->space, user, &page) != 0) { return -14; }
		if (output) { memcpy((void *) page.kernel_address, bytes, count); }
		else { memcpy(bytes, (const void *) page.kernel_address, count); }
		user += count;
		bytes += count;
		size -= count;
	}
	return 0;
}

int copy_from_user64(void *destination, const void *source, size_t size)
{
	return copy_user64(destination, (uintptr_t) source, size, 0);
}

int copy_to_user64(void *destination, const void *source, size_t size)
{
	return copy_user64((void *) source, (uintptr_t) destination, size, 1);
}

int64_t copy_string_from_user64(char *destination, size_t capacity, const char *source)
{
	uintptr_t base = (uintptr_t) source;
	int error = -36;

	if (destination == NULL || capacity == 0 || capacity > INT64_MAX) { return -22; }
	destination[0] = '\0';
	for (size_t i = 0; i < capacity; i++) {
		/* Keep a string within one declared range, even at adjacent boundaries. */
		if (i >= UINTPTR_MAX - base || !process64_user_range_valid(source, i + 1) ||
			copy_from_user64(destination + i, (const void *) (base + i), 1) != 0) {
			error = -14;
			break;
		}
		if (destination[i] == '\0') { return (int64_t) i; }
	}
	destination[0] = '\0';
	return error;
}
