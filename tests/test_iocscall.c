#include <stdio.h>
#include <string.h>

#define MAIN
#include "run68.h"

BOOL func_trace_f;
BOOL trace_f;
Long trap_pc;
jmp_buf jmp_when_abort;
unsigned short cwatchpoint;
EXEC_INSTRUCTION_INFO OP_info;
BOOL cpu_instruction_active;

static UChar memory[4096];
static int failures;
static UChar opm_address;
static UChar opm_status;
static UChar opm_register;
static UChar opm_value;
static Long opm_callback_work;
static int adpcm_start_calls;
static size_t adpcm_length;
static UShort adpcm_mode;
static int adpcm_control_mode = -1;
static int adpcm_fake_status;

static int fake_adpcm_start(void *context, const UChar *data, size_t length,
	UShort mode)
{
	(void)context;
	if (data != memory + 100)
		failures++;
	adpcm_start_calls++;
	adpcm_length = length;
	adpcm_mode = mode;
	return 0;
}

static int fake_adpcm_control(void *context, int mode)
{
	(void)context;
	adpcm_control_mode = mode;
	return 0;
}

static int fake_adpcm_status(const void *context)
{
	(void)context;
	return adpcm_fake_status;
}

static void expect_long(const char *name, Long expected, Long actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %08x, got %08x\n", name,
		        (ULong)expected, (ULong)actual);
		failures++;
	}
}

Long mem_get(Long address, char size)
{
	ULong p = (ULong)address & 0x00ffffffu;
	if (p == 0x00e90003)
		return opm_status;
	if (p == OPM_CALLBACK_WORK)
		return opm_callback_work;
	if (p >= sizeof(memory))
		return 0;
	Long value = memory[p];

	if (size >= S_WORD)
		value = (value << 8) | memory[p + 1];
	if (size == S_LONG)
		value = (value << 16) | ((Long)memory[p + 2] << 8) | memory[p + 3];
	return value;
}

void mem_set(Long address, Long value, char size)
{
	ULong p = (ULong)address & 0x00ffffffu;
	if (p == 0x00e90001 && size == S_BYTE) {
		opm_address = (UChar)value;
		return;
	}
	if (p == 0x00e90003 && size == S_BYTE) {
		opm_register = opm_address;
		opm_value = (UChar)value;
		return;
	}
	if (p == OPM_CALLBACK_WORK && size == S_LONG) {
		opm_callback_work = value;
		return;
	}
	if (p >= sizeof(memory))
		return;

	if (size == S_BYTE) {
		memory[p] = (UChar)value;
	} else if (size == S_WORD) {
		memory[p] = (UChar)((ULong)value >> 8);
		memory[p + 1] = (UChar)value;
	} else {
		memory[p] = (UChar)((ULong)value >> 24);
		memory[p + 1] = (UChar)((ULong)value >> 16);
		memory[p + 2] = (UChar)((ULong)value >> 8);
		memory[p + 3] = (UChar)value;
	}
}

void text_color(short color) { (void)color; }
Long get_locate(void) { return 0; }
void err68(char *message) { (void)message; }

static void call_iocs(UChar number)
{
	rd[0] = number;
	(void)iocs_call();
}

int main(void)
{
	struct tm value;

	prog_ptr = (char *)memory;
	mem_aloc = (Long)sizeof(memory);

	rd[1] = 0x07e8021d; /* 2024-02-29 */
	call_iocs(0x50);
	expect_long("DATEBCD", 0x04440229, rd[0]);
	rd[1] = rd[0];
	call_iocs(0x55);
	expect_long("DATEBIN", 0x47e8021d, rd[0]);
	rd[1] = 0x07e50101; /* 2021-01-01, leap counter 1, Friday */
	call_iocs(0x50);
	expect_long("DATEBCD leap counter", 0x15410101, rd[0]);

	rd[1] = 0x0017383b; /* 23:56:59 */
	call_iocs(0x52);
	expect_long("TIMEBCD", 0x10235659, rd[0]);
	rd[1] = rd[0];
	call_iocs(0x57);
	expect_long("TIMEBIN", 0x0017383b, rd[0]);

	rd[1] = 0x00010230; /* 1981-02-30 is invalid */
	call_iocs(0x50);
	expect_long("invalid DATEBCD", -1, rd[0]);

	rd[1] = 0x00123456;
	call_iocs(0x53);
	if (!run68_get_virtual_localtime(&value) || value.tm_hour != 12 ||
	    value.tm_min != 34 || value.tm_sec < 56 || value.tm_sec > 57) {
		fprintf(stderr, "TIMESET did not update the virtual clock\n");
		failures++;
	}

	(void)run68_console_ungetch('A');
	call_iocs(0x01);
	expect_long("B_KEYSNS", 0x00011e41, rd[0]);
	call_iocs(0x00);
	expect_long("B_KEYINP", 0x00001e41, rd[0]);

	strcpy((char *)memory + 20, "24/02/29");
	ra[1] = 20;
	call_iocs(0x58);
	expect_long("DATECNV", 0x47e8021d, rd[0]);
	expect_long("DATECNV address", 28, ra[1]);
	strcpy((char *)memory + 40, "23:56:59");
	ra[1] = 40;
	call_iocs(0x59);
	expect_long("TIMECNV", 0x0017383b, rd[0]);
	expect_long("TIMECNV address", 48, ra[1]);
	call_iocs(0x7f);
	if (rd[0] < 0 || rd[0] >= 8640000 || rd[1] != 0) {
		fprintf(stderr, "ONTIME returned an invalid startup duration\n");
		failures++;
	}

	iocs_set_adpcm_backend(NULL, fake_adpcm_start, fake_adpcm_control,
	                      fake_adpcm_status);
	memory[100] = 0x12;
	memory[101] = 0x34;
	ra[1] = 100;
	rd[1] = 0x0403;
	rd[2] = 2;
	call_iocs(0x60);
	expect_long("ADPCMOUT result", 0, rd[0]);
	expect_long("ADPCMOUT calls", 1, adpcm_start_calls);
	expect_long("ADPCMOUT length", 2, (Long)adpcm_length);
	expect_long("ADPCMOUT mode", 0x0403, adpcm_mode);
	adpcm_fake_status = 2;
	call_iocs(0x66);
	expect_long("ADPCMSNS", 2, rd[0]);
	rd[1] = 0;
	call_iocs(0x67);
	expect_long("ADPCMMOD stop", 0, rd[0]);
	expect_long("ADPCMMOD callback", 0, adpcm_control_mode);
	rd[1] = 3;
	call_iocs(0x67);
	expect_long("ADPCMMOD invalid", -1, rd[0]);
	iocs_set_adpcm_backend(NULL, NULL, NULL, NULL);

	rd[1] = 0x14;
	rd[2] = 0x2a;
	call_iocs(0x68);
	expect_long("OPMSET register", 0x14, opm_register);
	expect_long("OPMSET value", 0x2a, opm_value);
	opm_status = 0x83;
	call_iocs(0x69);
	expect_long("OPMSNS status", 0x83, rd[0]);

	iocs_audio_reset();
	ra[1] = 0x123456;
	call_iocs(0x6a);
	expect_long("OPMINTST install result", 0, rd[0]);
	expect_long("OPMINTST handler", 0x123456,
	            iocs_opm_interrupt_handler());
	expect_long("OPMINTST callback work", 0x123456, opm_callback_work);
	ra[1] = 0x234568;
	call_iocs(0x6a);
	expect_long("OPMINTST busy result", 0x123456, rd[0]);
	ra[1] = 0;
	call_iocs(0x6a);
	expect_long("OPMINTST disable result", 0, rd[0]);
	expect_long("OPMINTST disabled", 0, iocs_opm_interrupt_handler());

	ra[1] = 100;
	rd[1] = 0x11223344;
	call_iocs(0x88);
	expect_long("B_LPOKE address", 104, ra[1]);
	expect_long("B_LPOKE value", 0x11223344, mem_get(100, S_LONG));
	ra[1] = 100;
	call_iocs(0x82);
	expect_long("B_BPEEK value", 0x00000011, rd[0]);
	expect_long("B_BPEEK address", 101, ra[1]);

	memory[200] = 1;
	memory[201] = 2;
	memory[202] = 3;
	memory[203] = 4;
	ra[1] = 200;
	ra[2] = 300;
	rd[1] = 3;
	call_iocs(0x85);
	expect_long("B_MEMSTR source", 204, ra[1]);
	expect_long("B_MEMSTR destination", 304, ra[2]);
	expect_long("B_MEMSTR counter", -1, rd[1]);
	if (memcmp(memory + 200, memory + 300, 4) != 0) {
		fprintf(stderr, "B_MEMSTR copied incorrect bytes\n");
		failures++;
	}

	memory[400] = 5;
	memory[401] = 6;
	ra[1] = 500;
	ra[2] = 400;
	rd[1] = 1;
	call_iocs(0x89);
	if (memory[500] != 5 || memory[501] != 6 || ra[1] != 502 || ra[2] != 402) {
		fprintf(stderr, "B_MEMSET copied in the wrong direction\n");
		failures++;
	}

	memory[600] = 7;
	memory[601] = 8;
	memory[602] = 9;
	ra[1] = 600;
	ra[2] = 700;
	rd[1] = 5; /* increment both addresses */
	rd[2] = 3;
	call_iocs(0x8a);
	if (memcmp(memory + 600, memory + 700, 3) != 0 ||
	    ra[1] != 603 || ra[2] != 703 || rd[2] != 0) {
		fprintf(stderr, "DMAMOVE increment mode failed\n");
		failures++;
	}

	/* B_SUPER must switch A7 banks and return the user stack pointer. */
	sr = 0;
	ra[7] = 0x900;
	usp = 0x900;
	ssp = 0x700;
	ra[1] = 0;
	call_iocs(0x81);
	expect_long("B_SUPER return", 0x900, rd[0]);
	expect_long("B_SUPER supervisor A7", 0x700, ra[7]);
	ra[1] = 0x880;
	call_iocs(0x81);
	expect_long("B_SUPER user A7", 0x880, ra[7]);

	return failures == 0 ? 0 : 1;
}
