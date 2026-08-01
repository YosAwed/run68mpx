#ifndef RUN68_X68K_AUDIO_H
#define RUN68_X68K_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef struct X68K_AUDIO X68K_AUDIO;

X68K_AUDIO *x68k_audio_create_wav(const char *path);
/* Open the host's default real-time audio output. Currently available on macOS. */
X68K_AUDIO *x68k_audio_create_live(void);
void x68k_audio_advance_cpu_cycles(X68K_AUDIO *audio, uint32_t cpu_cycles);
int x68k_audio_irq_asserted(const X68K_AUDIO *audio);
int x68k_audio_adpcm_start(X68K_AUDIO *audio, const uint8_t *data,
                           size_t length, uint16_t mode);
int x68k_audio_adpcm_control(X68K_AUDIO *audio, int mode);
int x68k_audio_adpcm_status(const X68K_AUDIO *audio);
int x68k_audio_pcm8_start(X68K_AUDIO *audio, unsigned int channel,
                          const uint8_t *data, size_t length,
                          uint32_t mode);
int x68k_audio_pcm8_stop(X68K_AUDIO *audio, unsigned int channel);
int x68k_audio_pcm8_control(X68K_AUDIO *audio, int mode);
size_t x68k_audio_pcm8_remaining(const X68K_AUDIO *audio,
                                 unsigned int channel);

/* Finalize output and release the device. Returns zero on success. */
int x68k_audio_destroy(X68K_AUDIO *audio);

#endif
