#include "msm6258.h"

#include <stdlib.h>
#include <string.h>

enum {
	/* The X68000 configures the MSM6258 for its 10-bit output mode. */
	MSM6258_MIN_SIGNAL = -512,
	MSM6258_MAX_SIGNAL = 511,
	PCM8_MIN_SIGNAL = -2047,
	PCM8_MAX_SIGNAL = 2047,
	MSM6258_MAX_STEP = 48,
	PCM8_CHANNELS = 8
};

typedef struct X68K_ADPCM_VOICE {
	uint8_t *data;
	size_t length;
	size_t nibble_position;
	uint32_t sample_phase;
	uint32_t sample_divisor;
	int signal;
	int step;
	uint8_t pan;
	uint8_t volume;
	int minimum_signal;
	int maximum_signal;
	int output_scale;
	int active;
	int paused;
} X68K_ADPCM_VOICE;

struct X68K_MSM6258 {
	X68K_ADPCM_VOICE voice;
};

struct X68K_PCM8 {
	X68K_ADPCM_VOICE voices[PCM8_CHANNELS];
};

/* OKI MSM6258 4-bit ADPCM step values. */
static const int step_table[MSM6258_MAX_STEP + 1] = {
	16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
	60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
	209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
	658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552
};

static const int step_adjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/* 62500 Hz / these divisors gives the five IOCS ADPCM rates. */
static const uint8_t rate_divisors[5] = {16, 12, 8, 6, 4};

/* X68Sound-compatible PCM8 volume steps. Index 8 is unity. */
static const uint8_t pcm8_volumes[16] = {
	2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80
};

static int16_t mix_sample(int16_t original, int addition)
{
	int mixed = (int)original + addition;

	if (mixed > 32767)
		mixed = 32767;
	else if (mixed < -32768)
		mixed = -32768;
	return (int16_t)mixed;
}

static void voice_initialize(X68K_ADPCM_VOICE *voice, int pcm8)
{
	memset(voice, 0, sizeof(*voice));
	voice->sample_divisor = rate_divisors[4];
	voice->pan = 3;
	voice->volume = 16;
	voice->minimum_signal = pcm8 ? PCM8_MIN_SIGNAL : MSM6258_MIN_SIGNAL;
	voice->maximum_signal = pcm8 ? PCM8_MAX_SIGNAL : MSM6258_MAX_SIGNAL;
	/* Preserve the existing hardware ADPCM level; PCM8 index 8 is unity. */
	voice->output_scale = pcm8 ? 2 : 8;
}

static void voice_release(X68K_ADPCM_VOICE *voice)
{
	free(voice->data);
	voice->data = NULL;
	voice->length = 0;
	voice->nibble_position = 0;
	voice->active = 0;
	voice->paused = 0;
}

static int decode_nibble(X68K_ADPCM_VOICE *voice)
{
	size_t byte_position;
	uint8_t value;
	int magnitude;
	int delta;

	if (voice->nibble_position >= voice->length * 2u) {
		voice->active = 0;
		return 0;
	}
	byte_position = voice->nibble_position / 2u;
	value = voice->data[byte_position];
	if ((voice->nibble_position & 1u) == 0)
		value &= 0x0f;
	else
		value >>= 4;
	voice->nibble_position++;

	magnitude = step_table[voice->step];
	delta = magnitude / 8;
	if ((value & 1u) != 0)
		delta += magnitude / 4;
	if ((value & 2u) != 0)
		delta += magnitude / 2;
	if ((value & 4u) != 0)
		delta += magnitude;
	if ((value & 8u) != 0)
		delta = -delta;
	voice->signal += delta;
	if (voice->signal > voice->maximum_signal)
		voice->signal = voice->maximum_signal;
	else if (voice->signal < voice->minimum_signal)
		voice->signal = voice->minimum_signal;

	voice->step += step_adjust[value & 7u];
	if (voice->step > MSM6258_MAX_STEP)
		voice->step = MSM6258_MAX_STEP;
	else if (voice->step < 0)
		voice->step = 0;
	return 1;
}

static int voice_replace_data(X68K_ADPCM_VOICE *voice,
	const uint8_t *data, size_t length)
{
	uint8_t *copy = NULL;

	if (length > SIZE_MAX / 2u || (length != 0 && data == NULL))
		return -1;
	if (length != 0) {
		copy = (uint8_t *)malloc(length);
		if (copy == NULL)
			return -1;
		memcpy(copy, data, length);
	}
	free(voice->data);
	voice->data = copy;
	voice->length = length;
	voice->nibble_position = 0;
	voice->sample_phase = voice->sample_divisor - 1u;
	voice->signal = -2;
	voice->step = 0;
	voice->active = length != 0 && voice->pan != 0;
	voice->paused = 0;
	return 0;
}

static int voice_next_output(X68K_ADPCM_VOICE *voice)
{
	if (!voice->active || voice->paused)
		return 0;
	voice->sample_phase++;
	while (voice->sample_phase >= voice->sample_divisor) {
		voice->sample_phase -= voice->sample_divisor;
		if (!decode_nibble(voice))
			return 0;
	}
	return voice->signal * voice->output_scale * voice->volume / 16;
}

X68K_MSM6258 *x68k_msm6258_create(void)
{
	X68K_MSM6258 *device =
		(X68K_MSM6258 *)calloc(1, sizeof(X68K_MSM6258));

	if (device != NULL)
		voice_initialize(&device->voice, 0);
	return device;
}

void x68k_msm6258_destroy(X68K_MSM6258 *device)
{
	if (device == NULL)
		return;
	voice_release(&device->voice);
	free(device);
}

int x68k_msm6258_start(X68K_MSM6258 *device, const uint8_t *data,
	size_t length, uint16_t mode)
{
	unsigned int frequency = mode >> 8;
	unsigned int pan = mode & 0xffu;
	X68K_ADPCM_VOICE *voice;

	if (device == NULL || frequency >= 5 || pan >= 4 ||
	    (length != 0 && data == NULL))
		return -1;
	voice = &device->voice;
	voice->sample_divisor = rate_divisors[frequency];
	voice->pan = (uint8_t)pan;
	return voice_replace_data(voice, data, length);
}

int x68k_msm6258_control(X68K_MSM6258 *device, int mode)
{
	X68K_ADPCM_VOICE *voice;

	if (device == NULL || mode < 0 || mode > 2)
		return -1;
	voice = &device->voice;
	if (mode == 0) {
		voice->active = 0;
		voice->paused = 0;
	} else if (mode == 1) {
		voice->paused = 1;
	} else {
		voice->paused = 0;
	}
	return 0;
}

int x68k_msm6258_status(const X68K_MSM6258 *device)
{
	return device != NULL && device->voice.active ? 2 : 0;
}

void x68k_msm6258_mix(X68K_MSM6258 *device, int16_t *samples,
	size_t frames)
{
	size_t frame;

	if (device == NULL || samples == NULL)
		return;
	for (frame = 0; frame < frames; ++frame) {
		X68K_ADPCM_VOICE *voice = &device->voice;
		int output = voice_next_output(voice);

		/* IOCS pan: 0=off, 1=left, 2=right, 3=both. */
		if ((voice->pan & 1u) != 0)
			samples[frame * 2] = mix_sample(samples[frame * 2], output);
		if ((voice->pan & 2u) != 0)
			samples[frame * 2 + 1] =
				mix_sample(samples[frame * 2 + 1], output);
	}
}

X68K_PCM8 *x68k_pcm8_create(void)
{
	X68K_PCM8 *mixer = (X68K_PCM8 *)calloc(1, sizeof(*mixer));
	unsigned int channel;

	if (mixer == NULL)
		return NULL;
	for (channel = 0; channel < PCM8_CHANNELS; ++channel)
		voice_initialize(&mixer->voices[channel], 1);
	return mixer;
}

void x68k_pcm8_destroy(X68K_PCM8 *mixer)
{
	unsigned int channel;

	if (mixer == NULL)
		return;
	for (channel = 0; channel < PCM8_CHANNELS; ++channel)
		voice_release(&mixer->voices[channel]);
	free(mixer);
}

int x68k_pcm8_start(X68K_PCM8 *mixer, unsigned int channel,
	const uint8_t *data, size_t length, uint32_t mode)
{
	X68K_ADPCM_VOICE *voice;
	unsigned int volume;
	unsigned int frequency;
	unsigned int pan;

	if (mixer == NULL || channel >= PCM8_CHANNELS)
		return -1;
	voice = &mixer->voices[channel];
	volume = (mode >> 16) & 0xffu;
	frequency = (mode >> 8) & 0xffu;
	pan = mode & 0xffu;
	if (frequency != 0xffu && (frequency & 7u) >= 5)
		return -1; /* MXDRV PDX data uses the five ADPCM rates. */
	if (volume != 0xffu)
		voice->volume = pcm8_volumes[volume & 15u];
	if (frequency != 0xffu)
		voice->sample_divisor = rate_divisors[frequency & 7u];
	if (pan != 0xffu)
		voice->pan = (uint8_t)(pan & 3u);
	return voice_replace_data(voice, data, length);
}

int x68k_pcm8_stop(X68K_PCM8 *mixer, unsigned int channel)
{
	if (mixer == NULL || channel >= PCM8_CHANNELS)
		return -1;
	mixer->voices[channel].active = 0;
	mixer->voices[channel].paused = 0;
	return 0;
}

int x68k_pcm8_control(X68K_PCM8 *mixer, int mode)
{
	unsigned int channel;

	if (mixer == NULL || mode < 0 || mode > 2)
		return -1;
	for (channel = 0; channel < PCM8_CHANNELS; ++channel) {
		if (mode == 0) {
			mixer->voices[channel].active = 0;
			mixer->voices[channel].paused = 0;
		} else {
			mixer->voices[channel].paused = mode == 1;
		}
	}
	return 0;
}

size_t x68k_pcm8_remaining(const X68K_PCM8 *mixer,
	unsigned int channel)
{
	const X68K_ADPCM_VOICE *voice;
	size_t consumed;

	if (mixer == NULL || channel >= PCM8_CHANNELS)
		return 0;
	voice = &mixer->voices[channel];
	if (!voice->active)
		return 0;
	consumed = (voice->nibble_position + 1u) / 2u;
	return consumed >= voice->length ? 0 : voice->length - consumed;
}

void x68k_pcm8_mix(X68K_PCM8 *mixer, int16_t *samples, size_t frames)
{
	size_t frame;

	if (mixer == NULL || samples == NULL)
		return;
	for (frame = 0; frame < frames; ++frame) {
		int left = samples[frame * 2];
		int right = samples[frame * 2 + 1];
		unsigned int channel;

		for (channel = 0; channel < PCM8_CHANNELS; ++channel) {
			X68K_ADPCM_VOICE *voice = &mixer->voices[channel];
			int output = voice_next_output(voice);

			if ((voice->pan & 1u) != 0)
				left += output;
			if ((voice->pan & 2u) != 0)
				right += output;
		}
		samples[frame * 2] = mix_sample(0, left);
		samples[frame * 2 + 1] = mix_sample(0, right);
	}
}
