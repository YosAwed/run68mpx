#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "run68.h"

FILEINFO finfo[FILE_MAX];
INI_INFO ini_info;
char size_char[3] = {'b', 'w', 'l'};
Long ra[8];
Long rd[9];
Long usp;
Long ssp;
Long pc;
short sr;
char *prog_ptr;
int trap_count;
Long superjsr_ret;
Long psp[NEST_MAX];
Long nest_pc[NEST_MAX];
Long nest_sp[NEST_MAX];
char nest_cnt;
Long mem_aloc;
jmp_buf jmp_when_abort;
EXEC_INSTRUCTION_INFO OP_info;
BOOL cpu_instruction_active;

static int failures;

void err68(char *message)
{
	(void)message;
}

static void expect_u32(const char *name, ULong expected, ULong actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %08x, got %08x\n",
		        name, expected, actual);
		failures++;
	}
}

static void expect_abort(const char *name, Long address, char size, int write)
{
	if (setjmp(jmp_when_abort) == 0) {
		if (write)
			mem_set(address, 0x12345678, size);
		else
			(void)mem_get(address, size);
		fprintf(stderr, "%s: expected address error\n", name);
		failures++;
	}
}

static void test_operand_address_error_exception(void)
{
	int result;

	sr = 0x2000;
	mem_set(3 * 4, 0x00018000, S_LONG);
	mem_set(ENV_TOP, 0x32100000, S_LONG);
	memset(&OP_info, 0, sizeof(OP_info));
	OP_info.pc = ENV_TOP;
	ra[7] = 0x28000;
	usp = 0;
	ssp = 0x30000;
	sr = 0x0015;
	cpu_instruction_active = TRUE;
	result = setjmp(jmp_when_abort);
	if (result == 0) {
		(void)mem_get(ENV_TOP + 1, S_WORD);
		fprintf(stderr, "operand address error did not interrupt instruction\n");
		failures++;
		return;
	}

	expect_u32("address exception jump code", RUN68_ABORT_CPU_EXCEPTION,
	           (ULong)result);
	expect_u32("address exception vector", 0x00018000, (ULong)pc);
	expect_u32("address exception USP", 0x28000, (ULong)usp);
	expect_u32("address exception SSP", 0x2fff2, (ULong)ssp);
	expect_u32("address exception stacked SR", 0x0015,
	           (ULong)mem_get(0x2fff2, S_WORD));
	expect_u32("address exception stacked PC", ENV_TOP,
	           (ULong)mem_get(0x2fff4, S_LONG));
	expect_u32("address exception IR", 0x3210,
	           (ULong)mem_get(0x2fff8, S_WORD));
	expect_u32("address exception address", ENV_TOP + 1,
	           (ULong)mem_get(0x2fffa, S_LONG));
	expect_u32("address exception SSW", 0x0019,
	           (ULong)mem_get(0x2fffe, S_WORD));
}

int main(void)
{
	mem_aloc = 0x40000;
	prog_ptr = calloc(1, (size_t)mem_aloc);
	if (prog_ptr == NULL)
		return 2;
	memset(finfo, 0, sizeof(finfo));
	memset(&ini_info, 0, sizeof(ini_info));
	sr = 0x2000;

	mem_set(ENV_TOP, 0x12345678, S_LONG);
	expect_u32("big-endian long", 0x12345678,
	           (ULong)mem_get(ENV_TOP, S_LONG));
	expect_u32("big-endian byte 0", 0x12,
	           (ULong)mem_get(ENV_TOP, S_BYTE));
	expect_u32("big-endian byte 3", 0x78,
	           (ULong)mem_get(ENV_TOP + 3, S_BYTE));

	mem_set((Long)(0x01000000u | ENV_TOP), 0xa1b2c3d4, S_LONG);
	expect_u32("24-bit address wrapping", 0xa1b2c3d4,
	           (ULong)mem_get(ENV_TOP, S_LONG));

	mem_set(mem_aloc - 1, 0x5a, S_BYTE);
	expect_u32("last byte", 0x5a,
	           (ULong)mem_get(mem_aloc - 1, S_BYTE));
	expect_abort("long crossing allocation", mem_aloc - 2, S_LONG, 0);
	expect_abort("word at odd address", ENV_TOP + 1, S_WORD, 0);
	expect_abort("long write at odd address", ENV_TOP + 1, S_LONG, 1);
	test_operand_address_error_exception();

	free(prog_ptr);
	return failures == 0 ? 0 : 1;
}
