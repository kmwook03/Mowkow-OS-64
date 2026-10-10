#include <arch/platform64.h>
#ifdef __aarch64__
#include <arch/arch64.h>
#endif
#include <elf64_loader.h>
#include <fd64.h>
#include <memory64.h>
#include <stddef.h>
#include <stdint.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define EM_X86_64 62
#define EM_AARCH64 183
#define PT_LOAD 1
#define PT_DYNAMIC64 2
#define PT_INTERP64 3
#define PT_TLS64 7
#define PF_X64 0x1U
#define PF_W64 0x2U
#define PF_R64 0x4U
#define PF_MASK64 (PF_X64 | PF_W64 | PF_R64)
#define ELF_VERSION64 1
#define USER_IMAGE_MIN 0x400000
#define USER_IMAGE_MAX 0x800000

struct ELF64_EHDR {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} __attribute__((packed));

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
} __attribute__((packed));

_Static_assert(sizeof(struct ELF64_EHDR) == 64,
	"ELF64 file header size mismatch");
_Static_assert(offsetof(struct ELF64_EHDR, e_entry) == 24,
	"ELF64 entry offset mismatch");
_Static_assert(offsetof(struct ELF64_EHDR, e_phoff) == 32,
	"ELF64 program-header offset field mismatch");
_Static_assert(sizeof(struct ELF64_PHDR) == 56,
	"ELF64 program header size mismatch");
_Static_assert(offsetof(struct ELF64_PHDR, p_offset) == 8,
	"ELF64 segment offset field mismatch");
_Static_assert(offsetof(struct ELF64_PHDR, p_vaddr) == 16,
	"ELF64 segment address field mismatch");
_Static_assert(offsetof(struct ELF64_PHDR, p_filesz) == 32,
	"ELF64 file-size field mismatch");
_Static_assert(offsetof(struct ELF64_PHDR, p_memsz) == 40,
	"ELF64 memory-size field mismatch");

static void copy_bytes(void *dst, const void *src, size_t size)
{
	uint8_t *d;
	const uint8_t *s;

	d = (uint8_t *) dst;
	s = (const uint8_t *) src;
	while (size-- > 0) {
		*d++ = *s++;
	}
}

static void zero_bytes(void *dst, size_t size)
{
	uint8_t *d;

	d = (uint8_t *) dst;
	while (size-- > 0) {
		*d++ = 0;
	}
}

static int valid_header(const struct ELF64_EHDR *ehdr, size_t file_size)
{
	uint16_t machine;

#ifdef __aarch64__
	machine = EM_AARCH64;
#else
	machine = EM_X86_64;
#endif
	return file_size >= sizeof(*ehdr) &&
		ehdr->e_ident[0] == 0x7f && ehdr->e_ident[1] == 'E' &&
		ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F' &&
		ehdr->e_ident[4] == 2 && ehdr->e_ident[5] == 1 &&
		ehdr->e_ident[6] == ELF_VERSION64 &&
		ehdr->e_type == ET_EXEC && ehdr->e_machine == machine &&
		ehdr->e_version == ELF_VERSION64 && ehdr->e_flags == 0 &&
		ehdr->e_ehsize == sizeof(*ehdr) &&
		ehdr->e_phentsize == sizeof(struct ELF64_PHDR) &&
		ehdr->e_phoff >= sizeof(*ehdr) &&
		ehdr->e_phoff <= file_size &&
		ehdr->e_phnum <=
			(file_size - ehdr->e_phoff) / sizeof(struct ELF64_PHDR);
}

/* Requires valid_header(); validate ranges before touching the image window. */
static int image_range64(const uint8_t *file, size_t file_size,
	uintptr_t *low, uintptr_t *high)
{
	const struct ELF64_EHDR *ehdr = (const struct ELF64_EHDR *) file;
	const struct ELF64_PHDR *phdr;
	uint16_t i;
	uint16_t j;
	uint64_t end;
	int executable_entry = 0;

	*low = USER_IMAGE_MAX;
	*high = 0;
	phdr = (const struct ELF64_PHDR *) (file + ehdr->e_phoff);
	for (i = 0; i < ehdr->e_phnum; i++) {
		/* The static app runtime has no interpreter, relocator, or TLS setup. */
		if (phdr[i].p_type == PT_DYNAMIC64 ||
			phdr[i].p_type == PT_INTERP64 || phdr[i].p_type == PT_TLS64) {
			return -5;
		}
		if (phdr[i].p_type != PT_LOAD) {
			continue;
		}
		/* Subtract only after checking the start; never form an unchecked end. */
		if ((phdr[i].p_flags & ~PF_MASK64) != 0 ||
			phdr[i].p_filesz > phdr[i].p_memsz ||
			phdr[i].p_offset > file_size ||
			phdr[i].p_filesz > file_size - phdr[i].p_offset ||
			phdr[i].p_vaddr < USER_IMAGE_MIN ||
			phdr[i].p_vaddr >= USER_IMAGE_MAX ||
			phdr[i].p_memsz > USER_IMAGE_MAX - phdr[i].p_vaddr ||
			phdr[i].p_align < MEMMAN64_PAGE_SIZE ||
			(phdr[i].p_align & (phdr[i].p_align - 1)) != 0 ||
			(phdr[i].p_vaddr & (phdr[i].p_align - 1)) !=
				(phdr[i].p_offset & (phdr[i].p_align - 1))) {
			return -5;
		}
		if (phdr[i].p_memsz == 0) {
			continue;
		}
		end = phdr[i].p_vaddr + phdr[i].p_memsz;
		if ((phdr[i].p_flags & PF_X64) != 0 &&
			ehdr->e_entry >= phdr[i].p_vaddr && ehdr->e_entry < end) {
			executable_entry = 1;
		}
		/* Earlier PT_LOAD ranges have already passed the overflow checks.
		   Compare byte ranges: disjoint segments may share an aligned page. */
		for (j = 0; j < i; j++) {
			if (phdr[j].p_type == PT_LOAD && phdr[j].p_memsz != 0 &&
				phdr[i].p_vaddr < phdr[j].p_vaddr + phdr[j].p_memsz &&
				phdr[j].p_vaddr < end) {
				return -5;
			}
		}
		if (phdr[i].p_vaddr < *low) {
			*low = (uintptr_t) phdr[i].p_vaddr;
		}
		if (end > *high) {
			*high = (uintptr_t) end;
		}
	}
	/* Empty PT_LOAD entries cannot establish an image or a usable entry. */
	return *low < *high && executable_entry ? 0 : -6;
}

/* 이미지 창 [USER_IMAGE_MIN, USER_IMAGE_MAX)는 풀 밖에 예약된 고정 구간이라
   (memory64.h) 한 번에 프로세스 하나만 담을 수 있다. 페이징 격리가 없는 동안은
   앱 실행을 시스템 전체에서 직렬화한다 -- 콘솔이 여러 개여도 마찬가지다. */
static struct PROCESS64 *image_owner;

/* 이미지 창 하나. 앱을 동시에 돌려야 하면 프로세스별 페이징. */
void elf64_release_process(struct PROCESS64 *process)
{
	uint64_t flags = platform_irq_save64();

	if (image_owner == process) {
		image_owner = NULL;
	}
	platform_irq_restore64(flags);
}

int elf64_load_process(const char *path, struct PROCESS64 *process)
{
	struct FDHANDLE64 fh;
	uint8_t *file = NULL;
	size_t file_size = 0;
	size_t read_size;
	const struct ELF64_EHDR *ehdr;
	const struct ELF64_PHDR *phdr;
	uint16_t i;
	uintptr_t low;
	uintptr_t high;
	uintptr_t image_base;
	uintptr_t image_end;
	size_t image_size;
	uint64_t flags;
	int status = -1;

	flags = platform_irq_save64();
	if (image_owner != NULL) {
		platform_irq_restore64(flags);
		return -8;
	}
	image_owner = process;
	platform_irq_restore64(flags);

	if (fd64_open(&fh, path) == 0) {
		goto cleanup;
	}
	file_size = fh.info.size;
	file = (uint8_t *) memman64_alloc_4k(&memman64, file_size);
	if (file == NULL) {
		status = -2;
		goto cleanup;
	}
	read_size = fd64_read(&fh, file, file_size);
	if (read_size != file_size) {
		status = -3;
		goto cleanup;
	}
	ehdr = (const struct ELF64_EHDR *) file;
	if (valid_header(ehdr, file_size) == 0) {
		status = -4;
		goto cleanup;
	}
	status = image_range64(file, file_size, &low, &high);
	if (status != 0) {
		goto cleanup;
	}
	/* The image window is outside memman64's pool; image_owner serializes use. */
	if (align_down_checked64(low, MEMMAN64_PAGE_SIZE, &image_base) != 0 ||
		align_up_checked64(high, MEMMAN64_PAGE_SIZE, &image_end) != 0 ||
		image_end > USER_IMAGE_MAX || image_base >= image_end) {
		status = -5;
		goto cleanup;
	}
	image_size = (size_t) (image_end - image_base);
#ifdef __aarch64__
	zero_bytes((void *) arch64_phys_to_virt(image_base), image_size);
#else
	zero_bytes((void *) image_base, image_size);
#endif
	phdr = (const struct ELF64_PHDR *) (file + ehdr->e_phoff);
	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {
#ifdef __aarch64__
			copy_bytes((void *) arch64_phys_to_virt(
				(uintptr_t) phdr[i].p_vaddr), file + phdr[i].p_offset,
				(size_t) phdr[i].p_filesz);
#else
			copy_bytes((void *) (uintptr_t) phdr[i].p_vaddr, file + phdr[i].p_offset,
				(size_t) phdr[i].p_filesz);
#endif
		}
	}
#ifdef __aarch64__
	arch64_sync_user_code(image_base, image_size);
#endif
	process->entry = (uintptr_t) ehdr->e_entry;
	process->image.base = image_base;
	process->image.size = image_size;

cleanup:
	if (file != NULL) {
		(void) memman64_free_4k(&memman64, (uintptr_t) file, file_size);
	}
	if (status != 0) {
		elf64_release_process(process);
	}
	return status;
}
