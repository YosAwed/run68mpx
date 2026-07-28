#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "run68.h"

Long ra[8];
Long rd[9];
Long pc;
short sr;
char *prog_ptr;
Long mem_aloc = 0x01000000;

static int failures;
static int unexpected_error;

Long mem_get(Long address, char size)
{
	if (address == 0x2c && size == S_LONG)
		return HUMAN_WORK;
	unexpected_error = 1;
	return 0;
}

void mem_set(Long address, Long data, char size)
{
	(void)address;
	(void)data;
	(void)size;
	unexpected_error = 1;
}

int dos_call(UChar code)
{
	(void)code;
	unexpected_error = 1;
	return TRUE;
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

static void execute(UChar code)
{
	char opcode[2] = {(char)0xfe, (char)code};

	pc = 0;
	unexpected_error = 0;
	if (linef(opcode) != FALSE || unexpected_error || pc != 2) {
		fprintf(stderr, "FE%02x failed (pc=%08x, error=%d)\n",
		        code, (ULong)pc, unexpected_error);
		failures++;
	}
}

static void test_signed_arithmetic(void)
{
	memset(rd, 0, sizeof(rd));
	sr = 0;
	rd[0] = INT32_MAX;
	rd[1] = 2;
	execute(0x00);
	expect_u32("_LMUL wraps to 32 bits", 0xfffffffeu, (ULong)rd[0]);

	rd[0] = INT32_MIN;
	rd[1] = -1;
	execute(0x01);
	expect_u32("_LDIV INT_MIN/-1", 0x80000000u, (ULong)rd[0]);
	expect_u32("_LDIV clears carry", 0, (ULong)(UShort)sr & 1u);

	rd[0] = INT32_MIN;
	rd[1] = -1;
	execute(0x02);
	expect_u32("_LMOD INT_MIN/-1", 0, (ULong)rd[0]);

	rd[0] = 123;
	rd[1] = 0;
	execute(0x01);
	expect_u32("_LDIV by zero result", 0, (ULong)rd[0]);
	expect_u32("_LDIV by zero carry", 1, (ULong)(UShort)sr & 1u);
}

static void test_full_width_imul(void)
{
	memset(rd, 0, sizeof(rd));
	rd[0] = INT32_MIN;
	rd[1] = 2;
	execute(0x08);
	expect_u32("_IMUL high word", 0xffffffffu, (ULong)rd[0]);
	expect_u32("_IMUL low word", 0, (ULong)rd[1]);

	rd[0] = INT32_MAX;
	rd[1] = INT32_MAX;
	execute(0x08);
	expect_u32("_IMUL positive high word", 0x3fffffffu, (ULong)rd[0]);
	expect_u32("_IMUL positive low word", 0x00000001u, (ULong)rd[1]);
}

static void test_float_packing(void)
{
	memset(rd, 0, sizeof(rd));
	rd[0] = 1;
	execute(0x1c);
	expect_u32("_LTOF 1", 0x3f800000u, (ULong)rd[0]);

	rd[0] = (Long)UINT32_C(0x3fc00000);
	rd[1] = (Long)UINT32_C(0x40000000);
	execute(0x5d);
	expect_u32("_FMUL 1.5*2", 0x40400000u, (ULong)rd[0]);

	rd[0] = (Long)UINT32_C(0x40400000);
	rd[1] = (Long)UINT32_C(0x40000000);
	execute(0x5e);
	expect_u32("_FDIV 3/2", 0x3fc00000u, (ULong)rd[0]);
}

static void test_supervisor_state_is_restored(void)
{
	memset(rd, 0, sizeof(rd));
	sr = 0x0015;
	rd[0] = 3;
	rd[1] = 4;
	execute(0x00);
	expect_u32("FEFUNC restores user state", 0, (ULong)(UShort)sr & 0x2000u);
	expect_u32("FEFUNC preserves CCR", 0x15, (ULong)(UShort)sr & 0x1fu);
}

int main(void)
{
	test_signed_arithmetic();
	test_full_width_imul();
	test_float_packing();
	test_supervisor_state_is_restored();
	return failures == 0 ? 0 : 1;
}
