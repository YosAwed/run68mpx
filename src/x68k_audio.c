#include "x68k_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include "x68k_audio_live_apple.h"
#endif

#include "x68k_bus.h"
#include "msm6258.h"
#include "ym2151_ymfm.h"

enum {
	X68000_CPU_CLOCK = 10000000,
	X68000_OPM_CLOCK = 4000000,
	WAV_HEADER_SIZE = 44,
	AUDIO_FRAME_CHUNK = 64,
	AUDIO_MODE_WAV = 0,
	AUDIO_MODE_LIVE = 1
};

struct X68K_AUDIO {
	FILE *wav;
	X68K_YM2151 *opm;
	X68K_MSM6258 *adpcm;
	X68K_PCM8 *pcm8;
	uint64_t clock_remainder;
	uint64_t frames_written;
	int mode;
	int failed;
#if defined(__APPLE__)
	X68K_LIVE_AUDIO *live;
#endif
};

static void put_u16le(unsigned char *destination, uint16_t value)
{
	destination[0] = (unsigned char)value;
	destination[1] = (unsigned char)(value >> 8);
}

static void put_u32le(unsigned char *destination, uint32_t value)
{
	destination[0] = (unsigned char)value;
	destination[1] = (unsigned char)(value >> 8);
	destination[2] = (unsigned char)(value >> 16);
	destination[3] = (unsigned char)(value >> 24);
}

static int write_wav_header(FILE *wav, uint32_t sample_rate,
	uint32_t data_size)
{
	unsigned char header[WAV_HEADER_SIZE];
	uint32_t riff_size = data_size > UINT32_MAX - 36u
		? UINT32_MAX
		: data_size + 36u;

	memset(header, 0, sizeof(header));
	memcpy(header, "RIFF", 4);
	put_u32le(header + 4, riff_size);
	memcpy(header + 8, "WAVEfmt ", 8);
	put_u32le(header + 16, 16);
	put_u16le(header + 20, 1);
	put_u16le(header + 22, 2);
	put_u32le(header + 24, sample_rate);
	put_u32le(header + 28, sample_rate * 4);
	put_u16le(header + 32, 4);
	put_u16le(header + 34, 16);
	memcpy(header + 36, "data", 4);
	put_u32le(header + 40, data_size);
	return fwrite(header, 1, sizeof(header), wav) == sizeof(header) ? 0 : -1;
}

static int write_wav_frames(X68K_AUDIO *audio, const int16_t *samples,
	size_t frames)
{
	unsigned char bytes[AUDIO_FRAME_CHUNK * 4];
	size_t index;

	if (frames > AUDIO_FRAME_CHUNK)
		return -1;
	for (index = 0; index < frames * 2; ++index)
		put_u16le(bytes + index * 2, (uint16_t)samples[index]);
	if (fwrite(bytes, 4, frames, audio->wav) != frames)
		return -1;
	audio->frames_written += frames;
	return 0;
}

static int write_frames(X68K_AUDIO *audio, const int16_t *samples,
	size_t frames)
{
#if defined(__APPLE__)
	if (audio->mode == AUDIO_MODE_LIVE)
		return x68k_live_audio_write(audio->live, samples, frames);
#endif
	return write_wav_frames(audio, samples, frames);
}

X68K_AUDIO *x68k_audio_create_wav(const char *path)
{
	X68K_AUDIO *audio;
	X68K_OPM_BACKEND backend;

	if (path == NULL || path[0] == '\0')
		return NULL;
	audio = (X68K_AUDIO *)calloc(1, sizeof(*audio));
	if (audio == NULL)
		return NULL;
	audio->opm = x68k_ym2151_create(X68000_OPM_CLOCK);
	audio->adpcm = x68k_msm6258_create();
	audio->pcm8 = x68k_pcm8_create();
	if (audio->opm == NULL || audio->adpcm == NULL || audio->pcm8 == NULL) {
		x68k_pcm8_destroy(audio->pcm8);
		x68k_msm6258_destroy(audio->adpcm);
		x68k_ym2151_destroy(audio->opm);
		free(audio);
		return NULL;
	}
	audio->wav = fopen(path, "wb+");
	if (audio->wav == NULL ||
	    write_wav_header(audio->wav, x68k_ym2151_sample_rate(audio->opm), 0)
	        != 0) {
		if (audio->wav != NULL)
			fclose(audio->wav);
		x68k_pcm8_destroy(audio->pcm8);
		x68k_msm6258_destroy(audio->adpcm);
		x68k_ym2151_destroy(audio->opm);
		free(audio);
		return NULL;
	}

	backend = x68k_ym2151_bus_backend(audio->opm);
	x68k_bus_set_opm_backend(&backend);
	return audio;
}

X68K_AUDIO *x68k_audio_create_live(void)
{
#if defined(__APPLE__)
	X68K_AUDIO *audio;
	X68K_OPM_BACKEND backend;

	audio = (X68K_AUDIO *)calloc(1, sizeof(*audio));
	if (audio == NULL)
		return NULL;
	audio->mode = AUDIO_MODE_LIVE;
	audio->opm = x68k_ym2151_create(X68000_OPM_CLOCK);
	audio->adpcm = x68k_msm6258_create();
	audio->pcm8 = x68k_pcm8_create();
	if (audio->opm == NULL || audio->adpcm == NULL || audio->pcm8 == NULL) {
		x68k_pcm8_destroy(audio->pcm8);
		x68k_msm6258_destroy(audio->adpcm);
		x68k_ym2151_destroy(audio->opm);
		free(audio);
		return NULL;
	}
	audio->live = x68k_live_audio_create(
		x68k_ym2151_sample_rate(audio->opm));
	if (audio->live == NULL) {
		x68k_pcm8_destroy(audio->pcm8);
		x68k_msm6258_destroy(audio->adpcm);
		x68k_ym2151_destroy(audio->opm);
		free(audio);
		return NULL;
	}
	backend = x68k_ym2151_bus_backend(audio->opm);
	x68k_bus_set_opm_backend(&backend);
	return audio;
#else
	return NULL;
#endif
}

void x68k_audio_advance_cpu_cycles(X68K_AUDIO *audio, uint32_t cpu_cycles)
{
	uint64_t scaled_clocks;
	uint32_t opm_clocks;
	int16_t samples[AUDIO_FRAME_CHUNK * 2];

	if (audio == NULL || cpu_cycles == 0)
		return;
	scaled_clocks = audio->clock_remainder +
	                (uint64_t)cpu_cycles * X68000_OPM_CLOCK;
	opm_clocks = (uint32_t)(scaled_clocks / X68000_CPU_CLOCK);
	audio->clock_remainder = scaled_clocks % X68000_CPU_CLOCK;

	while (opm_clocks != 0) {
		uint32_t step = opm_clocks > 2048 ? 2048 : opm_clocks;
		size_t frames = x68k_ym2151_advance(audio->opm, step, samples,
		                                    AUDIO_FRAME_CHUNK);
		if (frames > AUDIO_FRAME_CHUNK) {
			audio->failed = 1;
			return;
		}
		x68k_msm6258_mix(audio->adpcm, samples, frames);
		x68k_pcm8_mix(audio->pcm8, samples, frames);
		if (!audio->failed && write_frames(audio, samples, frames) != 0)
			audio->failed = 1;
		opm_clocks -= step;
	}
}

int x68k_audio_irq_asserted(const X68K_AUDIO *audio)
{
	return audio != NULL && !audio->failed &&
	       x68k_ym2151_irq_asserted(audio->opm);
}

int x68k_audio_adpcm_start(X68K_AUDIO *audio, const uint8_t *data,
	size_t length, uint16_t mode)
{
	return audio == NULL ? 0 :
	       x68k_msm6258_start(audio->adpcm, data, length, mode);
}

int x68k_audio_adpcm_control(X68K_AUDIO *audio, int mode)
{
	return audio == NULL ? (mode >= 0 && mode <= 2 ? 0 : -1) :
	       x68k_msm6258_control(audio->adpcm, mode);
}

int x68k_audio_adpcm_status(const X68K_AUDIO *audio)
{
	return audio == NULL ? 0 : x68k_msm6258_status(audio->adpcm);
}

int x68k_audio_pcm8_start(X68K_AUDIO *audio, unsigned int channel,
	const uint8_t *data, size_t length, uint32_t mode)
{
	return audio == NULL ? 0 :
	       x68k_pcm8_start(audio->pcm8, channel, data, length, mode);
}

int x68k_audio_pcm8_stop(X68K_AUDIO *audio, unsigned int channel)
{
	return audio == NULL ? 0 : x68k_pcm8_stop(audio->pcm8, channel);
}

int x68k_audio_pcm8_control(X68K_AUDIO *audio, int mode)
{
	return audio == NULL ? (mode >= 0 && mode <= 2 ? 0 : -1) :
	       x68k_pcm8_control(audio->pcm8, mode);
}

size_t x68k_audio_pcm8_remaining(const X68K_AUDIO *audio,
	unsigned int channel)
{
	return audio == NULL ? 0 : x68k_pcm8_remaining(audio->pcm8, channel);
}

int x68k_audio_destroy(X68K_AUDIO *audio)
{
	int result;
	uint64_t byte_count;
	uint32_t data_size;

	if (audio == NULL)
		return 0;
	x68k_bus_set_opm_backend(NULL);
	result = audio->failed ? -1 : 0;
	if (audio->mode == AUDIO_MODE_LIVE) {
#if defined(__APPLE__)
		if (x68k_live_audio_destroy(audio->live, NULL) != 0)
			result = -1;
#endif
		x68k_pcm8_destroy(audio->pcm8);
		x68k_msm6258_destroy(audio->adpcm);
		x68k_ym2151_destroy(audio->opm);
		free(audio);
		return result;
	}
	byte_count = audio->frames_written * 4;
	data_size = byte_count > UINT32_MAX ? UINT32_MAX : (uint32_t)byte_count;
	if (fseek(audio->wav, 0, SEEK_SET) != 0)
		result = -1;
	else if (write_wav_header(audio->wav,
	                          x68k_ym2151_sample_rate(audio->opm),
	                          data_size) != 0)
		result = -1;
	if (fclose(audio->wav) != 0)
		result = -1;
	x68k_pcm8_destroy(audio->pcm8);
	x68k_msm6258_destroy(audio->adpcm);
	x68k_ym2151_destroy(audio->opm);
	free(audio);
	return result;
}
