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

BOOL get_data_at_ea_noinc(int accepted, int mode, int reg, int size,
	Long *data)
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
	ULong value = (ULong)result;
	ULong sign = size == S_BYTE ? 0x80u :
	             size == S_WORD ? 0x8000u : UINT32_C(0x80000000);
	ULong mask = size == S_BYTE ? 0xffu :
	             size == S_WORD ? 0xffffu : UINT32_MAX;

	value &= mask;
	sr &= (short)~0x0fu;
	if ((value & sign) != 0)
		sr |= 0x08;
	if (value == 0)
		sr |= 0x04;
}

void err68a(char *message, char *file, int line)
{
	(void)message;
	(void)file;
	(void)line;
	unexpected_error = 1;
}

static unsigned pack_bcd(unsigned value)
{
	return ((value / 10u) << 4) | (value % 10u);
}

static void test_multiply_case(int is_signed, unsigned src, unsigned dest)
{
	char opcode[2] = {(char)(is_signed ? 0xc3 : 0xc2), (char)0xc2};
	ULong expected;
	unsigned expected_ccr;

	memset(rd, 0, sizeof(rd));
	rd[1] = (Long)(UINT32_C(0x5a5a0000) | dest);
	rd[2] = (Long)src;
	if (is_signed)
		expected = (ULong)((int32_t)(int16_t)src *
		                   (int32_t)(int16_t)dest);
	else
		expected = (ULong)((uint32_t)(uint16_t)src *
		                   (uint32_t)(uint16_t)dest);
	expected_ccr = (expected & UINT32_C(0x80000000) ? 0x08u : 0) |
	               (expected == 0 ? 0x04u : 0) | 0x10u;

	sr = (short)0xa51fu;
	pc = 0;
	unexpected_error = 0;
	if (linec(opcode) != FALSE || unexpected_error || pc != 2 ||
	    (ULong)rd[1] != expected ||
	    ((unsigned)(UShort)sr & 0x1fu) != expected_ccr) {
		fprintf(stderr,
		        "MUL%c src=%04x dest=%04x: data %08x/%08x CCR %02x/%02x\n",
		        is_signed ? 'S' : 'U', src, dest, expected, (ULong)rd[1],
		        expected_ccr, (unsigned)(UShort)sr & 0x1fu);
		failures++;
	}
}

static void test_multiply(void)
{
	static const unsigned values[] = {0, 1, 0x7fffu, 0x8000u, 0xffffu};
	size_t src;
	size_t dest;
	int is_signed;

	for (is_signed = 0; is_signed <= 1; is_signed++)
		for (src = 0; src < sizeof(values) / sizeof(values[0]); src++)
			for (dest = 0; dest < sizeof(values) / sizeof(values[0]); dest++)
				test_multiply_case(is_signed, values[src], values[dest]);
}

static void test_abcd(void)
{
	char opcode[2] = {(char)0xc3, (char)0x02}; /* ABCD D2,D1 */
	unsigned src;
	unsigned dest;
	unsigned x;
	unsigned initial_z;

	for (src = 0; src < 100; src++) {
		for (dest = 0; dest < 100; dest++) {
			for (x = 0; x <= 1; x++) {
				for (initial_z = 0; initial_z <= 1; initial_z++) {
					unsigned sum = src + dest + x;
					unsigned carry = sum >= 100;
					unsigned result = pack_bcd(sum % 100);
					unsigned expected_flags = (carry ? 0x11u : 0) |
					    (initial_z && result == 0 ? 0x04u : 0);

					memset(rd, 0, sizeof(rd));
					rd[1] = (Long)(UINT32_C(0xa55a0000) | pack_bcd(dest));
					rd[2] = (Long)pack_bcd(src);
					sr = (short)(0x200au | (x ? 0x10u : 0) |
					             (initial_z ? 0x04u : 0));
					pc = 0;
					unexpected_error = 0;

					if (linec(opcode) != FALSE || unexpected_error || pc != 2 ||
					    (ULong)rd[1] != (UINT32_C(0xa55a0000) | result) ||
					    ((unsigned)(UShort)sr & 0x15u) != expected_flags) {
						fprintf(stderr,
						        "ABCD %02u+%02u X=%u Z=%u: data=%08x flags=%02x/%02x\n",
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
	test_multiply();
	test_abcd();
	return failures == 0 ? 0 : 1;
}
