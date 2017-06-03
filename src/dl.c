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
#include <sys/stat.h>
#include <dirent.h>
#include <stdarg.h>

#include <wget.h>

#include "wget_dl.h"

//Error reporting functions

//Set an error message. Call with msg=NULL to clear error.
void dl_error_set(dl_error_t *e, const char *msg)
{
	if (msg && e->msg)
		wget_error_printf_exit
			("Piling up error '%s' over error '%s'", msg, e->msg);

	wget_xfree(e->msg);
	if (msg)
		e->msg = wget_strdup(msg);
}
//Set an error message with printf format.
void dl_error_set_printf
	(dl_error_t *e, const char *format, ...)
{
	va_list arglist;
	va_start(arglist, format);
	if (e->msg)
		wget_error_printf_exit
			("Piling up error '%s' over error '%s'", format, e->msg);

	e->msg = wget_vaprintf(format, arglist);
	va_end(arglist);
}

//If the string is not a path, converts to path by prepending "./" to it,
//else returns NULL
static char *convert_to_path_if_not(const char *str)
{
	if (str) {
		char *buf;
		int fw_slash_absent = 1;
		int i, str_len;
		for (i = 0; str[i]; i++)
			if (str[i] == '/')
				fw_slash_absent = 0;
		str_len = i;
		if (fw_slash_absent) {
			buf = wget_malloc(str_len + 3);
			buf[0] = '.';
			buf[1] = '/';
			strcpy(buf + 2, str);
			return buf;
		}
	}

	return NULL;
}

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
	char *buf = NULL;
	dl_file_t *dm = NULL;
	dl_file_t dm_st;

	buf = convert_to_path_if_not(filename);
	dm_st.handle = dlopen(buf ? buf : filename,
			RTLD_LAZY | RTLD_LOCAL);
	wget_xfree(buf);

	if (dm_st.handle)
		dm = wget_memdup(&dm_st, sizeof(dl_file_t));
	else
		dl_error_set(e, dlerror());

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

#elif defined PLUGIN_SUPPORT_WINDOWS
#include <windows.h>

int dl_supported()
{
	return 1;
}

struct dl_file_st
{
	HMODULE handle;
};

static void dl_win32_set_last_error(dl_error_t *e)
{
	char *buf;

	DWORD error_code = GetLastError();

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_IGNORE_INSERTS
			| FORMAT_MESSAGE_FROM_SYSTEM,
			NULL, error_code, 0,
			(LPTSTR) &buf, 0, NULL);

	if (buf) {
		dl_error_set(e, buf);
		LocalFree(buf);
	} else {
		dl_error_set_printf(e, "Unknown error %d", (int) error_code);
	}
}

dl_file_t *dl_file_open(const char *filename, dl_error_t *e)
{
	char *buf = NULL;
	dl_file_t *dm = NULL;
	dl_file_t dm_st;

	buf = convert_to_path_if_not(filename);
	dm_st.handle = LoadLibrary(buf ? buf : filename);
	wget_xfree(buf);

	if (dm_st.handle)
		dm = wget_memdup(&dm_st, sizeof(dl_file_t));
	else
		dl_win32_set_last_error(e);

	return dm;
}

void *dl_file_lookup(dl_file_t *dm, const char *symbol, dl_error_t *e)
{
	void *res = GetProcAddress(dm->handle, symbol);
	if (! res)
		dl_win32_set_last_error(e);
	return res;
}

void dl_file_close(dl_file_t *dm)
{
	FreeLibrary(dm->handle);
	wget_free(dm);
}

#else

const static char *dl_unsupported
	= "Dynamic loading is not supported on the current platform.";

int dl_supported()
{
	return 0;
}

dl_file_t *dl_file_open(const char *filename, dl_error_t *e)
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
const static char *dl_prefixes[] = {"lib", "", NULL};
const static char *dl_suffixes[] = {".dll", NULL};
#elif defined __APPLE__
const static char *dl_prefixes[] = {"lib", NULL};
const static char *dl_suffixes[] = {".so", ".bundle", ".dylib", NULL};
#else
const static char *dl_prefixes[] = {"lib", NULL};
const static char *dl_suffixes[] = {".so", NULL};
#endif

//Matches the given path with the patterns of a loadable object file
//and returns a range to use as a name
static int dl_match(const char *path, size_t *start_out, size_t *len_out)
{
	size_t i, mark;
	size_t start, len;
	int match = 1;

	//Strip everything but the filename
	mark = 0;
	for (i = 0; path[i]; i++) {
		if (path[i] == '/')
			mark = i + 1;
#ifdef _WIN32
		if (path[i] == '\\')
			mark = i + 1;
#endif //_WIN32
	}
	start = mark;
	len = i - start;

	//Match for and remove the suffix
	for (i = 0; dl_suffixes[i]; i++) {
		size_t l = strlen(dl_suffixes[i]);
		if (l >= len)
			continue;
		if (memcmp(path + start + len - l, dl_suffixes[i], l) == 0) {
			len -= l;
			break;
		}
	}
	if (! dl_suffixes[i])
		match = 0;

	//Match for and remove the prefix
	for (i = 0; dl_prefixes[i]; i++) {
		size_t l = strlen(dl_prefixes[i]);
		if (l >= len)
			continue;
		if (memcmp(path + start, dl_prefixes[i], l) == 0) {
			start += l;
			len -= l;
			break;
		}
	}
	if (! dl_prefixes[i])
		match = 0;

	*start_out = start;
	*len_out = len;
	return match;
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

char *dl_get_name_from_path(const char *path, int strict)
{
	size_t start, len;
	int match = dl_match(path, &start, &len);

	if (!match && strict)
		return NULL;
	else
		return wget_strmemdup(path + start, len);
}

char *dl_search(const char *name, char **dirs, size_t n_dirs)
{
	int i;
	char *res = NULL;

	for (i = 0; res == NULL && i < n_dirs; i++) {
		struct dirent *ent;
		DIR *dirp = opendir(dirs[i]);
		if (! dirp)
			continue;
		while ((ent = readdir(dirp)) != NULL) {
			size_t start, len;
			char *filename;

			//Check whether the filename matches the pattern
			if (! dl_match(ent->d_name, &start, &len))
				continue;
			if (strlen(name) != len)
				continue;
			if (memcmp(ent->d_name + start, name, len) != 0)
				continue;

			//Check whether it is a regular file
			if (dirs[i] && *dirs[i]) {
				filename = wget_aprintf
					("%s/%s", dirs[i], ent->d_name);
			} else {
				filename = wget_aprintf("%s", ent->d_name);
			}
			if (! is_regular_file(filename)) {
				wget_free(filename);
				continue;
			}
			res = filename;
			break;
		}
		closedir(dirp);
	}
	return res;
}

int dl_list(const char *dir, char ***names_out, size_t *n_names_out)
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
		char *name;
		if ((ent = readdir(dirp)) == NULL)
			break;

		fname = ent->d_name;

		//Ignore entries that don't match the pattern
		name = dl_get_name_from_path(fname, 1);
		if (! name)
			continue;

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
	*n_names_out = buf->length / sizeof(void *);
	wget_buffer_deinit(buf);

	return 0;
}
