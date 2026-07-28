#include "filesearch.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SEARCH_SLOT_COUNT 32
#define SEARCH_PATH_SIZE 1024
#define SEARCH_PATTERN_SIZE 256

typedef struct {
	DIR *directory;
	ULong token;
	UChar *buffer;
	UChar attributes;
	BOOL extended_buffer;
	char path[SEARCH_PATH_SIZE];
	char pattern[SEARCH_PATTERN_SIZE];
} SEARCH_SLOT;

static SEARCH_SLOT search_slots[SEARCH_SLOT_COUNT];
static ULong next_token = 1;

static void put_word(UChar *p, UShort value)
{
	p[0] = (UChar)(value >> 8);
	p[1] = (UChar)value;
}

static void put_long(UChar *p, ULong value)
{
	p[0] = (UChar)(value >> 24);
	p[1] = (UChar)(value >> 16);
	p[2] = (UChar)(value >> 8);
	p[3] = (UChar)value;
}

static ULong get_long(const UChar *p)
{
	return ((ULong)p[0] << 24) | ((ULong)p[1] << 16) |
	       ((ULong)p[2] << 8) | (ULong)p[3];
}

static int ascii_equal(char left, char right)
{
	return tolower((unsigned char)left) == tolower((unsigned char)right);
}

static BOOL wildcard_match(const char *pattern, const char *name)
{
	const char *star = NULL;
	const char *retry = NULL;

	if (strcmp(pattern, "*.*") == 0)
		return TRUE;

	while (*name != '\0') {
		if (*pattern == '?' ||
		    (*pattern != '\0' && ascii_equal(*pattern, *name))) {
			pattern++;
			name++;
		} else if (*pattern == '*') {
			star = pattern++;
			retry = name;
		} else if (star != NULL) {
			pattern = star + 1;
			name = ++retry;
		} else {
			return FALSE;
		}
	}
	while (*pattern == '*')
		pattern++;
	return *pattern == '\0';
}

static UChar file_attributes(const char *name, const struct stat *info)
{
	UChar attributes = S_ISDIR(info->st_mode) ? 0x10u : 0x20u;

	if ((info->st_mode & S_IWUSR) == 0)
		attributes |= 0x01u;
	if (name[0] == '.' && name[1] != '\0')
		attributes |= 0x02u;
	return attributes;
}

static BOOL attributes_match(UChar actual, UChar requested)
{
	return (actual & requested) != 0 ||
	       (actual == 0 && (requested & 0x20u) != 0);
}

static void close_slot(SEARCH_SLOT *slot)
{
	if (slot->directory != NULL)
		closedir(slot->directory);
	memset(slot, 0, sizeof(*slot));
}

static SEARCH_SLOT *allocate_slot(void)
{
	int i;

	for (i = 0; i < SEARCH_SLOT_COUNT; i++) {
		if (search_slots[i].directory == NULL)
			return &search_slots[i];
	}
	close_slot(&search_slots[0]);
	return &search_slots[0];
}

static SEARCH_SLOT *find_slot(ULong token, const UChar *buffer)
{
	int i;

	for (i = 0; i < SEARCH_SLOT_COUNT; i++) {
		if (search_slots[i].directory != NULL &&
		    search_slots[i].token == token &&
		    search_slots[i].buffer == buffer)
			return &search_slots[i];
	}
	return NULL;
}

static BOOL split_search_name(const char *source, UShort attributes,
                              char *directory, size_t directory_size,
                              char *pattern, size_t pattern_size)
{
	char normalized[SEARCH_PATH_SIZE];
	const char *path = source;
	const char *separator;
	size_t i;

	if (strlen(source) >= sizeof(normalized))
		return FALSE;
	strcpy(normalized, source);
	for (i = 0; normalized[i] != '\0'; i++) {
		if (normalized[i] == '\\')
			normalized[i] = '/';
	}
	if (isalpha((unsigned char)normalized[0]) && normalized[1] == ':')
		path = normalized + 2;
	else
		path = normalized;

	separator = strrchr(path, '/');
	if (separator == NULL) {
		if (directory_size < 2)
			return FALSE;
		strcpy(directory, ".");
		if (strlen(path) >= pattern_size)
			return FALSE;
		strcpy(pattern, path);
	} else {
		size_t length = (size_t)(separator - path);
		if (length == 0) {
			if (directory_size < 2)
				return FALSE;
			strcpy(directory, "/");
		} else {
			if (length >= directory_size)
				return FALSE;
			memcpy(directory, path, length);
			directory[length] = '\0';
		}
		if (strlen(separator + 1) >= pattern_size)
			return FALSE;
		strcpy(pattern, separator + 1);
	}

	if ((attributes & 0xff00u) != 0) {
		if (pattern[0] == '\0') {
			strcpy(pattern, "*.*");
		} else if (strchr(pattern, '.') == NULL) {
			size_t length = strlen(pattern);
			if (length + 2 >= pattern_size)
				return FALSE;
			strcat(pattern, ".*");
		}
	}
	return pattern[0] != '\0';
}

static Long fill_next(SEARCH_SLOT *slot, UChar *buffer, size_t buffer_size,
                      Long no_match_error)
{
	struct dirent *entry;

	while ((entry = readdir(slot->directory)) != NULL) {
		char full_path[SEARCH_PATH_SIZE];
		struct stat info;
		struct tm local_time;
		UChar attributes;
		UShort date;
		UShort time_value;
		int year;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0 ||
		    !wildcard_match(slot->pattern, entry->d_name))
			continue;
		if (snprintf(full_path, sizeof(full_path), "%s/%s", slot->path,
		             entry->d_name) >= (int)sizeof(full_path))
			continue;
		if (stat(full_path, &info) != 0)
			continue;
		attributes = file_attributes(entry->d_name, &info);
		if (!attributes_match(attributes, slot->attributes))
			continue;

		memset(buffer + 21, 0, 32);
		buffer[21] = attributes;
		if (localtime_r(&info.st_mtime, &local_time) == NULL)
			memset(&local_time, 0, sizeof(local_time));
		year = local_time.tm_year + 1900;
		if (year < 1980)
			year = 1980;
		if (year > 2107)
			year = 2107;
		time_value = (UShort)((local_time.tm_hour << 11) |
		                      (local_time.tm_min << 5) |
		                      (local_time.tm_sec / 2));
		date = (UShort)(((year - 1980) << 9) |
		                ((local_time.tm_mon + 1) << 5) |
		                local_time.tm_mday);
		put_word(buffer + 22, time_value);
		put_word(buffer + 24, date);
		put_long(buffer + 26, (ULong)info.st_size);
		strncpy((char *)buffer + 30, entry->d_name, 22);
		buffer[52] = '\0';
		if (slot->extended_buffer && buffer_size >= 141) {
			strncpy((char *)buffer + 53, slot->path, 87);
			buffer[140] = '\0';
		}
		return 0;
	}

	close_slot(slot);
	put_long(buffer + 2, 0);
	return no_match_error;
}

Long run68_files_first(UChar *buffer, size_t buffer_size,
                       const char *name, short attributes)
{
	SEARCH_SLOT *slot;
	int i;

	if (buffer == NULL || name == NULL || buffer_size < 53)
		return -14;
	for (i = 0; i < SEARCH_SLOT_COUNT; i++) {
		if (search_slots[i].directory != NULL &&
		    search_slots[i].buffer == buffer)
			close_slot(&search_slots[i]);
	}

	slot = allocate_slot();
	if (!split_search_name(name, (UShort)attributes, slot->path,
	                       sizeof(slot->path), slot->pattern,
	                       sizeof(slot->pattern)))
		return -13;
	slot->attributes = (UChar)attributes;
	slot->extended_buffer = buffer_size >= 141;
	slot->buffer = buffer;
	slot->directory = opendir(slot->path);
	if (slot->directory == NULL) {
		memset(slot, 0, sizeof(*slot));
		return errno == ENOENT ? -3 : -5;
	}
	if (++next_token == 0)
		next_token = 1;
	slot->token = next_token;
	memset(buffer, 0, buffer_size >= 141 ? 141 : 53);
	buffer[0] = (UChar)attributes;
	put_long(buffer + 2, slot->token);
	return fill_next(slot, buffer, buffer_size, -2);
}

Long run68_files_next(UChar *buffer, size_t buffer_size)
{
	SEARCH_SLOT *slot;

	if (buffer == NULL || buffer_size < 53)
		return -14;
	slot = find_slot(get_long(buffer + 2), buffer);
	if (slot == NULL)
		return -18;
	return fill_next(slot, buffer, buffer_size, -18);
}

void run68_files_close_all(void)
{
	int i;

	for (i = 0; i < SEARCH_SLOT_COUNT; i++)
		close_slot(&search_slots[i]);
}
