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

#include <string.h>

#include <wget.h>

#include "wget_dl.h"
#include "wget_plugin.h"

//Strings
static const char *init_fn_name = "wget_plugin_initializer";
static const char *plugin_list_envvar = "WGET2_PLUGINS";

//Pointer array manipulation functions on wget_buffer_t
static inline size_t ptr_array_size(wget_buffer_t *buf)
{
	return buf->length / sizeof(void *);
}
static inline void ptr_array_append(wget_buffer_t *buf, void *ent)
{
	wget_buffer_memcat(buf, (void *) &ent, sizeof(void *));
}

//Splits string using the given separator and appends the array to buf.
static void split_string(const char *str, char separator, wget_buffer_t *buf)
{
	int i, mark;

	mark = 0;
	for (i = 0; str[i]; i++) {
		if (str[i] == separator) {
			if (i > mark) {
				ptr_array_append(buf,
					wget_strmemdup(str + mark, i - mark));
			}
			mark = i + 1;
		}
	}
	if (i > mark) {
		ptr_array_append(buf,
			wget_strmemdup(str + mark, i - mark));
	}
}

//Private members of the plugin
typedef struct {
	plugin_t parent;
	//Finalizer function, to be called when wget2 exits
	wget_plugin_finalizer_t finalizer;
	//The plugin's option processor
	wget_plugin_argp_t argp;
	//Buffer to store plugin name
	char name_buf[];
} plugin_priv_t;

static int initialized = 0;
//Plugin search paths
static wget_buffer_t search_paths[1];
//List of loaded plugins
static wget_buffer_t plugin_list[1];
//Index of plugins by plugin name
wget_stringmap_t *plugin_name_index;
//Whether any of the previous options forwarded was 'help'
int plugin_help_forwarded;


//Sets a list of directories to search for plugins, separated by
//_separator_.
void plugin_db_add_search_paths(const char *paths, char separator)
{
	split_string(paths, separator, search_paths);
}

//Clears list of directories to search for plugins
void plugin_db_clear_search_paths(void)
{
	size_t i;
	char **paths;
	size_t paths_len;

	paths = (char **) search_paths->data;
	paths_len = ptr_array_size(search_paths);

	for (i = 0; i < paths_len; i++)
		wget_free(paths[i]);

	wget_buffer_memset(search_paths, 0, 0);
}

//Searches for a given plugin by name.
//The name does not need to be null-terminated.
static plugin_t *_search_plugin(const char *name, size_t name_len)
{
	char buf[name_len + 1];
	memcpy(buf, name, name_len);
	buf[name_len] = 0;
	return (plugin_t *) wget_stringmap_get(plugin_name_index, buf);
}

//vtable
static void impl_register_finalizer
	(wget_plugin_t *p_plugin, wget_plugin_finalizer_t fn)
{
	plugin_priv_t *priv = (plugin_priv_t *) p_plugin;

	priv->finalizer = fn;
}

static const char *impl_get_name(wget_plugin_t *p_plugin)
{
	plugin_t *plugin = (plugin_t *) p_plugin;

	return plugin->name;
}

static void impl_register_argp
	(wget_plugin_t *p_plugin, wget_plugin_argp_t fn)
{
	plugin_priv_t *priv = (plugin_priv_t *) p_plugin;

	priv->argp = fn;
}

static struct wget_plugin_vtable vtable = {
	.get_name = impl_get_name,
	.register_finalizer = impl_register_finalizer,
	.register_argp = impl_register_argp
};

static void plugin_free(plugin_t *plugin)
{
	dl_file_close(plugin->dm);
	wget_free(plugin);
}

//Loads a plugin located at given path and assign it a name
static plugin_t *_load_plugin
	(const char *name, const char *path, dl_error_t *e)
{
	size_t name_len;
	dl_file_t *dm;
	plugin_t *plugin;
	plugin_priv_t *priv;
	wget_plugin_initializer_t init_fn;

	name_len = strlen(name);

	dm = dl_file_open(path, e);
	if (! dm)
		return NULL;

	//Create plugin object
	plugin = wget_malloc(sizeof(plugin_priv_t) + name_len + 1);

	//Initialize private members
	priv = (plugin_priv_t *) plugin;
	priv->finalizer = NULL;
	priv->argp = NULL;
	strcpy(priv->name_buf, name);

	//Initialize public members
	plugin->parent.plugin_data = NULL;
	plugin->parent.vtable = &vtable;
	plugin->name = priv->name_buf;
	plugin->dm = dm;

	//Call initializer
	*((void **)(&init_fn)) = dl_file_lookup(dm, init_fn_name, e);
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

	//Add to map
	wget_stringmap_put_noalloc(plugin_name_index, plugin->name, plugin);

	return plugin;
}

//Loads a plugin using its path. On failure it sets error and
//returns NULL.
plugin_t *plugin_db_load_from_path(const char *path, dl_error_t *e)
{
	char *name = dl_get_name_from_path(path, 0);
	plugin_t *plugin = _load_plugin(name, path, e);
	free(name);
	return plugin;
}

//Loads a plugin using its name. On failure it sets error and
//returns NULL.
plugin_t *plugin_db_load_from_name(const char *name, dl_error_t *e)
{
	//Search where the plugin is
	char **dirs = (char **) search_paths->data;
	size_t n_dirs = ptr_array_size(search_paths);
	plugin_t *plugin;

	char *filename = dl_search(name, dirs, n_dirs);
	if (! filename) {
		dl_error_set_printf(e, "Plugin '%s' not found in any of the "
				"plugin search paths.",
				name);
		return NULL;
	}

	//Delegate
	plugin = _load_plugin(name, filename, e);
	wget_free(filename);
	return plugin;
}

//Loads all plugins from environment variables. On any errors it
//logs them using wget_error_printf().
void plugin_db_load_from_envvar(void)
{
	dl_error_t e[1];
	wget_buffer_t buf[1];
	const char *str;
#ifdef _WIN32
	char sep = ';';
#else
	char sep = ':';
#endif

	//Fetch from environment variable
	str = getenv(plugin_list_envvar);

	if (str) {
		char **strings = NULL;
		size_t n_strings = 0, i;
		plugin_t *plugin;

		dl_error_init(e);

		//Split the value
		wget_buffer_init(buf, NULL, 0);
		split_string(str, sep, buf);
		strings = (char **) buf->data;
		n_strings = ptr_array_size(buf);

		//Load each plugin
		for (i = 0; i < n_strings; i++) {
			int local = 0;
			if (strchr(strings[i], '/'))
				local = 1;
#ifdef _WIN32
			if (strchr(strings[i], '\\'))
				local = 1;
#endif
			if (local)
				plugin = plugin_db_load_from_path(strings[i], e);
			else
				plugin = plugin_db_load_from_name(strings[i], e);

			if (! plugin) {
				wget_error_printf("Plugin '%s' failed to load: %s",
						strings[i], dl_error_get_msg(e));
				dl_error_set(e, NULL);
			}

			wget_free(strings[i]);
		}

		wget_buffer_deinit(buf);
	}
}

//Creates a list of all plugins found in plugin search paths.
void plugin_db_list(char ***names_out, size_t *n_names_out)
{
	char **paths = (char **) search_paths->data;
	size_t n_paths = ptr_array_size(search_paths);

	dl_list(paths, n_paths, names_out, n_names_out);
}

//Forwards a command line option to appropriate plugin.
int plugin_db_forward_option(const char *plugin_option, dl_error_t *e)
{
	const char *predicate;
	size_t plugin_name_len;
	plugin_t *plugin;
	plugin_priv_t *priv;
	size_t i;
	int op_res;

	//Get the plugin name
	for (i = 0; plugin_option[i] && plugin_option[i] != '.'; i++)
		;
	if (i == 0) {
		dl_error_set_printf(e, "'%s': Plugin name is missing.", plugin_option);
		return -1;
	}
	if (! plugin_option[i]) {
		dl_error_set_printf(e,
				"'%s': '.' is missing (separates plugin name and option)",
				plugin_option);
		return -1;
	}
	plugin_name_len = i;
	predicate = plugin_option + plugin_name_len + 1;

	//Search for plugin
	plugin = _search_plugin(plugin_option, plugin_name_len);
	if (! plugin) {
		char plugin_name[plugin_name_len + 1];
		memcpy(plugin_name, plugin_option, plugin_name_len);
		plugin_name[plugin_name_len] = 0;
		dl_error_set_printf(e, "Plugin '%s' is not loaded.",
				plugin_name);
		return -1;
	}
	priv = (plugin_priv_t *) plugin;

	if (! priv->argp) {
		dl_error_set_printf(e, "Plugin '%s' does not accept options.",
				plugin->name);
		return -1;
	}

	//Separate option from value
	for (i = 0; predicate[i] && predicate[i] != '='; i++)
		;
	if (i == 0) {
		dl_error_set_printf(e, "'%s': An option is required "
				"(after '.', and before '=' if present)",
				plugin_option);
		return -1;
	}
	if (predicate[i]) {
		size_t option_len = i;
		char option_name[option_len + 1];
		memcpy(option_name, predicate, option_len);
		option_name[option_len] = 0;

		if (strcmp(option_name, "help") == 0) {
			dl_error_set_printf(e, "'help' option does not "
					"accept arguments\n");
			return -1;
		}

		op_res = (* priv->argp)
			((wget_plugin_t *) plugin, option_name,
			 predicate + option_len + 1);
	} else {
		op_res = (* priv->argp)
			((wget_plugin_t *) plugin, predicate, NULL);
		if (strcmp(predicate, "help") == 0)
			plugin_help_forwarded = 1;
	}

	if (op_res < 0)
	{
		dl_error_set_printf(e, "Plugin '%s' did not accept option %s",
				plugin->name, predicate);
		return -1;
	}

	return 0;
}

//Shows help from all loaded plugins
void plugin_db_show_help(void)
{
	plugin_t **plugins = (plugin_t **) plugin_list->data;
	size_t n_plugins = ptr_array_size(plugin_list);
	size_t i;
	for (i = 0; i < n_plugins; i++) {
		plugin_priv_t *priv = (plugin_priv_t *) plugins[i];
		if (priv->argp) {
			printf("Options for %s:\n", plugins[i]->name);
			(* priv->argp)((wget_plugin_t *) plugins[i], "help", NULL);
			printf("\n");
		}
	}
	plugin_help_forwarded = 1;
}

//Returns 1 if any of the previous options forwarded was 'help'.
int plugin_db_help_forwarded(void)
{
	return plugin_help_forwarded;
}

//Initializes the plugin framework
void plugin_db_init(void)
{
	if (! initialized) {
		wget_buffer_init(search_paths, NULL, 0);
		wget_buffer_init(plugin_list, NULL, 0);
		plugin_name_index = wget_stringmap_create(16);
		wget_stringmap_set_key_destructor(plugin_name_index, NULL);
		wget_stringmap_set_value_destructor(plugin_name_index, NULL);
		plugin_help_forwarded = 0;

		initialized = 1;
	}
}

//Sends 'finalize' signal to all plugins and unloads all plugins
void plugin_db_finalize(int exitcode)
{
	size_t i;
	plugin_t **plugins = (plugin_t **) plugin_list->data;
	size_t n_plugins = ptr_array_size(plugin_list);

	for (i = 0; i < n_plugins; i++) {
		if (((plugin_priv_t *) plugins[i])->finalizer)
			(* ((plugin_priv_t *) plugins[i])->finalizer)
				((wget_plugin_t *) plugins[i], exitcode);
		plugin_free(plugins[i]);
	}
	wget_buffer_deinit(plugin_list);
	wget_stringmap_free(&plugin_name_index);
	char **paths = (char **) search_paths->data;
	size_t n_paths = ptr_array_size(search_paths);
	for (i = 0; i < n_paths; i++)
		wget_free(paths[i]);
	wget_buffer_deinit(search_paths);
	initialized = 0;
}
