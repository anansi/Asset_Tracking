/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <libhal.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "nm-hal-manager.h"
#include "nm-marshal.h"
#include "nm-dbus-manager.h"
#include "nm-utils.h"
#include "nm-device-wifi.h"
#include "nm-device-ethernet.h"
#include "nm-gsm-device.h"
#include "nm-hso-gsm-device.h"
#include "nm-cdma-device.h"

/* Killswitch poll frequency in seconds */
#define RFKILL_POLL_FREQUENCY 6

#define HAL_DBUS_SERVICE "org.freedesktop.Hal"

typedef struct {
	LibHalContext *hal_ctx;
	NMDBusManager *dbus_mgr;
	GSList *device_creators;
	gboolean rfkilled;  /* Authoritative rfkill state */

	/* Killswitch handling */
	GSList *killswitch_list;
	guint32 killswitch_poll_id;
	char *kswitch_err;
	gboolean poll_rfkilled;
	guint32 pending_polls;
	GSList *poll_proxies;

	gboolean disposed;
} NMHalManagerPrivate;

#define NM_HAL_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_HAL_MANAGER, NMHalManagerPrivate))

G_DEFINE_TYPE (NMHalManager, nm_hal_manager, G_TYPE_OBJECT)

enum {
	UDI_ADDED,
	UDI_REMOVED,
	RFKILL_CHANGED,
	HAL_REAPPEARED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


static gboolean poll_killswitches (gpointer user_data);

/* Device creators */

typedef struct {
	char *device_type_name;
	char *capability_str;
	gboolean (*is_device_fn) (NMHalManager *self, const char *udi);
	NMDeviceCreatorFn creator_fn;
} DeviceCreator;

static DeviceCreator *
get_creator (NMHalManager *self, const char *udi)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	DeviceCreator *creator;
	GSList *iter;

	for (iter = priv->device_creators; iter; iter = g_slist_next (iter)) {
		creator = (DeviceCreator *) iter->data;

		if (libhal_device_query_capability (priv->hal_ctx, udi, creator->capability_str, NULL) && 
		    creator->is_device_fn (self, udi))
			return creator;
	}

	return NULL;
}

/* end of device creators */

/* Common helpers for built-in device creators */

static char *
nm_get_device_driver_name (LibHalContext *ctx, const char *udi)
{
	char *origdev_udi;
	char *driver_name = NULL;

	origdev_udi = libhal_device_get_property_string (ctx, udi, "net.originating_device", NULL);
	if (!origdev_udi) {
		/* Older HAL uses 'physical_device' */
		origdev_udi = libhal_device_get_property_string (ctx, udi, "net.physical_device", NULL);
	}

	if (origdev_udi && libhal_device_property_exists (ctx, origdev_udi, "info.linux.driver", NULL)) {
		char *drv = libhal_device_get_property_string (ctx, origdev_udi, "info.linux.driver", NULL);
		driver_name = g_strdup (drv);
		libhal_free_string (drv);
	}
	libhal_free_string (origdev_udi);

	return driver_name;
}

/* Wired device creator */

static gboolean
is_wired_device (NMHalManager *self, const char *udi)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	char *category;
	gboolean is_wired = FALSE;

	if (libhal_device_property_exists (priv->hal_ctx, udi, "net.linux.ifindex", NULL) &&
		libhal_device_property_exists (priv->hal_ctx, udi, "info.category", NULL)) {

		category = libhal_device_get_property_string (priv->hal_ctx, udi, "info.category", NULL);
		if (category) {
			is_wired = strcmp (category, "net.80203") == 0;
			libhal_free_string (category);
		}
	}

	return is_wired;
}

static GObject *
wired_device_creator (NMHalManager *self, const char *udi, gboolean managed)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	GObject *device;
	char *iface;
	char *driver;

	iface = libhal_device_get_property_string (priv->hal_ctx, udi, "net.interface", NULL);
	if (!iface) {
		nm_warning ("Couldn't get interface for %s, ignoring.", udi);
		return NULL;
	}

	driver = nm_get_device_driver_name (priv->hal_ctx, udi);
	device = (GObject *) nm_device_ethernet_new (udi, iface, driver, managed);

	libhal_free_string (iface);
	g_free (driver);

	return device;
}

/* Wireless device creator */

static gboolean
is_wireless_device (NMHalManager *self, const char *udi)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	char *category;
	gboolean is_wireless = FALSE;

	if (libhal_device_property_exists (priv->hal_ctx, udi, "net.linux.ifindex", NULL) &&
		libhal_device_property_exists (priv->hal_ctx, udi, "info.category", NULL)) {

		category = libhal_device_get_property_string (priv->hal_ctx, udi, "info.category", NULL);
		if (category) {
			is_wireless = strcmp (category, "net.80211") == 0;
			libhal_free_string (category);
		}
	}

	return is_wireless;
}

static GObject *
wireless_device_creator (NMHalManager *self, const char *udi, gboolean managed)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	GObject *device;
	char *iface;
	char *driver;

	iface = libhal_device_get_property_string (priv->hal_ctx, udi, "net.interface", NULL);
	if (!iface) {
		nm_warning ("Couldn't get interface for %s, ignoring.", udi);
		return NULL;
	}

	driver = nm_get_device_driver_name (priv->hal_ctx, udi);
	device = (GObject *) nm_device_wifi_new (udi, iface, driver, managed);

	libhal_free_string (iface);
	g_free (driver);

	return device;
}

/* Modem device creator */

static gboolean
is_modem_device (NMHalManager *self, const char *udi)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	gboolean is_modem = FALSE;

	if (libhal_device_property_exists (priv->hal_ctx, udi, "info.category", NULL)) {
		char *category;

		category = libhal_device_get_property_string (priv->hal_ctx, udi, "info.category", NULL);
		if (category) {
			is_modem = strcmp (category, "serial") == 0;
			libhal_free_string (category);
		}
	}

	return is_modem;
}

static char *
get_hso_netdev (LibHalContext *ctx, const char *udi)
{
	char *serial_parent, *netdev = NULL;
	char **netdevs;
	int num, i;

	/* Get the serial interface's originating device UDI, used to find the
	 * originating device's netdev.
	 */
	serial_parent = libhal_device_get_property_string (ctx, udi, "serial.originating_device", NULL);
	if (!serial_parent)
		serial_parent = libhal_device_get_property_string (ctx, udi, "info.parent", NULL);
	if (!serial_parent)
		return NULL;

	/* Look for the originating device's netdev */
	netdevs = libhal_find_device_by_capability (ctx, "net", &num, NULL);
	for (i = 0; netdevs && !netdev && (i < num); i++) {
		char *netdev_parent, *tmp;

		netdev_parent = libhal_device_get_property_string (ctx, netdevs[i], "net.originating_device", NULL);
		if (!netdev_parent)
			netdev_parent = libhal_device_get_property_string (ctx, netdevs[i], "net.physical_device", NULL);
		if (!netdev_parent)
			continue;

		if (!strcmp (netdev_parent, serial_parent)) {
			/* We found it */
			tmp = libhal_device_get_property_string (ctx, netdevs[i], "net.interface", NULL);
			if (tmp) {
				netdev = g_strdup (tmp);
				libhal_free_string (tmp);
			}
		}

		libhal_free_string (netdev_parent);
	}
	libhal_free_string_array (netdevs);
	libhal_free_string (serial_parent);

	return netdev;
}

static GObject *
modem_device_creator (NMHalManager *self, const char *udi, gboolean managed)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	char *serial_device;
	char *parent_udi;
	char *driver_name = NULL;
	GObject *device = NULL;
	char **capabilities, **iter;
	gboolean type_gsm = FALSE;
	gboolean type_cdma = FALSE;
	char *netdev = NULL;

	serial_device = libhal_device_get_property_string (priv->hal_ctx, udi, "serial.device", NULL);

	/* Get the driver */
	parent_udi = libhal_device_get_property_string (priv->hal_ctx, udi, "info.parent", NULL);
	if (parent_udi) {
		driver_name = libhal_device_get_property_string (priv->hal_ctx, parent_udi, "info.linux.driver", NULL);
		libhal_free_string (parent_udi);
	}

	if (!serial_device || !driver_name)
		goto out;

	capabilities = libhal_device_get_property_strlist (priv->hal_ctx, udi, "modem.command_sets", NULL);
	/* 'capabilites' may be NULL */
	for (iter = capabilities; iter && *iter; iter++) {
		if (!strcmp (*iter, "GSM-07.07")) {
			type_gsm = TRUE;
			break;
		}
		if (!strcmp (*iter, "IS-707-A")) {
			type_cdma = TRUE;
			break;
		}
	}
	g_strfreev (capabilities);

	/* Compatiblity with the pre-specification bits */
	if (!type_gsm && !type_cdma) {
		capabilities = libhal_device_get_property_strlist (priv->hal_ctx, udi, "info.capabilities", NULL);
		for (iter = capabilities; *iter; iter++) {
			if (!strcmp (*iter, "gsm")) {
				type_gsm = TRUE;
				break;
			}
			if (!strcmp (*iter, "cdma")) {
				type_cdma = TRUE;
				break;
			}
		}
		g_strfreev (capabilities);
	}

	/* Special handling of 'hso' cards (until punted out to a modem manager) */
	if (type_gsm && !strcmp (driver_name, "hso"))
		netdev = get_hso_netdev (priv->hal_ctx, udi);

	if (type_gsm) {
		if (netdev)
			device = (GObject *) nm_hso_gsm_device_new (udi, serial_device + strlen ("/dev/"), NULL, netdev, driver_name, managed);
		else
			device = (GObject *) nm_gsm_device_new (udi, serial_device + strlen ("/dev/"), NULL, driver_name, managed);
	} else if (type_cdma)
		device = (GObject *) nm_cdma_device_new (udi, serial_device + strlen ("/dev/"), NULL, driver_name, managed);

out:
	libhal_free_string (serial_device);
	libhal_free_string (driver_name);

	return device;
}

static void
register_built_in_creators (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	DeviceCreator *creator;

	/* Wired device */
	creator = g_slice_new0 (DeviceCreator);
	creator->device_type_name = g_strdup ("Ethernet");
	creator->capability_str = g_strdup ("net.80203");
	creator->is_device_fn = is_wired_device;
	creator->creator_fn = wired_device_creator;
	priv->device_creators = g_slist_append (priv->device_creators, creator);

	/* Wireless device */
	creator = g_slice_new0 (DeviceCreator);
	creator->device_type_name = g_strdup ("802.11 WiFi");
	creator->capability_str = g_strdup ("net.80211");
	creator->is_device_fn = is_wireless_device;
	creator->creator_fn = wireless_device_creator;
	priv->device_creators = g_slist_append (priv->device_creators, creator);

	/* Modem */
	creator = g_slice_new0 (DeviceCreator);
	creator->device_type_name = g_strdup ("Modem");
	creator->capability_str = g_strdup ("modem");
	creator->is_device_fn = is_modem_device;
	creator->creator_fn = modem_device_creator;
	priv->device_creators = g_slist_append (priv->device_creators, creator);
}

static void
device_added (LibHalContext *ctx, const char *udi)
{
	NMHalManager *self = NM_HAL_MANAGER (libhal_ctx_get_user_data (ctx));
	DeviceCreator *creator;

//	nm_debug ("New device added (hal udi is '%s').", udi );

	/* Sometimes the device's properties (like net.interface) are not set up yet,
	 * so this call will fail, and it will actually be added when hal sets the device's
	 * capabilities a bit later on.
	 */
	creator = get_creator (self, udi);
	if (creator)
		g_signal_emit (self, signals[UDI_ADDED], 0, udi, creator->device_type_name, creator->creator_fn);
}

static void
device_removed (LibHalContext *ctx, const char *udi)
{
	NMHalManager *self = NM_HAL_MANAGER (libhal_ctx_get_user_data (ctx));

//	nm_debug ("Device removed (hal udi is '%s').", udi );

	g_signal_emit (self, signals[UDI_REMOVED], 0, udi);
}

static void
device_new_capability (LibHalContext *ctx, const char *udi, const char *capability)
{
	NMHalManager *self = NM_HAL_MANAGER (libhal_ctx_get_user_data (ctx));
	DeviceCreator *creator;

//	nm_debug ("nm_hal_device_new_capability() called with udi = %s, capability = %s", udi, capability );

	creator = get_creator (self, udi);
	if (creator)
		g_signal_emit (self, signals[UDI_ADDED], 0, udi, creator->device_type_name, creator->creator_fn);
}

static void
add_initial_devices (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	DeviceCreator *creator;
	GSList *iter;
	char **devices;
	int num_devices;
	int i;
	DBusError err;

	for (iter = priv->device_creators; iter; iter = g_slist_next (iter)) {
		creator = (DeviceCreator *) iter->data;

		dbus_error_init (&err);
		devices = libhal_find_device_by_capability (priv->hal_ctx,
													creator->capability_str,
													&num_devices,
													&err);

		if (dbus_error_is_set (&err)) {
			nm_warning ("could not find existing devices: %s", err.message);
			dbus_error_free (&err);
		}

		if (devices) {
			for (i = 0; i < num_devices; i++) {
				if (!creator->is_device_fn (self, devices[i]))
					continue;
				g_signal_emit (self, signals[UDI_ADDED], 0, devices[i], creator->device_type_name, creator->creator_fn);
			}
		}

		libhal_free_string_array (devices);
	}
}

static void
killswitch_poll_cleanup (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	if (priv->poll_proxies) {
		g_slist_foreach (priv->poll_proxies, (GFunc) g_object_unref, NULL);
		g_slist_free (priv->poll_proxies);
		priv->poll_proxies = NULL;
	}

	priv->pending_polls = 0;
	priv->poll_rfkilled = FALSE;
}

static void
killswitch_getpower_done (gpointer user_data)
{
	NMHalManager *self = NM_HAL_MANAGER (user_data);
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	priv->pending_polls--;

	if (priv->pending_polls > 0)
		return;

	if (priv->poll_rfkilled != priv->rfkilled) {
		priv->rfkilled = priv->poll_rfkilled;
		g_signal_emit (self, signals[RFKILL_CHANGED], 0, priv->rfkilled);
	}

	killswitch_poll_cleanup (self);

	/* Schedule next poll */
	priv->killswitch_poll_id = g_timeout_add (RFKILL_POLL_FREQUENCY * 1000,
	                                          poll_killswitches,
	                                          self);
}

static void 
killswitch_getpower_reply (DBusGProxy *proxy,
					  DBusGProxyCall *call_id,
					  gpointer user_data)
{
	NMHalManager *self = NM_HAL_MANAGER (user_data);
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	int power = 1;
	GError *err = NULL;

	if (dbus_g_proxy_end_call (proxy, call_id, &err,
	                           G_TYPE_INT, &power,
	                           G_TYPE_INVALID)) {
		if (power == 0)
			priv->poll_rfkilled = TRUE;
	} else {
		if (err->message) {
			/* Only print the error if we haven't seen it before */
		    if (!priv->kswitch_err || strcmp (priv->kswitch_err, err->message) != 0) {
				nm_warning ("Error getting killswitch power: %s.", err->message);
				g_free (priv->kswitch_err);
				priv->kswitch_err = g_strdup (err->message);

				/* If there was an error talking to HAL, treat that as rfkilled.
				 * See rh #448889.  On some Dell laptops, dellWirelessCtl
				 * may not be present, but HAL still advertises a killswitch,
				 * and calls to GetPower() will fail.  Thus we cannot assume
				 * that a failure of GetPower() automatically means the wireless
				 * is rfkilled, because in this situation NM would never bring
				 * the radio up.  Only assume failures between NM and HAL should
				 * block the radio, not failures of the HAL killswitch callout
				 * itself.
				 */
				if (strstr (err->message, "Did not receive a reply")) {
					nm_warning ("HAL did not reply to killswitch power request;"
					            " assuming radio is blocked.");
					priv->poll_rfkilled = TRUE;
				}
			}
		}
		g_error_free (err);
	}
}

static void
poll_one_killswitch (gpointer data, gpointer user_data)
{
	NMHalManager *self = NM_HAL_MANAGER (user_data);
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	DBusGProxy *proxy;

	proxy = dbus_g_proxy_new_for_name (nm_dbus_manager_get_connection (priv->dbus_mgr),
								"org.freedesktop.Hal",
								(char *) data,
								"org.freedesktop.Hal.Device.KillSwitch");

	dbus_g_proxy_begin_call (proxy, "GetPower",
						killswitch_getpower_reply,
						self,
						killswitch_getpower_done,
						G_TYPE_INVALID);
	priv->pending_polls++;
	priv->poll_proxies = g_slist_prepend (priv->poll_proxies, proxy);
}

static gboolean
poll_killswitches (gpointer user_data)
{
	NMHalManager *self = NM_HAL_MANAGER (user_data);
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	killswitch_poll_cleanup (self);

	g_slist_foreach (priv->killswitch_list, poll_one_killswitch, self);
	return FALSE;
}

static void
add_killswitch_device (NMHalManager *self, const char *udi)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	char *type;
	GSList *iter;

	type = libhal_device_get_property_string (priv->hal_ctx, udi, "killswitch.type", NULL);
	if (!type)
		return;

	if (strcmp (type, "wlan"))
		goto out;

	/* see if it's already in the list */
	for (iter = priv->killswitch_list; iter; iter = iter->next) {
		const char *list_udi = (const char *) iter->data;
		if (!strcmp (list_udi, udi))
			goto out;
	}

	/* Poll switches if this is the first switch we've found */
	if (!priv->killswitch_list)
		priv->killswitch_poll_id = g_idle_add (poll_killswitches, self);

	priv->killswitch_list = g_slist_append (priv->killswitch_list, g_strdup (udi));
	nm_info ("Found radio killswitch %s", udi);

out:
	libhal_free_string (type);
}

static void
add_killswitch_devices (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	char **udis;
	int num_udis;
	int i;
	DBusError	err;

	dbus_error_init (&err);
	udis = libhal_find_device_by_capability (priv->hal_ctx, "killswitch", &num_udis, &err);
	if (!udis)
		return;

	if (dbus_error_is_set (&err)) {
		nm_warning ("Could not find killswitch devices: %s", err.message);
		dbus_error_free (&err);
		return;
	}

	for (i = 0; i < num_udis; i++)
		add_killswitch_device (self, udis[i]);

	libhal_free_string_array (udis);
}

static gboolean
hal_init (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	DBusError error;
	DBusGConnection *connection; 

	priv->hal_ctx = libhal_ctx_new ();
	if (!priv->hal_ctx) {
		nm_warning ("Could not get connection to the HAL service.");
		return FALSE;
	}

	connection = nm_dbus_manager_get_connection (priv->dbus_mgr);
	libhal_ctx_set_dbus_connection (priv->hal_ctx,
									dbus_g_connection_get_connection (connection));

	dbus_error_init (&error);
	if (!libhal_ctx_init (priv->hal_ctx, &error)) {
		nm_warning ("libhal_ctx_init() failed: %s\n"
				    "Make sure the hal daemon is running?", 
				    error.message);
		goto error;
	}

	libhal_ctx_set_user_data (priv->hal_ctx, self);
	libhal_ctx_set_device_added (priv->hal_ctx, device_added);
	libhal_ctx_set_device_removed (priv->hal_ctx, device_removed);
	libhal_ctx_set_device_new_capability (priv->hal_ctx, device_new_capability);

	libhal_device_property_watch_all (priv->hal_ctx, &error);
	if (dbus_error_is_set (&error)) {
		nm_error ("libhal_device_property_watch_all(): %s", error.message);
		libhal_ctx_shutdown (priv->hal_ctx, NULL);
		goto error;
	}

	return TRUE;

error:
	if (dbus_error_is_set (&error))
		dbus_error_free (&error);
	if (priv->hal_ctx) {
		libhal_ctx_free (priv->hal_ctx);
		priv->hal_ctx = NULL;
	}
	return FALSE;
}

static void
hal_deinit (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);
	DBusError error;

	if (priv->killswitch_poll_id) {
		g_source_remove (priv->killswitch_poll_id);
		priv->killswitch_poll_id = 0;
	}
	killswitch_poll_cleanup (self);

	if (priv->killswitch_list) {
		g_slist_foreach (priv->killswitch_list, (GFunc) g_free, NULL);
		g_slist_free (priv->killswitch_list);
		priv->killswitch_list = NULL;
	}

	if (!priv->hal_ctx)
		return;

	dbus_error_init (&error);
	libhal_ctx_shutdown (priv->hal_ctx, &error);
	if (dbus_error_is_set (&error)) {
		nm_warning ("libhal shutdown failed - %s", error.message);
		dbus_error_free (&error);
	}

	libhal_ctx_free (priv->hal_ctx);
	priv->hal_ctx = NULL;
}

static void
name_owner_changed (NMDBusManager *dbus_mgr,
					const char *name,
					const char *old,
					const char *new,
					gpointer user_data)
{
	NMHalManager *self = NM_HAL_MANAGER (user_data);
	gboolean old_owner_good = (old && (strlen (old) > 0));
	gboolean new_owner_good = (new && (strlen (new) > 0));

	/* Only care about signals from HAL */
	if (strcmp (name, HAL_DBUS_SERVICE))
		return;

	if (!old_owner_good && new_owner_good) {
		nm_info ("HAL re-appeared");
		/* HAL just appeared */
		if (!hal_init (self))
			nm_warning ("Could not re-connect to HAL!!");
		else
			g_signal_emit (self, signals[HAL_REAPPEARED], 0);
	} else if (old_owner_good && !new_owner_good) {
		/* HAL went away. Bad HAL. */
		nm_info ("HAL disappeared");
		hal_deinit (self);
	}
}

static void
connection_changed (NMDBusManager *dbus_mgr,
					DBusGConnection *connection,
					gpointer user_data)
{
	NMHalManager *self = NM_HAL_MANAGER (user_data);

	if (!connection) {
		hal_deinit (self);
		return;
	}

	if (nm_dbus_manager_name_has_owner (dbus_mgr, HAL_DBUS_SERVICE)) {
		if (!hal_init (self))
			nm_warning ("Could not re-connect to HAL!!");
	}
}

void
nm_hal_manager_query_devices (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	/* Find hardware we care about */
	if (priv->hal_ctx) {
		add_killswitch_devices (self);
		add_initial_devices (self);
	}
}

gboolean
nm_hal_manager_udi_exists (NMHalManager *self, const char *udi)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	return libhal_device_property_exists (priv->hal_ctx, udi, "info.udi", NULL);
}

NMHalManager *
nm_hal_manager_new (void)
{
	NMHalManager *self;
	NMHalManagerPrivate *priv;

	self = NM_HAL_MANAGER (g_object_new (NM_TYPE_HAL_MANAGER, NULL));

	priv = NM_HAL_MANAGER_GET_PRIVATE (self);
 	if (!nm_dbus_manager_name_has_owner (priv->dbus_mgr, HAL_DBUS_SERVICE)) {
		nm_info ("Waiting for HAL to start...");
		return self;
	}

	if (!hal_init (self)) {
		g_object_unref (self);
		self = NULL;
	}

	return self;
}

static void
nm_hal_manager_init (NMHalManager *self)
{
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	priv->rfkilled = FALSE;

	priv->dbus_mgr = nm_dbus_manager_get ();

	register_built_in_creators (self);

	g_signal_connect (priv->dbus_mgr,
	                  "name-owner-changed",
	                  G_CALLBACK (name_owner_changed),
	                  self);
	g_signal_connect (priv->dbus_mgr,
	                  "dbus-connection-changed",
	                  G_CALLBACK (connection_changed),
	                  self);
}

static void
destroy_creator (gpointer data, gpointer user_data)
{
	DeviceCreator *creator = (DeviceCreator *) data;

	g_free (creator->device_type_name);
	g_free (creator->capability_str);
	g_slice_free (DeviceCreator, data);
}

static void
dispose (GObject *object)
{
	NMHalManager *self = NM_HAL_MANAGER (object);
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	if (priv->disposed) {
		G_OBJECT_CLASS (nm_hal_manager_parent_class)->dispose (object);
		return;
	}
	priv->disposed = TRUE;

	g_object_unref (priv->dbus_mgr);

	g_slist_foreach (priv->device_creators, destroy_creator, NULL);
	g_slist_free (priv->device_creators);

	hal_deinit (self);

	G_OBJECT_CLASS (nm_hal_manager_parent_class)->dispose (object);	
}

static void
finalize (GObject *object)
{
	NMHalManager *self = NM_HAL_MANAGER (object);
	NMHalManagerPrivate *priv = NM_HAL_MANAGER_GET_PRIVATE (self);

	g_free (priv->kswitch_err);

	G_OBJECT_CLASS (nm_hal_manager_parent_class)->finalize (object);
}

static void
nm_hal_manager_class_init (NMHalManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMHalManagerPrivate));

	/* virtual methods */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	/* Signals */
	signals[UDI_ADDED] =
		g_signal_new ("udi-added",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMHalManagerClass, udi_added),
					  NULL, NULL,
					  _nm_marshal_VOID__STRING_STRING_POINTER,
					  G_TYPE_NONE, 3,
					  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);

	signals[UDI_REMOVED] =
		g_signal_new ("udi-removed",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMHalManagerClass, udi_removed),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__STRING,
					  G_TYPE_NONE, 1,
					  G_TYPE_STRING);

	signals[RFKILL_CHANGED] =
		g_signal_new ("rfkill-changed",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMHalManagerClass, rfkill_changed),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__BOOLEAN,
					  G_TYPE_NONE, 1,
					  G_TYPE_BOOLEAN);

	signals[HAL_REAPPEARED] =
		g_signal_new ("hal-reappeared",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMHalManagerClass, hal_reappeared),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__VOID,
					  G_TYPE_NONE, 0);
}

