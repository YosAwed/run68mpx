#ifndef RUN68_YM2151_YMFM_H
#define RUN68_YM2151_YMFM_H

#include <stddef.h>
#include <stdint.h>

#include "x68k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct X68K_YM2151 X68K_YM2151;

X68K_YM2151 *x68k_ym2151_create(uint32_t input_clock);
void x68k_ym2151_destroy(X68K_YM2151 *device);
void x68k_ym2151_reset(X68K_YM2151 *device);

uint32_t x68k_ym2151_sample_rate(const X68K_YM2151 *device);
uint8_t x68k_ym2151_read_status(X68K_YM2151 *device);
void x68k_ym2151_write_register(X68K_YM2151 *device, uint8_t reg,
                                uint8_t value);
int x68k_ym2151_irq_asserted(const X68K_YM2151 *device);

/*
 * Advance by YM2151 input clocks. Generated stereo frames are interleaved in
 * output. If capacity is too small, the extra frames are generated and
 * discarded so chip and timer state remain synchronized.
 */
size_t x68k_ym2151_advance(X68K_YM2151 *device, uint32_t clocks,
                           int16_t *output, size_t frame_capacity);

X68K_OPM_BACKEND x68k_ym2151_bus_backend(X68K_YM2151 *device);

#ifdef __cplusplus
}
#endif

#endif
