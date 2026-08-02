#undef MAIN

#include <errno.h>
#include <stdlib.h>

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

BOOL run68_parse_stack_size_kb(const char *text, Long *kb_out)
{
	char *end = NULL;
	long kb;

	if (text == NULL || kb_out == NULL)
		return FALSE;
	errno = 0;
	kb = strtol(text, &end, 10);
	if (errno == ERANGE || end == text || *end != '\0')
		return FALSE;
	/* Reject values that would truncate when stored in Long (int32). */
	if (kb != (long)(Long)kb)
		return FALSE;
	if (kb < RUN68_MIN_STACK_KB || kb > RUN68_MAX_STACK_KB)
		return FALSE;
	*kb_out = (Long)kb;
	return TRUE;
}

BOOL run68_stack_fits_memory(Long memory_bytes)
{
	return memory_bytes > PROG_TOP;
}
