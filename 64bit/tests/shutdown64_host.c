#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#define POWER64_HOST_TEST 1
#include "../src64/arch/x86_64/power64.c"
#include "../src64/kernel/shutdown64.c"

static jmp_buf stopped64;
static uint64_t irq64;
static int sync_status64, sync_calls64, reenter64, power_calls64;
static int power_succeeds64, halt_calls64;
static uint32_t pci_id64, pci_base64, pci_enable64, pci_bdf64;
static uint16_t power_port64, power_value64;
static enum MESSAGE_ID64 messages64[16];
static size_t message_count64;

uint64_t platform_irq_save64(void)
{
	uint64_t old = irq64;

	irq64 = 1;
	return old;
}

void platform_irq_restore64(uint64_t state) { irq64 = state; }
void platform_irq_disable64(void) { irq64 = 1; }
void platform_halt64(void)
{
	assert(irq64 == 1 && sync_calls64 == 1 && sync_status64 == 0);
	halt_calls64++;
	longjmp(stopped64, 1);
}

int fd64_shutdown(void)
{
	assert(irq64 == 0 && power_calls64 == 0);
	sync_calls64++;
	if (reenter64) {
		reenter64 = 0;
		shutdown64(NULL);
	}
	return sync_status64;
}

void console64_message_con(struct CONSOLE64 *con, enum MESSAGE_ID64 id)
{
	(void) con;
	assert(message_count64 < sizeof(messages64) / sizeof(messages64[0]));
	messages64[message_count64++] = id;
}

uint32_t pci64_read32(uint32_t bdf, uint8_t offset)
{
	assert(irq64 == 1);
	if (bdf != pci_bdf64) { return UINT32_MAX; }
	if (offset == 0) { return pci_id64; }
	if (offset == 0x40) { return pci_base64; }
	if (offset == 0x44 || offset == 0x80) { return pci_enable64; }
	assert(0);
	return 0;
}

void power64_test_out16(uint16_t port, uint16_t value)
{
	assert(irq64 == 1 && sync_calls64 == 1 && sync_status64 == 0);
	power_calls64++;
	power_port64 = port;
	power_value64 = value;
	if (power_succeeds64) { longjmp(stopped64, 1); }
}

uint16_t power64_test_in16(uint16_t port)
{
	assert(port == power_port64 && irq64 == 1);
	return 0;
}

uint64_t poll_deadline64(uint32_t milliseconds)
{
	assert(milliseconds == 1000);
	return 42;
}
int poll_deadline_expired64(uint64_t deadline) { assert(deadline == 42); return 1; }
void x86_boot_log64(const char *s) { (void) s; }
void x86_boot_log_uint64(uint64_t n) { (void) n; }

static void reset64(void)
{
	shutdown_busy64 = 0;
	irq64 = 0;
	sync_status64 = sync_calls64 = reenter64 = power_calls64 = halt_calls64 = 0;
	pci_id64 = 0x71138086U;
	pci_base64 = 0x1801;
	pci_enable64 = 1;
	pci_bdf64 = 0x0b;
	message_count64 = 0;
	power_succeeds64 = 0;
}

static void request64(void)
{
	if (setjmp(stopped64) == 0) {
		shutdown64(NULL);
		assert(0); /* Successful storage shutdown must never resume the shell. */
	}
	assert(messages64[0] == MESSAGE_SHUTDOWN_SYNC64);
	assert(messages64[1] == MESSAGE_SHUTDOWN_POWEROFF64);
	assert(irq64 == 1);
}

int main(void)
{
	reset64();
	sync_status64 = -5;
	reenter64 = 1;
	shutdown64(NULL);
	assert(sync_calls64 == 1 && power_calls64 == 0 && halt_calls64 == 0);
	assert(irq64 == 0 && shutdown_busy64 == 0 && message_count64 == 3);
	assert(messages64[1] == MESSAGE_SHUTDOWN_BUSY64);
	assert(messages64[2] == MESSAGE_SHUTDOWN_FAILED64);
	/* A failed shutdown releases the request claim and permits a later retry. */
	sync_calls64 = 0;
	sync_status64 = 0;
	message_count64 = 0;
	power_succeeds64 = 1;
	request64();
	assert(power_calls64 == 1 && power_port64 == 0x1804 && power_value64 == 0x2000);
	assert(halt_calls64 == 0 && message_count64 == 2);

	reset64();
	pci_id64 = 0x29188086U;
	pci_bdf64 = 0xf8;
	pci_base64 = 0xa601;
	pci_enable64 = 0x80;
	request64();
	assert(power_calls64 == 1 && power_port64 == 0xa604 && power_value64 == 0x2000);
	assert(halt_calls64 == 1 && messages64[2] == MESSAGE_SHUTDOWN_HALTED64);

	reset64();
	pci_enable64 = 0;
	request64();
	assert(power_calls64 == 0 && halt_calls64 == 1);
	reset64();
	pci_base64 = 1;
	request64();
	assert(power_calls64 == 0 && halt_calls64 == 1);
	reset64();
	pci_id64 = 0x12348086U;
	request64();
	assert(power_calls64 == 0 && halt_calls64 == 1);
	return 0;
}
