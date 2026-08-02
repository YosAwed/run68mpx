#undef MAIN

#include <stdlib.h>
#include <string.h>

#include "run68.h"

static Long resolve_env_address(Long env)
{
	ULong pdb;

	if (env != 0)
		return env;
	pdb = (ULong)psp[nest_cnt];
	if ((pdb & 1u) != 0 || !run68_guest_buffer_ok((Long)pdb, 0x14u))
		return -1;
	return mem_get((Long)pdb + 0x10, S_LONG);
}

Long run68_setenv_call(Long stack_address)
{
	Long name;
	Long env;
	Long value;
	const char *name_ptr;
	const char *value_ptr;
	Long env_address;

	if (!run68_guest_buffer_ok(stack_address, 12u))
		return -14;
	name = mem_get(stack_address, S_LONG);
	env = mem_get(stack_address + 4, S_LONG);
	value = mem_get(stack_address + 8, S_LONG);
	name_ptr = run68_guest_string(name, 255);
	if (name_ptr == NULL)
		return -14;
	if (value == 0)
		value_ptr = NULL;
	else {
		value_ptr = run68_guest_string(value, 255);
		if (value_ptr == NULL)
			return -14;
	}
	env_address = resolve_env_address(env);
	if (env_address < 0)
		return -10;
	return Setenv_common(env_address, name_ptr, value_ptr);
}

Long run68_getenv_call(Long stack_address)
{
	Long name;
	Long env;
	Long buf;
	const char *name_ptr;

	if (!run68_guest_buffer_ok(stack_address, 12u))
		return -14;
	name = mem_get(stack_address, S_LONG);
	env = mem_get(stack_address + 4, S_LONG);
	buf = mem_get(stack_address + 8, S_LONG);
	/* Getenv_common currently reads only the primary ENV_TOP block. */
	if (env != 0)
		return -10;
	name_ptr = run68_guest_string(name, 255);
	if (name_ptr == NULL || !run68_guest_buffer_ok(buf, 256u))
		return -14;
	return Getenv_common(name_ptr, prog_ptr + buf);
}

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

Long Setenv_common(Long env_address, const char *name_p, const char *value_p)
{
	unsigned char *env_base;
	unsigned char *env_end;
	unsigned char *mem_ptr;
	unsigned char *write_ptr;
	size_t name_len;
	size_t value_len;
	size_t entry_len;
	unsigned char *rebuilt;
	size_t env_size;
	size_t payload_size;
	size_t rebuilt_len = 0;
	Long result = 0;

	if (name_p == NULL || name_p[0] == '\0')
		return -14;
	if (env_address < 0 || ((ULong)env_address & 1u) != 0 ||
	    (ULong)env_address > (ULong)mem_aloc ||
	    4u > (ULong)mem_aloc - (ULong)env_address)
		return -10;
	env_size = (size_t)(ULong)mem_get(env_address, S_LONG);
	if (env_size < 5u || env_size > (size_t)((ULong)mem_aloc -
	                                         (ULong)env_address))
		return -10;
	env_base = (unsigned char *)prog_ptr + env_address;
	env_end = env_base + env_size;
	payload_size = env_size - 4u;
	rebuilt = malloc(payload_size);
	if (rebuilt == NULL)
		return -8;
	name_len = strlen(name_p);
	value_len = value_p == NULL ? 0 : strlen(value_p);
	if (name_len + value_len + 2 > 255) {
		result = -14;
		goto done;
	}

	for (mem_ptr = env_base + 4;
	     mem_ptr < env_end && *mem_ptr != 0; ) {
		char ename[256];
		unsigned char *entry = mem_ptr;
		size_t i;

		for (i = 0; mem_ptr < env_end && *mem_ptr != '\0' &&
		     *mem_ptr != '='; i++) {
			if (i + 1 >= sizeof(ename)) {
				result = -10;
				goto done;
			}
			ename[i] = (char)*(mem_ptr++);
		}
		if (mem_ptr >= env_end) {
			result = -10;
			goto done;
		}
		ename[i] = '\0';
		if (*mem_ptr == '=')
			mem_ptr++;
		while (mem_ptr < env_end && *mem_ptr)
			mem_ptr++;
		if (mem_ptr >= env_end) {
			result = -10;
			goto done;
		}
		mem_ptr++;

		if (_stricmp(name_p, ename) == 0)
			continue;

		entry_len = (size_t)(mem_ptr - entry);
		if (entry_len >= payload_size ||
		    rebuilt_len > payload_size - entry_len - 1u) {
			result = -10;
			goto done;
		}
		memcpy(rebuilt + rebuilt_len, entry, entry_len);
		rebuilt_len += entry_len;
	}

	/* Human68k deletes the variable for both NULL and an empty value. */
	if (value_p != NULL && value_p[0] != '\0') {
		entry_len = name_len + 1 + value_len + 1;
		if (entry_len >= payload_size ||
		    rebuilt_len > payload_size - entry_len - 1u) {
			result = -10;
			goto done;
		}
		memcpy(rebuilt + rebuilt_len, name_p, name_len);
		rebuilt_len += name_len;
		rebuilt[rebuilt_len++] = '=';
		memcpy(rebuilt + rebuilt_len, value_p, value_len);
		rebuilt_len += value_len;
		rebuilt[rebuilt_len++] = '\0';
	}

	rebuilt[rebuilt_len++] = '\0';
	write_ptr = env_base + 4;
	memset(write_ptr, 0, payload_size);
	memcpy(write_ptr, rebuilt, rebuilt_len);
done:
	free(rebuilt);
	return result;
}
