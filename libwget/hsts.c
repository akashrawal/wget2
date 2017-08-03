/*
 * Copyright(c) 2014 Tim Ruehsen
 * Copyright(c) 2015-2016 Free Software Foundation, Inc.
 *
 * This file is part of libwget.
 *
 * Libwget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Libwget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libwget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * HSTS routines
 *
 * Changelog
 * 28.01.2014  Tim Ruehsen  created
 *
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <wget.h>
#include "private.h"

typedef struct {
	wget_hsts_db_t parent;

	char *
		fname;
	wget_hashmap_t *
		entries;
	wget_thread_mutex_t
		mutex;
	int64_t
		load_time;
} _hsts_db_impl_t;

typedef struct {
	const char *
		host;
	int64_t
		expires; // expiry time
	int64_t
		created; // creation time
	int64_t
		maxage; // max-age in seconds
	uint16_t
		port;
	unsigned char
		include_subdomains : 1; // whether or not subdomains are included
} wget_hsts_t;

#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
static unsigned int G_GNUC_WGET_PURE _hash_hsts(const wget_hsts_t *hsts)
{
	unsigned int hash = hsts->port;
	const unsigned char *p;

	for (p = (unsigned char *)hsts->host; *p; p++)
		hash = hash * 101 + *p;

	return hash;
}

static int G_GNUC_WGET_NONNULL_ALL G_GNUC_WGET_PURE _compare_hsts(const wget_hsts_t *h1, const wget_hsts_t *h2)
{
	int n;

	if ((n = strcmp(h1->host, h2->host)))
		return n;

	return h1->port < h2->port ? -1 : (h1->port > h2->port ? 1 : 0);
}

static wget_hsts_t *_init_hsts(wget_hsts_t *hsts)
{
	if (!hsts)
		hsts = xmalloc(sizeof(wget_hsts_t));

	memset(hsts, 0, sizeof(*hsts));
	hsts->created = time(NULL);

	return hsts;
}

static void _deinit_hsts(wget_hsts_t *hsts)
{
	if (hsts) {
		xfree(hsts->host);
	}
}

static void _free_hsts(wget_hsts_t *hsts)
{
	if (hsts) {
		_deinit_hsts(hsts);
		xfree(hsts);
	}
}

static wget_hsts_t *_new_hsts(const char *host, uint16_t port, time_t maxage, int include_subdomains)
{
	wget_hsts_t *hsts = _init_hsts(NULL);

	hsts->host = wget_strdup(host);
	hsts->port = port ? port : 443;
	hsts->include_subdomains = !!include_subdomains;

	if (maxage <= 0 || maxage >= INT64_MAX / 2 || hsts->created < 0 || hsts->created >= INT64_MAX / 2) {
		hsts->maxage = 0;
		hsts->expires = 0;
	} else {
		hsts->maxage = maxage;
		hsts->expires = hsts->created + maxage;
	}

	return hsts;
}

int wget_hsts_host_match(const wget_hsts_db_t *p_hsts_db, const char *host, uint16_t port)
{
	return (* p_hsts_db->vtable->host_match)(p_hsts_db, host, port);
}
static int impl_hsts_db_host_match(const wget_hsts_db_t *p_hsts_db, const char *host, uint16_t port)
{
	_hsts_db_impl_t *hsts_db = (_hsts_db_impl_t *) p_hsts_db;

	wget_hsts_t hsts, *hstsp;
	const char *p;
	int64_t now = time(NULL);

	// first look for an exact match
	// if it's the default port, "normalize" it
	// we assume the scheme is HTTP
	hsts.port = (port == 80 ? 443 : port);
	hsts.host = host;
	if ((hstsp = wget_hashmap_get(hsts_db->entries, &hsts)) && hstsp->expires >= now)
		return 1;

	// now look for a valid subdomain match
	for (p = host; (p = strchr(p, '.')); ) {
		hsts.host = ++p;
		if ((hstsp = wget_hashmap_get(hsts_db->entries, &hsts)) && hstsp->include_subdomains && hstsp->expires >= now)
			return 1;
	}

	return 0;
}


void wget_hsts_db_set_fname(wget_hsts_db_t *p_hsts_db, const char *fname)
{
	_hsts_db_impl_t *hsts_db = (_hsts_db_impl_t *) p_hsts_db;

	xfree(hsts_db->fname);
	if (fname)
		hsts_db->fname = wget_strdup(fname);
}

//TODO: Where does this fit in virtualization?
void wget_hsts_db_deinit(wget_hsts_db_t *p_hsts_db)
{
	_hsts_db_impl_t *hsts_db = (_hsts_db_impl_t *) p_hsts_db;

	if (hsts_db) {
		xfree(hsts_db->fname);
		wget_thread_mutex_lock(&hsts_db->mutex);
		wget_hashmap_free(&hsts_db->entries);
		wget_thread_mutex_unlock(&hsts_db->mutex);
	}
}

void wget_hsts_db_free(wget_hsts_db_t **hsts_db)
{
	if (hsts_db) {
		(* (*hsts_db)->vtable->free)(*hsts_db);
		*hsts_db = NULL;
	}
}
static void impl_hsts_db_free(wget_hsts_db_t *hsts_db)
{
	if (hsts_db) {
		wget_hsts_db_deinit(hsts_db);
		xfree(hsts_db);
	}
}

static void _hsts_db_add_entry(_hsts_db_impl_t *hsts_db, wget_hsts_t *hsts)
{
	wget_thread_mutex_lock(&hsts_db->mutex);

	if (hsts->maxage == 0) {
		if (wget_hashmap_remove(hsts_db->entries, hsts))
			debug_printf("removed HSTS %s:%hu\n", hsts->host, hsts->port);
		_free_hsts(hsts);
		hsts = NULL;
	} else {
		wget_hsts_t *old = wget_hashmap_get(hsts_db->entries, hsts);

		if (old) {
			if (old->created < hsts->created || old->maxage != hsts->maxage || old->include_subdomains != hsts->include_subdomains) {
				old->created = hsts->created;
				old->expires = hsts->expires;
				old->maxage = hsts->maxage;
				old->include_subdomains = hsts->include_subdomains;
				debug_printf("update HSTS %s:%hu (maxage=%lld, includeSubDomains=%d)\n", old->host, old->port, (long long)old->maxage, old->include_subdomains);
			}
			_free_hsts(hsts);
			hsts = NULL;
		} else {
			// key and value are the same to make wget_hashmap_get() return old 'hsts'
			// debug_printf("add HSTS %s:%hu (maxage=%lld, includeSubDomains=%d)\n", hsts->host, hsts->port, (long long)hsts->maxage, hsts->include_subdomains);
			wget_hashmap_put_noalloc(hsts_db->entries, hsts, hsts);
			// no need to free anything here
		}
	}

	wget_thread_mutex_unlock(&hsts_db->mutex);
}

void wget_hsts_db_add(wget_hsts_db_t *p_hsts_db, const char *host, uint16_t port, time_t maxage, int include_subdomains)
{
	(* p_hsts_db->vtable->add)(p_hsts_db, host, port, maxage, include_subdomains);
}
static void impl_hsts_db_add(wget_hsts_db_t *p_hsts_db, const char *host, uint16_t port, time_t maxage, int include_subdomains)
{
	_hsts_db_impl_t *hsts_db = (_hsts_db_impl_t *) p_hsts_db;

	wget_hsts_t *hsts = _new_hsts(host, port, maxage, include_subdomains);

	_hsts_db_add_entry(hsts_db, hsts);
}

static int _hsts_db_load(_hsts_db_impl_t *hsts_db, FILE *fp)
{
	wget_hsts_t hsts;
	struct stat st;
	char *buf = NULL, *linep, *p;
	size_t bufsize = 0;
	ssize_t buflen;
	int64_t now = time(NULL);
	int ok;

	// if the database file hasn't changed since the last read
	// there's no need to reload

	if (fstat(fileno(fp), &st) == 0) {
		if (st.st_mtime != hsts_db->load_time)
			hsts_db->load_time = st.st_mtime;
		else
			return 0;
	}

	while ((buflen = wget_getline(&buf, &bufsize, fp)) >= 0) {
		linep = buf;

		while (isspace(*linep)) linep++; // ignore leading whitespace
		if (!*linep) continue; // skip empty lines

		if (*linep == '#')
			continue; // skip comments

		// strip off \r\n
		while (buflen > 0 && (buf[buflen] == '\n' || buf[buflen] == '\r'))
			buf[--buflen] = 0;

		_init_hsts(&hsts);
		ok = 0;

		// parse host
		if (*linep) {
			for (p = linep; *linep && !isspace(*linep); )
				linep++;
			hsts.host = wget_strmemdup(p, linep - p);
		}

		// parse port
		if (*linep) {
			for (p = ++linep; *linep && !isspace(*linep); )
				linep++;
			hsts.port = (uint16_t) atoi(p);
			if (hsts.port == 0)
				hsts.port = 443;
		}

		// parse includeSubDomains
		if (*linep) {
			for (p = ++linep; *linep && !isspace(*linep); )
				linep++;
			hsts.include_subdomains = atoi(p) ? 1 : 0;
		}

		// parse creation time
		if (*linep) {
			for (p = ++linep; *linep && !isspace(*linep); )
				linep++;
			hsts.created = atoll(p);
			if (hsts.created < 0 || hsts.created >= INT64_MAX / 2)
				hsts.created = 0;
		}

		// parse max age
		if (*linep) {
			for (p = ++linep; *linep && !isspace(*linep); )
				linep++;
			hsts.maxage = atoll(p);
			if (hsts.maxage < 0 || hsts.maxage >= INT64_MAX / 2)
				hsts.maxage = 0; // avoid integer overflow here
			hsts.expires = hsts.maxage ? hsts.created + hsts.maxage : 0;
			if (hsts.expires < now) {
				// drop expired entry
				_deinit_hsts(&hsts);
				continue;
			}
			ok = 1;
		}

		if (ok) {
			_hsts_db_add_entry(hsts_db, wget_memdup(&hsts, sizeof(hsts)));
		} else {
			_deinit_hsts(&hsts);
			error_printf(_("Failed to parse HSTS line: '%s'\n"), buf);
		}
	}

	xfree(buf);

	if (ferror(fp)) {
		hsts_db->load_time = 0; // reload on next call to this function
		return -1;
	}

	return 0;
}

// Load the HSTS cache from a flat file
// Protected by flock()

int wget_hsts_db_load(wget_hsts_db_t *p_hsts_db)
{
	return (* p_hsts_db->vtable->load)(p_hsts_db);
}
static int impl_hsts_db_load(wget_hsts_db_t *p_hsts_db)
{
	_hsts_db_impl_t *hsts_db = (_hsts_db_impl_t *) p_hsts_db;

	if (!hsts_db || !hsts_db->fname || !*hsts_db->fname)
		return 0;

	if (wget_update_file(hsts_db->fname, (wget_update_load_t)_hsts_db_load, NULL, hsts_db)) {
		error_printf(_("Failed to read HSTS data\n"));
		return -1;
	} else {
		debug_printf(_("Fetched HSTS data from '%s'\n"), hsts_db->fname);
		return 0;
	}
}

static int G_GNUC_WGET_NONNULL_ALL _hsts_save(FILE *fp, const wget_hsts_t *hsts)
{
	fprintf(fp, "%s %hu %d %lld %lld\n", hsts->host, hsts->port, hsts->include_subdomains, (long long)hsts->created, (long long)hsts->maxage);
	return 0;
}

static int _hsts_db_save(void *hsts_db, FILE *fp)
{
	wget_hashmap_t *entries = ((_hsts_db_impl_t *)hsts_db)->entries;

	if (wget_hashmap_size(entries) > 0) {
		fputs("#HSTS 1.0 file\n", fp);
		fputs("#Generated by Wget2 " PACKAGE_VERSION ". Edit at your own risk.\n", fp);
		fputs("# <hostname> <port> <incl. subdomains> <created> <max-age>\n", fp);

		wget_hashmap_browse(entries, (wget_hashmap_browse_t)_hsts_save, fp);

		if (ferror(fp))
			return -1;
	}

	return 0;
}

// Save the HSTS cache to a flat file
// Protected by flock()

int wget_hsts_db_save(wget_hsts_db_t *p_hsts_db)
{
	return (* p_hsts_db->vtable->save)(p_hsts_db);
}
static int impl_hsts_db_save(wget_hsts_db_t *p_hsts_db)
{
	_hsts_db_impl_t *hsts_db = (_hsts_db_impl_t *) p_hsts_db;

	int size;

	if (!hsts_db || !hsts_db->fname || !*hsts_db->fname)
		return -1;

	if (wget_update_file(hsts_db->fname, (wget_update_load_t)_hsts_db_load, _hsts_db_save, hsts_db)) {
		error_printf(_("Failed to write HSTS file '%s'\n"), hsts_db->fname);
		return -1;
	}

	if ((size = wget_hashmap_size(hsts_db->entries)))
		debug_printf(_("Saved %d HSTS entr%s into '%s'\n"), size, size != 1 ? "ies" : "y", hsts_db->fname);
	else
		debug_printf(_("No HSTS entries to save. Table is empty.\n"));

	return 0;
}

//vtable
static struct wget_hsts_db_vtable vtable = {
	.load = impl_hsts_db_load,
	.save = impl_hsts_db_save,
	.host_match = impl_hsts_db_host_match,
	.add = impl_hsts_db_add,
	.free = impl_hsts_db_free
};

/*
 * Constructor for the default implementation of HSTS database
 *
 * This function does no file IO, data is read only when \ref wget_hsts_db_load "wget_hsts_db_load()" is called.
 *
 * \param p_hsts_db TODO
 * \param fname The file where the data is stored, or NULL.
 * \return A new wget_hsts_db_t
 */
wget_hsts_db_t *wget_hsts_db_init(wget_hsts_db_t *p_hsts_db, const char *fname)
{
	_hsts_db_impl_t *hsts_db = (_hsts_db_impl_t *) p_hsts_db;

	if (!hsts_db)
		hsts_db = xmalloc(sizeof(_hsts_db_impl_t));

	memset(hsts_db, 0, sizeof(*hsts_db));
	hsts_db->parent.vtable = &vtable;
	if (fname)
		hsts_db->fname = wget_strdup(fname);
	hsts_db->entries = wget_hashmap_create(16, (wget_hashmap_hash_t)_hash_hsts, (wget_hashmap_compare_t)_compare_hsts);
	wget_hashmap_set_key_destructor(hsts_db->entries, (wget_hashmap_key_destructor_t)_free_hsts);
	wget_hashmap_set_value_destructor(hsts_db->entries, (wget_hashmap_value_destructor_t)_free_hsts);
	wget_thread_mutex_init(&hsts_db->mutex);

	return (wget_hsts_db_t *) hsts_db;
}
