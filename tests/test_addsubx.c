#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "run68.h"

Long ra[8];
Long rd[9];
Long pc;
short sr;

static int failures;
static int unexpected_error;
static Long source_memory;
static Long destination_memory;

void err68a(char *message, char *file, int line)
{
	(void)message;
	(void)file;
	(void)line;
	unexpected_error = 1;
}

static ULong size_mask(int size)
{
	return size == S_BYTE ? 0xffu :
	       size == S_WORD ? 0xffffu : UINT32_MAX;
}

static unsigned size_bytes(int size, int reg)
{
	if (size == S_BYTE)
		return reg == 7 ? 2u : 1u;
	return size == S_WORD ? 2u : 4u;
}

BOOL get_data_at_ea(int accepted, int mode, int reg, int size, Long *data)
{
	ULong mask = size_mask(size);

	(void)accepted;
	if (mode == EA_AIPD) {
		ra[reg] = (Long)((ULong)ra[reg] - size_bytes(size, reg));
		*data = (reg == 2 ? source_memory : destination_memory) & mask;
		return FALSE;
	}
	if (mode == EA_DD) {
		*data = (Long)((ULong)rd[reg] & mask);
		return FALSE;
	}
	unexpected_error = 1;
	return TRUE;
}

BOOL get_data_at_ea_noinc(int accepted, int mode, int reg, int size,
	Long *data)
{
	return get_data_at_ea(accepted, mode, reg, size, data);
}

BOOL set_data_at_ea(int accepted, int mode, int reg, int size, Long data)
{
	ULong mask = size_mask(size);

	(void)accepted;
	if (mode == EA_AI && reg == 1) {
		destination_memory = (Long)((ULong)data & mask);
		return FALSE;
	}
	unexpected_error = 1;
	return TRUE;
}

static unsigned expected_flags(int subtract, unsigned src, unsigned dest,
	unsigned x, unsigned initial_z)
{
	int signed_result;
	unsigned result;
	unsigned carry;
	unsigned overflow;
	unsigned flags;

	if (subtract) {
		int difference = (int)dest - (int)src - (int)x;
		signed_result = (int)(int8_t)dest - (int)(int8_t)src - (int)x;
		result = (unsigned)difference & 0xffu;
		carry = dest < src + x;
		overflow = signed_result < -128 || signed_result > 127;
	} else {
		unsigned sum = dest + src + x;
		signed_result = (int)(int8_t)dest + (int)(int8_t)src + (int)x;
		result = sum & 0xffu;
		carry = sum > 0xffu;
		overflow = signed_result < -128 || signed_result > 127;
	}

	flags = (carry ? 0x11u : 0) |
	        ((result & 0x80u) ? 0x08u : 0) |
	        (initial_z && result == 0 ? 0x04u : 0) |
	        (overflow ? 0x02u : 0);
	return flags;
}

static void run_register_case(int subtract, unsigned src, unsigned dest,
	unsigned x, unsigned initial_z)
{
	char opcode[2] = {(char)(subtract ? 0x93 : 0xd3), (char)0x02};
	unsigned expected_result = (subtract ? dest - src - x : dest + src + x) &
	                           0xffu;
	unsigned flags = expected_flags(subtract, src, dest, x, initial_z);

	memset(rd, 0, sizeof(rd));
	rd[1] = (Long)(UINT32_C(0xa55a3c00) | dest);
	rd[2] = (Long)(UINT32_C(0x12340000) | src);
	sr = (short)(0xa500u | (x ? 0x10u : 0) | (initial_z ? 0x04u : 0));
	pc = 0;
	unexpected_error = 0;

	if ((subtract ? line9(opcode) : lined(opcode)) != FALSE ||
	    unexpected_error || pc != 2 ||
	    (ULong)rd[1] != (UINT32_C(0xa55a3c00) | expected_result) ||
	    ((unsigned)(UShort)sr & 0x1fu) != flags) {
		fprintf(stderr,
		        "%s.B D2,D1 src=%02x dest=%02x X=%u Z=%u: "
		        "data=%08x/%08x flags=%02x/%02x err=%d\n",
		        subtract ? "SUBX" : "ADDX", src, dest, x, initial_z,
		        UINT32_C(0xa55a3c00) | expected_result, (ULong)rd[1],
		        flags, (unsigned)(UShort)sr & 0x1fu, unexpected_error);
		failures++;
	}
}

static void test_register_forms(void)
{
	unsigned src;
	unsigned dest;
	unsigned x;
	unsigned initial_z;
	int subtract;

	for (subtract = 0; subtract <= 1; subtract++)
		for (src = 0; src <= 0xff; src++)
			for (dest = 0; dest <= 0xff; dest++)
				for (x = 0; x <= 1; x++)
					for (initial_z = 0; initial_z <= 1; initial_z++)
						run_register_case(subtract, src, dest, x, initial_z);
}

static void test_memory_forms(void)
{
	char addx[2] = {(char)0xd3, (char)0x0a}; /* ADDX.B -(A2),-(A1) */
	char subx[2] = {(char)0x93, (char)0x8a}; /* SUBX.L -(A2),-(A1) */

	memset(ra, 0, sizeof(ra));
	ra[1] = 0x1000;
	ra[2] = 0x2000;
	source_memory = 0xff;
	destination_memory = 0;
	sr = 0x14;
	pc = 0;
	unexpected_error = 0;
	if (lined(addx) != FALSE || unexpected_error || ra[1] != 0x0fff ||
	    ra[2] != 0x1fff || destination_memory != 0 ||
	    ((UShort)sr & 0x1f) != 0x15) {
		fprintf(stderr,
		        "ADDX.B memory failed: A1=%08x A2=%08x data=%08x SR=%02x err=%d\n",
		        (ULong)ra[1], (ULong)ra[2], (ULong)destination_memory,
		        (UShort)sr & 0x1f, unexpected_error);
		failures++;
	}

	ra[1] = 0x1000;
	ra[2] = 0x2000;
	source_memory = 1;
	destination_memory = (Long)UINT32_C(0x80000000);
	sr = 0;
	pc = 0;
	unexpected_error = 0;
	if (line9(subx) != FALSE || unexpected_error || ra[1] != 0x0ffc ||
	    ra[2] != 0x1ffc ||
	    (ULong)destination_memory != UINT32_C(0x7fffffff) ||
	    ((UShort)sr & 0x1f) != 0x02) {
		fprintf(stderr,
		        "SUBX.L memory failed: A1=%08x A2=%08x data=%08x SR=%02x err=%d\n",
		        (ULong)ra[1], (ULong)ra[2], (ULong)destination_memory,
		        (UShort)sr & 0x1f, unexpected_error);
		failures++;
	}
}

int main(void)
{
	test_register_forms();
	test_memory_forms();
	return failures == 0 ? 0 : 1;
}
