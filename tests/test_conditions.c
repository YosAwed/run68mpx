#include <stdint.h>
#include <stdio.h>

#include "run68.h"

Long ra[8];
Long rd[9];
Long pc;
short sr;

static int failures;
static int errors;

void err68a(char *message, char *file, int line)
{
	(void)message;
	(void)file;
	(void)line;
	errors++;
}

static unsigned expected_nzvc(ULong value, ULong sign, ULong mask,
                              unsigned overflow, unsigned carry)
{
	unsigned flags = 0;

	value &= mask;
	if ((value & sign) != 0)
		flags |= 0x08;
	if (value == 0)
		flags |= 0x04;
	if (overflow)
		flags |= 0x02;
	if (carry)
		flags |= 0x01;
	return flags;
}

static void report_flags(const char *operation, unsigned src, unsigned dest,
                         unsigned expected, unsigned actual)
{
	if (expected != actual) {
		fprintf(stderr,
		        "%s src=%02x dest=%02x: expected CCR %02x, got %02x\n",
		        operation, src, dest, expected, actual);
		failures++;
	}
}

static void test_all_byte_additions(void)
{
	unsigned dest;
	unsigned src;

	for (dest = 0; dest <= 0xff; dest++) {
		for (src = 0; src <= 0xff; src++) {
			ULong result = (ULong)add_long((Long)src, (Long)dest, S_BYTE);
			ULong byte_result = result & 0xffu;
			unsigned carry = dest + src > 0xffu;
			unsigned overflow =
			    ((~(dest ^ src) & (dest ^ byte_result) & 0x80u) != 0);
			unsigned expected = expected_nzvc(
			    byte_result, 0x80u, 0xffu, overflow, carry);

			sr = 0;
			add_conditions((Long)src, (Long)dest, (Long)result,
			               S_BYTE, TRUE);
			if (carry)
				expected |= 0x10;
			report_flags("ADD.B", src, dest, expected,
			             (unsigned)sr & 0x1fu);
		}
	}
}

static void test_all_byte_subtractions(void)
{
	unsigned dest;
	unsigned src;

	for (dest = 0; dest <= 0xff; dest++) {
		for (src = 0; src <= 0xff; src++) {
			ULong result = (ULong)sub_long((Long)src, (Long)dest, S_BYTE);
			ULong byte_result = result & 0xffu;
			unsigned borrow = src > dest;
			unsigned overflow =
			    (((dest ^ src) & (dest ^ byte_result) & 0x80u) != 0);
			unsigned expected = expected_nzvc(
			    byte_result, 0x80u, 0xffu, overflow, borrow);

			sr = 0;
			sub_conditions((Long)src, (Long)dest, (Long)result,
			               S_BYTE, TRUE);
			if (borrow)
				expected |= 0x10;
			report_flags("SUB.B", src, dest, expected,
			             (unsigned)sr & 0x1fu);

			sr = 0x10;
			cmp_conditions((Long)src, (Long)dest, (Long)result, S_BYTE);
			report_flags("CMP.B", src, dest, expected | 0x10,
			             (unsigned)sr & 0x1fu);
		}
	}
}

static void expect_flags(const char *name, unsigned expected)
{
	unsigned actual = (unsigned)sr & 0x1fu;
	if (expected != actual) {
		fprintf(stderr, "%s: expected CCR %02x, got %02x\n",
		        name, expected, actual);
		failures++;
	}
}

static void test_cumulative_zero(void)
{
	sr = 0;
	neg_conditions(0, 0, S_BYTE, FALSE);
	expect_flags("NEGX keeps cleared Z clear for a zero result", 0x00);

	sr = 0x04;
	neg_conditions(0, 0, S_BYTE, TRUE);
	expect_flags("NEGX keeps set Z for a zero result", 0x04);

	sr = 0x04;
	neg_conditions(0, 0, S_BYTE, CCR_Z_REF());
	expect_flags("NEGX accepts the saved Z bit value", 0x04);

	sr = 0x04;
	neg_conditions(1, -1, S_BYTE, TRUE);
	expect_flags("NEGX clears Z for a nonzero result", 0x19);

	sr = 0x04;
	sub_conditions(0, 0, 0, S_BYTE, FALSE);
	expect_flags("SUBX keeps cleared cumulative Z clear", 0x00);
}

static void test_long_wraparound(void)
{
	Long result;

	result = add_long(1, INT32_MAX, S_LONG);
	if ((ULong)result != UINT32_C(0x80000000)) {
		fprintf(stderr, "ADD.L wraparound: got %08x\n", (ULong)result);
		failures++;
	}

	result = sub_long(1, INT32_MIN, S_LONG);
	if ((ULong)result != UINT32_C(0x7fffffff)) {
		fprintf(stderr, "SUB.L wraparound: got %08x\n", (ULong)result);
		failures++;
	}

	errors = 0;
	result = add_long(1, 2, 99);
	if (result != 0 || errors != 1) {
		fprintf(stderr, "invalid arithmetic size was not rejected\n");
		failures++;
	}
}

static int reference_condition(unsigned condition, unsigned flags)
{
	int carry = (flags & 0x01u) != 0;
	int overflow = (flags & 0x02u) != 0;
	int zero = (flags & 0x04u) != 0;
	int negative = (flags & 0x08u) != 0;

	switch (condition) {
		case 0x0: return 1;
		case 0x1: return 0;
		case 0x2: return !carry && !zero;
		case 0x3: return carry || zero;
		case 0x4: return !carry;
		case 0x5: return carry;
		case 0x6: return !zero;
		case 0x7: return zero;
		case 0x8: return !overflow;
		case 0x9: return overflow;
		case 0xa: return !negative;
		case 0xb: return negative;
		case 0xc: return negative == overflow;
		case 0xd: return negative != overflow;
		case 0xe: return !zero && negative == overflow;
		case 0xf: return zero || negative != overflow;
	}
	return 0;
}

static void test_all_conditions(void)
{
	unsigned flags;
	unsigned condition;

	for (flags = 0; flags < 32; flags++) {
		for (condition = 0; condition < 16; condition++) {
			int expected = reference_condition(condition, flags);
			int actual;

			sr = (short)(0xa500u | flags);
			actual = get_cond((char)condition) != FALSE;
			if (actual != expected) {
				fprintf(stderr,
				        "condition %x CCR=%02x: expected %d, got %d\n",
				        condition, flags, expected, actual);
				failures++;
			}
		}
	}
}

int main(void)
{
	test_all_byte_additions();
	test_all_byte_subtractions();
	test_cumulative_zero();
	test_long_wraparound();
	test_all_conditions();
	return failures == 0 ? 0 : 1;
}
