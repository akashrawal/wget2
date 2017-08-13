/*
 * Copyright(c) 2015-2017 Free Software Foundation, Inc.
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
 * HTTP Public Key Pinning
 *
 */

#include <config.h>

#include <wget.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/stat.h>
#include <limits.h>
#include "private.h"

typedef struct {
	wget_hpkp_db_t parent;

	char *
		fname;
	wget_hashmap_t *
		entries;
	wget_thread_mutex_t
		mutex;
	int64_t
		load_time;
} _hpkp_db_impl_t;

struct _wget_hpkp_st {
	const char *
		host;
	int64_t
		expires;
	int64_t
		created;
	int64_t
		maxage;
	char
		include_subdomains;
	wget_vector_t *
		pins;
};

struct _wget_hpkp_pin_st {
	const char *
		pin_b64; /* base64 encoded <pin> */
	const void *
		pin; /* binary hash */
	const char *
		hash_type; /* type of <pin>, e.g. 'sha-256' */
	size_t
		pinsize; /* size of <pin> */
};
typedef struct _wget_hpkp_pin_st wget_hpkp_pin_t;

/**
 * \file
 * \brief HTTP Public Key Pinning (RFC 7469) routines
 * \defgroup libwget-hpkp HTTP Public Key Pinning (RFC 7469) routines
 * @{
 *
 * This is an implementation of RFC 7469.
 */

#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
static unsigned int G_GNUC_WGET_PURE _hash_hpkp(const wget_hpkp_t *hpkp)
{
	unsigned int hash = 0;
	const unsigned char *p;

	for (p = (unsigned char *)hpkp->host; *p; p++)
		hash = hash * 101 + *p; // possible integer overflow, suppression above

	return hash;
}

static int G_GNUC_WGET_NONNULL_ALL G_GNUC_WGET_PURE _compare_hpkp(const wget_hpkp_t *h1, const wget_hpkp_t *h2)
{
	return strcmp(h1->host, h2->host);
}

/*
 * Compare function for SPKI hashes. Returns 0 if they're equal.
 */
static int G_GNUC_WGET_NONNULL_ALL _compare_pin(wget_hpkp_pin_t *p1, wget_hpkp_pin_t *p2)
{
	int n;

	if ((n = strcmp(p1->hash_type, p2->hash_type)))
		return n;

	if (p1->pinsize < p2->pinsize)
		return -1;

	if (p1->pinsize > p2->pinsize)
		return 1;

	return memcmp(p1->pin, p2->pin, p1->pinsize);
}

static void _hpkp_pin_free(wget_hpkp_pin_t *pin)
{
	if (pin) {
		xfree(pin->hash_type);
		xfree(pin->pin);
		xfree(pin->pin_b64);
	}
}

/**
 * \param hpkp a HPKP database entry
 * \param pin_type The type of hash supplied, e.g. "sha256"
 * \param pin_b64 The public key hash in base64 format
 *
 * Adds a public key hash to HPKP database entry.
 */
void wget_hpkp_pin_add(wget_hpkp_t *hpkp, const char *pin_type, const char *pin_b64)
{
	wget_hpkp_pin_t *pin = xcalloc(1, sizeof(wget_hpkp_pin_t));
	size_t len_b64 = strlen(pin_b64);

	pin->hash_type = wget_strdup(pin_type);
	pin->pin_b64 = wget_strdup(pin_b64);
	pin->pin = (unsigned char *)wget_base64_decode_alloc(pin_b64, len_b64, &pin->pinsize);

	if (!hpkp->pins) {
		hpkp->pins = wget_vector_create(5, -2, (wget_vector_compare_t)_compare_pin);
		wget_vector_set_destructor(hpkp->pins, (wget_vector_destructor_t)_hpkp_pin_free);
	}

	wget_vector_add_noalloc(hpkp->pins, pin);
}

/**
 * Free hpkp_t instance created by wget_hpkp_new()
 * It can be used as destructor function in vectors and hashmaps.
 *
 * \param hpkp a HPKP database entry
 */
void wget_hpkp_free(wget_hpkp_t *hpkp)
{
	if (hpkp) {
		xfree(hpkp->host);
		wget_vector_free(&hpkp->pins);
		xfree(hpkp);
	}
}

/*
 * TODO HPKP: wget_hpkp_new() should get an IRI rather than a string, and check by itself
 * whether it is HTTPS, not an IP literal, etc.
 *
 * This is also applicable to HSTS.
 */
/**
 * \return A newly allocated and initialized HPKP structure
 *
 * Creates a new HPKP structure initialized with the given values.
 */
wget_hpkp_t *wget_hpkp_new(void)
{
	wget_hpkp_t *hpkp = xcalloc(1, sizeof(wget_hpkp_t));

	hpkp->created = time(NULL);

	return hpkp;
}

/**
 * \param hpkp a HPKP database entry
 * \param host Hostname of the web server
 *
 * Sets the hostname of the web server into given HPKP database entry.
 */
void wget_hpkp_set_host(wget_hpkp_t *hpkp, const char *host)
{
	xfree(hpkp->host);
	hpkp->host = wget_strdup(host);
}

/**
 * \param hpkp a HPKP database entry
 * \param maxage Maximum time the entry is valid (in seconds)
 *
 * Sets the maximum time the HPKP entry is valid.
 * Corresponds to 'max-age' directive in 'Public-Key-Pins' HTTP response header.
 */
void wget_hpkp_set_maxage(wget_hpkp_t *hpkp, time_t maxage)
{
	int64_t now;

	// avoid integer overflow here
	if (maxage <= 0 || maxage >= INT64_MAX / 2 || (now = time(NULL)) < 0 || now >= INT64_MAX / 2) {
		hpkp->maxage = 0;
		hpkp->expires = 0;
	} else {
		hpkp->maxage = maxage;
		hpkp->expires = now + maxage;
	}
}

/**
 * \param hpkp a HPKP database entry
 * \param include_subdomains Nonzero if this entry is also valid for all subdomains, zero otherwise.
 *
 * Sets whether the entry is also valid for all subdomains. 
 * Corresponds to the optional 'includeSubDomains' directive in 'Public-Key-Pins' HTTP response header.
 */
void wget_hpkp_set_include_subdomains(wget_hpkp_t *hpkp, int include_subdomains)
{
	hpkp->include_subdomains = !!include_subdomains;
}

//TODO: Set parameter directions (for the entire file)
/**
 * \param hpkp a HPKP database entry
 * \return The number of public key hashes added.
 *
 * Gets the number of public key hashes added to the given HPKP database entry.
 */
size_t wget_hpkp_get_n_pins(wget_hpkp_t *hpkp)
{
	return (size_t) wget_vector_size(hpkp->pins);
}

/**
 * \param hpkp a HPKP database entry
 * \param[out] pin_types An array of pointers where hash types will be stored.
 * \param[out] pins_b64 An array of pointers where the public keys in base64 format will be stored
 *
 * Gets all the public key hashes added to the given HPKP database entry.
 *
 * The size of the arrays used must be atleast one returned by \ref wget_hpkp_get_n_pins "wget_hpkp_get_n_pins()".
 */
void wget_hpkp_get_pins_b64(wget_hpkp_t *hpkp, const char **pin_types, const char **pins_b64)
{
	int i, n_pins;

	n_pins = wget_vector_size(hpkp->pins);

	for (i = 0; i < n_pins; i++) {
		wget_hpkp_pin_t *pin = (wget_hpkp_pin_t *) wget_vector_get(hpkp->pins, i);
		pin_types[i] = pin->hash_type;
		pins_b64[i] = pin->pin_b64;
	}
}

/**
 * \param hpkp a HPKP database entry
 * \param[out] pin_types An array of pointers where hash types will be stored.
 * \param[out] pins An array of pointers where the public keys in binary format will be stored
 *
 * Gets all the public key hashes added to the given HPKP database entry.
 *
 * The size of the arrays used must be atleast one returned by \ref wget_hpkp_get_n_pins "wget_hpkp_get_n_pins()".
 */
void wget_hpkp_get_pins(wget_hpkp_t *hpkp, const char **pin_types, size_t *sizes, const void **pins)
{
	int i, n_pins;

	n_pins = wget_vector_size(hpkp->pins);

	for (i = 0; i < n_pins; i++) {
		wget_hpkp_pin_t *pin = (wget_hpkp_pin_t *) wget_vector_get(hpkp->pins, i);
		pin_types[i] = pin->hash_type;
		sizes[i] = pin->pinsize;
		pins[i] = pin->pin;
	}
}

/**
 * \param hpkp a HPKP database entry
 * \return The hostname this entry is valid for
 *
 * Gets the hostname this entry is valid for, as set by \ref wget_hpkp_set_host "wget_hpkp_set_host()"
 */
const char * wget_hpkp_get_host(wget_hpkp_t *hpkp)
{
	return hpkp->host;
}

/**
 * \param hpkp a HPKP database entry
 * \return The maximum time (in seconds) the entry is valid
 *
 * Gets the maximum time this entry is valid for, as set by \ref wget_hpkp_set_maxage "wget_hpkp_set_maxage()"
 */
time_t wget_hpkp_get_maxage(wget_hpkp_t *hpkp)
{
	return hpkp->maxage;
}

/**
 * \param hpkp a HPKP database entry
 * \return 1 if the HPKP entry is also valid for all subdomains, 0 otherwise
 *
 * Gets whether the HPKP database entry is also valid for the subdomains.
 */
int wget_hpkp_get_include_subdomains(wget_hpkp_t *hpkp)
{
	return hpkp->include_subdomains;
}

/*
 * TODO: Fix parameter names
 *       It was easier when all documentation is done in the headers!
 */
/**
 * \param[in] hpkp_db Pointer to the pointer of an HPKP database, provided by wget_hpkp_db_init()
 *
 * Closes and frees the HPKP database, except for the structure.
 * `hpkp_db` can then be passed to \ref wget_hpkp_db_init "wget_hpkp_db_init()".
 */
void wget_hpkp_db_deinit(wget_hpkp_db_t *p_hpkp_db)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	if (hpkp_db) {
		xfree(hpkp_db->fname);
		wget_thread_mutex_lock(&hpkp_db->mutex);
		wget_hashmap_free(&hpkp_db->entries);
		wget_thread_mutex_unlock(&hpkp_db->mutex);
	}
}

/**
 * \param[in] hpkp_db Pointer to the pointer of an HPKP database, provided by wget_hpkp_db_init()
 *
 * Closes and frees the HPKP database. A double pointer is required because this function will
 * set the handle (pointer) to the HPKP database to NULL to prevent potential use-after-free conditions.
 */
void wget_hpkp_db_free(wget_hpkp_db_t **p_hpkp_db)
{
	if (p_hpkp_db) {
		(*(*p_hpkp_db)->vtable->free)(*p_hpkp_db);
		*p_hpkp_db = NULL;
	}
}
static void impl_hpkp_db_free(wget_hpkp_db_t *p_hpkp_db)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	if (hpkp_db) {
		wget_hpkp_db_deinit(p_hpkp_db);
		xfree(hpkp_db);
	}
}

/*
static int _wget_hpkp_contains_pin(wget_hpkp_t *hpkp, wget_hpkp_pin_t *pin)
{
	return !wget_vector_contains(hpkp->pins, pin);
}

static int _wget_hpkp_compare_pins(wget_hpkp_t *hpkp1, wget_hpkp_t *hpkp2)
{
	return wget_vector_browse(hpkp1->pins, (wget_vector_browse_t)_wget_hpkp_contains_pin, hpkp2);
}
*/

// TODO: Shouldn't the return values be put into enum?
/**
 * \param[in] hpkp_db a HPKP database
 * \param[in] host The hostname in question.
 * \param[in] pubkey The public key
 * \param[in] pubkeysize Size of `pubkey`
 * \return  1 if both host and public key was found in the database,
 *         <0 if host was found and public key was not found,
 *          0 if host was not found.
 *
 * Checks the validity of the given hostname and public key combination.
 */
int wget_hpkp_db_check_pubkey(wget_hpkp_db_t *p_hpkp_db, const char *host, const void *pubkey, size_t pubkeysize)
{
	return (*p_hpkp_db->vtable->check_pubkey)(p_hpkp_db, host, pubkey, pubkeysize);
}
static int impl_hpkp_db_check_pubkey(wget_hpkp_db_t *p_hpkp_db, const char *host, const void *pubkey, size_t pubkeysize)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	wget_hpkp_t key;
	wget_hpkp_t *hpkp = NULL;
	char digest[wget_hash_get_len(WGET_DIGTYPE_SHA256)];
	int subdomain = 0;

	for (const char *domain = host; *domain && !hpkp; domain = strchrnul(domain, '.')) {
		while (*domain == '.')
			domain++;

		key.host = domain;
		hpkp = wget_hashmap_get(hpkp_db->entries, &key);

		if (!hpkp)
			subdomain = 1;
	}

	if (!hpkp)
		return 0; // OK, host is not in database

	if (subdomain && !hpkp->include_subdomains)
		return 0; // OK, found a matching super domain which isn't responsible for <host>

	if (wget_hash_fast(WGET_DIGTYPE_SHA256, pubkey, pubkeysize, digest))
		return -1;

	wget_hpkp_pin_t pinkey = { .pin = digest, .pinsize = sizeof(digest), .hash_type = "sha256" };

	if (wget_vector_find(hpkp->pins, &pinkey) != -1)
		return 1; // OK, pinned pubkey found

	return -2;
}

/* We 'consume' _hpkp and thus set *_hpkp to NULL, so that the calling function
 * can't access it any more */
/**
 * TODO: Fix parameter name
 * \param[in] hpkp_db a HPKP database
 * \param[in] hpkp pointer to HPKP database entry (will be set to NULL)
 *
 * Adds an entry to given HPKP database.
 * The database takes the ownership of the HPKP entry and the calling function must not access the entry afterwards.
 */
void wget_hpkp_db_add(wget_hpkp_db_t *p_hpkp_db, wget_hpkp_t **_hpkp)
{
	(*p_hpkp_db->vtable->add)(p_hpkp_db, *_hpkp);
	*_hpkp = NULL;
}
static void impl_hpkp_db_add(wget_hpkp_db_t *p_hpkp_db, wget_hpkp_t *hpkp)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	if (!hpkp)
		return;

	wget_thread_mutex_lock(&hpkp_db->mutex);

	if (hpkp->maxage == 0 || wget_vector_size(hpkp->pins) == 0) {
		if (wget_hashmap_remove(hpkp_db->entries, hpkp))
			debug_printf("removed HPKP %s\n", hpkp->host);
		wget_hpkp_free(hpkp);
	} else {
		wget_hpkp_t *old = wget_hashmap_get(hpkp_db->entries, hpkp);

		if (old) {
			old->created = hpkp->created;
			old->maxage = hpkp->maxage;
			old->expires = hpkp->expires;
			old->include_subdomains = hpkp->include_subdomains;
			wget_vector_free(&old->pins);
			old->pins = hpkp->pins;
			hpkp->pins = NULL;
			debug_printf("update HPKP %s (maxage=%lld, includeSubDomains=%d)\n", old->host, (long long)old->maxage, old->include_subdomains);
			wget_hpkp_free(hpkp);
		} else {
			// key and value are the same to make wget_hashmap_get() return old 'hpkp'
			/* debug_printf("add HPKP %s (maxage=%lld, includeSubDomains=%d)\n", hpkp->host, (long long)hpkp->maxage, hpkp->include_subdomains); */
			wget_hashmap_put_noalloc(hpkp_db->entries, hpkp, hpkp);
			// no need to free anything here
		}
	}

	wget_thread_mutex_unlock(&hpkp_db->mutex);
}

static int _hpkp_db_load(_hpkp_db_impl_t *hpkp_db, FILE *fp)
{
	int64_t created, max_age;
	long long _created, _max_age;
	int include_subdomains;

	wget_hpkp_t *hpkp = NULL;
	struct stat st;
	char *buf = NULL, *linep;
	size_t bufsize = 0;
	ssize_t buflen;
	char hash_type[32], host[256], pin_b64[256];
	int64_t now = time(NULL);

	// if the database file hasn't changed since the last read
	// there's no need to reload

	if (fstat(fileno(fp), &st) == 0) {
		if (st.st_mtime != hpkp_db->load_time)
			hpkp_db->load_time = st.st_mtime;
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

		if (*linep != '*') {
			wget_hpkp_db_add((wget_hpkp_db_t *) hpkp_db, &hpkp);

			if (sscanf(linep, "%255s %d %lld %lld", host, &include_subdomains, &_created, &_max_age) == 4) {
				created = _created;
				max_age = _max_age;
				if (created < 0 || max_age < 0 || created >= INT64_MAX / 2 || max_age >= INT64_MAX / 2) {
					max_age = 0; // avoid integer overflow here
				}
				int64_t expires = created + max_age;
				if (max_age && expires >= now) {
					hpkp = wget_hpkp_new();
					hpkp->host = wget_strdup(host);
					hpkp->maxage = max_age;
					hpkp->created = created;
					hpkp->expires = expires;
					hpkp->include_subdomains = !!include_subdomains;
				} else
					debug_printf("HPKP: entry '%s' is expired\n", host);
			} else {
				error_printf("HPKP: could not parse host line '%s'\n", buf);
			}
		} else if (hpkp) {
			if (sscanf(linep, "*%31s %255s", hash_type, pin_b64) == 2) {
				wget_hpkp_pin_add(hpkp, hash_type, pin_b64);
			} else {
				error_printf("HPKP: could not parse pin line '%s'\n", buf);
			}
		} else {
			debug_printf("HPKP: skipping PIN entry: '%s'\n", buf);
		}
	}

	wget_hpkp_db_add((wget_hpkp_db_t *) hpkp_db, &hpkp);

	xfree(buf);

	if (ferror(fp)) {
		hpkp_db->load_time = 0; // reload on next call to this function
		return -1;
	}

	return 0;
}

/**
 * \param[in] hpkp_db Handle to an HPKP database, obtained with wget_hpkp_db_init()
 * \param[in] fname Path to a file
 * \return WGET_HPKP_OK on success, or a negative number on error
 *
 * Reads the file specified by `filename` and loads its contents into the HPKP database
 * provided by `hpkp_db`.
 *
 * If this function cannot correctly parse the whole file, `WGET_HPKP_ERROR` is returned.
 * Since the format of the file might change without notice, hand-crafted files are discouraged.
 * To create an HPKP database file that is guaranteed to be correctly parsed by this function,
 * wget_hpkp_db_save() should be used.
 *
 * The entries in the file are subject to sanity checks as if they were added to the HPKP database
 * via wget_hpkp_db_add(). In particular, if an entry is expired due to `creation_time + max_age > cur_time`
 * it will not be added to the database, and a subsequent call to wget_hpkp_db_save() with the same `hpkp_db` handle
 * and file name will overwrite the file without all the expired entries. Thus, if all the entries in the file are
 * expired, the database will be empty and a subsequent call to wget_hpkp_db_save() with the same parameters will
 * cause the file to be deleted.
 *
 * If the file cannot be opened for writing `WGET_HPKP_ERROR_FILE_OPEN` is returned,
 * or `WGET_HPKP_ERROR` in any other case.
 */
int wget_hpkp_db_load(wget_hpkp_db_t *p_hpkp_db)
{
	return (*p_hpkp_db->vtable->load)(p_hpkp_db);
}
static int impl_hpkp_db_load(wget_hpkp_db_t *p_hpkp_db)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	if (!hpkp_db || !hpkp_db->fname || !*hpkp_db->fname)
		return 0;

	if (wget_update_file(hpkp_db->fname, (wget_update_load_t)_hpkp_db_load, NULL, hpkp_db)) {
		error_printf(_("Failed to read HPKP data\n"));
		return -1;
	} else {
		debug_printf("Fetched HPKP data from '%s'\n", hpkp_db->fname);
		return 0;
	}
}

static int _hpkp_save_pin(FILE *fp, wget_hpkp_pin_t *pin)
{
	fprintf(fp, "*%s %s\n", pin->hash_type, pin->pin_b64);

	if (ferror(fp))
		return -1;

	return 0;
}

static int G_GNUC_WGET_NONNULL_ALL _hpkp_save(FILE *fp, const wget_hpkp_t *hpkp)
{
	if (wget_vector_size(hpkp->pins) == 0)
		debug_printf("HPKP: drop '%s', no PIN entries\n", hpkp->host);
	else if (hpkp->expires < time(NULL))
		debug_printf("HPKP: drop '%s', expired\n", hpkp->host);
	else {
		fprintf(fp, "%s %d %lld %lld\n", hpkp->host, hpkp->include_subdomains, (long long)hpkp->created, (long long)hpkp->maxage);

		if (ferror(fp))
			return -1;

		return wget_vector_browse(hpkp->pins, (wget_vector_browse_t)_hpkp_save_pin, fp);
	}

	return 0;
}

static int _hpkp_db_save(_hpkp_db_impl_t *hpkp_db, FILE *fp)
{
	wget_hashmap_t *entries = hpkp_db->entries;

	if (wget_hashmap_size(entries) > 0) {
		fputs("# HPKP 1.0 file\n", fp);
		fputs("#Generated by Wget2 " PACKAGE_VERSION ". Edit at your own risk.\n", fp);
		fputs("#<hostname> <incl. subdomains> <created> <max-age>\n\n", fp);

		if (ferror(fp))
			return -1;

		return wget_hashmap_browse(entries, (wget_hashmap_browse_t)_hpkp_save, fp);
	}

	return 0;
}

/**
 * \param[in] hpkp_db Handle to an HPKP database, obtained with wget_hpkp_db_init()
 * \param[in] fname Path to a file
 * \return The number of SPKIs written to the file, or a negative number on error.
 *
 * Saves the current HPKP database to the specified file.
 *
 * The information will be stored in a human-readable format for inspection,
 * but it is discouraged to rely on it for external processing. In particular,
 * no application other than wget2 should modify the contents of the file
 * as the format might change between releases without notice.
 *
 * This function returns the number of SPKIs written to the file, which is effectively
 * equal to the number of SPKIs in the database when this function was called, and thus,
 * might be zero. If the file specified by `filename` exists, all its contents
 * will be overwritten with the current contents of the database. Otherwise, if the file exists
 * but there are no SPKIs in the database, the file will be deleted to avoid leaving an empty file.
 *
 * If the file cannot be opened for writing `WGET_HPKP_ERROR_FILE_OPEN` is returned, and
 * `WGET_HPKP_ERROR` in any other case.
 */
int wget_hpkp_db_save(wget_hpkp_db_t *p_hpkp_db)
{
	return (*p_hpkp_db->vtable->save)(p_hpkp_db);
}
static int impl_hpkp_db_save(wget_hpkp_db_t *p_hpkp_db)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	int size;

	if (!hpkp_db || !hpkp_db->fname || !*hpkp_db->fname)
		return -1;

	if (wget_update_file(hpkp_db->fname, (wget_update_load_t)_hpkp_db_load, (wget_update_load_t)_hpkp_db_save, hpkp_db)) {
		error_printf(_("Failed to write HPKP file '%s'\n"), hpkp_db->fname);
		return -1;
	}

	if ((size = wget_hashmap_size(hpkp_db->entries)))
		debug_printf("Saved %d HPKP entr%s into '%s'\n", size, size != 1 ? "ies" : "y", hpkp_db->fname);
	else
		debug_printf("No HPKP entries to save. Table is empty.\n");

	return 0;
}

static struct wget_hpkp_db_vtable vtable = {
	.load = impl_hpkp_db_load,
	.save = impl_hpkp_db_save,
	.free = impl_hpkp_db_free,
	.add = impl_hpkp_db_add,
	.check_pubkey = impl_hpkp_db_check_pubkey
};

/**
 * \return Handle (pointer) to an HPKP database
 *
 * Initializes a new HPKP database.
 */
wget_hpkp_db_t *wget_hpkp_db_init(wget_hpkp_db_t *p_hpkp_db, const char *fname)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	if (!hpkp_db)
		hpkp_db = xcalloc(1, sizeof(_hpkp_db_impl_t));
	else
		memset(hpkp_db, 0, sizeof(*hpkp_db));

	hpkp_db->parent.vtable = &vtable;
	if (fname)
		hpkp_db->fname = wget_strdup(fname);
	hpkp_db->entries = wget_hashmap_create(16, (wget_hashmap_hash_t)_hash_hpkp, (wget_hashmap_compare_t)_compare_hpkp);
	wget_hashmap_set_key_destructor(hpkp_db->entries, (wget_hashmap_key_destructor_t)wget_hpkp_free);

	/*
	 * Keys and values for the hashmap are 'hpkp' entries, so value == key.
	 * The hash function hashes hostname.
	 * The compare function compares hostname.
	 *
	 * Since the value == key, we just need the value destructor for freeing hashmap entries.
	 */

	wget_thread_mutex_init(&hpkp_db->mutex);

	return (wget_hpkp_db_t *) hpkp_db;
}

void wget_hpkp_db_set_fname(wget_hpkp_db_t *p_hpkp_db, const char *fname)
{
	_hpkp_db_impl_t *hpkp_db = (_hpkp_db_impl_t *) p_hpkp_db;

	xfree(hpkp_db->fname);
	if (fname)
		hpkp_db->fname = wget_strdup(fname);
}

/**@}*/
