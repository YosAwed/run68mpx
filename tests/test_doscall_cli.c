#include <stdio.h>
#include <string.h>

#define MAIN
#include "run68.h"

static UChar memory[ENV_TOP + ENV_SIZE + 64];
static int failures;

Long mem_get(Long address, char size)
{
	const UChar *p = memory + (ULong)address;
	Long value = *p;

	if (size == S_BYTE)
		return value;
	value = (value << 8) | p[1];
	if (size == S_WORD)
		return value;
	return (value << 16) | ((Long)p[2] << 8) | p[3];
}

void mem_set(Long address, Long value, char size)
{
	UChar *p = memory + (ULong)address;

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
	expect_long("SETENV PATH", 0, Setenv_common("PATH", "A:\\BIN"));
	expect_long("GETENV PATH", 0, Getenv_common("PATH", buf));
	expect_string("PATH value", "A:\\BIN", buf);

	expect_long("overwrite PATH", 0, Setenv_common("PATH", "B:\\TMP"));
	expect_long("GETENV PATH2", 0, Getenv_common("PATH", buf));
	expect_string("PATH value2", "B:\\TMP", buf);

	expect_long("SETENV FOO", 0, Setenv_common("FOO", "bar"));
	expect_long("GETENV FOO", 0, Getenv_common("FOO", buf));
	expect_string("FOO value", "bar", buf);

	expect_long("delete FOO with empty value", 0,
	            Setenv_common("FOO", ""));
	expect_long("missing FOO", -10, Getenv_common("FOO", buf));
	expect_string("FOO cleared", "", buf);
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
	            Setenv_common("PATH", "value"));
	expect_long("GETENV rejects overlong name", -10,
	            Getenv_common("PATH", buf));
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
	test_malformed_environment();
	test_settime_word();
	return failures == 0 ? 0 : 1;
}
