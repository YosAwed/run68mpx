#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "run68.h"
#include "link_l_case.h"

Long ra[8];
Long rd[9];
Long pc;
Long usp;
Long ssp;
short sr;
int trap_count;

static int failures;
static int unexpected_error;
static int testing_movem;
static int testing_control;
static int testing_status;
static int status_accepted;
static Long status_source;
static Long status_written;
static UShort movem_mask;
static unsigned movem_write_count;
static Long movem_write_address[4];
static Long movem_write_data[4];
static char movem_write_size[4];
static Long control_immediate;
static Long control_word_address;
static Long control_word_value;
static Long control_long_address;
static Long control_long_value;
static Long control_vector_address;
static Long control_vector_value;

BOOL get_data_at_ea(int accepted, int mode, int reg, int size, Long *data)
{
	if (testing_status && mode == EA_DD && size == S_WORD) {
		status_accepted = accepted;
		*data = status_source;
		return FALSE;
	}
	(void)accepted;
	(void)mode;
	(void)reg;
	(void)size;
	(void)data;
	unexpected_error = 1;
	return TRUE;
}

BOOL get_data_at_ea_noinc(int accepted, int mode, int reg, int size,
	Long *data)
{
	(void)accepted;
	if (mode != EA_DD || size != S_BYTE) {
		unexpected_error = 1;
		return TRUE;
	}
	*data = rd[reg] & 0xff;
	return FALSE;
}

BOOL set_data_at_ea(int accepted, int mode, int reg, int size, Long data)
{
	if (testing_status && mode == EA_DD && size == S_WORD) {
		status_accepted = accepted;
		status_written = data;
		return FALSE;
	}
	(void)accepted;
	if (mode != EA_DD || size != S_BYTE) {
		unexpected_error = 1;
		return TRUE;
	}
	rd[reg] = (Long)(((ULong)rd[reg] & 0xffffff00u) |
	                 ((ULong)data & 0xffu));
	return FALSE;
}

BOOL get_ea(Long save_pc, int accepted, int mode, int reg, Long *data)
{
	(void)accepted;
	(void)save_pc;
	if (testing_movem && mode == EA_AI) {
		*data = ra[reg];
		return FALSE;
	}
	unexpected_error = 1;
	return TRUE;
}

Long imi_get(char size)
{
	if (testing_movem && size == S_WORD) {
		pc += 2;
		return movem_mask;
	}
	if (testing_control) {
		pc = (Long)((ULong)pc + (size == S_LONG ? 4u : 2u));
		return control_immediate;
	}
	unexpected_error = 1;
	return 0;
}

Long mem_get(Long address, char size)
{
	if (testing_movem && size == S_LONG) {
		if ((ULong)address == UINT32_C(0x1000))
			return (Long)UINT32_C(0x11223344);
		if ((ULong)address == UINT32_C(0x1004))
			return (Long)UINT32_C(0xdeadbeef);
	}
	if (testing_movem && size == S_WORD) {
		if ((ULong)address == UINT32_C(0x1000))
			return (Long)UINT32_C(0x8001);
		if ((ULong)address == UINT32_C(0x1002))
			return (Long)UINT32_C(0x7fff);
	}
	if (testing_control) {
		if (size == S_WORD && address == control_word_address)
			return control_word_value;
		if (size == S_LONG && address == control_long_address)
			return control_long_value;
		if (size == S_LONG && address == control_vector_address)
			return control_vector_value;
	}
	unexpected_error = 1;
	return 0;
}

void mem_set(Long address, Long data, char size)
{
	if ((testing_movem || testing_control) && movem_write_count < 4) {
		movem_write_address[movem_write_count] = address;
		movem_write_data[movem_write_count] = data;
		movem_write_size[movem_write_count] = size;
		movem_write_count++;
		return;
	}
	unexpected_error = 1;
}

void general_conditions(Long result, int size)
{
	(void)result;
	(void)size;
	unexpected_error = 1;
}

void neg_conditions(Long dest, Long result, int size, BOOL zero_flag)
{
	(void)dest;
	(void)result;
	(void)size;
	(void)zero_flag;
	unexpected_error = 1;
}

Long sub_long(Long src, Long dest, int size)
{
	(void)src;
	(void)dest;
	(void)size;
	unexpected_error = 1;
	return 0;
}

int iocs_call(void)
{
	unexpected_error = 1;
	return TRUE;
}

void err68(char *message)
{
	(void)message;
	unexpected_error = 1;
}

void err68a(char *message, char *file, int line)
{
	(void)message;
	(void)file;
	(void)line;
	unexpected_error = 1;
}

static unsigned pack_bcd(unsigned value)
{
	return ((value / 10u) << 4) | (value % 10u);
}

static void test_nbcd(void)
{
	char opcode[2] = {(char)0x48, (char)0x00}; /* NBCD D0 */
	unsigned src;
	unsigned x;
	unsigned initial_z;

	for (src = 0; src < 100; src++) {
		for (x = 0; x <= 1; x++) {
			for (initial_z = 0; initial_z <= 1; initial_z++) {
				int difference = -(int)src - (int)x;
				unsigned borrow = difference < 0;
				unsigned decimal_result =
				    (unsigned)((difference % 100 + 100) % 100);
				unsigned result = pack_bcd(decimal_result);
				unsigned expected_flags = (borrow ? 0x11u : 0) |
				    (initial_z && result == 0 ? 0x04u : 0);

				rd[0] = (Long)(UINT32_C(0xa55a0000) | pack_bcd(src));
				sr = (short)(0x200au | (x ? 0x10u : 0) |
				             (initial_z ? 0x04u : 0));
				pc = 0;
				unexpected_error = 0;

				if (line4(opcode) != FALSE || unexpected_error || pc != 2 ||
				    (ULong)rd[0] != (UINT32_C(0xa55a0000) | result) ||
				    ((unsigned)(UShort)sr & 0x15u) != expected_flags) {
					fprintf(stderr,
					        "NBCD %02u X=%u Z=%u: data=%08x flags=%02x/%02x\n",
					        src, x, initial_z, (ULong)rd[0], expected_flags,
					        (unsigned)(UShort)sr & 0x15u);
					failures++;
				}
			}
		}
	}
}

static void test_movem_postincrement_base_register(void)
{
	char opcode[2] = {(char)0x4c, (char)0xd8};

	memset(ra, 0, sizeof(ra));
	memset(rd, 0, sizeof(rd));
	ra[0] = (Long)UINT32_C(0x1000);
	movem_mask = UINT16_C(0x0101); /* D0/A0 */
	testing_movem = 1;
	unexpected_error = 0;
	pc = 0;

	if (line4(opcode) != FALSE || unexpected_error || pc != 4 ||
	    (ULong)rd[0] != UINT32_C(0x11223344) ||
	    (ULong)ra[0] != UINT32_C(0x1008)) {
		fprintf(stderr,
		        "MOVEM.L (A0)+,D0/A0: D0=%08x A0=%08x pc=%d err=%d\n",
		        (ULong)rd[0], (ULong)ra[0], pc, unexpected_error);
		failures++;
	}
	testing_movem = 0;
}

static void test_movem_word_sign_extension(void)
{
	char opcode[2] = {(char)0x4c, (char)0x98};

	memset(ra, 0, sizeof(ra));
	memset(rd, 0, sizeof(rd));
	ra[0] = (Long)UINT32_C(0x1000);
	movem_mask = UINT16_C(0x0101); /* D0/A0 */
	testing_movem = 1;
	unexpected_error = 0;
	pc = 0;

	if (line4(opcode) != FALSE || unexpected_error || pc != 4 ||
	    (ULong)rd[0] != UINT32_C(0xffff8001) ||
	    (ULong)ra[0] != UINT32_C(0x1004)) {
		fprintf(stderr,
		        "MOVEM.W (A0)+,D0/A0: D0=%08x A0=%08x pc=%d err=%d\n",
		        (ULong)rd[0], (ULong)ra[0], pc, unexpected_error);
		failures++;
	}
	testing_movem = 0;
}

static void test_movem_predecrement_base_register(void)
{
	char opcode[2] = {(char)0x48, (char)0xe0};

	memset(ra, 0, sizeof(ra));
	memset(rd, 0, sizeof(rd));
	ra[0] = (Long)UINT32_C(0x1000);
	rd[0] = (Long)UINT32_C(0x11223344);
	movem_mask = UINT16_C(0x8080); /* D0/A0, reversed predecrement mask */
	movem_write_count = 0;
	testing_movem = 1;
	unexpected_error = 0;
	pc = 0;
	sr = (short)0xa51f;

	if (line4(opcode) != FALSE || unexpected_error || pc != 4 ||
	    (ULong)ra[0] != UINT32_C(0x0ff8) || movem_write_count != 2 ||
	    (ULong)movem_write_address[0] != UINT32_C(0x0ffc) ||
	    (ULong)movem_write_data[0] != UINT32_C(0x1000) ||
	    movem_write_size[0] != S_LONG ||
	    (ULong)movem_write_address[1] != UINT32_C(0x0ff8) ||
	    (ULong)movem_write_data[1] != UINT32_C(0x11223344) ||
	    movem_write_size[1] != S_LONG || (UShort)sr != UINT16_C(0xa51f)) {
		fprintf(stderr,
		        "MOVEM.L D0/A0,-(A0): A0=%08x writes=%u "
		        "[%08x]=%08x [%08x]=%08x sr=%04x err=%d\n",
		        (ULong)ra[0], movem_write_count,
		        movem_write_count > 0 ? (ULong)movem_write_address[0] : 0,
		        movem_write_count > 0 ? (ULong)movem_write_data[0] : 0,
		        movem_write_count > 1 ? (ULong)movem_write_address[1] : 0,
		        movem_write_count > 1 ? (ULong)movem_write_data[1] : 0,
		        (UShort)sr, unexpected_error);
		failures++;
	}
	testing_movem = 0;
}

static void begin_control_test(void)
{
	testing_control = 1;
	unexpected_error = 0;
	movem_write_count = 0;
	control_immediate = 0;
	control_word_address = -1;
	control_word_value = 0;
	control_long_address = -1;
	control_long_value = 0;
	control_vector_address = -1;
	control_vector_value = 0;
	pc = 0;
}

static void end_control_test(void)
{
	testing_control = 0;
}

static void test_move_usp(void)
{
	char move_to_usp[2] = {(char)0x4e, (char)0x62};
	char move_from_usp[2] = {(char)0x4e, (char)0x6b};

	begin_control_test();
	sr = 0x2000;
	ra[2] = (Long)UINT32_C(0x12345678);
	usp = 0;
	if (line4(move_to_usp) != FALSE || unexpected_error ||
	    (ULong)usp != UINT32_C(0x12345678)) {
		fprintf(stderr, "MOVE A2,USP failed: USP=%08x err=%d\n",
		        (ULong)usp, unexpected_error);
		failures++;
	}

	pc = 0;
	unexpected_error = 0;
	usp = 0;
	ra[3] = -1;
	if (line4(move_from_usp) != FALSE || unexpected_error || ra[3] != 0) {
		fprintf(stderr, "MOVE USP,A3 with zero USP failed: A3=%08x err=%d\n",
		        (ULong)ra[3], unexpected_error);
		failures++;
	}
	end_control_test();
}

static void test_status_register_moves(void)
{
	char move_from_sr[2] = {(char)0x40, (char)0xc0};
	char move_to_ccr[2] = {(char)0x44, (char)0xc1};
	char move_to_sr[2] = {(char)0x46, (char)0xc2};

	testing_status = 1;
	unexpected_error = 0;
	pc = 0;
	sr = (short)0xa51f;
	status_accepted = 0;
	status_written = 0;
	if (line4(move_from_sr) != FALSE || unexpected_error || pc != 2 ||
	    status_accepted != EA_VariableData ||
	    (UShort)status_written != UINT16_C(0xa51f)) {
		fprintf(stderr, "MOVE SR,D0 mask=%03x data=%04x err=%d\n",
		        status_accepted, (UShort)status_written, unexpected_error);
		failures++;
	}

	pc = 0;
	unexpected_error = 0;
	sr = (short)0xa500;
	status_source = 0x0015;
	status_accepted = 0;
	if (line4(move_to_ccr) != FALSE || unexpected_error || pc != 2 ||
	    status_accepted != EA_Data || (UShort)sr != UINT16_C(0xa515)) {
		fprintf(stderr, "MOVE D1,CCR mask=%03x sr=%04x err=%d\n",
		        status_accepted, (UShort)sr, unexpected_error);
		failures++;
	}

	pc = 0;
	unexpected_error = 0;
	sr = 0x2000;
	status_source = (Long)UINT32_C(0x851f);
	status_accepted = 0;
	if (line4(move_to_sr) != FALSE || unexpected_error || pc != 2 ||
	    status_accepted != EA_Data || (UShort)sr != UINT16_C(0x851f)) {
		fprintf(stderr, "MOVE D2,SR mask=%03x sr=%04x err=%d\n",
		        status_accepted, (UShort)sr, unexpected_error);
		failures++;
	}
	testing_status = 0;
}

static void test_link_unlk(void)
{
	char link_a2[2] = {(char)0x4e, (char)0x52};
	char unlk_a2[2] = {(char)0x4e, (char)0x5a};
	char link_l_a0[2] = {(char)0x48, (char)0x08};

	begin_control_test();
	control_immediate = (Long)UINT32_C(0xfff0); /* -16 */
	ra[7] = 0x1000;
	ra[2] = 0x2000;
	if (line4(link_a2) != FALSE || unexpected_error || pc != 4 ||
	    ra[7] != 0x0fec || ra[2] != 0x0ffc || movem_write_count != 1 ||
	    movem_write_address[0] != 0x0ffc || movem_write_data[0] != 0x2000 ||
	    movem_write_size[0] != S_LONG) {
		fprintf(stderr,
		        "LINK A2,#-16 failed: A7=%08x A2=%08x pc=%d writes=%u err=%d\n",
		        (ULong)ra[7], (ULong)ra[2], pc, movem_write_count,
		        unexpected_error);
		failures++;
	}

	pc = 0;
	unexpected_error = 0;
	control_long_address = 0x0f00;
	control_long_value = 0x2222;
	ra[2] = 0x0f00;
	if (line4(unlk_a2) != FALSE || unexpected_error ||
	    ra[2] != 0x2222 || ra[7] != 0x0f04) {
		fprintf(stderr, "UNLK A2 failed: A7=%08x A2=%08x err=%d\n",
		        (ULong)ra[7], (ULong)ra[2], unexpected_error);
		failures++;
	}

	pc = 0;
	unexpected_error = 0;
	movem_write_count = 0;
	/* Keep this state vector identical to the Musashi LINK.L test. */
	pc = (Long)LINK_L_INITIAL_PC;
	control_immediate = (Long)LINK_L_DISPLACEMENT;
	ra[7] = (Long)LINK_L_INITIAL_SP;
	ra[0] = (Long)LINK_L_INITIAL_A0;
	if (line4(link_l_a0) != FALSE || unexpected_error ||
	    (ULong)pc != LINK_L_EXPECTED_PC ||
	    (ULong)ra[0] != LINK_L_EXPECTED_FRAME ||
	    (ULong)ra[7] != LINK_L_EXPECTED_SP || movem_write_count != 1 ||
	    (ULong)movem_write_address[0] != LINK_L_EXPECTED_FRAME ||
	    (ULong)movem_write_data[0] != LINK_L_INITIAL_A0 ||
	    movem_write_size[0] != S_LONG) {
		fprintf(stderr,
		        "LINK.L A0,#-0x10000 failed: A7=%08x A0=%08x pc=%08x "
		        "writes=%u err=%d\n",
		        (ULong)ra[7], (ULong)ra[0], (ULong)pc, movem_write_count,
		        unexpected_error);
		failures++;
	}
	end_control_test();
}

static void test_return_instructions(void)
{
	char rte[2] = {(char)0x4e, (char)0x73};
	char rts[2] = {(char)0x4e, (char)0x75};
	char rtr[2] = {(char)0x4e, (char)0x77};

	begin_control_test();
	ra[7] = 0x1000;
	usp = 0x1800;
	ssp = 0x1000;
	sr = 0x2000;
	control_word_address = 0x1000;
	control_word_value = 0x001f;
	control_long_address = 0x1002;
	control_long_value = (Long)UINT32_C(0x12345678);
	trap_count = 0;
	if (line4(rte) != FALSE || unexpected_error ||
	    (UShort)sr != 0x001f || (ULong)pc != UINT32_C(0x12345678) ||
	    ra[7] != 0x1800 || ssp != 0x1006 || trap_count != RAS_INTERVAL) {
		fprintf(stderr,
		        "RTE failed: SR=%04x PC=%08x A7=%08x trap=%d err=%d\n",
		        (UShort)sr, (ULong)pc, (ULong)ra[7], trap_count,
		        unexpected_error);
		failures++;
	}

	pc = 0;
	unexpected_error = 0;
	ra[7] = 0x1000;
	control_long_address = 0x1000;
	control_long_value = (Long)UINT32_C(0x89abcdef);
	if (line4(rts) != FALSE || unexpected_error ||
	    (ULong)pc != UINT32_C(0x89abcdef) || ra[7] != 0x1004) {
		fprintf(stderr, "RTS failed: PC=%08x A7=%08x err=%d\n",
		        (ULong)pc, (ULong)ra[7], unexpected_error);
		failures++;
	}

	pc = 0;
	unexpected_error = 0;
	ra[7] = 0x1000;
	sr = (short)0xa500;
	control_word_address = 0x1000;
	control_word_value = 0x0015;
	control_long_address = 0x1002;
	control_long_value = (Long)UINT32_C(0x13579bdf);
	if (line4(rtr) != FALSE || unexpected_error ||
	    (UShort)sr != 0xa515 || (ULong)pc != UINT32_C(0x13579bdf) ||
	    ra[7] != 0x1006) {
		fprintf(stderr, "RTR failed: SR=%04x PC=%08x A7=%08x err=%d\n",
		        (UShort)sr, (ULong)pc, (ULong)ra[7], unexpected_error);
		failures++;
	}
	end_control_test();
}

static void test_traps(void)
{
	char trap9[2] = {(char)0x4e, (char)0x49};
	char trapv[2] = {(char)0x4e, (char)0x76};

	begin_control_test();
	pc = 0x100;
	ra[7] = 0x1000;
	ssp = 0x2000;
	sr = (short)0x8015;
	control_vector_address = 0x00a4; /* vector 32 + 9 */
	control_vector_value = (Long)UINT32_C(0x123400);
	if (line4(trap9) != FALSE || unexpected_error ||
	    (ULong)pc != UINT32_C(0x123400) || usp != 0x1000 ||
	    ra[7] != 0x1ffa || ssp != 0x1ffa ||
	    (UShort)sr != 0x2015 || movem_write_count != 2 ||
	    movem_write_address[0] != 0x1ffc || movem_write_data[0] != 0x102 ||
	    movem_write_size[0] != S_LONG ||
	    movem_write_address[1] != 0x1ffa ||
	    (UShort)movem_write_data[1] != 0x8015 ||
	    movem_write_size[1] != S_WORD) {
		fprintf(stderr,
		        "TRAP #9 failed: SR=%04x PC=%08x A7=%08x writes=%u err=%d\n",
		        (UShort)sr, (ULong)pc, (ULong)ra[7], movem_write_count,
		        unexpected_error);
		failures++;
	}

	pc = 0x200;
	ra[7] = 0x1000;
	ssp = 0x2000;
	sr = 0;
	movem_write_count = 0;
	unexpected_error = 0;
	if (line4(trapv) != FALSE || unexpected_error || pc != 0x202 ||
	    ra[7] != 0x1000 || movem_write_count != 0) {
		fprintf(stderr, "TRAPV clear failed: PC=%08x A7=%08x err=%d\n",
		        (ULong)pc, (ULong)ra[7], unexpected_error);
		failures++;
	}

	pc = 0x200;
	ra[7] = 0x1000;
	ssp = 0x2000;
	sr = 0x0002;
	movem_write_count = 0;
	control_vector_address = 0x001c; /* vector 7 */
	control_vector_value = (Long)UINT32_C(0xabcdef);
	unexpected_error = 0;
	if (line4(trapv) != FALSE || unexpected_error ||
	    (ULong)pc != UINT32_C(0xabcdef) || usp != 0x1000 ||
	    ra[7] != 0x1ffa || ssp != 0x1ffa ||
	    (UShort)sr != 0x2002 || movem_write_count != 2) {
		fprintf(stderr, "TRAPV set failed: SR=%04x PC=%08x A7=%08x err=%d\n",
		        (UShort)sr, (ULong)pc, (ULong)ra[7], unexpected_error);
		failures++;
	}
	end_control_test();
}

int main(void)
{
	memset(ra, 0, sizeof(ra));
	test_nbcd();
	test_movem_postincrement_base_register();
	test_movem_word_sign_extension();
	test_movem_predecrement_base_register();
	test_move_usp();
	test_status_register_moves();
	test_link_unlk();
	test_return_instructions();
	test_traps();
	return failures == 0 ? 0 : 1;
}
