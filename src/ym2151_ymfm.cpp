#include "ym2151_ymfm.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <new>

#include "ymfm_opm.h"

namespace
{

class run68_ymfm_interface : public ymfm::ymfm_interface
{
public:
	void reset()
	{
		m_current_clock = 0;
		m_busy_end = 0;
		m_timer_expiry.fill(k_timer_disabled);
		m_irq_asserted = false;
	}

	void advance(uint32_t clocks)
	{
		uint64_t target = m_current_clock + clocks;
		for (;;)
		{
			uint32_t timer = 0;
			uint64_t expiry = m_timer_expiry[0];
			if (m_timer_expiry[1] < expiry)
			{
				timer = 1;
				expiry = m_timer_expiry[1];
			}
			if (expiry == k_timer_disabled || expiry > target)
				break;

			m_current_clock = expiry;
			m_timer_expiry[timer] = k_timer_disabled;
			m_engine->engine_timer_expired(timer);
		}
		m_current_clock = target;
	}

	bool irq_asserted() const { return m_irq_asserted; }

	void ymfm_set_timer(uint32_t timer, int32_t duration_in_clocks) override
	{
		if (timer >= m_timer_expiry.size())
			return;
		m_timer_expiry[timer] = duration_in_clocks < 0
			? k_timer_disabled
			: m_current_clock + static_cast<uint32_t>(duration_in_clocks);
	}

	void ymfm_set_busy_end(uint32_t clocks) override
	{
		m_busy_end = m_current_clock + clocks;
	}

	bool ymfm_is_busy() override
	{
		return m_current_clock < m_busy_end;
	}

	void ymfm_update_irq(bool asserted) override
	{
		m_irq_asserted = asserted;
	}

private:
	static constexpr uint64_t k_timer_disabled =
		std::numeric_limits<uint64_t>::max();

	uint64_t m_current_clock = 0;
	uint64_t m_busy_end = 0;
	std::array<uint64_t, 2> m_timer_expiry = {
		k_timer_disabled, k_timer_disabled
	};
	bool m_irq_asserted = false;
};

int16_t clamp_sample(int32_t value)
{
	return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

void bus_reset(void *context)
{
	x68k_ym2151_reset(static_cast<X68K_YM2151 *>(context));
}

uint8_t bus_read_status(void *context)
{
	return x68k_ym2151_read_status(static_cast<X68K_YM2151 *>(context));
}

void bus_write_register(void *context, uint8_t reg, uint8_t value)
{
	x68k_ym2151_write_register(static_cast<X68K_YM2151 *>(context), reg,
	                            value);
}

} // namespace

struct X68K_YM2151
{
	explicit X68K_YM2151(uint32_t clock) :
		input_clock(clock),
		chip(interface),
		sample_rate(chip.sample_rate(clock)),
		clocks_per_sample(sample_rate == 0 ? 0 : clock / sample_rate)
	{
		interface.reset();
		chip.reset();
	}

	uint32_t input_clock;
	run68_ymfm_interface interface;
	ymfm::ym2151 chip;
	uint32_t sample_rate;
	uint32_t clocks_per_sample;
	uint32_t sample_clock_phase = 0;
};

extern "C" X68K_YM2151 *x68k_ym2151_create(uint32_t input_clock)
{
	if (input_clock == 0)
		return nullptr;
	try
	{
		X68K_YM2151 *device = new X68K_YM2151(input_clock);
		if (device->sample_rate == 0 || device->clocks_per_sample == 0)
		{
			delete device;
			return nullptr;
		}
		return device;
	}
	catch (...)
	{
		return nullptr;
	}
}

extern "C" void x68k_ym2151_destroy(X68K_YM2151 *device)
{
	delete device;
}

extern "C" void x68k_ym2151_reset(X68K_YM2151 *device)
{
	if (device == nullptr)
		return;
	device->sample_clock_phase = 0;
	device->interface.reset();
	device->chip.reset();
}

extern "C" uint32_t x68k_ym2151_sample_rate(const X68K_YM2151 *device)
{
	return device == nullptr ? 0 : device->sample_rate;
}

extern "C" uint8_t x68k_ym2151_read_status(X68K_YM2151 *device)
{
	return device == nullptr ? 0 : device->chip.read_status();
}

extern "C" void x68k_ym2151_write_register(X68K_YM2151 *device,
	uint8_t reg, uint8_t value)
{
	if (device == nullptr)
		return;
	device->chip.write_address(reg);
	device->chip.write_data(value);
}

extern "C" int x68k_ym2151_irq_asserted(const X68K_YM2151 *device)
{
	return device != nullptr && device->interface.irq_asserted();
}

extern "C" size_t x68k_ym2151_advance(X68K_YM2151 *device,
	uint32_t clocks, int16_t *output, size_t frame_capacity)
{
	if (device == nullptr)
		return 0;

	size_t frames = 0;
	while (clocks != 0)
	{
		uint32_t remaining = device->clocks_per_sample -
		                     device->sample_clock_phase;
		uint32_t step = std::min(clocks, remaining);
		device->interface.advance(step);
		device->sample_clock_phase += step;
		clocks -= step;

		if (device->sample_clock_phase == device->clocks_per_sample)
		{
			ymfm::ym2151::output_data sample;
			device->chip.generate(&sample);
			if (output != nullptr && frames < frame_capacity)
			{
				output[frames * 2] = clamp_sample(sample.data[0]);
				output[frames * 2 + 1] = clamp_sample(sample.data[1]);
			}
			device->sample_clock_phase = 0;
			frames++;
		}
	}
	return frames;
}

extern "C" X68K_OPM_BACKEND x68k_ym2151_bus_backend(
	X68K_YM2151 *device)
{
	X68K_OPM_BACKEND backend = {
		device,
		bus_reset,
		bus_read_status,
		bus_write_register
	};
	return backend;
}
