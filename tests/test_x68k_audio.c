#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x68k_audio.h"
#include "x68k_bus.h"

static uint32_t get_u32le(const unsigned char *source)
{
	return (uint32_t)source[0] |
	       ((uint32_t)source[1] << 8) |
	       ((uint32_t)source[2] << 16) |
	       ((uint32_t)source[3] << 24);
}

static void write_opm(uint8_t reg, uint8_t value)
{
	(void)x68k_bus_write(0x00e90001, 0, reg);
	(void)x68k_bus_write(0x00e90003, 0, value);
}

static void program_test_tone(void)
{
	static const uint8_t offsets[] = {0x00, 0x08, 0x10, 0x18};
	size_t index;

	write_opm(0x20, 0xc7);
	write_opm(0x28, 0x4a);
	write_opm(0x30, 0x00);
	for (index = 0; index < sizeof(offsets); ++index) {
		uint8_t offset = offsets[index];
		write_opm((uint8_t)(0x40 + offset), 0x01);
		write_opm((uint8_t)(0x60 + offset), 0x00);
		write_opm((uint8_t)(0x80 + offset), 0x1f);
		write_opm((uint8_t)(0xa0 + offset), 0x00);
		write_opm((uint8_t)(0xc0 + offset), 0x00);
		write_opm((uint8_t)(0xe0 + offset), 0x0f);
	}
	write_opm(0x08, 0x78);
}

int main(int argc, char **argv)
{
	const char *path;
	X68K_AUDIO *audio;
	FILE *wav;
	unsigned char header[44];
	unsigned char sample_bytes[4096];
	size_t bytes_read;
	size_t index;
	int nonzero = 0;
	int result = 0;

	if (argc != 2)
		return 2;
	path = argv[1];
	(void)remove(path);
	audio = x68k_audio_create_wav(path);
	if (audio == NULL) {
		fprintf(stderr, "failed to create WAV audio backend\n");
		return 1;
	}
	program_test_tone();
	x68k_audio_advance_cpu_cycles(audio, 1000000);
	if (x68k_audio_destroy(audio) != 0) {
		fprintf(stderr, "failed to finalize WAV audio backend\n");
		return 1;
	}

	wav = fopen(path, "rb");
	if (wav == NULL || fread(header, 1, sizeof(header), wav) != sizeof(header)) {
		fprintf(stderr, "failed to read generated WAV\n");
		if (wav != NULL)
			fclose(wav);
		(void)remove(path);
		return 1;
	}
	if (memcmp(header, "RIFF", 4) != 0 ||
	    memcmp(header + 8, "WAVEfmt ", 8) != 0 ||
	    memcmp(header + 36, "data", 4) != 0) {
		fprintf(stderr, "invalid WAV header\n");
		result = 1;
	}
	if (get_u32le(header + 24) != 62500) {
		fprintf(stderr, "unexpected WAV sample rate: %u\n",
		        get_u32le(header + 24));
		result = 1;
	}
	if (get_u32le(header + 40) != 25000) {
		fprintf(stderr, "unexpected WAV data size: %u\n",
		        get_u32le(header + 40));
		result = 1;
	}

	bytes_read = fread(sample_bytes, 1, sizeof(sample_bytes), wav);
	for (index = 0; index < bytes_read; ++index)
		nonzero = nonzero || sample_bytes[index] != 0;
	if (!nonzero) {
		fprintf(stderr, "generated WAV contains only silence\n");
		result = 1;
	}
	fclose(wav);
	(void)remove(path);
	return result;
}
