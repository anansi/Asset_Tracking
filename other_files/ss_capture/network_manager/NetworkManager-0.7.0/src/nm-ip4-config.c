/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/* NetworkManager -- Network link manager
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
 * (C) Copyright 2005 - 2008 Red Hat, Inc.
 */


#include <glib.h>
#include <stdio.h>
#include <string.h>
#include "nm-ip4-config.h"
#include "nm-dbus-manager.h"
#include "NetworkManager.h"
#include "NetworkManagerUtils.h"
#include "nm-setting-ip4-config.h"
#include "nm-utils.h"

#include <netlink/route/addr.h>
#include <netlink/utils.h>
#include <netinet/in.h>

#include "nm-ip4-config-glue.h"
#include "nm-dbus-glib-types.h"


G_DEFINE_TYPE (NMIP4Config, nm_ip4_config, G_TYPE_OBJECT)

#define NM_IP4_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_IP4_CONFIG, NMIP4ConfigPrivate))

typedef struct {
	char *path;

	GSList *addresses;
	guint32	ptp_address;

	guint32	mtu;	/* Maximum Transmission Unit of the interface */
	guint32	mss;	/* Maximum Segment Size of the route */

	GArray *nameservers;
	GPtrArray *domains;
	GPtrArray *searches;

	GSList *routes;
} NMIP4ConfigPrivate;


enum {
	PROP_0,
	PROP_ADDRESSES,
	PROP_NAMESERVERS,
	PROP_DOMAINS,
	PROP_ROUTES,

	LAST_PROP
};


NMIP4Config *
nm_ip4_config_new (void)
{
	return (NMIP4Config *) g_object_new (NM_TYPE_IP4_CONFIG, NULL);
}

void
nm_ip4_config_export (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;
	NMDBusManager *dbus_mgr;
	DBusGConnection *connection;
	static guint32 counter = 0;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	g_return_if_fail (priv->path == NULL);

	dbus_mgr = nm_dbus_manager_get ();
	connection = nm_dbus_manager_get_connection (dbus_mgr);
	priv->path = g_strdup_printf (NM_DBUS_PATH "/IP4Config/%d", counter++);

	dbus_g_connection_register_g_object (connection, priv->path, G_OBJECT (config));
	g_object_unref (dbus_mgr);
}

const char *
nm_ip4_config_get_dbus_path (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), FALSE);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->path;
}

void
nm_ip4_config_take_address (NMIP4Config *config, NMIP4Address *address)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (address != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	priv->addresses = g_slist_append (priv->addresses, address);
}

void
nm_ip4_config_add_address (NMIP4Config *config,
                           NMIP4Address *address)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (address != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	priv->addresses = g_slist_append (priv->addresses, nm_ip4_address_dup (address));
}

void
nm_ip4_config_replace_address (NMIP4Config *config,
                               guint i,
                               NMIP4Address *new_address)
{
	NMIP4ConfigPrivate *priv;
	GSList *old;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	old = g_slist_nth (priv->addresses, i);
	g_return_if_fail (old != NULL);
	nm_ip4_address_unref ((NMIP4Address *) old->data);

	old->data = nm_ip4_address_dup (new_address);
}

NMIP4Address *nm_ip4_config_get_address (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	return (NMIP4Address *) g_slist_nth_data (NM_IP4_CONFIG_GET_PRIVATE (config)->addresses, i);
}

guint32 nm_ip4_config_get_num_addresses (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_slist_length (NM_IP4_CONFIG_GET_PRIVATE (config)->addresses);
}

guint32 nm_ip4_config_get_ptp_address (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->ptp_address;
}

void nm_ip4_config_set_ptp_address (NMIP4Config *config, guint32 ptp_addr)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	NM_IP4_CONFIG_GET_PRIVATE (config)->ptp_address = ptp_addr;
}

void nm_ip4_config_add_nameserver (NMIP4Config *config, guint32 nameserver)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	if (nameserver != 0)
		g_array_append_val (NM_IP4_CONFIG_GET_PRIVATE (config)->nameservers, nameserver);
}

guint32 nm_ip4_config_get_nameserver (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->nameservers, guint32, i);
}

guint32 nm_ip4_config_get_num_nameservers (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->nameservers->len;
}

void nm_ip4_config_reset_nameservers (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (priv->nameservers->len)
		g_array_remove_range (priv->nameservers, 0, priv->nameservers->len);
}

void
nm_ip4_config_take_route (NMIP4Config *config, NMIP4Route *route)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (route != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	priv->routes = g_slist_append (priv->routes, route);
}

void
nm_ip4_config_add_route (NMIP4Config *config, NMIP4Route *route)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (route != NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	priv->routes = g_slist_append (priv->routes, nm_ip4_route_dup (route));
}

void
nm_ip4_config_replace_route (NMIP4Config *config,
							 guint i,
							 NMIP4Route *new_route)
{
	NMIP4ConfigPrivate *priv;
	GSList *old;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	old = g_slist_nth (priv->routes, i);
	g_return_if_fail (old != NULL);
	nm_ip4_route_unref ((NMIP4Route *) old->data);

	old->data = nm_ip4_route_dup (new_route);
}

NMIP4Route *
nm_ip4_config_get_route (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	return (NMIP4Route *) g_slist_nth_data (NM_IP4_CONFIG_GET_PRIVATE (config)->routes, i);
}

guint32 nm_ip4_config_get_num_routes (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return g_slist_length (NM_IP4_CONFIG_GET_PRIVATE (config)->routes);
}

void nm_ip4_config_reset_routes (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	g_slist_foreach (priv->routes, (GFunc) g_free, NULL);
	priv->routes = NULL;
}

void nm_ip4_config_add_domain (NMIP4Config *config, const char *domain)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));
	g_return_if_fail (domain != NULL);
	g_return_if_fail (strlen (domain) > 0);

	if (!strlen (domain))
		return;

	g_ptr_array_add (NM_IP4_CONFIG_GET_PRIVATE (config)->domains, g_strdup (domain));
}

const char *nm_ip4_config_get_domain (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	return (const char *) g_ptr_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->domains, i);
}

guint32 nm_ip4_config_get_num_domains (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->domains->len;
}

void nm_ip4_config_add_search (NMIP4Config *config, const char *search)
{
	g_return_if_fail (config != NULL);
	g_return_if_fail (search != NULL);
	g_return_if_fail (strlen (search) > 0);

	g_ptr_array_add (NM_IP4_CONFIG_GET_PRIVATE (config)->searches, g_strdup (search));
}

const char *nm_ip4_config_get_search (NMIP4Config *config, guint i)
{
	g_return_val_if_fail (config != NULL, NULL);

	return (const char *) g_ptr_array_index (NM_IP4_CONFIG_GET_PRIVATE (config)->searches, i);
}

guint32 nm_ip4_config_get_num_searches (NMIP4Config *config)
{
	g_return_val_if_fail (config != NULL, 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->searches->len;
}

void nm_ip4_config_reset_searches (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (priv->searches->len)
		g_ptr_array_remove_range (priv->searches, 0, priv->searches->len);
}

guint32 nm_ip4_config_get_mtu (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->mtu;
}

void nm_ip4_config_set_mtu (NMIP4Config *config, guint32 mtu)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	NM_IP4_CONFIG_GET_PRIVATE (config)->mtu = mtu;
}

guint32 nm_ip4_config_get_mss (NMIP4Config *config)
{
	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	return NM_IP4_CONFIG_GET_PRIVATE (config)->mss;
}

void nm_ip4_config_set_mss (NMIP4Config *config, guint32 mss)
{
	g_return_if_fail (NM_IS_IP4_CONFIG (config));

	NM_IP4_CONFIG_GET_PRIVATE (config)->mss = mss;
}

/* libnl convenience/conversion functions */

static int ip4_addr_to_rtnl_local (guint32 ip4_address, struct rtnl_addr *addr)
{
	struct nl_addr * local = NULL;
	int err = 0;

	g_return_val_if_fail (addr != NULL, -1);

	local = nm_utils_ip4_addr_to_nl_addr (ip4_address);
	err = rtnl_addr_set_local (addr, local);
	nl_addr_put (local);

	return err;
}

static int ip4_addr_to_rtnl_peer (guint32 ip4_address, struct rtnl_addr *addr)
{
	struct nl_addr * peer = NULL;
	int err = 0;

	g_return_val_if_fail (addr != NULL, -1);

	peer = nm_utils_ip4_addr_to_nl_addr (ip4_address);
	err = rtnl_addr_set_peer (addr, peer);
	nl_addr_put (peer);

	return err;
}

static int ip4_addr_to_rtnl_broadcast (guint32 ip4_broadcast, struct rtnl_addr *addr)
{
	struct nl_addr	* local = NULL;
	int err = 0;

	g_return_val_if_fail (addr != NULL, -1);

	local = nm_utils_ip4_addr_to_nl_addr (ip4_broadcast);
	err = rtnl_addr_set_broadcast (addr, local);
	nl_addr_put (local);

	return err;
}


struct rtnl_addr *
nm_ip4_config_to_rtnl_addr (NMIP4Config *config, guint32 i, guint32 flags)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	NMIP4Address *config_addr;
	struct rtnl_addr *addr;
	gboolean success = TRUE;

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	config_addr = nm_ip4_config_get_address (config, i);
	g_return_val_if_fail (config_addr != NULL, NULL);

	if (!(addr = rtnl_addr_alloc()))
		return NULL;

	if (flags & NM_RTNL_ADDR_ADDR)
		success = (ip4_addr_to_rtnl_local (nm_ip4_address_get_address (config_addr), addr) >= 0);

	if (flags & NM_RTNL_ADDR_PTP_ADDR)
		success = (ip4_addr_to_rtnl_peer (priv->ptp_address, addr) >= 0);

	if (flags & NM_RTNL_ADDR_PREFIX)
		rtnl_addr_set_prefixlen (addr, nm_ip4_address_get_prefix (config_addr));

	if (flags & NM_RTNL_ADDR_BROADCAST) {
		guint32 hostmask, network, bcast, netmask;

		netmask = nm_utils_ip4_prefix_to_netmask (nm_ip4_address_get_prefix (config_addr));
		network = ntohl (nm_ip4_address_get_address (config_addr)) & ntohl (netmask);
		hostmask = ~ntohl (netmask);
		bcast = htonl (network | hostmask);

		success = (ip4_addr_to_rtnl_broadcast (bcast, addr) >= 0);
	}

	if (!success) {
		rtnl_addr_put (addr);
		addr = NULL;
	}

	return addr;
}

static void
nm_ip4_config_init (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (config);

	priv->nameservers = g_array_new (FALSE, TRUE, sizeof (guint32));
	priv->domains = g_ptr_array_new ();
	priv->searches = g_ptr_array_new ();
}

static void
finalize (GObject *object)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (object);

	nm_utils_slist_free (priv->addresses, (GDestroyNotify) nm_ip4_address_unref);
	nm_utils_slist_free (priv->routes, (GDestroyNotify) nm_ip4_route_unref);
	g_array_free (priv->nameservers, TRUE);
	g_ptr_array_free (priv->domains, TRUE);
	g_ptr_array_free (priv->searches, TRUE);
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_ADDRESSES:
		nm_utils_ip4_addresses_to_gvalue (priv->addresses, value);
		break;
	case PROP_NAMESERVERS:
		g_value_set_boxed (value, priv->nameservers);
		break;
	case PROP_DOMAINS:
		g_value_set_boxed (value, priv->domains);
		break;
	case PROP_ROUTES:
		nm_utils_ip4_routes_to_gvalue (priv->routes, value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_ip4_config_class_init (NMIP4ConfigClass *config_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (config_class);

	g_type_class_add_private (config_class, sizeof (NMIP4ConfigPrivate));

	/* virtual methods */
	object_class->get_property = get_property;
	object_class->finalize = finalize;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_ADDRESSES,
		 g_param_spec_boxed (NM_IP4_CONFIG_ADDRESSES,
							"Addresses",
							"IP4 addresses",
							DBUS_TYPE_G_ARRAY_OF_ARRAY_OF_UINT,
							G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_NAMESERVERS,
		 g_param_spec_boxed (NM_IP4_CONFIG_NAMESERVERS,
							 "Nameservers",
							 "DNS list",
							 DBUS_TYPE_G_UINT_ARRAY,
							 G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_DOMAINS,
		 g_param_spec_boxed (NM_IP4_CONFIG_DOMAINS,
							 "Domains",
							 "Domains",
							 DBUS_TYPE_G_ARRAY_OF_STRING,
							 G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_ROUTES,
		 g_param_spec_boxed (NM_IP4_CONFIG_ROUTES,
						 "Routes",
						 "Routes",
						 DBUS_TYPE_G_ARRAY_OF_ARRAY_OF_UINT,
						 G_PARAM_READABLE));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (config_class),
									 &dbus_glib_nm_ip4_config_object_info);
}
