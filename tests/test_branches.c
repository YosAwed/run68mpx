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
static Long immediate_value;
static Long stored_address;
static Long stored_data;
static char stored_size;

void err68a(char *message, char *file, int line)
{
	(void)message;
	(void)file;
	(void)line;
	unexpected_error = 1;
}

Long imi_get(char size)
{
	pc = (Long)((ULong)pc + (size == S_LONG ? 4u : 2u));
	return immediate_value;
}

void mem_set(Long address, Long data, char size)
{
	stored_address = address;
	stored_data = data;
	stored_size = size;
}

BOOL get_data_at_ea_noinc(int accepted, int mode, int reg, int size,
	Long *data)
{
	(void)accepted;
	(void)mode;
	(void)reg;
	(void)size;
	(void)data;
	unexpected_error = 1;
	return TRUE;
}

BOOL set_data_at_ea(int accepted, int mode, int reg, int size, Long data)
{
	(void)accepted;
	if (mode != EA_DD || size != S_BYTE) {
		unexpected_error = 1;
		return TRUE;
	}
	rd[reg] = (Long)(((ULong)rd[reg] & 0xffffff00u) |
	                 ((ULong)data & 0xffu));
	return FALSE;
}

static void expect_u32(const char *name, ULong expected, ULong actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %08x, got %08x\n",
		        name, expected, actual);
		failures++;
	}
}

static void run_line6(UChar code1, UChar code2, Long initial_pc)
{
	char opcode[2] = {(char)code1, (char)code2};

	pc = initial_pc;
	unexpected_error = 0;
	if (line6(opcode) != FALSE || unexpected_error) {
		fprintf(stderr, "line6 opcode %02x%02x failed\n", code1, code2);
		failures++;
	}
}

static void test_bra(void)
{
	sr = 0;
	run_line6(0x60, 0x7f, 0);
	expect_u32("BRA.B positive", 129, (ULong)pc);

	run_line6(0x60, 0x80, 0);
	expect_u32("BRA.B negative", UINT32_C(0xffffff82), (ULong)pc);

	immediate_value = (Long)UINT32_C(0x8000);
	run_line6(0x60, 0x00, 0);
	expect_u32("BRA.W negative", UINT32_C(0xffff8002), (ULong)pc);

	run_line6(0x60, 0x01, INT32_MAX - 1);
	expect_u32("BRA.B PC wraparound", UINT32_C(0x80000001), (ULong)pc);
}

static void test_conditional_branch(void)
{
	sr = 0x04; /* Z: BNE false */
	immediate_value = 6;
	run_line6(0x66, 0x00, 0x100);
	expect_u32("BNE.W not taken skips extension", 0x104, (ULong)pc);

	sr = 0;
	immediate_value = 6;
	run_line6(0x66, 0x00, 0x100);
	expect_u32("BNE.W taken", 0x108, (ULong)pc);
}

static void test_bsr(void)
{
	memset(ra, 0, sizeof(ra));
	ra[7] = 0x1000;
	run_line6(0x61, 0xfe, 0x100);
	expect_u32("BSR.B target", 0x100, (ULong)pc);
	expect_u32("BSR.B stack pointer", 0x0ffc, (ULong)ra[7]);
	expect_u32("BSR.B return address", 0x102, (ULong)stored_data);
	expect_u32("BSR.B stack address", 0x0ffc, (ULong)stored_address);
	expect_u32("BSR.B stack size", S_LONG, (ULong)stored_size);

	ra[7] = 0x1000;
	immediate_value = 6;
	run_line6(0x61, 0x00, 0x100);
	expect_u32("BSR.W target", 0x108, (ULong)pc);
	expect_u32("BSR.W return address", 0x104, (ULong)stored_data);
}

static void run_line5(UChar code1, UChar code2, Long initial_pc)
{
	char opcode[2] = {(char)code1, (char)code2};

	pc = initial_pc;
	unexpected_error = 0;
	if (line5(opcode) != FALSE || unexpected_error) {
		fprintf(stderr, "line5 opcode %02x%02x failed\n", code1, code2);
		failures++;
	}
}

static void test_dbcc(void)
{
	memset(rd, 0, sizeof(rd));
	sr = 0;
	immediate_value = (Long)UINT32_C(0xfffe);
	rd[0] = (Long)UINT32_C(0xa55a0001);
	run_line5(0x51, 0xc8, 0x100); /* DBF D0 */
	expect_u32("DBF branch target", 0x100, (ULong)pc);
	expect_u32("DBF preserves upper word", UINT32_C(0xa55a0000),
	           (ULong)rd[0]);

	rd[0] = (Long)UINT32_C(0xa55a0000);
	run_line5(0x51, 0xc8, 0x100);
	expect_u32("DBF exhausted fallthrough", 0x104, (ULong)pc);
	expect_u32("DBF terminal value", UINT32_C(0xa55affff), (ULong)rd[0]);

	rd[0] = (Long)UINT32_C(0xa55a1234);
	run_line5(0x50, 0xc8, 0x100); /* DBT D0 */
	expect_u32("DBT does not decrement", UINT32_C(0xa55a1234),
	           (ULong)rd[0]);
	expect_u32("DBT fallthrough", 0x104, (ULong)pc);

	rd[0] = 1;
	run_line5(0x51, 0xc8, INT32_MAX - 1);
	expect_u32("DBF PC boundary", UINT32_C(0x7ffffffe), (ULong)pc);
}

static void test_scc(void)
{
	short initial_sr;

	rd[0] = (Long)UINT32_C(0xa55a3c55);
	sr = (short)0xa504; /* Z set */
	initial_sr = sr;
	run_line5(0x57, 0xc0, 0); /* SEQ D0 */
	expect_u32("SEQ true", UINT32_C(0xa55a3cff), (ULong)rd[0]);
	expect_u32("Scc preserves SR", (UShort)initial_sr, (UShort)sr);

	rd[0] = (Long)UINT32_C(0xa55a3c55);
	sr = (short)0xa500;
	initial_sr = sr;
	run_line5(0x57, 0xc0, 0);
	expect_u32("SEQ false", UINT32_C(0xa55a3c00), (ULong)rd[0]);
	expect_u32("Scc false preserves SR", (UShort)initial_sr, (UShort)sr);
}

int main(void)
{
	test_bra();
	test_conditional_branch();
	test_bsr();
	test_dbcc();
	test_scc();
	return failures == 0 ? 0 : 1;
}
