#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "run68.h"

Long ra[8];
Long rd[9];
Long pc;
short sr;

enum shift_operation {
	OP_AS,
	OP_LS,
	OP_ROX,
	OP_RO
};

struct shift_result {
	ULong data;
	unsigned ccr;
};

static int failures;
static int unexpected_error;
static ULong memory_value;

BOOL get_data_at_ea_noinc(int accepted, int mode, int reg, int size, Long *data)
{
	(void)accepted;
	(void)reg;
	if (mode != EA_AI || size != S_WORD) {
		unexpected_error = 1;
		return TRUE;
	}
	*data = (Long)(memory_value & 0xffffu);
	return FALSE;
}

BOOL set_data_at_ea(int accepted, int mode, int reg, int size, Long data)
{
	(void)accepted;
	(void)reg;
	if (mode != EA_AI || size != S_WORD) {
		unexpected_error = 1;
		return TRUE;
	}
	memory_value = (ULong)data & 0xffffu;
	return FALSE;
}

void general_conditions(Long result, int size)
{
	ULong mask = size == S_BYTE ? 0xffu :
	             size == S_WORD ? 0xffffu : UINT32_MAX;
	ULong sign = size == S_BYTE ? 0x80u :
	             size == S_WORD ? 0x8000u : UINT32_C(0x80000000);
	ULong value = (ULong)result & mask;

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

static ULong width_mask(int size)
{
	if (size == S_BYTE)
		return UINT32_C(0xff);
	if (size == S_WORD)
		return UINT32_C(0xffff);
	return UINT32_MAX;
}

static ULong sign_mask(int size)
{
	if (size == S_BYTE)
		return UINT32_C(0x80);
	if (size == S_WORD)
		return UINT32_C(0x8000);
	return UINT32_C(0x80000000);
}

static struct shift_result reference_shift(enum shift_operation operation,
	int left, int size, unsigned count, ULong original, unsigned initial_x)
{
	ULong mask = width_mask(size);
	ULong sign = sign_mask(size);
	ULong value = original & mask;
	unsigned x = initial_x != 0;
	unsigned c = operation == OP_ROX ? x : 0;
	unsigned v = 0;
	unsigned i;
	struct shift_result result;

	for (i = 0; i < count; i++) {
		unsigned outgoing;

		if (left) {
			ULong old_sign = value & sign;
			outgoing = (value & sign) != 0;
			value = (value << 1) & mask;
			if (operation == OP_AS && (value & sign) != old_sign)
				v = 1;
			if (operation == OP_RO || operation == OP_ROX)
				value |= operation == OP_RO ? outgoing : x;
		} else {
			outgoing = (unsigned)(value & 1u);
			if (operation == OP_AS)
				value = (value >> 1) | (value & sign);
			else {
				value >>= 1;
				if (operation == OP_RO)
					value |= outgoing ? sign : 0;
				else if (operation == OP_ROX)
					value |= x ? sign : 0;
			}
		}

		c = outgoing;
		if (operation != OP_RO)
			x = outgoing;
	}

	result.data = (original & ~mask) | value;
	result.ccr = (x ? 0x10u : 0) |
	             ((value & sign) != 0 ? 0x08u : 0) |
	             (value == 0 ? 0x04u : 0) |
	             (v ? 0x02u : 0) |
	             (c ? 0x01u : 0);
	return result;
}

static void run_case(enum shift_operation operation, int left, int size,
	int register_count, unsigned count, ULong value, unsigned initial_x)
{
	const unsigned count_register = 6;
	const unsigned destination_register = 0;
	unsigned encoded_count = register_count ? count_register : count;
	char opcode[2];
	struct shift_result expected;
	unsigned actual_count = register_count ? count % 64u : count;

	if (!register_count && count == 8)
		encoded_count = 0;
	opcode[0] = (char)(0xe0u | ((encoded_count & 7u) << 1) |
	                   (left ? 1u : 0u));
	opcode[1] = (char)(((unsigned)size << 6) |
	                   (register_count ? 0x20u : 0) |
	                   ((unsigned)operation << 3) |
	                   destination_register);

	memset(rd, 0, sizeof(rd));
	rd[destination_register] = (Long)value;
	rd[count_register] = (Long)count;
	sr = (short)(0xa500u | (initial_x ? 0x10u : 0));
	pc = 0;
	unexpected_error = 0;
	expected = reference_shift(operation, left, size, actual_count, value,
	                           initial_x);

	if (linee(opcode) != FALSE || unexpected_error || pc != 2 ||
	    (ULong)rd[destination_register] != expected.data ||
	    ((unsigned)sr & 0x1fu) != expected.ccr ||
	    ((unsigned)(UShort)sr & ~0x1fu) != (0xa500u & ~0x1fu)) {
		fprintf(stderr,
		        "shift mismatch op=%d dir=%c size=%d count=%u reg=%d "
		        "value=%08x X=%u: data %08x/%08x CCR %02x/%02x pc=%d err=%d\n",
		        operation, left ? 'L' : 'R', size, count, register_count,
		        value, initial_x, expected.data,
		        (ULong)rd[destination_register], expected.ccr,
		        (unsigned)sr & 0x1fu, pc, unexpected_error);
		failures++;
	}
}

static void test_register_shifts(void)
{
	static const ULong values[] = {
		UINT32_C(0x00000000), UINT32_C(0x00000001),
		UINT32_C(0x80808080), UINT32_C(0xffffffff),
		UINT32_C(0xa55a3cc3)
	};
	static const unsigned register_counts[] = {
		0, 1, 7, 8, 15, 16, 31, 32, 33, 63,
		UINT32_C(0xffffffff), /* -1 */
		UINT32_C(0xfffffffe), /* -2 */
		UINT32_C(0xffffffc0), /* -64 */
		UINT32_C(0xffffffbf), /* -65 */
		UINT32_C(0x80000000)  /* INT32_MIN */
	};
	int operation;
	int left;
	int size;
	unsigned x;
	size_t vi;
	size_t ci;

	for (operation = OP_AS; operation <= OP_RO; operation++) {
		for (left = 0; left <= 1; left++) {
			for (size = S_BYTE; size <= S_LONG; size++) {
				for (x = 0; x <= 1; x++) {
					for (vi = 0; vi < sizeof(values) / sizeof(values[0]); vi++) {
						run_case((enum shift_operation)operation, left, size,
						         0, 1, values[vi], x);
						run_case((enum shift_operation)operation, left, size,
						         0, 8, values[vi], x);
						for (ci = 0; ci < sizeof(register_counts) /
						                         sizeof(register_counts[0]); ci++)
							run_case((enum shift_operation)operation, left, size,
							         1, register_counts[ci], values[vi], x);
					}
				}
			}
		}
	}
}

static void run_memory_case(enum shift_operation operation, int left,
	ULong value, unsigned initial_x)
{
	char opcode[2];
	struct shift_result expected = reference_shift(
	    operation, left, S_WORD, 1, value, initial_x);

	opcode[0] = (char)(0xe0u | ((unsigned)operation << 1) |
	                   (left ? 1u : 0u));
	opcode[1] = (char)(0xc0u | ((unsigned)EA_AI << 3));
	memory_value = value & 0xffffu;
	sr = (short)(0xa500u | (initial_x ? 0x10u : 0));
	pc = 0;
	unexpected_error = 0;

	if (linee(opcode) != FALSE || unexpected_error || pc != 2 ||
	    memory_value != (expected.data & 0xffffu) ||
	    ((unsigned)(UShort)sr & 0x1fu) != expected.ccr ||
	    ((unsigned)(UShort)sr & ~0x1fu) != (0xa500u & ~0x1fu)) {
		fprintf(stderr,
		        "memory shift mismatch op=%d dir=%c value=%04x X=%u: "
		        "data %04x/%04x CCR %02x/%02x pc=%d err=%d\n",
		        operation, left ? 'L' : 'R', value & 0xffffu, initial_x,
		        expected.data & 0xffffu, memory_value, expected.ccr,
		        (unsigned)(UShort)sr & 0x1fu, pc, unexpected_error);
		failures++;
	}
}

static void test_memory_shifts(void)
{
	static const ULong values[] = {
		0, 1, 0x7fffu, 0x8000u, 0xa55au, 0xffffu
	};
	int operation;
	int left;
	unsigned x;
	size_t vi;

	for (operation = OP_AS; operation <= OP_RO; operation++)
		for (left = 0; left <= 1; left++)
			for (x = 0; x <= 1; x++)
				for (vi = 0; vi < sizeof(values) / sizeof(values[0]); vi++)
					run_memory_case((enum shift_operation)operation,
					                left, values[vi], x);
}

int main(void)
{
	test_register_shifts();
	test_memory_shifts();
	return failures == 0 ? 0 : 1;
}
