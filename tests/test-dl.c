/*
 * Copyright(c) 2017 Free Software Foundation, Inc.
 *
 * This file is part of Wget.
 *
 * Wget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * Dynamic loading related testing
 *
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <wget.h>
#include "../libwget/private.h"

#include "../src/wget_dl.h"

#define libassert(expr) \
do { \
	if (! (expr)) { \
		error_printf_exit(__FILE__ ":%d: " \
				"Failed assertion [" #expr "]: %s\n", \
				__LINE__, strerror(errno)); \
	} \
} while(0)

#define OBJECT_DIR "./.test_dl_dir"

static void copy_file(const char *src, const char *dst)
{
	struct stat statbuf;
	int sfd, dfd;
	char buf[256];
	size_t size_remain;

	debug_printf("  Copying %s --> %s\n", src, dst);
	libassert(stat(src, &statbuf) == 0);
	libassert((sfd = open(src, O_RDONLY | O_BINARY)) >= 0);
	libassert((dfd = open(dst, O_WRONLY | O_CREAT | O_BINARY,
					statbuf.st_mode)) >= 0);
	size_remain = statbuf.st_size;
	while(size_remain > 0) {
		size_t io_size = size_remain;
		if (io_size > sizeof(buf))
			io_size = sizeof(buf);
		libassert(read(sfd, buf, io_size) == io_size);
		libassert(write(dfd, buf, io_size) == io_size);
		size_remain -= io_size;
	}
	close(sfd);
	close(dfd);
}

static void dump_list(char **list, size_t list_len)
{
	size_t i;

	for (i = 0; i < list_len; i++)
		debug_printf("  %s\n", list[i]);
}

static void free_list(char **list, size_t list_len)
{
	size_t i;

	for (i = 0; i < list_len; i++)
		wget_free(list[i]);

	wget_free(list);
}

static void prepare_object_dir(const char *name1, ...)
{
	va_list arglist;
	const char *one_name;

	libassert(mkdir(OBJECT_DIR, 0755) == 0);

	//Copy each library into directory
	va_start(arglist, name1);
	one_name = name1;
	while(one_name) {
		char *src = dl_build_filename(".libs", one_name);
		char *dst = dl_build_filename(OBJECT_DIR, one_name);

		copy_file(src, dst);

		wget_free(src);
		wget_free(dst);

		one_name = va_arg(arglist, const char *);
	}
	va_end(arglist);
}

static void add_empty_file(const char *filename)
{
	char *rpl_filename = wget_aprintf(OBJECT_DIR "/%s", filename);
	FILE *stream;
	libassert(stream = fopen(rpl_filename, "w"));
	fclose(stream);
	wget_free(rpl_filename);
}

static void remove_object_dir()
{
	DIR *dirp;
	struct dirent *ent;

	dirp = opendir(OBJECT_DIR);
	if (! dirp)
		return;

	while((ent = readdir(dirp)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0
				|| strcmp(ent->d_name, "..") == 0)
			continue;
		char *filename = wget_aprintf(OBJECT_DIR "/%s", ent->d_name);
		libassert(remove(filename) == 0);
		wget_free(filename);
	}

	closedir(dirp);

	remove(OBJECT_DIR);
}

//Test whether dl_list() works
static void test_dl_list()
{
	remove_object_dir();
	char **names;
	size_t names_len;
	int fail = 0;

	prepare_object_dir("alpha", "beta", NULL);
	add_empty_file("x");
	add_empty_file("file_which_is_not_a_library");
	add_empty_file("libreoffice.png");
	add_empty_file("not_a_library.so");
	add_empty_file("not_a_library.dll");
	add_empty_file("not_a_library.dylib");
	libassert(mkdir(OBJECT_DIR "/somedir", 0755) == 0);
	libassert(mkdir(OBJECT_DIR "/libactuallyadir.so", 0755) == 0);
	libassert(mkdir(OBJECT_DIR "/libactuallyadir.dll", 0755) == 0);
	libassert(mkdir(OBJECT_DIR "/libactuallyadir.dylib", 0755) == 0);

	libassert(dl_list(OBJECT_DIR, &names, &names_len) == 0);

	if (names_len != 2) {
		fail = 1;
	} else {
		if (strcmp(names[0], "alpha") == 0) {
			if (strcmp(names[1], "beta") != 0)
				fail = 1;
		} else if (strcmp(names[0], "beta") == 0) {
			if (strcmp(names[1], "alpha") != 0)
				fail = 1;
		} else {
			fail = 1;
		}
	}
	if (fail == 1)
	{
		error_printf("dl_list() returned incorrect list\n");
		error_printf("List contains\n");
		dump_list(names, names_len);
	}

	free_list(names, names_len);
}

int main(int argc, const char **argv)
{
	if (! dl_supported()) {
		info_printf("Skipping dynamic loading tests\n");

		return 77;
	}

	//TODO: Move common code between test.c into a library?
	// if VALGRIND testing is enabled, we have to call ourselves with
	// valgrind checking
	const char *valgrind = getenv("VALGRIND_TESTS");

	if (!valgrind || !*valgrind || !strcmp(valgrind, "0")) {
		// fallthrough
	}
	else if (!strcmp(valgrind, "1")) {
		char cmd[strlen(argv[0]) + 256];

		snprintf(cmd, sizeof(cmd), "VALGRIND_TESTS=\"\" valgrind "
				"--error-exitcode=301 --leak-check=yes "
				"--show-reachable=yes --track-origins=yes %s",
				argv[0]);
		return system(cmd) != 0;
	} else {
		char cmd[strlen(valgrind) + strlen(argv[0]) + 32];

		snprintf(cmd, sizeof(cmd), "VALGRIND_TESTS="" %s %s",
				valgrind, argv[0]);
		return system(cmd) != 0;
	}

	//TODO: write tests here
	test_dl_list();

	remove_object_dir();

	return 0;
}
