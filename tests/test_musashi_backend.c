#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_backend.h"

Long ra[8];
Long rd[9];
Long usp;
Long ssp;
Long pc;
short sr;
char *prog_ptr;
Long mem_aloc;
BOOL cpu_instruction_active;

static int fline_calls;
static int iocs_calls;
static int pcm8_calls;
static BOOL fline_should_finish;

int prog_exec(void)
{
	return FALSE;
}

int linef(char *opcode)
{
	++fline_calls;
	rd[0] = (UChar)opcode[1];
	pc = run68_add32(pc, 2);
	return fline_should_finish;
}

int iocs_call(void)
{
	++iocs_calls;
	rd[1] = 0x12345678;
	return FALSE;
}

int pcm8_call(void)
{
	++pcm8_calls;
	rd[2] = 0x2468ace0;
	return FALSE;
}

Long mem_get(Long address, char size)
{
	ULong a = (ULong)address & 0x00ffffffu;
	UChar *p = (UChar *)prog_ptr + a;

	if (size == S_BYTE)
		return p[0];
	if (size == S_WORD)
		return (Long)(((ULong)p[0] << 8) | p[1]);
	return (Long)(((ULong)p[0] << 24) | ((ULong)p[1] << 16) |
	              ((ULong)p[2] << 8) | p[3]);
}

void mem_set(Long address, Long value, char size)
{
	ULong a = (ULong)address & 0x00ffffffu;
	UChar *p = (UChar *)prog_ptr + a;

	if (size == S_BYTE) {
		p[0] = (UChar)value;
	} else if (size == S_WORD) {
		p[0] = (UChar)((ULong)value >> 8);
		p[1] = (UChar)value;
	} else {
		p[0] = (UChar)((ULong)value >> 24);
		p[1] = (UChar)((ULong)value >> 16);
		p[2] = (UChar)((ULong)value >> 8);
		p[3] = (UChar)value;
	}
}

static void put_word(ULong address, UShort value)
{
	mem_set((Long)address, value, S_WORD);
}

static void expect_u32(const char *label, ULong expected, ULong actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %08x, got %08x\n",
		        label, expected, actual);
		exit(1);
	}
}

int main(void)
{
	mem_aloc = 0x40000;
	prog_ptr = calloc(1, (size_t)mem_aloc);
	if (prog_ptr == NULL)
		return 1;

	pc = 0x10000;
	ra[7] = 0x30000;
	usp = ra[7];
	ssp = ra[7];
	sr = 0;
	put_word(0x10000, 0x7001); /* moveq #1,d0 */
	put_word(0x10002, 0x5280); /* addq.l #1,d0 */
	put_word(0x10004, 0xff2a); /* run68 DOSCALL HLE */
	put_word(0x10006, 0x4e4f); /* trap #15 / IOCS HLE */
	put_word(0x10008, 0x4e42); /* trap #2 / PCM8 HLE */
	put_word(0x1000a, 0x4e71); /* nop */

	if (!cpu_backend_select("musashi"))
		return 1;
	cpu_backend_prepare();
	expect_u32("initial sr", 0, (UShort)sr);

	cpu_backend_execute_one();
	if (cpu_backend_last_cycles() == 0) {
		fprintf(stderr, "Musashi did not report instruction cycles\n");
		return 1;
	}
	expect_u32("moveq d0", 1, (ULong)rd[0]);
	expect_u32("moveq pc", 0x10002, (ULong)pc);
	expect_u32("moveq user mode", 0, (UShort)sr & 0x2000u);
	cpu_backend_execute_one();
	expect_u32("addq d0", 2, (ULong)rd[0]);
	expect_u32("addq pc", 0x10004, (ULong)pc);
	cpu_backend_execute_one();
	expect_u32("fline calls", 1, (ULong)fline_calls);
	expect_u32("fline result", 0x2a, (ULong)rd[0]);
	expect_u32("fline pc", 0x10006, (ULong)pc);
	cpu_backend_execute_one();
	expect_u32("iocs calls", 1, (ULong)iocs_calls);
	expect_u32("iocs result", 0x12345678, (ULong)rd[1]);
	expect_u32("iocs pc", 0x10008, (ULong)pc);
	cpu_backend_execute_one();
	expect_u32("pcm8 calls", 1, (ULong)pcm8_calls);
	expect_u32("pcm8 result", 0x2468ace0, (ULong)rd[2]);
	expect_u32("pcm8 pc", 0x1000a, (ULong)pc);
	put_word(0x1000a, 0xff4c);
	fline_should_finish = TRUE;
	if (cpu_backend_execute_one() != TRUE) {
		fprintf(stderr, "terminating DOSCALL did not stop execution\n");
		return 1;
	}
	expect_u32("terminating fline pc", 0x1000c, (ULong)pc);
	fline_should_finish = FALSE;

	/* An odd word operand must use Musashi's MC68000 address-error frame. */
	put_word(0x10008, 0x3410); /* move.w (a0),d2 */
	put_word(0x12000, 0x4e71);
	mem_set(3 * 4, 0x12000, S_LONG);
	pc = 0x10008;
	ra[0] = 0x20001;
	ra[7] = 0x30000;
	usp = 0x30000;
	ssp = 0x31000;
	sr = 0;
	cpu_backend_execute_one();
	expect_u32("address error pc", 0x12000, (ULong)pc);
	expect_u32("address error usp", 0x30000, (ULong)usp);
	expect_u32("address error frame", 0x30ff2, (ULong)ra[7]);
	expect_u32("address error supervisor", 0x2000,
	           (UShort)sr & 0x2000u);

	/* A level 6 request must enter the MC68000 autovector and return via RTE. */
	put_word(0x10000, 0x4e71); /* nop */
	put_word(0x13000, 0x5283); /* addq.l #1,d3 */
	put_word(0x13002, 0x4e73); /* rte */
	mem_set(0x78, 0x13000, S_LONG);
	pc = 0x10000;
	rd[3] = 0;
	ra[7] = 0x30000;
	usp = 0x30000;
	ssp = 0x31000;
	sr = 0;
	cpu_backend_prepare();
	cpu_backend_set_irq(6);
	cpu_backend_execute_one();
	expect_u32("IRQ6 handler result", 1, (ULong)rd[3]);
	expect_u32("IRQ6 handler PC", 0x13002, (ULong)pc);
	cpu_backend_set_irq(0);
	cpu_backend_execute_one();
	expect_u32("IRQ6 return PC", 0x10000, (ULong)pc);
	expect_u32("IRQ6 return mode", 0, (UShort)sr & 0x2000u);

	/* MFP-backed OPM IRQs supply vector 0x43 instead of autovectoring. */
	put_word(0x14000, 0x5884); /* addq.l #4,d4 */
	put_word(0x14002, 0x4e73); /* rte */
	mem_set(0x43 * 4, 0x14000, S_LONG);
	pc = 0x10000;
	rd[4] = 0;
	ra[7] = 0x30000;
	usp = 0x30000;
	ssp = 0x31000;
	sr = 0;
	cpu_backend_prepare();
	cpu_backend_set_irq_vector(6, 0x43);
	cpu_backend_execute_one();
	expect_u32("vectored IRQ6 handler result", 4, (ULong)rd[4]);
	expect_u32("vectored IRQ6 handler PC", 0x14002, (ULong)pc);
	cpu_backend_set_irq(0);
	cpu_backend_execute_one();
	expect_u32("vectored IRQ6 return PC", 0x10000, (ULong)pc);

	/* Batched execution must stop immediately when an HLE call terminates. */
	put_word(0x10000, 0x7001); /* moveq #1,d0 */
	put_word(0x10002, 0x5280); /* addq.l #1,d0 */
	put_word(0x10004, 0xff4c); /* terminating DOSCALL HLE */
	put_word(0x10006, 0x5285); /* must not execute */
	pc = 0x10000;
	rd[0] = 0;
	rd[5] = 0;
	ra[7] = 0x30000;
	usp = 0x30000;
	ssp = 0x31000;
	sr = 0;
	fline_should_finish = TRUE;
	cpu_backend_prepare();
	if (cpu_backend_execute_cycles(256) != TRUE) {
		fprintf(stderr, "terminating batched DOSCALL did not stop execution\n");
		return 1;
	}
	expect_u32("batched fline pc", 0x10006, (ULong)pc);
	expect_u32("instruction after batched fline", 0, (ULong)rd[5]);

	free(prog_ptr);
	return 0;
}
