#ifndef RUN68_MSM6258_H
#define RUN68_MSM6258_H

#include <stddef.h>
#include <stdint.h>

typedef struct X68K_MSM6258 X68K_MSM6258;

X68K_MSM6258 *x68k_msm6258_create(void);
void x68k_msm6258_destroy(X68K_MSM6258 *device);

/* IOCS mode is (frequency 0..4) * 256 + pan 0..3. */
int x68k_msm6258_start(X68K_MSM6258 *device, const uint8_t *data,
                       size_t length, uint16_t mode);
int x68k_msm6258_control(X68K_MSM6258 *device, int mode);
int x68k_msm6258_status(const X68K_MSM6258 *device);

/* Add decoded output to interleaved signed 16-bit stereo PCM at 62500 Hz. */
void x68k_msm6258_mix(X68K_MSM6258 *device, int16_t *samples,
                      size_t frames);

#endif
