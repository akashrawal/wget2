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
#include <assert.h>

#include <wget.h>
#include "../libwget/private.h"

#include "../src/wget_dl.h"

#define OBJECT_DIR "./.test_dl_objects"

static void copy_file(const char *src, const char *dst)
{
	struct stat statbuf;
	int sfd, dfd;
	char buf[256];
	size_t size_remain;

	assert(stat(src, &statbuf) == 0);
	assert(sfd = open(src, O_RDONLY | O_BINARY));
	assert(dfd = open(dst, O_WRONLY | O_CREAT, statbuf.st_mode));
	size_remain = statbuf.st_size;
	while(size_remain > 0) {
		size_t io_size = size_remain;
		if (io_size > sizeof(buf))
			io_size = sizeof(buf);
		assert(read(sfd, buf, io_size) == io_size);
		assert(write(dfd, buf, io_size) == io_size);
		size_remain -= io_size;
		printf("x\n");
	}
	close(sfd);
	close(dfd);
}

static void prepare_object_dir(const char *name1, ...)
{
	va_list arglist;	
	const char *one_name;

	assert(mkdir(OBJECT_DIR, 0755) == 0);

	//Copy each library into directory
	va_start(arglist, name1);
	one_name = name1;
	while(one_name)
	{
		char *src = dl_build_filename(".libs", one_name);
		char *dst = dl_build_filename(OBJECT_DIR, one_name);
		
		copy_file(src, dst);	

		wget_free(src);
		wget_free(dst);

		one_name = va_arg(arglist, const char *);
	}
	va_end(arglist);
}

static void remove_object_dir()
{
	DIR *dirp;
	struct dirent *ent;

	assert(dirp = opendir(OBJECT_DIR));

	while(ent = readdir(dirp))
	{
		char *filename = wget_aprintf(OBJECT_DIR "/%s", ent->d_name);
		assert(remove(filename) == 0);
		wget_free(filename);
	}

	closedir(dirp);

	remove(OBJECT_DIR);
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
	prepare_object_dir("alpha", "beta", NULL);

	return 0;
}
