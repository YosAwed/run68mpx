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
	static const int16_t pcm8_filter_reference[] = {
		7168, 6893, 6627, 6370, 22506, 21638, 20799, 19988
	};
	X68K_MSM6258 *device = x68k_msm6258_create();
	X68K_PCM8 *pcm8;
	uint8_t long_sample[64];
	int16_t samples[128 * 2];
	size_t remaining;
	int quiet_first;
	int loud_first;
	int filter_matches;
	unsigned int channel;

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

	memset(long_sample, 0x77, sizeof(long_sample));
	pcm8 = x68k_pcm8_create();
	if (pcm8 == NULL)
		return 1;
	expect_true("reject PCM8 raw PCM mode",
	            x68k_pcm8_start(pcm8, 0, long_sample,
	                             sizeof(long_sample), 0x00080503) != 0);
	expect_true("start PCM8 left",
	            x68k_pcm8_start(pcm8, 0, long_sample,
	                             sizeof(long_sample), 0x00080401) == 0);
	expect_true("start PCM8 right",
	            x68k_pcm8_start(pcm8, 1, long_sample,
	                             sizeof(long_sample), 0x00080402) == 0);
	memset(samples, 0, sizeof(samples));
	x68k_pcm8_mix(pcm8, samples, 32);
	expect_true("PCM8 left channel", channel_nonzero(samples, 32, 0));
	expect_true("PCM8 right channel", channel_nonzero(samples, 32, 1));
	expect_true("PCM8 independent equal voices", samples[0] == samples[1]);
	filter_matches = 1;
	for (channel = 0; channel < 8; ++channel) {
		if (samples[channel * 2] != pcm8_filter_reference[channel] ||
		    samples[channel * 2 + 1] != pcm8_filter_reference[channel])
			filter_matches = 0;
	}
	expect_true("X68Sound PCM8 filter reference", filter_matches);
	remaining = x68k_pcm8_remaining(pcm8, 0);
	expect_true("PCM8 remaining decreases",
	            remaining != 0 && remaining < sizeof(long_sample));
	expect_true("PCM8 pause", x68k_pcm8_control(pcm8, 1) == 0);
	memset(samples, 0, sizeof(samples));
	x68k_pcm8_mix(pcm8, samples, 32);
	expect_true("PCM8 pause silence", !channel_nonzero(samples, 32, 0));
	expect_true("PCM8 pause preserves position",
	            x68k_pcm8_remaining(pcm8, 0) == remaining);
	expect_true("PCM8 resume", x68k_pcm8_control(pcm8, 2) == 0);
	memset(samples, 0, sizeof(samples));
	x68k_pcm8_mix(pcm8, samples, 32);
	expect_true("PCM8 resume output", channel_nonzero(samples, 32, 0));
	expect_true("PCM8 resume advances",
	            x68k_pcm8_remaining(pcm8, 0) < remaining);

	expect_true("PCM8 quiet voice",
	            x68k_pcm8_start(pcm8, 0, long_sample,
	                             sizeof(long_sample), 0x00000401) == 0);
	memset(samples, 0, sizeof(samples));
	x68k_pcm8_mix(pcm8, samples, 64);
	quiet_first = samples[0];
	expect_true("PCM8 loud voice",
	            x68k_pcm8_start(pcm8, 0, long_sample,
	                             sizeof(long_sample), 0x000f0401) == 0);
	memset(samples, 0, sizeof(samples));
	x68k_pcm8_mix(pcm8, samples, 64);
	loud_first = samples[0];
	expect_true("PCM8 volume scaling", loud_first > quiet_first);

	for (channel = 0; channel < 8; ++channel)
		expect_true("PCM8 saturation voice",
		            x68k_pcm8_start(pcm8, channel, long_sample,
		                             sizeof(long_sample), 0x000f0403) == 0);
	memset(samples, 0, sizeof(samples));
	x68k_pcm8_mix(pcm8, samples, 128);
	expect_true("PCM8 saturates left", channel_peak(samples, 128, 0) >= 32767);
	expect_true("PCM8 saturates right", channel_peak(samples, 128, 1) >= 32767);
	expect_true("PCM8 abort", x68k_pcm8_control(pcm8, 0) == 0);
	expect_true("PCM8 abort clears remaining",
	            x68k_pcm8_remaining(pcm8, 0) == 0);
	x68k_pcm8_destroy(pcm8);
	return failures != 0;
}
