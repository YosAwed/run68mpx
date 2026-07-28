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
static Long index_value;
static Long accessed_address;
static Long written_data;
static char accessed_size;

void err68a(char *message, char *file, int line)
{
	(void)message;
	(void)file;
	(void)line;
	unexpected_error = 1;
}

Long imi_get(char size)
{
	pc += size == S_LONG ? 4 : 2;
	return immediate_value;
}

Long idx_get(void)
{
	pc += 2;
	return index_value;
}

Long mem_get(Long address, char size)
{
	accessed_address = address;
	accessed_size = size;
	return (Long)UINT32_C(0x12345678);
}

void mem_set(Long address, Long data, char size)
{
	accessed_address = address;
	written_data = data;
	accessed_size = size;
}

void inc_ra(char reg, char size)
{
	ra[(unsigned char)reg] = (Long)((ULong)ra[(unsigned char)reg] +
	    ((reg == 7 && size == S_BYTE) ? 2u :
	     size == S_BYTE ? 1u : size == S_WORD ? 2u : 4u));
}

void dec_ra(char reg, char size)
{
	ra[(unsigned char)reg] = (Long)((ULong)ra[(unsigned char)reg] -
	    ((reg == 7 && size == S_BYTE) ? 2u :
	     size == S_BYTE ? 1u : size == S_WORD ? 2u : 4u));
}

static void expect_u32(const char *name, ULong expected, ULong actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %08x, got %08x\n",
		        name, expected, actual);
		failures++;
	}
}

static void test_get_ea_wraparound(void)
{
	Long address = 0;

	ra[0] = INT32_MAX;
	immediate_value = 1;
	pc = 0;
	expect_u32("get_ea AID status", FALSE,
	           (ULong)get_ea(0, EA_Control, EA_AID, 0, &address));
	expect_u32("get_ea AID wraparound", UINT32_C(0x80000000),
	           (ULong)address);

	index_value = 1;
	pc = 0;
	get_ea(0, EA_Control, EA_AIX, 0, &address);
	expect_u32("get_ea AIX wraparound", UINT32_C(0x80000000),
	           (ULong)address);

	immediate_value = 1;
	pc = 0;
	get_ea(INT32_MAX, EA_Control, EA_PC, 2, &address);
	expect_u32("get_ea PC displacement wraparound", UINT32_C(0x80000000),
	           (ULong)address);

	index_value = 1;
	pc = 0;
	get_ea(INT32_MAX, EA_Control, EA_PCX, 3, &address);
	expect_u32("get_ea PC index wraparound", UINT32_C(0x80000000),
	           (ULong)address);
}

static void test_data_access_wraparound(void)
{
	Long data = 0;

	ra[0] = INT32_MAX;
	immediate_value = 1;
	pc = 0;
	get_data_at_ea(EA_Data, EA_AID, 0, S_WORD, &data);
	expect_u32("read AID address wraparound", UINT32_C(0x80000000),
	           (ULong)accessed_address);
	expect_u32("read data", UINT32_C(0x12345678), (ULong)data);
	expect_u32("read size", S_WORD, (ULong)accessed_size);

	index_value = 1;
	pc = 0;
	set_data_at_ea(EA_VariableMemory, EA_AIX, 0, S_LONG,
	               (Long)UINT32_C(0xa55a3cc3));
	expect_u32("write AIX address wraparound", UINT32_C(0x80000000),
	           (ULong)accessed_address);
	expect_u32("write data", UINT32_C(0xa55a3cc3), (ULong)written_data);
}

static void test_absolute_short_sign_extension(void)
{
	Long address = 0;

	immediate_value = 0x8000;
	pc = 0;
	get_ea(0, EA_Control, 7, 0, &address);
	expect_u32("absolute short sign extension", UINT32_C(0xffff8000),
	           (ULong)address);
}

static void test_stack_pointer_byte_steps(void)
{
	Long data;

	ra[7] = 0x1000;
	get_data_at_ea(EA_Data, EA_AIPI, 7, S_BYTE, &data);
	expect_u32("A7 byte postincrement", 0x1002, (ULong)ra[7]);

	ra[7] = 0x1000;
	get_data_at_ea(EA_Data, EA_AIPD, 7, S_BYTE, &data);
	expect_u32("A7 byte predecrement", 0x0ffe, (ULong)ra[7]);
	expect_u32("A7 predecrement access address", 0x0ffe,
	           (ULong)accessed_address);
}

static void test_noinc_restores_pc(void)
{
	Long data;

	ra[0] = 0x1000;
	immediate_value = 6;
	pc = 0x200;
	get_data_at_ea_noinc(EA_Data, EA_AID, 0, S_BYTE, &data);
	expect_u32("noinc restores PC", 0x200, (ULong)pc);
	expect_u32("noinc still calculates EA", 0x1006,
	           (ULong)accessed_address);
}

int main(void)
{
	memset(ra, 0, sizeof(ra));
	memset(rd, 0, sizeof(rd));
	test_get_ea_wraparound();
	test_data_access_wraparound();
	test_absolute_short_sign_extension();
	test_stack_pointer_byte_steps();
	test_noinc_restores_pc();
	if (unexpected_error)
		failures++;
	return failures == 0 ? 0 : 1;
}
