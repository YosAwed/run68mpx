#undef MAIN

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "run68.h"

static BOOL host_path(const char *source, char *destination, size_t size,
	                  UShort *drive)
{
	const char *path = source;
	size_t i;
	size_t length;

	if (source == NULL || destination == NULL || drive == NULL || size == 0)
		return FALSE;
	*drive = 1; /* An unqualified host path is exposed as drive A:. */
	if (source[0] != '\0' && source[1] == ':') {
		int letter = toupper((unsigned char)source[0]);

		if (letter < 'A' || letter > 'Z')
			return FALSE;
		*drive = (UShort)(letter - 'A' + 1);
		path = source + 2;
	}
	length = strlen(path);
	if (length >= size)
		return FALSE;
	memcpy(destination, path, length + 1);
	for (i = 0; i < length; ++i) {
		if (destination[i] == '\\')
			destination[i] = '/';
	}
	return TRUE;
}

static Long fatchk(Long file, Long raw_buffer, UShort buffer_size)
{
	struct stat info;
	const char *path;
	char normalized[256];
	ULong buffer = (ULong)raw_buffer;
	BOOL extended = (buffer & UINT32_C(0x80000000)) != 0;
	uint64_t sectors;
	UShort drive;
	/* drive.w + one extent + terminator: 8 bytes (word) or 14 (long). */
	const ULong result_size = extended ? 14u : 8u;

	path = run68_guest_string(file, 255);
	buffer &= UINT32_C(0x7fffffff);
	if (path == NULL ||
	    !host_path(path, normalized, sizeof(normalized), &drive) ||
	    buffer > (ULong)mem_aloc ||
	    result_size > (ULong)mem_aloc - buffer ||
	    (extended && buffer_size < result_size))
		return -14;
	if (stat(normalized, &info) != 0)
		return -2;
	/* Host files are represented as one contiguous synthetic sector run. */
	sectors = ((uint64_t)info.st_size + 1023u) / 1024u;
	if ((!extended && sectors > UINT16_MAX) || sectors > UINT32_MAX)
		return -14;
	mem_set((Long)buffer, drive, S_WORD);
	if (extended) {
		mem_set((Long)buffer + 2, 1, S_LONG);
		mem_set((Long)buffer + 6, (Long)sectors, S_LONG);
		mem_set((Long)buffer + 10, 0, S_LONG);
	} else {
		mem_set((Long)buffer + 2, 1, S_WORD);
		mem_set((Long)buffer + 4, (Long)sectors, S_WORD);
		mem_set((Long)buffer + 6, 0, S_WORD);
	}
	return (Long)result_size;
}

Long run68_fatchk_call(Long stack_address)
{
	Long file;
	Long buffer;
	UShort buffer_size = 0;

	if (!run68_guest_buffer_ok(stack_address, 8u))
		return -14;
	file = mem_get(stack_address, S_LONG);
	buffer = mem_get(stack_address + 4, S_LONG);
	/* The legacy form has only two long arguments; LEN.w exists only here. */
	if (((ULong)buffer & UINT32_C(0x80000000)) != 0) {
		if (!run68_guest_buffer_ok(stack_address, 10u))
			return -14;
		buffer_size = (UShort)mem_get(stack_address + 8, S_WORD);
	}
	return fatchk(file, buffer, buffer_size);
}
