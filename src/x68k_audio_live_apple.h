#ifndef RUN68_X68K_AUDIO_LIVE_APPLE_H
#define RUN68_X68K_AUDIO_LIVE_APPLE_H

#include <stddef.h>
#include <stdint.h>

typedef struct X68K_LIVE_AUDIO X68K_LIVE_AUDIO;

typedef struct X68K_LIVE_AUDIO_STATS {
	uint64_t output_frames;
	uint64_t rendered_frames;
	uint64_t nonzero_samples;
	uint32_t callbacks;
	uint16_t peak;
} X68K_LIVE_AUDIO_STATS;

X68K_LIVE_AUDIO *x68k_live_audio_create(uint32_t source_sample_rate);
int x68k_live_audio_write(X68K_LIVE_AUDIO *audio, const int16_t *samples,
	size_t frames);
int x68k_live_audio_destroy(X68K_LIVE_AUDIO *audio,
	X68K_LIVE_AUDIO_STATS *stats);

#endif
