#include <stdio.h>

#define MAIN
#include "run68.h"

static int failures;

static void expect_long(const char *name, Long expected, Long actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %ld, got %ld\n",
		        name, (long)expected, (long)actual);
		failures++;
	}
}

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

int main(void)
{
	expect_long("default stack", RUN68_DEFAULT_STACK_SIZE, stack_size);
	expect_long("default prog top",
	            STACK_TOP + RUN68_DEFAULT_STACK_SIZE + PSP_SIZE, PROG_TOP);
	expect_true("default fits 1MB",
	            run68_stack_fits_memory(0x100000));

	expect_true("set 128KB", run68_set_stack_size_kb(128));
	expect_long("128KB stack bytes", 128 * 1024, stack_size);
	expect_long("128KB prog top",
	            STACK_TOP + 128 * 1024 + PSP_SIZE, PROG_TOP);

	expect_false("reject 0KB", run68_set_stack_size_kb(0));
	expect_false("reject oversized KB",
	             run68_set_stack_size_kb(RUN68_MAX_STACK_KB + 1));
	expect_long("stack unchanged after reject", 128 * 1024, stack_size);

	expect_true("set 4KB", run68_set_stack_size_kb(4));
	expect_false("4KB does not fit tiny memory",
	             run68_stack_fits_memory(PROG_TOP));
	expect_true("4KB fits memory+1",
	            run68_stack_fits_memory(PROG_TOP + 1));

	return failures == 0 ? 0 : 1;
}
