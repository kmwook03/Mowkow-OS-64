#include <kstring64.h>
#include <process64.h>
#include <user_access64.h>

int user_access_begin64(struct USER_ACCESS64 *access, const void *user,
	size_t size, unsigned int permissions)
{
	struct PROCESS64 *process = process64_current();
	int status;

	if (access == NULL || access->permissions != 0 || access->space != NULL ||
		(permissions != USER_READ64 && permissions != USER_WRITE64)) { return -22; }
	if (process == NULL) { return -14; }
	/* Acquire lifetime first, then inspect ranges and leaves. The calling
	 * execution flow owns the process record until its syscall unwinds.
	 */
	status = address_space_pin64(process->space);
	if (status != 0) { return -14; }
	if (!(permissions == USER_WRITE64 ? process64_user_write_valid((void *) user, size) :
		process64_user_range_valid(user, size))) {
		(void) address_space_unpin64(process->space);
		return -14;
	}
	*access = (struct USER_ACCESS64) {
		.space = process->space, .address = (uintptr_t) user,
		.size = size, .permissions = permissions
	};
	return 0;
}

void user_access_end64(struct USER_ACCESS64 *access)
{
	if (access == NULL) { return; }
	if (access->space != NULL) { (void) address_space_unpin64(access->space); }
	*access = (struct USER_ACCESS64) {0};
}

static int copy_access64(const struct USER_ACCESS64 *access, void *kernel,
	size_t size, int output)
{
	uintptr_t user;
	unsigned char *bytes = kernel;

	if (access == NULL || access->space == NULL || kernel == NULL ||
		access->permissions != (output ? USER_WRITE64 : USER_READ64) ||
		size > access->size || size > UINTPTR_MAX - (uintptr_t) kernel) { return -22; }
	user = access->address;
	while (size != 0) {
		struct USER_PAGE64 page;
		size_t count = USER_PAGE_SIZE64 - (user & (USER_PAGE_SIZE64 - 1));

		if (count > size) { count = size; }
		/* The lease proved every leaf and freezes the original mappings. */
		if (address_space_query64(access->space, user, &page) != 0) { return -14; }
		if (output) { memcpy((void *) page.kernel_address, bytes, count); }
		else { memcpy(bytes, (const void *) page.kernel_address, count); }
		user += count;
		bytes += count;
		size -= count;
	}
	return 0;
}

int copy_to_user_access64(const struct USER_ACCESS64 *access, const void *source, size_t size)
{
	return copy_access64(access, (void *) source, size, 1);
}

static int copy_user64(void *kernel, uintptr_t user, size_t size, int output)
{
	struct USER_ACCESS64 access = {0};
	int status;

	if (kernel == NULL) { return -22; }
	status = user_access_begin64(&access, (const void *) user, size,
		output ? USER_WRITE64 : USER_READ64);
	if (status != 0) { return status; }
	status = copy_access64(&access, kernel, size, output);
	user_access_end64(&access);
	return status;
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
	struct PROCESS64 *process = process64_current();
	uintptr_t base = (uintptr_t) source;
	int64_t result = -36;

	if (destination == NULL || capacity == 0 || capacity > INT64_MAX ||
		capacity > UINTPTR_MAX - (uintptr_t) destination) { return -22; }
	destination[0] = '\0';
	if (process == NULL || address_space_pin64(process->space) != 0) { return -14; }
	for (size_t i = 0; i < capacity; i++) {
		/* Keep a string within one declared range, even at adjacent boundaries. */
		if (i >= UINTPTR_MAX - base || !process64_user_range_valid(source, i + 1) ||
			copy_from_user64(destination + i, (const void *) (base + i), 1) != 0) {
			result = -14;
			break;
		}
		if (destination[i] == '\0') { result = (int64_t) i; break; }
	}
	(void) address_space_unpin64(process->space);
	if (result < 0) { destination[0] = '\0'; }
	return result;
}
