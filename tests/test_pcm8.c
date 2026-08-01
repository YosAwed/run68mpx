#include <stdio.h>
#include <string.h>

#include "run68.h"

Long ra[8];
Long rd[9];
Long usp;
Long ssp;
Long pc;
short sr;
char *prog_ptr;
Long mem_aloc;
BOOL cpu_instruction_active;

static int failures;
static int start_calls;
static unsigned int last_channel;
static const UChar *last_data;
static size_t last_length;
static ULong last_mode;
static int last_control = -1;

static void expect_long(const char *name, Long expected, Long actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %ld, got %ld\n", name,
		        (long)expected, (long)actual);
		failures++;
	}
}

static int fake_start(void *context, unsigned int channel,
	const UChar *data, size_t length, ULong mode)
{
	(void)context;
	start_calls++;
	last_channel = channel;
	last_data = data;
	last_length = length;
	last_mode = mode;
	return 0;
}

static int fake_control(void *context, int mode)
{
	(void)context;
	last_control = mode;
	return 0;
}

static size_t fake_remaining(const void *context, unsigned int channel)
{
	(void)context;
	return channel == 3 ? 17 : 0;
}

int main(void)
{
	UChar memory[256];
	int backend_token;

	memset(memory, 0, sizeof(memory));
	prog_ptr = (char *)memory;
	mem_aloc = (Long)sizeof(memory);
	memory[32] = 0x77;
	pcm8_set_backend(&backend_token, fake_start, fake_control, fake_remaining);

	ra[1] = 32;
	rd[0] = 3;
	rd[1] = 0x00080401;
	rd[2] = 4;
	pcm8_call();
	expect_long("start result", 0, rd[0]);
	expect_long("start calls", 1, start_calls);
	expect_long("start channel", 3, (Long)last_channel);
	expect_long("start length", 4, (Long)last_length);
	expect_long("start mode", 0x00080401, (Long)last_mode);
	expect_long("start data", 0x77, last_data == NULL ? -1 : last_data[0]);

	rd[0] = 3;
	rd[2] = -1;
	pcm8_call();
	expect_long("remaining", 17, rd[0]);

	rd[0] = 3;
	rd[1] = 0x00ffffff;
	rd[2] = 0;
	pcm8_call();
	expect_long("stop calls", 2, start_calls);
	expect_long("stop length", 0, (Long)last_length);
	expect_long("stop null data", 1, last_data == NULL);

	ra[1] = 254;
	rd[0] = 2;
	rd[2] = 4;
	pcm8_call();
	expect_long("range rejection", -1, rd[0]);
	expect_long("range does not call", 2, start_calls);

	rd[0] = 0x0100;
	pcm8_call();
	expect_long("pause", 1, last_control);
	rd[0] = 0x0102;
	pcm8_call();
	expect_long("resume", 2, last_control);
	rd[0] = 0x0101;
	pcm8_call();
	expect_long("abort", 0, last_control);

	rd[0] = 0x01fc;
	pcm8_call();
	expect_long("version probe", 1, rd[0]);
	rd[0] = 0x01fe;
	pcm8_call();
	expect_long("lock", 0, rd[0]);
	rd[0] = 0x01ff;
	pcm8_call();
	expect_long("unlock", 0, rd[0]);
	rd[0] = 0x0200;
	pcm8_call();
	expect_long("unknown command", -1, rd[0]);
	expect_long("call diagnostics", 11, (Long)pcm8_call_count());
	expect_long("start diagnostics", 1, (Long)pcm8_start_count());
	expect_long("channel diagnostics", 1 << 3,
	            (Long)pcm8_used_channel_mask());

	pcm8_set_backend(NULL, NULL, NULL, NULL);
	expect_long("unregister preserves diagnostics", 11,
	            (Long)pcm8_call_count());
	return failures != 0;
}
