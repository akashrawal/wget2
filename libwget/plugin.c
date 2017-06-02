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
 * Plugin support
 *
 */

#include <config.h>

#include <wget.h>
#include "private.h"

/**Registers a function to be called when wget exits.
* \param[in] plugin Used to identify the plugin
* \param[in] fn A function pointer to be called
*/
void wget_plugin_register_finalizer
		(wget_plugin_t *plugin, wget_plugin_finalizer_t fn)
{
	(* plugin->vtable->register_finalizer)(plugin, fn);
}
