#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "msm6258.h"

static int failures;

static void expect_true(const char *name, int value)
{
	if (!value) {
		fprintf(stderr, "%s failed\n", name);
		failures++;
	}
}

static int channel_nonzero(const int16_t *samples, size_t frames,
	unsigned int channel)
{
	size_t frame;

	for (frame = 0; frame < frames; ++frame) {
		if (samples[frame * 2 + channel] != 0)
			return 1;
	}
	return 0;
}

static int channel_peak(const int16_t *samples, size_t frames,
	unsigned int channel)
{
	size_t frame;
	int peak = 0;

	for (frame = 0; frame < frames; ++frame) {
		int value = samples[frame * 2 + channel];
		int magnitude = value < 0 ? -value : value;

		if (magnitude > peak)
			peak = magnitude;
	}
	return peak;
}

int main(void)
{
	static const uint8_t rising_sample[] = {0x77, 0x77, 0x77, 0x77};
	X68K_MSM6258 *device = x68k_msm6258_create();
	int16_t samples[128 * 2];

	if (device == NULL)
		return 1;
	expect_true("reject invalid rate",
	            x68k_msm6258_start(device, rising_sample,
	                                sizeof(rising_sample), 0x0503) != 0);

	memset(samples, 0, sizeof(samples));
	expect_true("start stereo",
	            x68k_msm6258_start(device, rising_sample,
	                                sizeof(rising_sample), 0x0403) == 0);
	x68k_msm6258_mix(device, samples, 64);
	expect_true("left output", channel_nonzero(samples, 64, 0));
	expect_true("right output", channel_nonzero(samples, 64, 1));
	expect_true("10-bit output range",
	            channel_peak(samples, 64, 0) <= 4096);
	expect_true("first nibble on first tick", samples[0] == 224);

	memset(samples, 0, sizeof(samples));
	expect_true("start left",
	            x68k_msm6258_start(device, rising_sample,
	                                sizeof(rising_sample), 0x0401) == 0);
	x68k_msm6258_mix(device, samples, 32);
	expect_true("left pan output", channel_nonzero(samples, 32, 0));
	expect_true("right pan silence", !channel_nonzero(samples, 32, 1));

	memset(samples, 0, sizeof(samples));
	expect_true("restart for pause",
	            x68k_msm6258_start(device, rising_sample,
	                                sizeof(rising_sample), 0x0403) == 0);
	expect_true("pause", x68k_msm6258_control(device, 1) == 0);
	x68k_msm6258_mix(device, samples, 16);
	expect_true("paused status", x68k_msm6258_status(device) == 2);
	expect_true("paused silence", !channel_nonzero(samples, 16, 0));
	expect_true("resume", x68k_msm6258_control(device, 2) == 0);
	x68k_msm6258_mix(device, samples, 32);
	expect_true("resumed output", channel_nonzero(samples, 32, 0));
	expect_true("stop", x68k_msm6258_control(device, 0) == 0);
	expect_true("stopped status", x68k_msm6258_status(device) == 0);

	x68k_msm6258_destroy(device);
	return failures != 0;
}
