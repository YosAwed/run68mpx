#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "run68.h"

Long ra[8];
Long rd[9];
Long pc;
Long usp;
Long ssp;
short sr;
int trap_count;

static UChar memory[0x4000];
static int failures;

static ULong read_be(Long address, char size)
{
	ULong a = (ULong)address;
	ULong value = memory[a];
	int bytes = size == S_LONG ? 4 : size == S_WORD ? 2 : 1;
	int i;

	for (i = 1; i < bytes; i++)
		value = (value << 8) | memory[a + (ULong)i];
	return value;
}

Long mem_get(Long address, char size)
{
	return (Long)read_be(address, size);
}

void mem_set(Long address, Long data, char size)
{
	ULong a = (ULong)address;
	ULong value = (ULong)data;
	int bytes = size == S_LONG ? 4 : size == S_WORD ? 2 : 1;
	int i;

	for (i = bytes - 1; i >= 0; i--) {
		memory[a + (ULong)i] = (UChar)(value & 0xffu);
		value >>= 8;
	}
}

static void expect_u32(const char *name, ULong expected, ULong actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %08x, got %08x\n",
		        name, expected, actual);
		failures++;
	}
}

static void reset_cpu(void)
{
	memset(memory, 0, sizeof(memory));
	memset(ra, 0, sizeof(ra));
	pc = 0;
	usp = 0;
	ssp = 0;
	sr = 0;
	trap_count = 0;
}

static void test_user_exception(void)
{
	reset_cpu();
	ra[7] = 0x1800;
	ssp = 0x2000;
	sr = (short)0x8015;
	mem_set(5 * 4, 0x00123456, S_LONG);
	cpu_enter_exception(5, (Long)UINT32_C(0x11112222));

	expect_u32("user exception USP", 0x1800, (ULong)usp);
	expect_u32("user exception SSP", 0x1ffa, (ULong)ssp);
	expect_u32("user exception A7", 0x1ffa, (ULong)ra[7]);
	expect_u32("user exception stacked SR", 0x8015,
	           read_be(0x1ffa, S_WORD));
	expect_u32("user exception stacked PC", 0x11112222,
	           read_be(0x1ffc, S_LONG));
	expect_u32("user exception live SR", 0x2015, (UShort)sr);
	expect_u32("user exception vector PC", 0x00123456, (ULong)pc);
}

static void test_nested_supervisor_exception(void)
{
	reset_cpu();
	ra[7] = 0x2200;
	ssp = 0x2200;
	usp = 0x1800;
	sr = 0x2010;
	mem_set(4 * 4, 0x00004000, S_LONG);
	cpu_enter_exception(4, 0x1234);

	expect_u32("nested exception A7", 0x21fa, (ULong)ra[7]);
	expect_u32("nested exception USP", 0x1800, (ULong)usp);
	expect_u32("nested exception SR", 0x2010, read_be(0x21fa, S_WORD));
	expect_u32("nested exception PC", 0x1234, read_be(0x21fc, S_LONG));
}

static void test_rte_to_user(void)
{
	reset_cpu();
	ra[7] = 0x1ffa;
	ssp = 0x1ffa;
	usp = 0x1800;
	sr = 0x2000;
	mem_set(0x1ffa, 0x001f, S_WORD);
	mem_set(0x1ffc, (Long)UINT32_C(0x89abcdef), S_LONG);
	cpu_return_from_exception();

	expect_u32("RTE user SR", 0x001f, (UShort)sr);
	expect_u32("RTE user PC", 0x89abcdef, (ULong)pc);
	expect_u32("RTE user A7", 0x1800, (ULong)ra[7]);
	expect_u32("RTE saved SSP", 0x2000, (ULong)ssp);
	expect_u32("RTE trap count", RAS_INTERVAL, (ULong)trap_count);
}

static void test_rte_to_supervisor(void)
{
	reset_cpu();
	ra[7] = 0x21fa;
	ssp = 0x21fa;
	usp = 0x1800;
	sr = 0x2000;
	mem_set(0x21fa, 0x2300, S_WORD);
	mem_set(0x21fc, 0x4567, S_LONG);
	cpu_return_from_exception();

	expect_u32("RTE supervisor A7", 0x2200, (ULong)ra[7]);
	expect_u32("RTE supervisor SSP", 0x2200, (ULong)ssp);
	expect_u32("RTE supervisor SR", 0x2300, (UShort)sr);
}

static void test_sr_stack_bank_switch(void)
{
	reset_cpu();
	ra[7] = 0x1800;
	ssp = 0x2200;
	sr = 0x0015;
	cpu_set_sr(0x2015);
	expect_u32("SR enters supervisor USP", 0x1800, (ULong)usp);
	expect_u32("SR enters supervisor A7", 0x2200, (ULong)ra[7]);

	ra[7] = 0x21f0;
	cpu_set_sr(0x0015);
	expect_u32("SR leaves supervisor SSP", 0x21f0, (ULong)ssp);
	expect_u32("SR leaves supervisor A7", 0x1800, (ULong)ra[7]);
}

static void test_address_error_frame(void)
{
	reset_cpu();
	ra[7] = 0x1800;
	ssp = 0x2200;
	sr = (short)0x8015;
	mem_set(3 * 4, 0x00003456, S_LONG);
	cpu_enter_address_error(0x00123457, 0x00102030, 0x3210,
	                        FALSE, FALSE);

	expect_u32("address error A7", 0x21f2, (ULong)ra[7]);
	expect_u32("address error SSP", 0x21f2, (ULong)ssp);
	expect_u32("address error stacked SR", 0x8015,
	           read_be(0x21f2, S_WORD));
	expect_u32("address error stacked PC", 0x00102030,
	           read_be(0x21f4, S_LONG));
	expect_u32("address error IR", 0x3210,
	           read_be(0x21f8, S_WORD));
	expect_u32("address error address", 0x00123457,
	           read_be(0x21fa, S_LONG));
	expect_u32("address error SSW", 0x0019,
	           read_be(0x21fe, S_WORD));
	expect_u32("address error vector PC", 0x00003456, (ULong)pc);
}

static void test_instruction_address_error_status(void)
{
	reset_cpu();
	ra[7] = 0x2200;
	ssp = 0x2200;
	usp = 0x1800;
	sr = 0x2000;
	mem_set(3 * 4, 0x00003456, S_LONG);
	cpu_enter_address_error(0x00123457, 0x00123457, 0,
	                        FALSE, TRUE);

	/* Read + instruction access + supervisor program function code (6). */
	expect_u32("instruction address error SSW", 0x0016,
	           read_be(0x21fe, S_WORD));
}

int main(void)
{
	test_user_exception();
	test_nested_supervisor_exception();
	test_rte_to_user();
	test_rte_to_supervisor();
	test_sr_stack_bank_switch();
	test_address_error_frame();
	test_instruction_address_error_status();
	return failures == 0 ? 0 : 1;
}
