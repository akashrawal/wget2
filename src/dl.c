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
#include <sys/stat.h>
#include <dirent.h>

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

#if defined _WIN32
#define DL_NATIVE_PREFIX ""
#define DL_NATIVE_SUFFIX ".dll"
#elif defined __APPLE__
#define DL_NATIVE_PREFIX "lib"
#define DL_NATIVE_SUFFIX ".dylib"
#else
#define DL_NATIVE_PREFIX "lib"
#define DL_NATIVE_SUFFIX ".so"
#endif

char *dl_build_filename(const char *dir, const char *name)
{
	if (dir) {
		return wget_aprintf("%s/" DL_NATIVE_PREFIX "%s" DL_NATIVE_SUFFIX,
				dir, name);
	} else {
		return wget_aprintf(DL_NATIVE_PREFIX "%s" DL_NATIVE_SUFFIX,
				name);
	}
}

static int is_regular_file(const char *filename)
{
	struct stat statbuf;	

	if (stat(filename, &statbuf) < 0)
		return 0;
	if (S_ISREG(statbuf.st_mode))
		return 1;
	return 0;
}

char *dl_search(const char *name, char **dirs, int n_dirs)
{
	int i;

	for (i = 0; i < n_dirs; i++) {
		char *filename;

		filename = dl_build_filename(dirs[i], name);
		if (is_regular_file(filename))
			return filename;

		wget_free(filename);	
	}
	return NULL;
}

int dl_list(const char *dir, char ***names_out, int *n_names_out)
{
	DIR *dirp;
	wget_buffer_t buf[1];

	dirp = opendir(dir);
	if (!dir)
		return -1;

	wget_buffer_init(buf, NULL, 0);

	while(1) {
		struct dirent *ent;
		char *fname;
		size_t fname_len;
		char *name;
		if ((ent = readdir(dirp)) == NULL)
			break;

		fname = ent->d_name;
		fname_len = strlen(fname);

		//Ignore entries that don't match with the pattern
		{
			size_t pl = strlen(DL_NATIVE_PREFIX);
			size_t sl = strlen(DL_NATIVE_SUFFIX);
			if (fname_len <= pl + sl)
				continue;
			if (strncmp(fname, DL_NATIVE_PREFIX, pl) != 0)
				continue;
			if (strcmp(fname + fname_len - sl,
						DL_NATIVE_SUFFIX) != 0)
				continue;

			//Also trim prefix and suffix from fname
			name = wget_strmemdup(fname + pl, fname_len - pl - sl);
		}

		//Ignore entries that are not regular files
		{
			char *sfname = wget_aprintf("%s/%s", dir, fname);
			int x = is_regular_file(sfname);
			wget_free(sfname);
			if (!x) {
				wget_free(name);
				continue;
			}
		}

		//Add to the list
		wget_buffer_memcat(buf, &name, sizeof(void *));
	}

	closedir(dirp);
	*names_out = (char **) wget_memdup(buf->data, buf->length);
	*n_names_out = buf->length;
	wget_buffer_deinit(buf);

	return 0;
}

