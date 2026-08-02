#include <stdio.h>

#define MAIN
#include "run68.h"

static int failures;

static void expect_true(const char *name, BOOL value)
{
	if (!value) {
		fprintf(stderr, "%s: expected TRUE\n", name);
		failures++;
	}
}

static void expect_false(const char *name, BOOL value)
{
	if (value) {
		fprintf(stderr, "%s: expected FALSE\n", name);
		failures++;
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

int main(void)
{
	Long kb = -1;

	expect_true("parse 1KB", run68_parse_stack_size_kb("1", &kb));
	expect_long("1KB value", 1, kb);
	expect_true("parse 4096KB",
	            run68_parse_stack_size_kb("4096", &kb));
	expect_long("4096KB value", 4096, kb);

	expect_false("reject empty", run68_parse_stack_size_kb("", &kb));
	expect_false("reject trailing junk",
	             run68_parse_stack_size_kb("64x", &kb));
	expect_false("reject 0KB", run68_parse_stack_size_kb("0", &kb));
	expect_false("reject over max",
	             run68_parse_stack_size_kb("4097", &kb));
	/*
	 * 4294967297 truncates to 1 when cast to Long (int32). The parser must
	 * reject it instead of accepting it as 1KB.
	 */
	expect_false("reject Long truncation",
	             run68_parse_stack_size_kb("4294967297", &kb));
	expect_false("reject 2^32",
	             run68_parse_stack_size_kb("4294967296", &kb));

	return failures == 0 ? 0 : 1;
}
