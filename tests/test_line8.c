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

BOOL get_data_at_ea(int accepted, int mode, int reg, int size, Long *data)
{
	(void)accepted;
	if (mode != EA_DD) {
		unexpected_error = 1;
		return TRUE;
	}
	*data = rd[reg];
	if (size == S_BYTE)
		*data &= 0xff;
	else if (size == S_WORD)
		*data &= 0xffff;
	return FALSE;
}

BOOL get_data_at_ea_noinc(int accepted, int mode, int reg, int size, Long *data)
{
	return get_data_at_ea(accepted, mode, reg, size, data);
}

BOOL set_data_at_ea(int accepted, int mode, int reg, int size, Long data)
{
	(void)accepted;
	if (mode != EA_DD) {
		unexpected_error = 1;
		return TRUE;
	}
	if (size == S_BYTE)
		rd[reg] = (Long)(((ULong)rd[reg] & 0xffffff00u) |
		                 ((ULong)data & 0xffu));
	else if (size == S_WORD)
		rd[reg] = (Long)(((ULong)rd[reg] & 0xffff0000u) |
		                 ((ULong)data & 0xffffu));
	else
		rd[reg] = data;
	return FALSE;
}

void general_conditions(Long result, int size)
{
	(void)result;
	(void)size;
}

void err68a(char *message, char *file, int line)
{
	(void)message;
	(void)file;
	(void)line;
	unexpected_error = 1;
}

static void expect_u32(const char *name, ULong expected, ULong actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %08x, got %08x\n",
		        name, expected, actual);
		failures++;
	}
}

static void execute_division(UChar opcode1, UChar opcode2)
{
	char opcode[2] = {(char)opcode1, (char)opcode2};
	pc = 0;
	unexpected_error = 0;
	if (line8(opcode) != FALSE || unexpected_error) {
		fprintf(stderr, "division opcode %02x%02x failed\n", opcode1, opcode2);
		failures++;
	}
}

static void test_divu_remainder(void)
{
	memset(rd, 0, sizeof(rd));
	sr = 0x0003;
	rd[1] = 1000500;
	rd[2] = 1000;
	execute_division(0x82, 0xc2); /* divu.w d2,d1 */
	expect_u32("DIVU quotient and 16-bit remainder", 0x01f403e8,
	           (ULong)rd[1]);
	expect_u32("DIVU clears C and V", 0, (ULong)sr & 0x3u);
}

static void test_divs_negative_remainder(void)
{
	memset(rd, 0, sizeof(rd));
	sr = 0;
	rd[1] = -1000;
	rd[2] = 300;
	execute_division(0x83, 0xc2); /* divs.w d2,d1 */
	expect_u32("DIVS signed quotient and remainder", 0xff9cfffd,
	           (ULong)rd[1]);
}

static void test_divs_overflow(void)
{
	memset(rd, 0, sizeof(rd));
	sr = 0;
	rd[1] = (Long)INT32_MIN;
	rd[2] = -1;
	execute_division(0x83, 0xc2); /* divs.w d2,d1 */
	expect_u32("DIVS overflow preserves destination", 0x80000000,
	           (ULong)rd[1]);
	expect_u32("DIVS overflow sets V", 0x2, (ULong)sr & 0x2u);
}

static unsigned pack_bcd(unsigned value)
{
	return ((value / 10u) << 4) | (value % 10u);
}

static void test_sbcd(void)
{
	char opcode[2] = {(char)0x83, (char)0x02}; /* SBCD D2,D1 */
	unsigned src;
	unsigned dest;
	unsigned x;
	unsigned initial_z;

	for (src = 0; src < 100; src++) {
		for (dest = 0; dest < 100; dest++) {
			for (x = 0; x <= 1; x++) {
				for (initial_z = 0; initial_z <= 1; initial_z++) {
					int difference = (int)dest - (int)src - (int)x;
					unsigned borrow = difference < 0;
					unsigned decimal_result =
					    (unsigned)((difference % 100 + 100) % 100);
					unsigned result = pack_bcd(decimal_result);
					unsigned expected_flags = (borrow ? 0x11u : 0) |
					    (initial_z && result == 0 ? 0x04u : 0);

					memset(rd, 0, sizeof(rd));
					rd[1] = (Long)(UINT32_C(0xa55a0000) | pack_bcd(dest));
					rd[2] = (Long)pack_bcd(src);
					sr = (short)(0x200au | (x ? 0x10u : 0) |
					             (initial_z ? 0x04u : 0));
					pc = 0;
					unexpected_error = 0;

					if (line8(opcode) != FALSE || unexpected_error || pc != 2 ||
					    (ULong)rd[1] != (UINT32_C(0xa55a0000) | result) ||
					    ((unsigned)(UShort)sr & 0x15u) != expected_flags) {
						fprintf(stderr,
						        "SBCD %02u-%02u X=%u Z=%u: data=%08x flags=%02x/%02x\n",
						        dest, src, x, initial_z, (ULong)rd[1],
						        expected_flags, (unsigned)(UShort)sr & 0x15u);
						failures++;
					}
				}
			}
		}
	}
}

int main(void)
{
	test_divu_remainder();
	test_divs_negative_remainder();
	test_divs_overflow();
	test_sbcd();
	return failures == 0 ? 0 : 1;
}
