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
static Long immediate;

Long imi_get(char size)
{
	pc = run68_add32(pc, size == S_LONG ? 4 : 2);
	return immediate;
}

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
	(void)result;
	(void)size;
	unexpected_error = 1;
}

Long add_long(Long src, Long dest, int size)
{
	(void)src;
	(void)dest;
	(void)size;
	unexpected_error = 1;
	return 0;
}

Long sub_long(Long src, Long dest, int size)
{
	(void)src;
	(void)dest;
	(void)size;
	unexpected_error = 1;
	return 0;
}

void add_conditions(Long src, Long dest, Long result, int size, BOOL zero)
{
	(void)src; (void)dest; (void)result; (void)size; (void)zero;
	unexpected_error = 1;
}

void sub_conditions(Long src, Long dest, Long result, int size, BOOL zero)
{
	(void)src; (void)dest; (void)result; (void)size; (void)zero;
	unexpected_error = 1;
}

void cmp_conditions(Long src, Long dest, Long result, int size)
{
	(void)src; (void)dest; (void)result; (void)size;
	unexpected_error = 1;
}

Long mem_get(Long address, char size)
{
	(void)address; (void)size;
	unexpected_error = 1;
	return 0;
}

void mem_set(Long address, Long data, char size)
{
	(void)address; (void)data; (void)size;
	unexpected_error = 1;
}

void err68a(char *message, char *file, int line)
{
	(void)message; (void)file; (void)line;
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

static void test_eori_to_sr(void)
{
	char opcode[2] = {(char)0x0a, (char)0x7c};

	sr = (short)0xa55a;
	immediate = 0x0f0f;
	pc = 0;
	unexpected_error = 0;
	if (line0(opcode) != FALSE || unexpected_error || pc != 4)
		failures++;
	expect_u32("EORI to SR", 0xaa55, (ULong)(UShort)sr);

	sr = 0x001f;
	pc = 0;
	unexpected_error = 0;
	if (line0(opcode) != TRUE || !unexpected_error || pc != 2) {
		fprintf(stderr, "unprivileged EORI to SR was not rejected\n");
		failures++;
	}
}

static void test_bit_31(void)
{
	char btst[2] = {(char)0x01, (char)0x01}; /* BTST D0,D1 */
	char bchg[2] = {(char)0x01, (char)0x41}; /* BCHG D0,D1 */

	memset(rd, 0, sizeof(rd));
	rd[0] = 31;
	rd[1] = (Long)UINT32_C(0x80000000);
	sr = 0x0004;
	pc = 0;
	unexpected_error = 0;
	if (line0(btst) != FALSE || unexpected_error || pc != 2)
		failures++;
	expect_u32("BTST bit 31 clears Z", 0, (ULong)(UShort)sr & 4u);

	pc = 0;
	unexpected_error = 0;
	if (line0(bchg) != FALSE || unexpected_error || pc != 2)
		failures++;
	expect_u32("BCHG bit 31", 0, (ULong)rd[1]);
	expect_u32("BCHG old one clears Z", 0, (ULong)(UShort)sr & 4u);
}

int main(void)
{
	test_eori_to_sr();
	test_bit_31();
	return failures == 0 ? 0 : 1;
}
