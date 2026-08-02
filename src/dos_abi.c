#undef MAIN

#include "run68.h"

typedef struct {
	short mode;
	Long length;
	Long owner;
} RUN68_S_MALLOC_ARGS;

typedef struct {
	short id;
	Long start;
	Long length;
	Long initial;
} RUN68_S_PROCESS_ARGS;

static BOOL parse_s_malloc(Long stack_address, RUN68_S_MALLOC_ARGS *out)
{
	if (!run68_guest_buffer_ok(stack_address, 6u))
		return FALSE;
	out->mode = (short)mem_get(stack_address, S_WORD);
	out->length = mem_get(stack_address + 2, S_LONG);
	out->owner = 0;
	if (((UShort)out->mode & 0x8000u) != 0) {
		if (!run68_guest_buffer_ok(stack_address, 10u))
			return FALSE;
		out->owner = mem_get(stack_address + 6, S_LONG);
	}
	return TRUE;
}

static BOOL parse_s_process(Long stack_address, RUN68_S_PROCESS_ARGS *out)
{
	if (!run68_guest_buffer_ok(stack_address, 14u))
		return FALSE;
	out->id = (short)mem_get(stack_address, S_WORD);
	out->start = mem_get(stack_address + 2, S_LONG);
	out->length = mem_get(stack_address + 6, S_LONG);
	out->initial = mem_get(stack_address + 10, S_LONG);
	return TRUE;
}

Long run68_s_malloc_call(Long stack_address)
{
	RUN68_S_MALLOC_ARGS args;

	if (!parse_s_malloc(stack_address, &args))
		return -14;
	if (((UShort)args.mode & 0x7fffu) > 2u)
		return -14;
	if (((UShort)args.mode & 0x8000u) != 0 && args.owner != 0)
		return -14;
	/* Allocation is performed by doscall; this validates the ABI only. */
	return 0;
}

Long run68_s_process_call(Long stack_address)
{
	RUN68_S_PROCESS_ARGS args;

	if (!parse_s_process(stack_address, &args))
		return -14;
	(void)args;
	return -14; /* Sub-memory process management is not implemented. */
}

BOOL run68_parse_s_malloc_abi(Long stack_address, short *mode, Long *length,
                              Long *owner)
{
	RUN68_S_MALLOC_ARGS args;

	if (!parse_s_malloc(stack_address, &args))
		return FALSE;
	if (mode != NULL)
		*mode = args.mode;
	if (length != NULL)
		*length = args.length;
	if (owner != NULL)
		*owner = args.owner;
	return TRUE;
}

BOOL run68_parse_s_process_abi(Long stack_address, short *id, Long *start,
                               Long *length, Long *initial)
{
	RUN68_S_PROCESS_ARGS args;

	if (!parse_s_process(stack_address, &args))
		return FALSE;
	if (id != NULL)
		*id = args.id;
	if (start != NULL)
		*start = args.start;
	if (length != NULL)
		*length = args.length;
	if (initial != NULL)
		*initial = args.initial;
	return TRUE;
}
