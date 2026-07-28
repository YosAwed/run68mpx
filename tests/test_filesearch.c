#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filesearch.h"

static int failures;

static ULong read_long(const UChar *p)
{
	return ((ULong)p[0] << 24) | ((ULong)p[1] << 16) |
	       ((ULong)p[2] << 8) | p[3];
}

static void expect(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "%s\n", message);
		failures++;
	}
}

static void make_file(const char *path, size_t size)
{
	FILE *file = fopen(path, "wb");
	size_t i;

	if (file == NULL) {
		perror(path);
		exit(2);
	}
	for (i = 0; i < size; i++)
		fputc((int)(i & 0xffu), file);
	fclose(file);
}

int main(void)
{
	char directory[] = "/tmp/run68-files-XXXXXX";
	char path[512];
	char pattern[512];
	UChar buffer[141];
	int found_alpha = 0;
	int found_beta = 0;
	Long result;

	if (mkdtemp(directory) == NULL)
		return 2;
	snprintf(path, sizeof(path), "%s/ALPHA.X", directory);
	make_file(path, 17);
	snprintf(path, sizeof(path), "%s/beta.x", directory);
	make_file(path, 33);
	snprintf(path, sizeof(path), "%s/ignore.txt", directory);
	make_file(path, 5);
	snprintf(path, sizeof(path), "%s/subdir", directory);
	mkdir(path, 0700);

	memset(buffer, 0, sizeof(buffer));
	snprintf(pattern, sizeof(pattern), "%s/*.X", directory);
	result = run68_files_first(buffer, 53, pattern, 0x20);
	while (result == 0) {
		if (strcmp((char *)buffer + 30, "ALPHA.X") == 0) {
			found_alpha++;
			expect(read_long(buffer + 26) == 17,
			       "ALPHA.X size was not stored in big-endian form");
		}
		if (strcmp((char *)buffer + 30, "beta.x") == 0)
			found_beta++;
		expect((buffer[21] & 0x20u) != 0,
		       "regular file archive attribute is missing");
		result = run68_files_next(buffer, 53);
	}
	expect(result == -18, "NFILES did not return -18 at end of search");
	expect(found_alpha == 1 && found_beta == 1,
	       "case-insensitive wildcard search returned wrong files");
	expect(run68_files_next(buffer, 53) == -18,
	       "stale NFILES buffer was accepted");

	memset(buffer, 0, sizeof(buffer));
	snprintf(pattern, sizeof(pattern), "%s/alpha", directory);
	result = run68_files_first(buffer, sizeof(buffer), pattern, 0x0120);
	expect(result == 0 && strcmp((char *)buffer + 30, "ALPHA.X") == 0,
	       "FILES completion mode did not append .* to the pattern");
	expect(strcmp((char *)buffer + 53, directory) == 0,
	       "extended FILES buffer did not receive the search path");

	memset(buffer, 0, sizeof(buffer));
	snprintf(pattern, sizeof(pattern), "%s/sub*", directory);
	result = run68_files_first(buffer, 53, pattern, 0x10);
	expect(result == 0 && strcmp((char *)buffer + 30, "subdir") == 0 &&
	       (buffer[21] & 0x10u) != 0,
	       "directory attribute search failed");

	memset(buffer, 0, sizeof(buffer));
	snprintf(pattern, sizeof(pattern), "%s/missing.*", directory);
	expect(run68_files_first(buffer, 53, pattern, 0x20) == -2,
	       "FILES missing-file result was not -2");

	run68_files_close_all();
	snprintf(path, sizeof(path), "%s/ALPHA.X", directory);
	unlink(path);
	snprintf(path, sizeof(path), "%s/beta.x", directory);
	unlink(path);
	snprintf(path, sizeof(path), "%s/ignore.txt", directory);
	unlink(path);
	snprintf(path, sizeof(path), "%s/subdir", directory);
	rmdir(path);
	rmdir(directory);
	return failures == 0 ? 0 : 1;
}
