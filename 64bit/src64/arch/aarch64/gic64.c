/* BCM2712 GIC-400 (GICv2) interrupt controller. */
#include <arch/arch64.h>
#include <stdint.h>

#define GICD_BASE 0x107fff9000ULL
#define GICC_BASE 0x107fffa000ULL

#define GICD_CTLR 0x000
#define GICD_TYPER 0x004
#define GICD_IGROUPR 0x080
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_ICPENDR 0x280
#define GICD_IPRIORITYR 0x400
#define GICD_ICFGR 0xc00

#define GICC_CTLR 0x000
#define GICC_PMR 0x004
#define GICC_BPR 0x008
#define GICC_IAR 0x00c
#define GICC_EOIR 0x010

#define TIMER_INTID 30U
#define SPURIOUS_INTID 1023U

extern uintptr_t arch64_timer_handle_irq(uintptr_t frame);

static volatile uint32_t *gicd_reg64(uint32_t offset)
{
	return (volatile uint32_t *) arch64_phys_to_virt(
		(uintptr_t) GICD_BASE + offset);
}

static volatile uint32_t *gicc_reg64(uint32_t offset)
{
	return (volatile uint32_t *) arch64_phys_to_virt(
		(uintptr_t) GICC_BASE + offset);
}

void arch64_irqctl_init(void)
{
	uint32_t config;
	uint32_t register_count;
	uint32_t i;
	volatile uint8_t *priority;

	/* PPI registers 0 are banked per CPU. Route the physical timer to the
	   non-secure group used by EL1, level-sensitive as described by the DT. */
	*gicc_reg64(GICC_CTLR) = 0;
	*gicd_reg64(GICD_CTLR) = 0;
	register_count = (*gicd_reg64(GICD_TYPER) & 0x1fU) + 1;
	for (i = 0; i < register_count; i++) {
		*gicd_reg64(GICD_ICENABLER + i * 4) = 0xffffffffU;
		*gicd_reg64(GICD_ICPENDR + i * 4) = 0xffffffffU;
	}
	*gicd_reg64(GICD_IGROUPR) |= 1U << TIMER_INTID;
	priority = (volatile uint8_t *) arch64_phys_to_virt(
		(uintptr_t) GICD_BASE + GICD_IPRIORITYR + TIMER_INTID);
	*priority = 0x80;
	config = *gicd_reg64(GICD_ICFGR + (TIMER_INTID / 16) * 4);
	config &= ~(3U << ((TIMER_INTID % 16) * 2));
	*gicd_reg64(GICD_ICFGR + (TIMER_INTID / 16) * 4) = config;
	*gicd_reg64(GICD_ISENABLER) = 1U << TIMER_INTID;

	*gicc_reg64(GICC_PMR) = 0xf0;
	*gicc_reg64(GICC_BPR) = 0;
	*gicc_reg64(GICC_CTLR) = 1;
	*gicd_reg64(GICD_CTLR) = 1;
	__asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

uintptr_t arch64_irq_dispatch(uintptr_t frame)
{
	uint32_t acknowledge;
	uint32_t intid;

	acknowledge = *gicc_reg64(GICC_IAR);
	intid = acknowledge & 0x3ffU;
	if (intid == SPURIOUS_INTID) {
		return frame;
	}
	if (intid == TIMER_INTID) {
		frame = arch64_timer_handle_irq(frame);
	}
	*gicc_reg64(GICC_EOIR) = acknowledge;
	__asm__ volatile ("dsb sy" ::: "memory");
	return frame;
}
