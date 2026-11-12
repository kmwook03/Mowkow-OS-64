#ifndef MOWKOW64_ARCH_ADDRESS_SPACE64_H
#define MOWKOW64_ARCH_ADDRESS_SPACE64_H

#include <address_space64.h>

#define AS_TABLES_MAX64 8U

/* Kernel aliases, allocated and owned by the common layer. All tables are
 * eagerly allocated, so map/activate never allocate. Backends may reference
 * immutable kernel-lifetime tables but must never add them to this list.
 */
struct AS_TABLES64 {
	uintptr_t page[AS_TABLES_MAX64];
};

unsigned int as_arch_table_count64(void);
int as_arch_supported64(void);
uintptr_t as_arch_physical64(uintptr_t alias);
uintptr_t as_arch_alias64(uintptr_t physical);
int as_arch_build64(const struct AS_TABLES64 *tables);
/* Returns only an owned leaf for an allowed user window. Parent descriptors
 * must exactly match this object's table links, never follow arbitrary PAs.
 */
uint64_t *as_arch_leaf64(const struct AS_TABLES64 *tables, uintptr_t address);
uint64_t as_arch_encode64(uintptr_t physical, unsigned int permissions);
int as_arch_decode64(uint64_t entry, uintptr_t *physical, unsigned int *permissions);
int as_arch_irq_off64(void);
void as_arch_install64(uintptr_t root);
int as_arch_sync_code64(uintptr_t alias, size_t size);

#endif
