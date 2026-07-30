#include "x68k_bus.h"

#include <string.h>

enum {
	X68K_ACCESS_BYTE = 0,
	X68K_MFP_FIRST_PORT = 0x00e88001u,
	X68K_MFP_LAST_PORT = 0x00e8802fu,
	X68K_OPM_ADDRESS_PORT = 0x00e90001u,
	X68K_OPM_DATA_PORT = 0x00e90003u
};

static X68K_OPM_BACKEND opm_backend;
static uint8_t opm_address;
static uint64_t opm_write_count;
static uint8_t mfp_registers[24];

enum {
	MFP_IERB = 4,
	MFP_IMRB = 10,
	MFP_VECTOR = 11,
	MFP_OPM_BIT = 0x08,
	MFP_OPM_CHANNEL = 3
};

static int mfp_register_index(uint32_t address)
{
	if (address < X68K_MFP_FIRST_PORT || address > X68K_MFP_LAST_PORT ||
	    (address & 1u) == 0)
		return -1;
	return (int)((address - X68K_MFP_FIRST_PORT) / 2u);
}

void x68k_bus_reset(void)
{
	opm_address = 0;
	opm_write_count = 0;
	memset(mfp_registers, 0, sizeof(mfp_registers));
	mfp_registers[MFP_VECTOR] = 0x40;
	if (opm_backend.reset != NULL)
		opm_backend.reset(opm_backend.context);
}

void x68k_bus_set_opm_backend(const X68K_OPM_BACKEND *backend)
{
	if (backend == NULL)
		memset(&opm_backend, 0, sizeof(opm_backend));
	else
		opm_backend = *backend;
	x68k_bus_reset();
}

int x68k_bus_read(uint32_t address, int size, uint32_t *value)
{
	int mfp_index;

	address &= 0x00ffffffu;
	if (size != X68K_ACCESS_BYTE)
		return 0;
	mfp_index = mfp_register_index(address);
	if (mfp_index >= 0) {
		if (value != NULL)
			*value = mfp_registers[mfp_index];
		return 1;
	}
	if (address != X68K_OPM_DATA_PORT)
		return 0;

	if (value != NULL) {
		if (opm_backend.read_status != NULL)
			*value = opm_backend.read_status(opm_backend.context);
		else
			*value = 0;
	}
	return 1;
}

int x68k_bus_write(uint32_t address, int size, uint32_t value)
{
	int mfp_index;

	address &= 0x00ffffffu;
	if (size != X68K_ACCESS_BYTE)
		return 0;
	mfp_index = mfp_register_index(address);
	if (mfp_index >= 0) {
		mfp_registers[mfp_index] = (uint8_t)value;
		return 1;
	}

	if (address == X68K_OPM_ADDRESS_PORT) {
		opm_address = (uint8_t)value;
		return 1;
	}
	if (address == X68K_OPM_DATA_PORT) {
		if (opm_backend.write_register != NULL) {
			opm_write_count++;
			opm_backend.write_register(opm_backend.context, opm_address,
			                           (uint8_t)value);
		}
		return 1;
	}
	return 0;
}

uint64_t x68k_bus_opm_write_count(void)
{
	return opm_write_count;
}

int x68k_bus_opm_irq_vector(void)
{
	if ((mfp_registers[MFP_IERB] & MFP_OPM_BIT) == 0 ||
	    (mfp_registers[MFP_IMRB] & MFP_OPM_BIT) == 0)
		return -1;
	return (mfp_registers[MFP_VECTOR] & 0xf0) | MFP_OPM_CHANNEL;
}
