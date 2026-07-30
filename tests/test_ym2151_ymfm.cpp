#include <cstdint>
#include <cstdio>
#include <vector>

#include "x68k_bus.h"
#include "ym2151_ymfm.h"

namespace
{

int failures;

void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
	if (expected != actual)
	{
		std::fprintf(stderr, "%s: expected %08x, got %08x\n", name,
		             expected, actual);
		failures++;
	}
}

void write_opm(uint8_t reg, uint8_t value)
{
	(void)x68k_bus_write(0x00e90001, 0, reg);
	(void)x68k_bus_write(0x00e90003, 0, value);
}

void program_test_tone()
{
	write_opm(0x20, 0xc7); // channel 0, algorithm 7, left + right
	write_opm(0x28, 0x4a); // A4
	write_opm(0x30, 0x00);
	for (uint8_t offset : {0x00, 0x08, 0x10, 0x18})
	{
		write_opm(static_cast<uint8_t>(0x40 + offset), 0x01);
		write_opm(static_cast<uint8_t>(0x60 + offset), 0x00);
		write_opm(static_cast<uint8_t>(0x80 + offset), 0x1f);
		write_opm(static_cast<uint8_t>(0xa0 + offset), 0x00);
		write_opm(static_cast<uint8_t>(0xc0 + offset), 0x00);
		write_opm(static_cast<uint8_t>(0xe0 + offset), 0x0f);
	}
	write_opm(0x08, 0x78); // key on all four operators, channel 0
}

} // namespace

int main()
{
	X68K_YM2151 *device = x68k_ym2151_create(4000000);
	if (device == nullptr)
	{
		std::fprintf(stderr, "failed to create YM2151\n");
		return 2;
	}

	X68K_OPM_BACKEND backend = x68k_ym2151_bus_backend(device);
	x68k_bus_set_opm_backend(&backend);
	expect_u32("sample rate", 62500, x68k_ym2151_sample_rate(device));

	program_test_tone();
	expect_u32("busy after write", 0x80,
	           x68k_ym2151_read_status(device) & 0x80);

	constexpr size_t frame_count = 4096;
	std::vector<int16_t> samples(frame_count * 2);
	expect_u32("generated frames", frame_count,
	           static_cast<uint32_t>(x68k_ym2151_advance(
		           device, static_cast<uint32_t>(frame_count * 64),
		           samples.data(), frame_count)));
	expect_u32("busy cleared", 0,
	           x68k_ym2151_read_status(device) & 0x80);

	bool nonzero = false;
	for (int16_t sample : samples)
		nonzero = nonzero || sample != 0;
	if (!nonzero)
	{
		std::fprintf(stderr, "YM2151 test tone produced silence\n");
		failures++;
	}

	write_opm(0x12, 0xff);
	write_opm(0x14, 0x0a); // load and enable timer B
	(void)x68k_ym2151_advance(device, 4096, nullptr, 0);
	if (!x68k_ym2151_irq_asserted(device))
	{
		std::fprintf(stderr, "YM2151 timer B did not assert IRQ\n");
		failures++;
	}

	write_opm(0x14, 0x20); // reset timer B status
	expect_u32("timer B IRQ cleared", 0,
	           static_cast<uint32_t>(x68k_ym2151_irq_asserted(device)));

	x68k_bus_set_opm_backend(nullptr);
	x68k_ym2151_destroy(device);
	return failures == 0 ? 0 : 1;
}
