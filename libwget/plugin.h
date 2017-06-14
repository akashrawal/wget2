/*
 * Copyright(c) 2017 Free Software Foundation, Inc.
 *
 * This file is part of libwget.
 *
 * Libwget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Libwget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Libwget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * Header file for project-private plugin support related definitions
 */

#ifndef _LIBWGET_PLUGIN_H
#define _LIBWGET_PLUGIN_H

struct wget_plugin_vtable
{
	const char * (* get_name)(wget_plugin_t *);
	void (* register_finalizer)(wget_plugin_t *, wget_plugin_finalizer_t);
	void (* register_argp)(wget_plugin_t *, wget_plugin_argp_t);
};

typedef struct
{
	wget_plugin_t parent;

	// Pointer to the vtable. Used by wget to implement functions.
	struct wget_plugin_vtable *vtable;
} wget_plugin_priv_t;

#define WGET_PLUGIN_VTABLE(plugin) (((wget_plugin_priv_t *) (plugin))->vtable)


#endif // _LIBWGET_PLUGIN_H
