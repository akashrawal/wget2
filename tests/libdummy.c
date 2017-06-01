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
 *
 *
 * Dummy libraries for testing dynamic loading abstraction
 *
 */

#include <config.h>

#include <string.h>

#define stringify(x) (#x)
#define fn_name(x) dl_test_fn_ ## x

void dl_test_write_param(char buf[16])
{
	strcpy(buf, stringify(PARAM));
}

void fn_name(PARAM)(char buf[16])
{
	dl_test_write_param(buf);
}
