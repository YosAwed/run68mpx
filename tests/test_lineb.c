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
	if (mode == EA_DD)
		*data = rd[reg];
	else if (mode == EA_AD)
		*data = ra[reg];
	else {
		unexpected_error = 1;
		return TRUE;
	}
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
	(void)accepted; (void)mode; (void)reg; (void)size; (void)data;
	unexpected_error = 1;
	return TRUE;
}

void err68a(char *message, char *file, int line)
{
	(void)message; (void)file; (void)line;
	unexpected_error = 1;
}

static void test_cmpa_word_is_long_compare(void)
{
	char opcode[2] = {(char)0xb2, (char)0xc0}; /* CMPA.W D0,A1 */

	memset(ra, 0, sizeof(ra));
	memset(rd, 0, sizeof(rd));
	ra[1] = 0x00010000;
	rd[0] = 0;
	sr = 0x001f;
	pc = 0;
	unexpected_error = 0;
	if (lineb(opcode) != FALSE || unexpected_error || pc != 2) {
		fprintf(stderr, "CMPA.W execution failed\n");
		failures++;
	}
	if (((UShort)sr & 0x1fu) != 0x10u) {
		fprintf(stderr, "CMPA.W used word flags: SR=%04x\n", (UShort)sr);
		failures++;
	}
}

static void test_cmpa_word_sign_extension(void)
{
	char opcode[2] = {(char)0xb2, (char)0xc0}; /* CMPA.W D0,A1 */

	ra[1] = 0;
	rd[0] = 0x0000ffff;
	sr = 0x0010;
	pc = 0;
	unexpected_error = 0;
	if (lineb(opcode) != FALSE || unexpected_error || pc != 2) {
		failures++;
		return;
	}
	/* 0 - (-1) = 1; unsigned subtraction borrows from 0xffffffff. */
	if (((UShort)sr & 0x1fu) != 0x11u) {
		fprintf(stderr, "CMPA.W sign extension flags: SR=%04x\n", (UShort)sr);
		failures++;
	}
}

int main(void)
{
	test_cmpa_word_is_long_compare();
	test_cmpa_word_sign_extension();
	return failures == 0 ? 0 : 1;
}
