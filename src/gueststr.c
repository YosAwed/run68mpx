#undef MAIN

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "run68.h"

const char *run68_guest_string(Long address, size_t max_chars)
{
	const char *text;
	size_t available;

	if (address < 0 || (ULong)address >= (ULong)mem_aloc)
		return NULL;
	text = prog_ptr + address;
	available = (size_t)((ULong)mem_aloc - (ULong)address);
	if (available == 0)
		return NULL;
	/* Require a terminator within max_chars + 1 bytes. */
	if (max_chars < SIZE_MAX && available > max_chars + 1u)
		available = max_chars + 1u;
	return memchr(text, '\0', available) != NULL ? text : NULL;
}

BOOL run68_guest_buffer_ok(Long address, size_t bytes)
{
	if (bytes == 0)
		return TRUE;
	if (address < 0 || (ULong)address >= (ULong)mem_aloc)
		return FALSE;
	return bytes <= (size_t)((ULong)mem_aloc - (ULong)address);
}
