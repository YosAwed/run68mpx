#include "msm6258.h"

#include <stdlib.h>
#include <string.h>

enum {
	MSM6258_MIN_SIGNAL = -2048,
	MSM6258_MAX_SIGNAL = 2047,
	MSM6258_MAX_STEP = 48
};

struct X68K_MSM6258 {
	uint8_t *data;
	size_t length;
	size_t nibble_position;
	uint32_t sample_phase;
	uint32_t sample_divisor;
	int signal;
	int step;
	int previous_input;
	int filter_output;
	uint8_t pan;
	int active;
	int paused;
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

static int16_t mix_sample(int16_t original, int addition)
{
	int mixed = (int)original + addition;

	if (mixed > 32767)
		mixed = 32767;
	else if (mixed < -32768)
		mixed = -32768;
	return (int16_t)mixed;
}

static int decode_nibble(X68K_MSM6258 *device)
{
	size_t byte_position;
	uint8_t value;
	int magnitude;
	int delta;

	if (device->nibble_position >= device->length * 2u) {
		device->active = 0;
		return 0;
	}
	byte_position = device->nibble_position / 2u;
	value = device->data[byte_position];
	if ((device->nibble_position & 1u) == 0)
		value &= 0x0f;
	else
		value >>= 4;
	device->nibble_position++;

	magnitude = step_table[device->step];
	delta = magnitude / 8;
	if ((value & 1u) != 0)
		delta += magnitude / 4;
	if ((value & 2u) != 0)
		delta += magnitude / 2;
	if ((value & 4u) != 0)
		delta += magnitude;
	if ((value & 8u) != 0)
		delta = -delta;
	device->signal += delta;
	if (device->signal > MSM6258_MAX_SIGNAL)
		device->signal = MSM6258_MAX_SIGNAL;
	else if (device->signal < MSM6258_MIN_SIGNAL)
		device->signal = MSM6258_MIN_SIGNAL;

	device->step += step_adjust[value & 7u];
	if (device->step > MSM6258_MAX_STEP)
		device->step = MSM6258_MAX_STEP;
	else if (device->step < 0)
		device->step = 0;
	return 1;
}

X68K_MSM6258 *x68k_msm6258_create(void)
{
	return (X68K_MSM6258 *)calloc(1, sizeof(X68K_MSM6258));
}

void x68k_msm6258_destroy(X68K_MSM6258 *device)
{
	if (device == NULL)
		return;
	free(device->data);
	free(device);
}

int x68k_msm6258_start(X68K_MSM6258 *device, const uint8_t *data,
	size_t length, uint16_t mode)
{
	uint8_t *copy = NULL;
	unsigned int frequency = mode >> 8;
	unsigned int pan = mode & 0xffu;

	if (device == NULL || frequency >= 5 || pan >= 4 ||
	    length > SIZE_MAX / 2u ||
	    (length != 0 && data == NULL))
		return -1;
	if (length != 0) {
		copy = (uint8_t *)malloc(length);
		if (copy == NULL)
			return -1;
		memcpy(copy, data, length);
	}
	free(device->data);
	device->data = copy;
	device->length = length;
	device->nibble_position = 0;
	device->sample_phase = 0;
	device->sample_divisor = rate_divisors[frequency];
	device->signal = 0;
	device->step = 0;
	device->previous_input = 0;
	device->filter_output = 0;
	device->pan = (uint8_t)pan;
	device->active = length != 0;
	device->paused = 0;
	return 0;
}

int x68k_msm6258_control(X68K_MSM6258 *device, int mode)
{
	if (device == NULL || mode < 0 || mode > 2)
		return -1;
	if (mode == 0) {
		device->active = 0;
		device->paused = 0;
	} else if (mode == 1) {
		device->paused = 1;
	} else {
		device->paused = 0;
	}
	return 0;
}

int x68k_msm6258_status(const X68K_MSM6258 *device)
{
	return device != NULL && device->active ? 2 : 0;
}

void x68k_msm6258_mix(X68K_MSM6258 *device, int16_t *samples,
	size_t frames)
{
	size_t frame;

	if (device == NULL || samples == NULL)
		return;
	for (frame = 0; frame < frames; ++frame) {
		int input = 0;
		int output;

		if (device->active && !device->paused) {
			device->sample_phase++;
			while (device->sample_phase >= device->sample_divisor) {
				device->sample_phase -= device->sample_divisor;
				if (!decode_nibble(device))
					break;
			}
			if (device->active)
				input = device->signal * 16;
		}
		/* A small DC blocker approximates the X68000 ADPCM output filter. */
		output = input - device->previous_input +
		         (device->filter_output * 255) / 256;
		device->previous_input = input;
		device->filter_output = output;
		output /= 2;

		/* IOCS pan: 0=off, 1=left, 2=right, 3=both. */
		if ((device->pan & 1u) != 0)
			samples[frame * 2] = mix_sample(samples[frame * 2], output);
		if ((device->pan & 2u) != 0)
			samples[frame * 2 + 1] =
				mix_sample(samples[frame * 2 + 1], output);
	}
}
