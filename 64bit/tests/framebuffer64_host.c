#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <arch/cache_range64.h>
#include <framebuffer64.h>

/* Exercise the actual mailbox parser and early-console clipping on the host. */
#include "../src64/arch/aarch64/fb64.c"

const uint8_t hankaku64[4096] = { [65 * 16] = 0xff };
const uint8_t hangul_font64[11520] = { 0 };
static uintptr_t flush_address64[64];
static size_t flush_bytes64[64], flush_count64, sync_count64;

uint32_t graphic64_rgb32(uint8_t index)
{
	return (uint32_t) index * 0x010101U;
}

void platform_framebuffer_flush64(uintptr_t address, size_t bytes)
{
	assert(flush_count64 < 64 && bytes > 0);
	flush_address64[flush_count64] = address;
	flush_bytes64[flush_count64++] = bytes;
}

void platform_framebuffer_sync64(void)
{
	sync_count64++;
}

static void geometry64(void)
{
	_Alignas(64) uint8_t pixels[512];
	struct FRAMEBUFFER64 fb = { 0 }, saved;
	struct BOOTINFO64 info = { .scrnx = 3, .scrny = 2,
		.bytes_per_scanline = 16, .bpp = 32, .framebuffer_type = 1,
		.vram = (uintptr_t) pixels, .reserved2 = 28 };

	assert(framebuffer_boot64(&fb, &info) == 0);
	saved = fb;
	info.reserved2--;
	assert(framebuffer_boot64(&fb, &info) == -22);
	assert(memcmp(&fb, &saved, sizeof(fb)) == 0);
	info.reserved2 = 0;
	assert(framebuffer_boot64(&fb, &info) == 0 && fb.size == 32);
	info.scrnx = 0;
	assert(framebuffer_boot64(&fb, &info) == -22);
	info.scrnx = 3;
	info.bytes_per_scanline = 0;
	assert(framebuffer_boot64(&fb, &info) == -22);
	assert(framebuffer_init64(NULL, pixels, 512, 1, 1, 4, 32) == -22);
	assert(framebuffer_init64(&fb, pixels + 1, 511, 1, 1, 4, 32) == -22);
	assert(framebuffer_init64(&fb, pixels, 512, 1, 1, 5, 32) == -22);
	assert(framebuffer_init64(&fb, pixels, 512, 1, 1, 4, 24) == -22);
	assert(framebuffer_init64(&fb, pixels, 512, INT_MAX, INT_MAX,
		UINT32_MAX, 32) == -22);
	assert(framebuffer_init64(&fb, (uint8_t *) (UINTPTR_MAX - 7),
		16, 1, 1, 1, 8) == -22);
	assert(framebuffer_boot64(&fb, NULL) == -22);
}

static void backend64(uint8_t bpp)
{
	_Alignas(64) uint8_t pixels[512], expected[512];
	uint8_t source[] = { 1, 2, 3, 4 }, owners[] = { 7, 1, 7, 7 };
	struct FRAMEBUFFER64 fb;
	int bytes = bpp / 8;
	static const int64_t edges[] = { INT64_MIN, -1, 0, 1, 3, 4, 6, INT64_MAX };

	memset(pixels, 0xa5, sizeof(pixels));
	memset(expected, 0xa5, sizeof(expected));
	assert(framebuffer_init64(&fb, pixels + 64, 32 * 2 + 4 * bytes,
		4, 3, 32, bpp) == 0);
	for (int y = 0; y < 3; y++) {
		assert(framebuffer_span64(&fb, 0, y, source, owners, 7, 4) == 0);
		for (int x = 0; x < 4; x++) {
			if (owners[x] != 7) { continue; }
			if (bpp == 8) { expected[64 + y * 32 + x] = source[x]; }
			else {
				uint32_t color = graphic64_rgb32(source[x]);
				memcpy(expected + 64 + y * 32 + x * 4, &color, 4);
			}
		}
	}
	assert(memcmp(pixels, expected, sizeof(pixels)) == 0);
	assert(framebuffer_span64(&fb, INT_MAX, 0, source, owners, 7, 4) == -22);
	assert(framebuffer_span64(&fb, 0, INT_MIN, source, owners, 7, 4) == -22);
	assert(framebuffer_span64(&fb, 2, 0, source, owners, 7, INT_MAX) == -22);
	assert(framebuffer_span64(&fb, 0, 0, NULL, owners, 7, 4) == -22);
	assert(memcmp(pixels, expected, sizeof(pixels)) == 0);
	for (size_t a = 0; a < sizeof(edges) / sizeof(edges[0]); a++) {
		for (size_t b = 0; b < sizeof(edges) / sizeof(edges[0]); b++) {
			flush_count64 = sync_count64 = 0;
			framebuffer_flush64(&fb, edges[a], edges[a], edges[b], edges[b]);
			size_t n = 0;
			for (int y = 0; y < 3; y++) {
				int first = -1, last = -1;
				for (int x = 0; x < 4; x++) {
					if (x >= edges[a] && x < edges[b] &&
						y >= edges[a] && y < edges[b]) {
						if (first < 0) { first = x; }
						last = x;
					}
				}
				if (first < 0) { continue; }
				assert(n < flush_count64);
				assert(flush_address64[n] == (uintptr_t) pixels + 64 +
					(size_t) y * 32 + (size_t) first * bytes);
				assert(flush_bytes64[n++] == (size_t) (last - first + 1) * bytes);
			}
			assert(n == flush_count64);
			assert(sync_count64 == (n != 0));
		}
	}
}

static void cache_lines64(void)
{
	struct CACHE_RANGE64 range;

	for (uintptr_t line = 4; line <= 128; line *= 2) {
		for (uintptr_t address = 1; address < 256; address++) {
			for (size_t bytes = 1; bytes <= 256; bytes++) {
				assert(cache_range_init64(&range, address, bytes, line) == 0);
				assert(range.first <= address && address - range.first < line);
				assert(range.last <= address + bytes - 1);
				assert(address + bytes - 1 - range.last < line);
				assert(range.first % line == 0 && range.last % line == 0);
			}
		}
	}
	assert(cache_range_init64(&range, UINTPTR_MAX - 63, 63, 64) == 0);
	assert(range.first == UINTPTR_MAX - 63 && range.last == range.first);
	assert(cache_range_init64(&range, UINTPTR_MAX - 63, 64, 64) == -22);
	assert(cache_range_init64(&range, 0, 0, 64) == -22);
	assert(cache_range_init64(&range, 0, 1, 0) == -22);
	assert(cache_range_init64(&range, 0, 1, 3) == -22);
}

static void response64(void)
{
	uint32_t response[36] = {
		140, 0x80000000, TAG_SET_PHYSICAL_SIZE, 8, 0x80000008, 1920, 1080,
		TAG_SET_VIRTUAL_SIZE, 8, 0x80000008, 1920, 1080,
		TAG_SET_VIRTUAL_OFFSET, 8, 0x80000008, 0, 0,
		TAG_SET_DEPTH, 4, 0x80000004, 32,
		TAG_SET_PIXEL_ORDER, 4, 0x80000004, 1,
		TAG_ALLOCATE_BUFFER, 8, 0x80000008, 0xc0100000, 7680 * 1080,
		TAG_GET_PITCH, 4, 0x80000004, 7680, 0, 0
	};
	struct BOOTINFO64 info, saved;
	const uint32_t invalid[][2] = {
		{ 0, 144 }, { 1, 0 }, { 2, 0 }, { 3, 4 }, { 4, 8 },
		{ 5, 0 }, { 5, 65536 }, { 6, 0 }, { 6, 65536 },
		{ 9, 0x80000004 }, { 10, 1919 }, { 11, 1079 }, { 15, 1 },
		{ 16, 1 }, { 20, 24 }, { 24, 0 }, { 24, 2 },
		{ 27, 0x80000004 }, { 28, 0xc0000000 }, { 28, 0xc0100001 },
		{ 28, 0xfffff000 }, { 29, 0 }, { 29, 7680 * 1080 - 1 },
		{ 32, 4 }, { 33, 7679 }, { 33, 7681 }, { 33, 65536 }, { 34, 1 }
	};

	assert(framebuffer_response64(response, &info) == 0);
	assert(info.vram == arch64_phys_to_virt(0x100000));
	assert(info.scrnx == 1920 && info.scrny == 1080 && info.reserved2 == 7680 * 1080);
	saved = info;
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		uint32_t index = invalid[i][0], original = response[index];

		response[index] = invalid[i][1];
		assert(framebuffer_response64(response, &info) == -22);
		assert(memcmp(&info, &saved, sizeof(info)) == 0);
		response[index] = original;
	}
	assert(arch64_fb_probe(NULL) == -22);
}

static void early_clip64(void)
{
	_Alignas(64) uint8_t pixels[64 + 32 * 4 + 64];
	uint8_t bitmap[8 * 16];

	memset(pixels, 0xa5, sizeof(pixels));
	memset(bitmap, 1, sizeof(bitmap));
	framebuffer_info = (struct BOOTINFO64) { .scrnx = 5, .scrny = 4,
		.bytes_per_scanline = 32, .vram = (uintptr_t) pixels + 64 };
	assert(framebuffer_init64(&framebuffer64, pixels + 64, 32 * 3 + 20,
		5, 4, 32, 32) == 0);
	flush_count64 = 0;
	draw_bitmap64(bitmap, 8, 3, 2);
	assert(flush_count64 == 2);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 32; x++) {
			if (y >= 2 && x >= 12 && x < 20) { continue; }
			assert(pixels[64 + y * 32 + x] == 0xa5);
		}
	}
	for (int i = 0; i < 64; i++) {
		assert(pixels[i] == 0xa5 && pixels[64 + 128 + i] == 0xa5);
	}
	framebuffer_ready = 1;
	arch64_dbg_puts("A"); /* narrow response cannot underflow the right margin */
	assert(flush_count64 == 2);
}

int main(void)
{
	geometry64();
	backend64(8);
	backend64(32);
	cache_lines64();
	response64();
	early_clip64();
	puts("Framebuffer geometry, backends, clipped row flush, cache ranges and Pi reply: PASS");
	return 0;
}
