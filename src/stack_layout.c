#undef MAIN

#include "run68.h"

BOOL run68_set_stack_size_kb(Long kb)
{
	Long bytes;

	if (kb < RUN68_MIN_STACK_KB || kb > RUN68_MAX_STACK_KB)
		return FALSE;
	bytes = kb * 1024;
	if (bytes <= 0)
		return FALSE;
	stack_size = bytes;
	return TRUE;
}

BOOL run68_stack_fits_memory(Long memory_bytes)
{
	return memory_bytes > PROG_TOP;
}
