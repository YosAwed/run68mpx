#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "run68.h"

Long ra[8];
Long psp[NEST_MAX];
char nest_cnt;
short sr;
char *prog_ptr;

static int failures;
static const Long read_top = 0x40;
static const Long memory_size = 0x400;

Long mem_get(Long address, char size)
{
	const UChar *p = (const UChar *)prog_ptr + (ULong)address;
	Long value = *p;

	if (size == S_BYTE)
		return value;
	value = (value << 8) | p[1];
	if (size == S_WORD)
		return value;
	return (value << 16) | ((Long)p[2] << 8) | p[3];
}

void mem_set(Long address, Long value, char size)
{
	UChar *p = (UChar *)prog_ptr + (ULong)address;

	if (size == S_LONG) {
		p[0] = (UChar)((ULong)value >> 24);
		p[1] = (UChar)((ULong)value >> 16);
		p[2] = (UChar)((ULong)value >> 8);
		p[3] = (UChar)value;
	} else if (size == S_WORD) {
		p[0] = (UChar)((ULong)value >> 8);
		p[1] = (UChar)value;
	} else {
		p[0] = (UChar)value;
	}
}

static void put_u32(UChar *p, ULong value)
{
	p[0] = (UChar)(value >> 24);
	p[1] = (UChar)(value >> 16);
	p[2] = (UChar)(value >> 8);
	p[3] = (UChar)value;
}

static FILE *make_file(const UChar *data, size_t size)
{
	FILE *fp = tmpfile();

	if (fp == NULL)
		return NULL;
	if (fwrite(data, 1, size, fp) != size) {
		fclose(fp);
		return NULL;
	}
	rewind(fp);
	return fp;
}

static Long load_image(const char *filename, const UChar *data, size_t size,
			       Long limit, Long *loaded_size,
			       Long *initialized_size)
{
	char name[32];
	FILE *fp;

	strcpy(name, filename);
	fp = make_file(data, size);
	if (fp == NULL) {
		fprintf(stderr, "could not create test file\n");
		exit(2);
	}
	*initialized_size = limit;
	return prog_read(fp, name, read_top, loaded_size, initialized_size,
	                 FALSE);
}

static void expect_long(const char *name, Long expected, Long actual)
{
	if (expected != actual) {
		fprintf(stderr, "%s: expected %d, got %d\n",
		        name, expected, actual);
		failures++;
	}
}

static void make_x_header(UChar *image, ULong code, ULong data, ULong bss,
			  ULong reloc)
{
	memset(image, 0, XHEAD_SIZE);
	image[0] = 0x48;
	image[1] = 0x55;
	put_u32(image + 0x0c, code);
	put_u32(image + 0x10, data);
	put_u32(image + 0x14, bss);
	put_u32(image + 0x18, reloc);
}

static void test_valid_x_relocation(void)
{
	UChar image[XHEAD_SIZE + 10];
	Long loaded;
	Long initialized;

	memset(image, 0, sizeof(image));
	make_x_header(image, 8, 0, 4, 2);
	put_u32(image + XHEAD_SIZE + 4, 0x10);
	image[XHEAD_SIZE + 8] = 0;
	image[XHEAD_SIZE + 9] = 4;
	memset(prog_ptr, 0xa5, (size_t)memory_size);

	expect_long("valid X entry", read_top,
	            load_image("valid.x", image, sizeof(image), memory_size,
	                       &loaded, &initialized));
	expect_long("X memory image size", 12, loaded);
	expect_long("X initialized size", 8, initialized);
	expect_long("relocated address", read_top + 0x10,
	            mem_get(read_top + 4, S_LONG));
	expect_long("BSS is zeroed", 0, mem_get(read_top + 8, S_LONG));
}

static void test_extended_relocation(void)
{
	UChar image[XHEAD_SIZE + 14];
	Long loaded;
	Long initialized;

	memset(image, 0, sizeof(image));
	make_x_header(image, 8, 0, 0, 6);
	put_u32(image + XHEAD_SIZE + 4, 0x20);
	image[XHEAD_SIZE + 8] = 0;
	image[XHEAD_SIZE + 9] = 1;
	put_u32(image + XHEAD_SIZE + 10, 4);

	expect_long("extended relocation entry", read_top,
	            load_image("extended.x", image, sizeof(image), memory_size,
	                       &loaded, &initialized));
	expect_long("extended relocated address", read_top + 0x20,
	            mem_get(read_top + 4, S_LONG));
}

static void test_nonzero_link_base(void)
{
	UChar image[XHEAD_SIZE + 10];
	Long loaded;
	Long initialized;

	memset(image, 0, sizeof(image));
	make_x_header(image, 8, 0, 0, 2);
	put_u32(image + 0x04, 0x1000);
	put_u32(image + XHEAD_SIZE + 4, 0x1010);
	image[XHEAD_SIZE + 8] = 0;
	image[XHEAD_SIZE + 9] = 4;

	expect_long("nonzero base entry", read_top,
	            load_image("base.x", image, sizeof(image), memory_size,
	                       &loaded, &initialized));
	expect_long("nonzero base relocation", read_top + 0x10,
	            mem_get(read_top + 4, S_LONG));
}

static void test_rejects_malformed_x(void)
{
	UChar short_file[12] = {0};
	UChar bad_sections[XHEAD_SIZE + 4];
	UChar bad_reloc[XHEAD_SIZE + 10];
	UChar large_bss[XHEAD_SIZE + 4];
	Long loaded;
	Long initialized;

	expect_long("short X header", -11,
	            load_image("short.x", short_file, sizeof(short_file),
	                       memory_size, &loaded, &initialized));

	memset(bad_sections, 0, sizeof(bad_sections));
	make_x_header(bad_sections, 16, 0, 0, 0);
	expect_long("section exceeds file", -11,
	            load_image("sections.x", bad_sections,
	                       sizeof(bad_sections), memory_size,
	                       &loaded, &initialized));

	memset(bad_reloc, 0, sizeof(bad_reloc));
	make_x_header(bad_reloc, 8, 0, 0, 2);
	bad_reloc[XHEAD_SIZE + 8] = 0;
	bad_reloc[XHEAD_SIZE + 9] = 7;
	expect_long("odd relocation target", -11,
	            load_image("reloc.x", bad_reloc, sizeof(bad_reloc),
	                       memory_size, &loaded, &initialized));

	memset(large_bss, 0, sizeof(large_bss));
	make_x_header(large_bss, 4, 0, 0x200, 0);
	expect_long("BSS exceeds allocation", -8,
	            load_image("bss.x", large_bss, sizeof(large_bss),
	                       read_top + 0x100, &loaded, &initialized));
}

static void test_raw_file(void)
{
	const UChar image[] = {0x11, 0x22, 0x33, 0x44};
	Long loaded;
	Long initialized;

	expect_long("raw entry", read_top,
	            load_image("plain.r", image, sizeof(image), memory_size,
	                       &loaded, &initialized));
	expect_long("raw size", 4, loaded);
	expect_long("raw initialized size", 4, initialized);
	expect_long("raw contents", 0x11223344,
	            mem_get(read_top, S_LONG));
}

int main(void)
{
	prog_ptr = malloc((size_t)memory_size);
	if (prog_ptr == NULL)
		return 2;

	test_valid_x_relocation();
	test_extended_relocation();
	test_nonzero_link_base();
	test_rejects_malformed_x();
	test_raw_file();

	free(prog_ptr);
	return failures == 0 ? 0 : 1;
}
