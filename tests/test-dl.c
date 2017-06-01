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

#include <wget.h>
#include "../libwget/private.h"

#include "../src/wget_dl.h"

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

	return 0;
}
