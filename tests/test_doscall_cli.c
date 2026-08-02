#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIN
#include "run68.h"

static UChar memory[ENV_TOP + ENV_SIZE + 64];
static int failures;

#define CUSTOM_ENV 0x1000
#define FATCHK_PATH 0x4000
#define FATCHK_BUFFER 0x5000

Long mem_get(Long address, char size)
{
	size_t width = size == S_LONG ? 4u : size == S_WORD ? 2u : 1u;
	const UChar *p;
	Long value;

	if (address < 0 || (ULong)address > sizeof(memory) ||
	    width > sizeof(memory) - (ULong)address) {
		fprintf(stderr, "out-of-range read at $%08lx (%zu bytes)\n",
		        (unsigned long)(ULong)address, width);
		failures++;
		return 0;
	}
	p = memory + (ULong)address;
	value = *p;

	if (size == S_BYTE)
		return value;
	value = (value << 8) | p[1];
	if (size == S_WORD)
		return value;
	return (value << 16) | ((Long)p[2] << 8) | p[3];
}

void mem_set(Long address, Long value, char size)
{
	size_t width = size == S_LONG ? 4u : size == S_WORD ? 2u : 1u;
	UChar *p;

	if (address < 0 || (ULong)address > sizeof(memory) ||
	    width > sizeof(memory) - (ULong)address) {
		fprintf(stderr, "out-of-range write at $%08lx (%zu bytes)\n",
		        (unsigned long)(ULong)address, width);
		failures++;
		return;
	}
	p = memory + (ULong)address;

	if (size == S_LONG) {
		p[0] = (UChar)((ULong)value >> 24);
		p[1] = (UChar)((ULong)value >> 16);
		p[2] = (UChar)((ULong)value >> 8);
		p[3] = (UChar)value;
	} else if (size == S_WORD) {
		p[0] = (UChar)((ULong)value >> 8);
		p[1] = (UChar)value;
	} else {
		p[0] = (UChar)value;
	}
}

static void expect_long(const char *name, Long expected, Long actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %ld, got %ld\n",
		        name, (long)expected, (long)actual);
		failures++;
	}
}

static void expect_string(const char *name, const char *expected,
                          const char *actual)
{
	if (strcmp(expected, actual) != 0) {
		fprintf(stderr, "%s: expected \"%s\", got \"%s\"\n",
		        name, expected, actual);
		failures++;
	}
}

static void reset_env(void)
{
	memset(memory, 0, sizeof(memory));
	prog_ptr = (char *)memory;
	mem_aloc = (Long)sizeof(memory);
	mem_set(ENV_TOP, ENV_SIZE, S_LONG);
}

static void test_setenv_getenv(void)
{
	char buf[256];

	reset_env();
	expect_long("SETENV PATH", 0,
	            Setenv_common(ENV_TOP, "PATH", "A:\\BIN"));
	expect_long("GETENV PATH", 0, Getenv_common("PATH", buf));
	expect_string("PATH value", "A:\\BIN", buf);

	expect_long("overwrite PATH", 0,
	            Setenv_common(ENV_TOP, "PATH", "B:\\TMP"));
	expect_long("GETENV PATH2", 0, Getenv_common("PATH", buf));
	expect_string("PATH value2", "B:\\TMP", buf);

	expect_long("SETENV FOO", 0,
	            Setenv_common(ENV_TOP, "FOO", "bar"));
	expect_long("GETENV FOO", 0, Getenv_common("FOO", buf));
	expect_string("FOO value", "bar", buf);

	expect_long("delete FOO with empty value", 0,
	            Setenv_common(ENV_TOP, "FOO", ""));
	expect_long("missing FOO", -10, Getenv_common("FOO", buf));
	expect_string("FOO cleared", "", buf);

	expect_long("restore FOO", 0,
	            Setenv_common(ENV_TOP, "FOO", "bar"));
	expect_long("delete FOO with NULL", 0,
	            Setenv_common(ENV_TOP, "FOO", NULL));
	expect_long("missing FOO after NULL", -10,
	            Getenv_common("FOO", buf));
}

static void test_setenv_explicit_block(void)
{
	char *entry = (char *)memory + CUSTOM_ENV + 4;

	reset_env();
	mem_set(CUSTOM_ENV, 128, S_LONG);
	expect_long("SETENV explicit block", 0,
	            Setenv_common(CUSTOM_ENV, "CHILD", "yes"));
	expect_string("explicit environment value", "CHILD=yes", entry);
	expect_long("main environment unchanged", -10,
	            Getenv_common("CHILD", entry + 64));
	expect_long("reject odd environment", -10,
	            Setenv_common(CUSTOM_ENV + 1, "BAD", "value"));
	mem_set(CUSTOM_ENV, (Long)sizeof(memory), S_LONG);
	expect_long("reject oversized environment", -10,
	            Setenv_common(CUSTOM_ENV, "BAD", "value"));
}

static void test_malformed_environment(void)
{
	char buf[256];

	reset_env();
	memset(memory + ENV_TOP + 4, 'A', 300);
	memory[ENV_TOP + 4 + 300] = '=';
	memory[ENV_TOP + 4 + 301] = 'x';
	memory[ENV_TOP + 4 + 302] = '\0';
	memory[ENV_TOP + 4 + 303] = '\0';
	expect_long("reject overlong environment name", -10,
	            Setenv_common(ENV_TOP, "PATH", "value"));
	expect_long("GETENV rejects overlong name", -10,
	            Getenv_common("PATH", buf));
}

static void make_fatchk_file(const char *path, size_t size)
{
	FILE *file = fopen(path, "wb");
	size_t i;

	if (file == NULL) {
		perror(path);
		exit(2);
	}
	for (i = 0; i < size; ++i)
		fputc((int)(i & 0xffu), file);
	fclose(file);
}

static void test_fatchk_forms(void)
{
	static const char host_file[] = "run68_fatchk_test.tmp";
	Long short_stack = (Long)sizeof(memory) - 8;
	Long long_stack = (Long)sizeof(memory) - 32;
	char qualified[sizeof(host_file) + 2];

	reset_env();
	make_fatchk_file(host_file, 2049);
	strcpy(qualified, "B:");
	strcat(qualified, host_file);
	strcpy((char *)memory + FATCHK_PATH, qualified);

	/* The legacy form ends exactly at guest memory and must not read LEN.w. */
	mem_set(short_stack, FATCHK_PATH, S_LONG);
	mem_set(short_stack + 4, FATCHK_BUFFER, S_LONG);
	expect_long("FATCHK short result", 8,
	            run68_fatchk_call(short_stack));
	expect_long("FATCHK short drive", 2,
	            mem_get(FATCHK_BUFFER, S_WORD));
	expect_long("FATCHK short first sector", 1,
	            mem_get(FATCHK_BUFFER + 2, S_WORD));
	expect_long("FATCHK short sector count", 3,
	            mem_get(FATCHK_BUFFER + 4, S_WORD));
	expect_long("FATCHK short terminator", 0,
	            mem_get(FATCHK_BUFFER + 6, S_WORD));

	mem_set(long_stack, FATCHK_PATH, S_LONG);
	mem_set(long_stack + 4,
	        (Long)(UINT32_C(0x80000000) | FATCHK_BUFFER), S_LONG);
	mem_set(long_stack + 8, 14, S_WORD);
	expect_long("FATCHK long result", 14,
	            run68_fatchk_call(long_stack));
	expect_long("FATCHK long drive", 2,
	            mem_get(FATCHK_BUFFER, S_WORD));
	expect_long("FATCHK long first sector", 1,
	            mem_get(FATCHK_BUFFER + 2, S_LONG));
	expect_long("FATCHK long sector count", 3,
	            mem_get(FATCHK_BUFFER + 6, S_LONG));
	expect_long("FATCHK long terminator", 0,
	            mem_get(FATCHK_BUFFER + 10, S_LONG));

	mem_set(long_stack + 8, 13, S_WORD);
	expect_long("FATCHK rejects short long-form buffer", -14,
	            run68_fatchk_call(long_stack));
	remove(host_file);
}

static void test_settime_word(void)
{
	struct tm value;
	/* 12:34:56 -> hours=12, minutes=34, seconds/2=28 */
	UShort packed = (UShort)((12u << 11) | (34u << 5) | 28u);

	if (!run68_set_virtual_time(((packed >> 11) & 0x1f),
	                            ((packed >> 5) & 0x3f),
	                            (packed & 0x1f) * 2)) {
		fprintf(stderr, "SETTIME helper rejected valid time\n");
		failures++;
		return;
	}
	if (!run68_get_virtual_localtime(&value) ||
	    value.tm_hour != 12 || value.tm_min != 34 || value.tm_sec != 56) {
		fprintf(stderr, "SETTIME round-trip failed: %02d:%02d:%02d\n",
		        value.tm_hour, value.tm_min, value.tm_sec);
		failures++;
	}
}

int main(void)
{
	test_setenv_getenv();
	test_setenv_explicit_block();
	test_malformed_environment();
	test_fatchk_forms();
	test_settime_word();
	return failures == 0 ? 0 : 1;
}
