/*
 * Copyright(c) 2017 Free Software Foundation, Inc.
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
 */

#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wget.h"
#include "fuzzer.h"

static void _cb(void *context, int flags, const char *tag, const char *attr, const char *val, size_t len, size_t pos G_GNUC_WGET_UNUSED)
{

}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 10000) // same as max_len = 10000 in .options file
		return 0;

	char *in = (char *) malloc(size + 1);

	assert(in != NULL);

	// 0 terminate
	memcpy(in, data, size);
	in[size] = 0;

	wget_xml_parse_buffer(in, NULL, NULL, 0);
	wget_xml_parse_buffer(in, _cb, NULL, XML_HINT_REMOVE_EMPTY_CONTENT);
	wget_html_parse_buffer(in, _cb, NULL, 0);
	wget_html_parse_buffer(in, _cb, NULL, XML_HINT_REMOVE_EMPTY_CONTENT);

	free(in);

	return 0;
}
