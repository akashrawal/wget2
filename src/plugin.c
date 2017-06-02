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
 * Plugin support implementation
 *
 */

#include <config.h>

#include <wget.h>

#include "wget_dl.h"
#include "wget_plugin.h"

//Name of the initializer function
const static char *init_fn_name = "wget_plugin_initializer";

//Pointer array manipulation functions on wget_buffer_t
static inline size_t ptr_array_size(wget_buffer_t *buf)
{
	return buf->length / sizeof(void *);
}
static inline void ptr_array_append(wget_buffer_t *buf, void *ent)
{
	wget_buffer_memcat(buf, (void *) &ent, sizeof(void *));
}

//Plugin search paths
wget_buffer_t search_paths[1];
wget_buffer_t plugin_list[1];
static int initialized;

//Initializes buffer objects if not already
void plugin_db_init()
{
	if (! initialized) {
		wget_buffer_init(search_paths, NULL, 0);
		wget_buffer_init(plugin_list, NULL, 0);
		initialized = 1;
	}
}

//Sets a list of directories to search for plugins, separated by
//_separator_.
void plugin_db_add_search_paths(const char *paths, char separator)
{
	int i, mark;

	mark = 0;
	for (i = 0; paths[i]; i++) {
		if (paths[i] == separator) {
			if (i > mark) {
				ptr_array_append(search_paths,
					wget_strmemdup(paths + mark, i - mark));
			}
			mark = i + 1;
		}
	}
}

//Clears list of directories to search for plugins
void plugin_db_clear_search_paths()
{
	int i;
	char **paths;
	size_t paths_len;

	paths = (char **) search_paths->data;
	paths_len = ptr_array_size(search_paths);

	for (i = 0; i < paths_len; i++)
		wget_free(paths[i]);

	wget_buffer_memset(search_paths, 0, 0);
}

//vtable
static void impl_register_finalizer
	(wget_plugin_t *p_plugin, wget_plugin_finalizer_t fn)
{
	plugin_t *plugin = (plugin_t *) p_plugin;

	plugin->finalizer = fn;
}

static struct wget_plugin_vtable vtable = {
	.register_finalizer = impl_register_finalizer
};

static void plugin_free(plugin_t *plugin)
{
	dl_file_close(plugin->dm);
	wget_free(plugin->name);
	wget_free(plugin);
}

static plugin_t *load_plugin_internal
	(const char *name, const char *path, dl_error_t *e)
{
	dl_file_t *dm;
	plugin_t *plugin;
	wget_plugin_initializer_t init_fn;

	dm = dl_file_open(path, e);
	if (! dm)
		return NULL;

	//Create plugin object
	plugin = wget_malloc(sizeof(plugin_t));
	plugin->parent.plugin_data = NULL;
	plugin->parent.vtable = &vtable;
	plugin->name = wget_strdup(name);
	plugin->dm = dm;
	plugin->finalizer = NULL;

	//Call initializer
	init_fn = dl_file_lookup(dm, init_fn_name, e);
	if (! init_fn) {
		plugin_free(plugin);
		return NULL;
	}
	if ((* init_fn)((wget_plugin_t *) plugin) != 0) {
		dl_error_set(e, "Plugin failed to initialize");
		plugin_free(plugin);
		return NULL;
	}

	//Add to plugin list
	ptr_array_append(plugin_list, (void *) plugin);

	return plugin;
}

plugin_t *plugin_db_load_from_path(const char *path, dl_error_t *e)
{
	char *name = dl_get_name_from_path(path, 0);
	plugin_t *plugin = load_plugin_internal(name, path, e);
	free(name);
	return plugin;
}

plugin_t *plugin_db_load_from_name(const char *name, dl_error_t *e)
{
	//Search where the plugin is
	char **dirs = (char **) search_paths->data;
	size_t n_dirs = ptr_array_size(search_paths);
	plugin_t *plugin;

	char *filename = dl_search(name, dirs, n_dirs);
	if (! filename) {
		dl_error_set_printf(e, "Plugin %s not found in any of the "
				"plugin search paths.",
				name);
		return NULL;
	}

	//Delegate
	plugin = load_plugin_internal(name, filename, e);
	wget_free(filename);
	return plugin;
}

void plugin_db_finalize(int exitcode)
{
	int i;
	plugin_t **plugins = (plugin_t **) plugin_list->data;
	size_t n_plugins = ptr_array_size(plugin_list);

	for (i = 0; i < n_plugins; i++) {
		if (plugins[i]->finalizer)
			(* plugins[i]->finalizer)
				((wget_plugin_t *) plugins[i], exitcode);
		plugin_free(plugins[i]);
	}
	wget_buffer_memset(plugin_list, 0, 0);
}
