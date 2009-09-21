/* libnm_glib -- Access NetworkManager information from applications
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#ifndef _LIB_NM_H_
#define _LIB_NM_H_

#ifndef NM_DISABLE_DEPRECATED

#include <glib.h>

G_BEGIN_DECLS

typedef enum libnm_glib_state
{
	LIBNM_NO_DBUS = 0,
	LIBNM_NO_NETWORKMANAGER,
	LIBNM_NO_NETWORK_CONNECTION,
	LIBNM_ACTIVE_NETWORK_CONNECTION,
	LIBNM_INVALID_CONTEXT
} libnm_glib_state;

typedef struct libnm_glib_ctx libnm_glib_ctx;


typedef void (*libnm_glib_callback_func) (libnm_glib_ctx *libnm_ctx, gpointer user_data);


libnm_glib_ctx		*libnm_glib_init				(void);
void				 libnm_glib_shutdown			(libnm_glib_ctx *ctx);

libnm_glib_state	 libnm_glib_get_network_state		(const libnm_glib_ctx *ctx);

guint				 libnm_glib_register_callback		(libnm_glib_ctx *ctx, libnm_glib_callback_func func, gpointer user_data, GMainContext *g_main_ctx);
void				 libnm_glib_unregister_callback	(libnm_glib_ctx *ctx, guint id);

G_END_DECLS

#endif /* NM_DISABLE_DEPRECATED */

#endif /* _LIB_NM_H_ */
