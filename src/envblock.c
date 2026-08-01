#undef MAIN

#include <string.h>

#include "run68.h"

Long Getenv_common(const char *name_p, char *buf_p)
{
	unsigned char *env_end = (unsigned char *)prog_ptr + ENV_TOP + ENV_SIZE;
	unsigned char *mem_ptr;

	for (mem_ptr = (unsigned char *)prog_ptr + ENV_TOP + 4;
	     mem_ptr < env_end && *mem_ptr != 0;
	     mem_ptr++) {
		char ename[256];
		size_t i;

		for (i = 0; mem_ptr < env_end && *mem_ptr != '\0' &&
		     *mem_ptr != '='; i++) {
			if (i + 1 >= sizeof(ename))
				return -10;
			ename[i] = (char)*(mem_ptr++);
		}
		if (mem_ptr >= env_end)
			return -10;
		ename[i] = '\0';
		if (_stricmp(name_p, ename) == 0) {
			while (mem_ptr < env_end &&
			       (*mem_ptr == '=' || *mem_ptr == ' '))
				mem_ptr++;
			if (mem_ptr >= env_end ||
			    memchr(mem_ptr, '\0', (size_t)(env_end - mem_ptr)) == NULL)
				return -10;
			strcpy(buf_p, (char *)mem_ptr);
			return 0;
		}
		while (mem_ptr < env_end && *mem_ptr)
			mem_ptr++;
	}
	(*buf_p) = 0;
	return -10;
}

Long Setenv_common(const char *name_p, const char *value_p)
{
	unsigned char *env_base = (unsigned char *)prog_ptr + ENV_TOP;
	unsigned char *env_end = env_base + ENV_SIZE;
	unsigned char *mem_ptr;
	unsigned char *write_ptr;
	size_t name_len;
	size_t value_len;
	size_t entry_len;
	unsigned char rebuilt[ENV_SIZE];
	size_t rebuilt_len = 0;

	if (name_p == NULL || name_p[0] == '\0')
		return -14;
	name_len = strlen(name_p);
	value_len = value_p == NULL ? 0 : strlen(value_p);
	if (name_len + value_len + 2 > 255)
		return -14;

	for (mem_ptr = env_base + 4;
	     mem_ptr < env_end && *mem_ptr != 0; ) {
		char ename[256];
		unsigned char *entry = mem_ptr;
		size_t i;

		for (i = 0; mem_ptr < env_end && *mem_ptr != '\0' &&
		     *mem_ptr != '='; i++) {
			if (i + 1 >= sizeof(ename))
				return -10;
			ename[i] = (char)*(mem_ptr++);
		}
		if (mem_ptr >= env_end)
			return -10;
		ename[i] = '\0';
		if (*mem_ptr == '=')
			mem_ptr++;
		while (mem_ptr < env_end && *mem_ptr)
			mem_ptr++;
		if (mem_ptr >= env_end)
			return -10;
		mem_ptr++;

		if (_stricmp(name_p, ename) == 0)
			continue;

		entry_len = (size_t)(mem_ptr - entry);
		if (rebuilt_len + entry_len >= ENV_SIZE - 5)
			return -10;
		memcpy(rebuilt + rebuilt_len, entry, entry_len);
		rebuilt_len += entry_len;
	}

	if (value_p != NULL && value_p[0] != '\0') {
		entry_len = name_len + 1 + value_len + 1;
		if (rebuilt_len + entry_len >= ENV_SIZE - 5)
			return -10;
		memcpy(rebuilt + rebuilt_len, name_p, name_len);
		rebuilt_len += name_len;
		rebuilt[rebuilt_len++] = '=';
		memcpy(rebuilt + rebuilt_len, value_p, value_len);
		rebuilt_len += value_len;
		rebuilt[rebuilt_len++] = '\0';
	}

	rebuilt[rebuilt_len++] = '\0';
	if (rebuilt_len + 4 > ENV_SIZE)
		return -10;
	mem_set(ENV_TOP, (Long)ENV_SIZE, S_LONG);
	write_ptr = env_base + 4;
	memset(write_ptr, 0, ENV_SIZE - 4);
	memcpy(write_ptr, rebuilt, rebuilt_len);
	return 0;
}
