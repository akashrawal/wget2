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

/**
 * \file
 * \brief Plugin API for wget2
 * \defgroup libwget-plugin Plugin API for wget2
 * @{
 *
 * This is the plugin API for wget2.
 *
 * Each plugin must define a `wget_plugin_initializer()` function which will be called when the plugin is loaded.
 * See \ref wget_plugin_initializer_t "wget_plugin_initializer_t" for the prototype.
 * `wget_plugin_initializer()` must also be declared to be exported using `WGET_EXPORT`.
 */

/**
 * Gets the name the plugin is known as.
 * \param[in] plugin The plugin handle
 * \return the name of this plugin. The returned string is owned by wget and should not be freed or altered.
 */
const char *wget_plugin_get_name(wget_plugin_t *plugin)
{
	return (* plugin->vtable->get_name)(plugin);
}

/**
 * Registers a function to be called when wget exits.
 * \param[in] plugin The plugin handle
 * \param[in] fn A function pointer to be called
 */
void wget_plugin_register_finalizer(wget_plugin_t *plugin, wget_plugin_finalizer_t fn)
{
	(* plugin->vtable->register_finalizer)(plugin, fn);
}

/**
 * Registers a function for command line option forwarding.
 *
 * A option can be forwarded using an option following the pattern:
 *
 *     --plugin-opt=<plugin-name>.<option>[=<value>]
 *
 * \param[in] plugin The plugin handle
 * \param[in] fn The function pointer to register
 */
void wget_plugin_register_argp(wget_plugin_t *plugin, wget_plugin_argp_t fn)
{
	(* plugin->vtable->register_argp)(plugin, fn);
}

/**
 * Marks the intercepted URL to be rejected. The URL will not be fetched by wget2 or passed to remaining plugins.
 *
 * Mutually exclusive with `wget_intercept_action_accept()`.
 *
 * \param action Handle for any action taken by the plugin
 */
void wget_intercept_action_reject(wget_intercept_action_t *action)
{
	(* action->vtable->action_reject)(action);
}

/**
 * Marks the intercepted URL to be accepted.
 * The URL will not be passed to remaining plugins. wget2 will not filter the URL by any accept or reject pattern.
 *
 * Mutually exclusive with `wget_intercept_action_reject()`.
 *
 * \param action Handle for any action taken by the plugin
 */
void wget_intercept_action_accept(wget_intercept_action_t *action)
{
	(* action->vtable->action_accept)(action);
}

/**
 * Specifies an alternative URL to be fetched instead of the intercepted URL.
 *
 * \param action Handle for any action taken by the plugin
 * \param iri Alternative URL to be fetched
 */
void wget_intercept_action_set_alt_url(wget_intercept_action_t *action, const wget_iri_t *iri)
{
	(* action->vtable->action_set_alt_url)(action, iri);
}

/**
 * Specifies that the fetched data from intercepted URL should be written to an alternative file.
 *
 * \param action Handle for any action taken by the plugin
 * \param local_filename Alternative file name to use
 */
void wget_intercept_action_set_local_filename(wget_intercept_action_t *action, const char *local_filename)
{
	(* action->vtable->action_set_local_filename)(action, local_filename);
}

/**
 * Registers a plugin function for intercepting URLs
 *
 * The registered function will be passed an abstract object of type
 * \ref wget_intercept_action_t "wget_intercept_action_t" which can be used to influence how wget will process the
 * URL.
 *
 * \see wget_intercept_action_reject
 * \see wget_intercept_action_accept
 * \see wget_intercept_action_set_alt_url
 * \see wget_intercept_action_set_local_filename
 *
 * \param[in] plugin The plugin handle
 * \param[in] filter_fn The plugin function that will be passed the URL to be fetched
 */
void wget_plugin_register_url_filter(wget_plugin_t *plugin, wget_plugin_url_filter_t filter_fn)
{
	(* plugin->vtable->register_url_filter)(plugin, filter_fn);
}

/**
 * Provides wget2 with another HSTS database to use.
 *
 * Each database, including wget2's own implementation, has an integer value known as priority associated with it.
 * The implementation with the highest priority value is actually used by wget2.
 *
 * wget2's own implementation has priority value 0.
 *
 * wget2 will automatically call wget_hsts_db_free() on any database it decides it no longer needs,
 * so plugins do not need to do so.
 *
 * \param[in] plugin The plugin handle
 * \param[in] hsts_db HSTS database to add
 * \param[in] priority The priority value to use
 */
void wget_plugin_add_hsts_db(wget_plugin_t *plugin, wget_hsts_db_t *hsts_db, int priority)
{
	(* plugin->vtable->add_hsts_db)(plugin, hsts_db, priority);
}

/**
 * Provides wget2 with another HPKP database to use.
 *
 * Each database, including wget2's own implementation, has an integer value known as priority associated with it.
 * The implementation with the highest priority value is actually used by wget2.
 *
 * wget2's own implementation has priority value 0.
 *
 * wget2 will automatically call wget_hpkp_db_free() on any database it decides it no longer needs,
 * so plugins do not need to do so.
 *
 * \param[in] plugin The plugin handle
 * \param[in] hpkp_db HPKP database to add
 * \param[in] priority The priority value to use
 */
void wget_plugin_add_hpkp_db(wget_plugin_t *plugin, wget_hpkp_db_t *hpkp_db, int priority)
{
	(* plugin->vtable->add_hpkp_db)(plugin, hpkp_db, priority);
}

/**
 * Provides wget2 with another OCSP database to use.
 *
 * Each database, including wget2's own implementation, has an integer value known as priority associated with it.
 * The implementation with the highest priority value is actually used by wget2.
 *
 * wget2's own implementation has priority value 0.
 *
 * wget2 will automatically call wget_ocsp_db_free() on any database it decides it no longer needs,
 * so plugins do not need to do so.
 *
 * \param[in] plugin The plugin handle
 * \param[in] ocsp_db OCSP database to add
 * \param[in] priority The priority value to use
 */
void wget_plugin_add_ocsp_db(wget_plugin_t *plugin, wget_ocsp_db_t *ocsp_db, int priority)
{
	(* plugin->vtable->add_ocsp_db)(plugin, ocsp_db, priority);
}

/** @} */
