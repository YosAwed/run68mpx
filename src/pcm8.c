#include "run68.h"

static void *pcm8_context;
static PCM8_START_CALLBACK pcm8_start;
static PCM8_CONTROL_CALLBACK pcm8_control;
static PCM8_REMAINING_CALLBACK pcm8_remaining;
static uint64_t pcm8_calls;
static uint64_t pcm8_start_calls;
static unsigned int pcm8_channel_mask;

void pcm8_set_backend(void *context, PCM8_START_CALLBACK start,
	PCM8_CONTROL_CALLBACK control, PCM8_REMAINING_CALLBACK remaining)
{
	if (context != NULL && context != pcm8_context) {
		pcm8_calls = 0;
		pcm8_start_calls = 0;
		pcm8_channel_mask = 0;
	}
	pcm8_context = context;
	pcm8_start = start;
	pcm8_control = control;
	pcm8_remaining = remaining;
}

int pcm8_call(void)
{
	ULong command = (ULong)rd[0] & 0xffffu;

	pcm8_calls++;

	if (command < 8u) {
		unsigned int channel = (unsigned int)command;

		if (rd[2] < 0) {
			size_t rest = pcm8_remaining == NULL ? 0 :
			              pcm8_remaining(pcm8_context, channel);
			rd[0] = rest > 0x7fffffffu ? 0x7fffffff : (Long)rest;
			return FALSE;
		}
		if (rd[2] == 0) {
			rd[0] = pcm8_start == NULL ? 0 :
			        pcm8_start(pcm8_context, channel, NULL, 0,
			                   (ULong)rd[1]);
			return FALSE;
		}
		{
			ULong address = (ULong)ra[1] & 0x00ffffffu;
			ULong length = (ULong)rd[2];

			if (address > (ULong)mem_aloc ||
			    length > (ULong)mem_aloc - address) {
				rd[0] = -1;
			} else if (pcm8_start != NULL &&
			           pcm8_start(pcm8_context, channel,
			                      (const UChar *)prog_ptr + address,
			                      (size_t)length, (ULong)rd[1]) != 0) {
				rd[0] = -1;
			} else {
				rd[0] = 0;
				pcm8_start_calls++;
				pcm8_channel_mask |= 1u << channel;
			}
		}
		return FALSE;
	}

	switch (command) {
		case 0x0100: /* pause all channels */
			rd[0] = pcm8_control == NULL ? 0 :
			        pcm8_control(pcm8_context, 1);
			break;
		case 0x0101: /* abort/stop all channels */
			rd[0] = pcm8_control == NULL ? 0 :
			        pcm8_control(pcm8_context, 0);
			break;
		case 0x0102: /* resume all channels */
			rd[0] = pcm8_control == NULL ? 0 :
			        pcm8_control(pcm8_context, 2);
			break;
		case 0x01fc: /* PCM8 compatibility/version probe */
			rd[0] = 1;
			break;
		case 0x01fe: /* lock */
		case 0x01ff: /* unlock */
			rd[0] = 0;
			break;
		default:
			rd[0] = -1;
			break;
	}
	return FALSE;
}

uint64_t pcm8_start_count(void)
{
	return pcm8_start_calls;
}

uint64_t pcm8_call_count(void)
{
	return pcm8_calls;
}

unsigned int pcm8_used_channel_mask(void)
{
	return pcm8_channel_mask;
}
