#ifndef RUN68_X68K_BUS_H
#define RUN68_X68K_BUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The YM2151 exposes an address port and a data/status port.  The bus owns
 * the address latch; a backend only needs to implement chip-level accesses.
 */
typedef struct {
	void *context;
	void (*reset)(void *context);
	uint8_t (*read_status)(void *context);
	void (*write_register)(void *context, uint8_t reg, uint8_t value);
} X68K_OPM_BACKEND;

void x68k_bus_reset(void);
void x68k_bus_set_opm_backend(const X68K_OPM_BACKEND *backend);

/* Return non-zero when the access belongs to a mapped X68000 device. */
int x68k_bus_read(uint32_t address, int size, uint32_t *value);
int x68k_bus_write(uint32_t address, int size, uint32_t value);
int x68k_bus_opm_irq_vector(void);
uint64_t x68k_bus_opm_write_count(void);

#ifdef __cplusplus
}
#endif

#endif
