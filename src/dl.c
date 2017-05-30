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
 * Dynamic loading abstraction for object files
 *
 */

#include <config.h>

#include <string.h>

#include <wget.h>

#include "wget_dl.h"

#if defined PLUGIN_SUPPORT_LIBDL
#include <dlfcn.h>

int dl_supported()
{
	return 1;
}

struct dl_file_st
{
	void *handle;
};

//Opens an object file
dl_file_t *dl_file_open(const char *filename, dl_error_t *e)
{
	const char *rpl_filename = filename;
	char *buf = NULL;
	dl_file_t *dm = NULL;
	dl_file_t dm_st;

	if (filename) {
		//Prepend filename with "./" if there is no "/" in it
		//because we don't want dlopen() to start searching.
		int fw_slash_absent = 1;
		int i, filename_len;
		for (i = 0; filename[i]; i++)
			if (filename[i] == '/')
				fw_slash_absent = 0;
		filename_len = i;
		if (fw_slash_absent) {
			buf = wget_malloc(filename_len + 3);
			buf[0] = '.';
			buf[1] = '/';
			strcpy(buf + 2, filename);
			rpl_filename = buf;
		}
	}

	dm_st.handle = dlopen(rpl_filename, RTLD_LOCAL);
	if (dm_st.handle)
		dm = wget_memdup(&dm_st, sizeof(dl_file_t));
	else
		dl_error_set(e, dlerror());

	wget_xfree(buf);
	return dm;
}

void *dl_file_lookup(dl_file_t *dm, const char *symbol, dl_error_t *e)
{
	void *res;
	char *error;

	res = dlsym(dm->handle, symbol);
	error = dlerror();
	if (error) {
		dl_error_set(e, error);
		return NULL;
	}

	return res;
}

void dl_file_close(dl_file_t *dm)
{
	dlclose(dm->handle);

	wget_free(dm);
}

//TODO: Implementation for windows
//#elif defined PLUGIN_SUPPORT_WINDOWS

#else

const static char *dl_unsupported
	= "Dynamic loading is not supported on the current platform.";

int dl_supported()
{
	return 0;
}

dl_file_t *dl_file_load(const char *filename, dl_error_t *e)
{
	dl_error_set(e, dl_unsupported);
	return NULL;
}

void *dl_file_lookup(dl_file_t *dm, const char *symbol, dl_error_t *e)
{
	dl_error_set(e, dl_unsupported);
	return NULL;
}

void dl_file_close(dl_file_t *dm)
{
}

#endif
