#include <stdio.h>
#include <string.h>

#include "run68.h"

static int failures;

static void expect_string(const char *name, const char *expected,
                          const char *actual)
{
	if (strcmp(expected, actual) != 0) {
		fprintf(stderr, "%s: expected \"%s\", got \"%s\"\n",
		        name, expected, actual);
		failures++;
	}
}

int main(void)
{
	char text[40];
	struct tm local = {0};
	time_t timestamp;
	time_t decoded;
	ULong packed;

	expect_string("negative decimal", "-2147483648",
	              _ltoa((Long)INT32_MIN, text, 10));
	expect_string("unsigned hexadecimal", "ffffffff",
	              _ltoa((Long)-1, text, 16));

	strcpy(text, "AbC-z9");
	expect_string("lowercase conversion", "abc-z9", _strlwr(text));

	local.tm_year = 2024 - 1900;
	local.tm_mon = 1;
	local.tm_mday = 29;
	local.tm_hour = 23;
	local.tm_min = 58;
	local.tm_sec = 56;
	local.tm_isdst = -1;
	timestamp = mktime(&local);
	if (timestamp == (time_t)-1 ||
	    run68_pack_dos_datetime(timestamp, &packed) == FALSE) {
		fprintf(stderr, "could not pack valid DOS date/time\n");
		failures++;
	} else {
		if (packed != 0x585dbf5cu) {
			fprintf(stderr, "DOS date/time: expected 585dbf5c, got %08x\n",
			        packed);
			failures++;
		}
		if (run68_unpack_dos_datetime(packed, &decoded) == FALSE ||
		    decoded != timestamp) {
			fprintf(stderr, "DOS date/time did not round-trip\n");
			failures++;
		}
	}

	if (run68_unpack_dos_datetime(0x00210000u, &decoded) == FALSE) {
		fprintf(stderr, "DOS epoch date was rejected\n");
		failures++;
	}
	if (run68_unpack_dos_datetime(0x00200000u, &decoded) != FALSE) {
		fprintf(stderr, "invalid zero day was accepted\n");
		failures++;
	}
	if (run68_unpack_dos_datetime(0x005e0000u, &decoded) != FALSE) {
		fprintf(stderr, "invalid February date was accepted\n");
		failures++;
	}

	return failures == 0 ? 0 : 1;
}
