#include <asmfunc64.h>
#include <elf64_loader.h>
#include <fd64.h>
#include <memory64.h>
#include <stddef.h>
#include <stdint.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1
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
	return file_size >= sizeof(*ehdr) &&
		ehdr->e_ident[0] == 0x7f && ehdr->e_ident[1] == 'E' &&
		ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F' &&
		ehdr->e_ident[4] == 2 && ehdr->e_ident[5] == 1 &&
		ehdr->e_type == ET_EXEC && ehdr->e_machine == EM_X86_64 &&
		ehdr->e_phentsize == sizeof(struct ELF64_PHDR) &&
		ehdr->e_phoff + (uint64_t) ehdr->e_phnum * sizeof(struct ELF64_PHDR) <= file_size;
}

/* 이미지 창 [USER_IMAGE_MIN, USER_IMAGE_MAX)는 풀 밖에 예약된 고정 구간이라
   (memory64.h) 한 번에 프로세스 하나만 담을 수 있다. 페이징 격리가 없는 동안은
   앱 실행을 시스템 전체에서 직렬화한다 -- 콘솔이 여러 개여도 마찬가지다. */
static struct PROCESS64 *image_owner;

/* 이미지 창 하나. 앱을 동시에 돌려야 하면 프로세스별 페이징. */
void elf64_release_process(struct PROCESS64 *process)
{
	if (image_owner == process) {
		image_owner = NULL;
	}
}

static int load_image(const char *path, struct PROCESS64 *process)
{
	struct FDHANDLE64 fh;
	uint8_t *file;
	size_t file_size;
	size_t read_size;
	const struct ELF64_EHDR *ehdr;
	const struct ELF64_PHDR *phdr;
	uint16_t i;
	uintptr_t low;
	uintptr_t high;
	uintptr_t image_base;
	size_t image_size;

	if (fd64_open(&fh, path) == 0) {
		return -1;
	}
	file_size = fh.info.size;
	file = (uint8_t *) memman64_alloc_4k(&memman64, file_size);
	if (file == NULL) {
		return -2;
	}
	read_size = fd64_read(&fh, file, file_size);
	if (read_size != file_size) {
		memman64_free_4k(&memman64, (uintptr_t) file, file_size);
		return -3;
	}
	ehdr = (const struct ELF64_EHDR *) file;
	if (valid_header(ehdr, file_size) == 0) {
		memman64_free_4k(&memman64, (uintptr_t) file, file_size);
		return -4;
	}
	low = USER_IMAGE_MAX;
	high = 0;
	phdr = (const struct ELF64_PHDR *) (file + ehdr->e_phoff);
	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD) {
			continue;
		}
		if (phdr[i].p_filesz > phdr[i].p_memsz ||
				phdr[i].p_offset + phdr[i].p_filesz > file_size ||
				phdr[i].p_vaddr < USER_IMAGE_MIN ||
				phdr[i].p_vaddr + phdr[i].p_memsz > USER_IMAGE_MAX ||
				phdr[i].p_align < MEMMAN64_PAGE_SIZE ||
				(phdr[i].p_vaddr & 0xfff) != (phdr[i].p_offset & 0xfff)) {
			memman64_free_4k(&memman64, (uintptr_t) file, file_size);
			return -5;
		}
		if (phdr[i].p_vaddr < low) {
			low = (uintptr_t) phdr[i].p_vaddr;
		}
		if (phdr[i].p_vaddr + phdr[i].p_memsz > high) {
			high = (uintptr_t) (phdr[i].p_vaddr + phdr[i].p_memsz);
		}
	}
	if (low >= high || ehdr->e_entry < low || ehdr->e_entry >= high) {
		memman64_free_4k(&memman64, (uintptr_t) file, file_size);
		return -6;
	}
	/* [USER_IMAGE_MIN, USER_IMAGE_MAX)는 memman64가 다루지 않는 유저 이미지
	   창이다(memory64.h의 MEMMAN64_EARLY_START 주석). 위의 범위 검사가 그
	   창 안이라는 것을 이미 보장하므로 여기서 따로 할당하지 않는다.
	   페이즈 1 전까지는 상주 프로세스가 하나뿐이라 겹칠 상대도 없다. */
	image_base = align_down64(low, MEMMAN64_PAGE_SIZE);
	image_size = (size_t) (align_up64(high, MEMMAN64_PAGE_SIZE) - image_base);
	zero_bytes((void *) image_base, image_size);
	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {
			copy_bytes((void *) (uintptr_t) phdr[i].p_vaddr, file + phdr[i].p_offset,
				(size_t) phdr[i].p_filesz);
		}
	}
	process->entry = (uintptr_t) ehdr->e_entry;
	process->image.base = image_base;
	process->image.size = image_size;
	memman64_free_4k(&memman64, (uintptr_t) file, file_size);
	return 0;
}

int elf64_load_process(const char *path, struct PROCESS64 *process)
{
	uint64_t flags;
	int status;

	flags = io_load_rflags();
	io_cli();
	if (image_owner != NULL) {
		io_store_rflags(flags);
		return -8;
	}
	image_owner = process;
	io_store_rflags(flags);
	status = load_image(path, process);
	if (status != 0) {
		image_owner = NULL;
	}
	return status;
}
