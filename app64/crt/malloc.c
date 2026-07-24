#include <mowos.h>

struct MALLOC_HEADER {
	size_t size;
};

void *malloc(size_t size)
{
	struct MALLOC_HEADER *header;

	if (size > ~(size_t) 0 - sizeof(*header)) {
		return 0;
	}
	header = (struct MALLOC_HEADER *) alloc(size + sizeof(*header));
	if (header == 0) {
		return 0;
	}
	header->size = size + sizeof(*header);
	return header + 1;
}

void free(void *ptr)
{
	struct MALLOC_HEADER *header;

	if (ptr == 0) {
		return;
	}
	header = ((struct MALLOC_HEADER *) ptr) - 1;
	free_alloc(header, header->size);
}
