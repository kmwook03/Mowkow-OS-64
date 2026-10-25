#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block_fault64.h"
#include "../src64/drivers/block64.c"
#include "../src64/kernel/cache64.c"
#include "../src64/kernel/fd64.c"
#include "../src64/kernel/fd64_fat.c"
#include "../src64/kernel/fd64_dir.c"
#include "../src64/kernel/elf64_loader.c"

#define CHECK64(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "storage check failed at line %d: %s\n", __LINE__, #condition); \
		return 1; \
	} \
} while (0)

/* Independent fixture geometry, not values read from the mounted filesystem. */
#define FIXTURE_DATA64 1088U
#define FIXTURE_FILE64 1096U
#define FIXTURE_BYTES64 8192U
#define FIXTURE_NAME64 "fixture long.txt"

static struct BLOCK_FAULT64 backend64;
static uint32_t mutex_depth64;
static uint64_t irq_depth64;
static int guard_error64;
_Alignas(MEMMAN64_PAGE_SIZE)
static uint8_t memory_pool64[6 * 1024 * 1024];

uint64_t platform_irq_save64(void)
{
	return irq_depth64++;
}

void platform_irq_restore64(uint64_t flags)
{
	if (irq_depth64 != flags + 1) {
		guard_error64 = 1;
	}
	irq_depth64 = flags;
}

int mutex64_lock(struct MUTEX64 *mutex)
{
	if (mutex == NULL || mutex_depth64 != 0) {
		guard_error64 = 1;
		return -1;
	}
	mutex_depth64++;
	return 0;
}

int mutex64_unlock(struct MUTEX64 *mutex)
{
	if (mutex == NULL || mutex_depth64 != 1) {
		guard_error64 = 1;
		return -1;
	}
	mutex_depth64--;
	return 0;
}

static int transport_read64(uint64_t lba, uint32_t count, void *dst)
{
	return block_fault_transfer64(&backend64, lba, count, dst, 0);
}

static int transport_write64(uint64_t lba, uint32_t count, const void *src)
{
	return block_fault_transfer64(&backend64, lba, count, (void *) src, 1);
}

static uint64_t transport_sectors64(void)
{
	return backend64.sectors;
}

const struct BLOCK64_OPS ahci64_ops = {
	"fault64", transport_read64, transport_write64, transport_sectors64
};
const struct BLOCK64_OPS ata64_ops = {
	"fault64", transport_read64, transport_write64, transport_sectors64
};

int ahci64_probe(void)
{
	return 0;
}

static int read_payload64(void)
{
	struct FDHANDLE64 file;
	uint8_t data[FIXTURE_BYTES64 + 1];
	size_t i;

	memset(data, 0xcc, sizeof(data));
	CHECK64(fd64_open(&file, FIXTURE_NAME64) == 1);
	CHECK64(fd64_read(&file, data, sizeof(data)) == FIXTURE_BYTES64);
	for (i = 0; i < FIXTURE_BYTES64; i++) {
		CHECK64(data[i] == (uint8_t) i);
	}
	CHECK64(data[FIXTURE_BYTES64] == 0xcc);
	CHECK64(fd64_read(&file, data, 1) == 0);
	return 0;
}

static int partial_read64(void)
{
	struct FDHANDLE64 file;
	uint8_t data[FIXTURE_BYTES64];
	size_t i;

	CHECK64(fd64_open(&file, FIXTURE_NAME64) == 1);
	/* Fail the second data block after changing a prefix of the cache buffer.
	 * That invalid block must never leak into the caller's unread suffix.
	 */
	block_fault_arm64(&backend64, 0, FIXTURE_FILE64 + 8, 257, 0);
	memset(data, 0xcc, sizeof(data));
	CHECK64(fd64_read(&file, data, sizeof(data)) == 4096);
	CHECK64(backend64.failures == 1 && file.pos == 4096);
	for (i = 0; i < 4096; i++) {
		CHECK64(data[i] == (uint8_t) i);
		CHECK64(data[i + 4096] == 0xcc);
	}
	CHECK64(fd64_read(&file, data + 4096, 4096) == 4096);
	for (i = 0; i < sizeof(data); i++) {
		CHECK64(data[i] == (uint8_t) i);
	}
	return 0;
}

static int sync_failure64(uint32_t target, int persistent)
{
	struct FDHANDLE64 file;
	struct FDHANDLE64 rejected;
	uint8_t replacement[FIXTURE_BYTES64 + 512];
	const uint8_t *media;
	size_t i;
	size_t writes = 0;
	uint64_t expected[] = {1096, 1104, 1112, 32, 560, 1088};
	int failed = 0;

	CHECK64(fd64_open(&file, FIXTURE_NAME64) == 1);
	/* Public write grows the file into cluster 26, dirtying both FAT copies
	 * and publishing the increased size only after the data/FAT writes.
	 */
	memset(replacement, 0x5a, sizeof(replacement));
	backend64.event_count = 0;
	block_fault_arm64(&backend64, 1, target, 257, persistent);
	CHECK64(fd64_write(&file, replacement, sizeof(replacement)) ==
		(persistent != 0 ? 0 : sizeof(replacement)));
	CHECK64(backend64.failures == (persistent != 0 ? 2U : 1U));
	CHECK64(fd64_read_only == persistent);
	for (i = 0; i < backend64.event_count; i++) {
		const struct BLOCK_FAULT_EVENT64 *event = &backend64.events[i];

		if (event->write == 0) {
			continue;
		}
		CHECK64(writes < 6 && event->lba == expected[writes]);
		if (event->status != 0) {
			CHECK64(event->completed == 257 && event->lba == target);
			failed++;
		} else {
			CHECK64(event->completed == (size_t) event->count * 512);
			writes++;
		}
	}
	CHECK64(failed == (persistent != 0 ? 2 : 1));
	if (persistent != 0) {
		uint32_t index = (target / 8) % CACHE64_BLOCKS;

		CHECK64(blocks[index].valid != 0 && blocks[index].dirty != 0);
		if (target == 1096 || target == 1104 || target == 1112) {
			size_t changed = target == 1112 ? 512 : 4096;

			media = backend64.media + target * 512;
			for (i = 0; i < 4096; i++) {
				uint8_t old = target == 1112 ? 0 : (uint8_t) i;

				CHECK64(block_data(index)[i] == (i < changed ? 0x5a : 0));
				CHECK64(media[i] == (i < 257 ? 0x5a : old));
			}
		}
		/* Later metadata must remain uncommitted when an earlier stage fails. */
		CHECK64(writes < 6);
		if (target != FIXTURE_DATA64) {
			CHECK64(backend64.media[FIXTURE_DATA64 * 512 + 93] == 0x20);
		}
		CHECK64(fd64_create(&rejected, "REJECT.TXT") == 0);
		CHECK64(fd64_write(&file, replacement, 1) == 0);
		backend64.armed = 0;
		CHECK64(fd64_sync() >= 0);
		CHECK64(fd64_read_only == 1);
	} else {
		CHECK64(writes == 6);
	}
	media = backend64.media + FIXTURE_FILE64 * 512;
	CHECK64(memcmp(media, replacement, sizeof(replacement)) == 0);
	/* Verify both on-media FAT copies, independent of cached fd64 readers. */
	for (i = 0; i < 2; i++) {
		media = backend64.media + (32 + i * 528) * 512;
		CHECK64(media[25 * 4] == 26 && media[25 * 4 + 1] == 0);
		CHECK64(media[26 * 4] == 0xff && media[26 * 4 + 3] == 0x0f);
	}
	media = backend64.media + FIXTURE_DATA64 * 512;
	CHECK64(media[88] == 0x21 && media[89] == 0x5c);
	CHECK64(media[92] == 0 && media[93] == 0x22 && media[94] == 0 && media[95] == 0);
	return 0;
}

static int elf_failure64(int expected)
{
	struct PROCESS64 process = {0};
	size_t available = memman64_total(&memman64);

	CHECK64(elf64_load_process(FIXTURE_NAME64, &process) == expected);
	CHECK64(memman64_total(&memman64) == available);
	CHECK64(image_owner == NULL);
	CHECK64(process.entry == 0 && process.image.size == 0);
	/* A second attempt observes released ownership and intact filesystem state. */
	CHECK64(elf64_load_process(FIXTURE_NAME64, &process) == expected);
	CHECK64(memman64_total(&memman64) == available && image_owner == NULL);
	return 0;
}

static int run_case64(const char *mode, const char *argument)
{
	struct FDHANDLE64 file;
	char name[160];

	if (strcmp(mode, "mount-read-fail") == 0) {
		block_fault_arm64(&backend64, 0, strtoul(argument, NULL, 10), 257, 0);
		CHECK64(fd64_init() == -1 && fd64_initialized == 0);
		CHECK64(backend64.failures == 1);
		CHECK64(fd64_init() == 0);
		return read_payload64();
	}
	if (strcmp(mode, "mount-reject") == 0) {
		CHECK64(fd64_init() == -1 && fd64_initialized == 0);
		return 0;
	}
	CHECK64(fd64_init() == 0);
	if (strcmp(mode, "directory-read-fail") == 0) {
		block_fault_arm64(&backend64, 0, FIXTURE_DATA64, 257, 0);
		CHECK64(fd64_open(&file, FIXTURE_NAME64) == 0);
		CHECK64(file.dir.cluster == 0 && backend64.failures == 1);
		return read_payload64();
	}
	if (strcmp(mode, "open-reject") == 0) {
		CHECK64(fd64_open(&file, FIXTURE_NAME64) == 0);
		CHECK64(file.dir.cluster == 0);
		return 0;
	}
	if (strcmp(mode, "name") == 0) {
		CHECK64(fd64_file_at(0, NULL, name, sizeof(name)) == 1);
		CHECK64(strcmp(name, argument) == 0);
		CHECK64(fd64_file_count() == 1);
		CHECK64(fd64_open(&file, "FIXTUR~1.TXT") == 1);
		return 0;
	}
	if (strcmp(mode, "partial-read") == 0) {
		return partial_read64();
	}
	if (strcmp(mode, "sync-once") == 0 || strcmp(mode, "sync-persistent") == 0) {
		return sync_failure64((uint32_t) strtoul(argument, NULL, 10),
			strcmp(mode, "sync-persistent") == 0);
	}
	if (strcmp(mode, "elf") == 0) {
		return elf_failure64(atoi(argument));
	}
	if (strcmp(mode, "elf-read-fail") == 0) {
		block_fault_arm64(&backend64, 0, FIXTURE_FILE64 + 8, 257, 1);
		CHECK64(elf_failure64(-3) == 0);
		CHECK64(backend64.failures == 2);
		return 0;
	}
	if (strcmp(mode, "elf-valid") == 0) {
		uint8_t data[0x2010];
		uintptr_t low;
		uintptr_t high;

		CHECK64(fd64_open(&file, FIXTURE_NAME64) == 1);
		CHECK64(fd64_read(&file, data, sizeof(data)) == sizeof(data));
		CHECK64(valid_header((const struct ELF64_EHDR *) data, sizeof(data)) != 0);
		CHECK64(image_range64(data, sizeof(data), &low, &high) == 0);
		CHECK64(low == 0x400000 && high == 0x402000);
		return 0;
	}
	CHECK64(strcmp(mode, "read") == 0);
	return read_payload64();
}

int main(int argc, char **argv)
{
	FILE *image;
	long bytes;
	int status;

	CHECK64(argc >= 3 && argc <= 4);
	image = fopen(argv[1], "rb");
	CHECK64(image != NULL);
	CHECK64(fseek(image, 0, SEEK_END) == 0);
	bytes = ftell(image);
	CHECK64(bytes > 0 && bytes % 512 == 0);
	backend64.media = malloc((size_t) bytes);
	CHECK64(backend64.media != NULL);
	backend64.sectors = (uint64_t) bytes / 512;
	rewind(image);
	CHECK64(fread(backend64.media, 1, (size_t) bytes, image) == (size_t) bytes);
	CHECK64(fclose(image) == 0);
	memman64_init(&memman64);
	CHECK64(memman64_add_pool(&memman64, (uintptr_t) memory_pool64,
		sizeof(memory_pool64)) == 0);
	CHECK64(block64_init() == 0 && block64_part_base() == 0);
	status = run_case64(argv[2], argc == 4 ? argv[3] : "");
	free(backend64.media);
	CHECK64(mutex_depth64 == 0 && irq_depth64 == 0 && guard_error64 == 0);
	CHECK64(backend64.trace_overflow == 0);
	return status;
}
