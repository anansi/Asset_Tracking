/* nm-tool - information tool for NetworkManager
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

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <nm-client.h>
#include <nm-device.h>
#include <nm-device-ethernet.h>
#include <nm-device-wifi.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>
#include <nm-utils.h>
#include <nm-setting-ip4-config.h>

static gboolean
get_nm_state (NMClient *client)
{
	NMState state;
	char *state_string;
	gboolean success = TRUE;

	state = nm_client_get_state (client);

	switch (state) {
	case NM_STATE_ASLEEP:
		state_string = "asleep";
		break;

	case NM_STATE_CONNECTING:
		state_string = "connecting";
		break;

	case NM_STATE_CONNECTED:
		state_string = "connected";
		break;

	case NM_STATE_DISCONNECTED:
		state_string = "disconnected";
		break;

	case NM_STATE_UNKNOWN:
	default:
		state_string = "unknown";
		success = FALSE;
		break;
	}

	printf ("State: %s\n\n", state_string);

	return success;
}

static void
print_string (const char *label, const char *data)
{
#define SPACING 18
	int label_len = 0;
	char spaces[50];
	int i;

	g_return_if_fail (label != NULL);
	g_return_if_fail (data != NULL);

	label_len = strlen (label);
	if (label_len > SPACING)
		label_len = SPACING - 1;
	for (i = 0; i < (SPACING - label_len); i++)
		spaces[i] = 0x20;
	spaces[i] = 0x00;

	printf ("  %s:%s%s\n", label, &spaces[0], data);
}


static void
detail_access_point (gpointer data, gpointer user_data)
{
	NMAccessPoint *ap = NM_ACCESS_POINT (data);
	const char *active_bssid = (const char *) user_data;
	GString *str;
	gboolean active = FALSE;
	guint32 flags, wpa_flags, rsn_flags;
	const GByteArray * ssid;
	char *tmp;

	flags = nm_access_point_get_flags (ap);
	wpa_flags = nm_access_point_get_wpa_flags (ap);
	rsn_flags = nm_access_point_get_rsn_flags (ap);

	if (active_bssid) {
		const char *current_bssid = nm_access_point_get_hw_address (ap);
		if (current_bssid && !strcmp (current_bssid, active_bssid))
			active = TRUE;
	}

	str = g_string_new (NULL);
	g_string_append_printf (str,
							"%s, %s, Freq %d MHz, Rate %d Mb/s, Strength %d",
							(nm_access_point_get_mode (ap) == NM_802_11_MODE_INFRA) ? "Infra" : "Ad-Hoc",
							nm_access_point_get_hw_address (ap),
							nm_access_point_get_frequency (ap),
							nm_access_point_get_max_bitrate (ap) / 1000,
							nm_access_point_get_strength (ap));

	if (   !(flags & NM_802_11_AP_FLAGS_PRIVACY)
	    &&  (wpa_flags != NM_802_11_AP_SEC_NONE)
	    &&  (rsn_flags != NM_802_11_AP_SEC_NONE))
		g_string_append (str, ", Encrypted: ");

	if (   (flags & NM_802_11_AP_FLAGS_PRIVACY)
	    && (wpa_flags == NM_802_11_AP_SEC_NONE)
	    && (rsn_flags == NM_802_11_AP_SEC_NONE))
		g_string_append (str, " WEP");
	if (wpa_flags != NM_802_11_AP_SEC_NONE)
		g_string_append (str, " WPA");
	if (rsn_flags != NM_802_11_AP_SEC_NONE)
		g_string_append (str, " WPA2");
	if (   (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
	    || (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X))
		g_string_append (str, " Enterprise");

	/* FIXME: broadcast/hidden */

	ssid = nm_access_point_get_ssid (ap);
	tmp = g_strdup_printf ("  %s%s", active ? "*" : "",
	                       ssid ? nm_utils_escape_ssid (ssid->data, ssid->len) : "(none)");

	print_string (tmp, str->str);

	g_string_free (str, TRUE);
	g_free (tmp);
}

static gchar *
ip4_address_as_string (guint32 ip)
{
	struct in_addr tmp_addr;
	char buf[INET_ADDRSTRLEN+1];

	memset (&buf, '\0', sizeof (buf));
	tmp_addr.s_addr = ip;

	if (inet_ntop (AF_INET, &tmp_addr, buf, INET_ADDRSTRLEN)) {
		return g_strdup (buf);
	} else {
		nm_warning ("%s: error converting IP4 address 0x%X",
		            __func__, ntohl (tmp_addr.s_addr));
		return NULL;
	}
}

static const char *
get_dev_state_string (NMDeviceState state)
{
	if (state == NM_DEVICE_STATE_UNMANAGED)
		return "unmanaged";
	else if (state == NM_DEVICE_STATE_UNAVAILABLE)
		return "unavailable";
	else if (state == NM_DEVICE_STATE_DISCONNECTED)
		return "disconnected";
	else if (state == NM_DEVICE_STATE_PREPARE)
		return "connecting (prepare)";
	else if (state == NM_DEVICE_STATE_CONFIG)
		return "connecting (configuring)";
	else if (state == NM_DEVICE_STATE_NEED_AUTH)
		return "connecting (need authentication)";
	else if (state == NM_DEVICE_STATE_IP_CONFIG)
		return "connecting (getting IP configuration)";
	else if (state == NM_DEVICE_STATE_ACTIVATED)
		return "connected";
	else if (state == NM_DEVICE_STATE_FAILED)
		return "connection failed";
	return "unknown";
}

static void
detail_device (gpointer data, gpointer user_data)
{
	NMDevice *device = NM_DEVICE (data);
	NMClient *client = NM_CLIENT (user_data);
	char *tmp;
	NMDeviceState state;
	guint32 caps;
	guint32 speed;
	const GArray *array;
	const GPtrArray *connections;
	int j;
	gboolean is_default = FALSE;

	state = nm_device_get_state (device);

	printf ("- Device: %s ----------------------------------------------------------------\n",
	        nm_device_get_iface (device));

	/* General information */
	if (NM_IS_DEVICE_ETHERNET (device))
		print_string ("Type", "Wired");
	else if (NM_IS_DEVICE_WIFI (device))
		print_string ("Type", "802.11 WiFi");
	else if (NM_IS_GSM_DEVICE (device))
		print_string ("Type", "Mobile Broadband (GSM)");
	else if (NM_IS_CDMA_DEVICE (device))
		print_string ("Type", "Mobile Broadband (CDMA)");

	print_string ("Driver", nm_device_get_driver (device) ? nm_device_get_driver (device) : "(unknown)");

	print_string ("State", get_dev_state_string (state));

	connections = nm_client_get_active_connections (client);
	for (j = 0; connections && (j < connections->len); j++) {
		NMActiveConnection *candidate = g_ptr_array_index (connections, j);
		const GPtrArray *devices = nm_active_connection_get_devices (candidate);
		NMDevice *candidate_dev;

		if (!devices || !devices->len)
			continue;
		candidate_dev = g_ptr_array_index (devices, 0);

		if ((candidate_dev == device) && nm_active_connection_get_default(candidate))
			is_default = TRUE;
	}

	if (is_default)
		print_string ("Default", "yes");
	else
		print_string ("Default", "no");

	tmp = NULL;
	if (NM_IS_DEVICE_ETHERNET (device))
		tmp = g_strdup (nm_device_ethernet_get_hw_address (NM_DEVICE_ETHERNET (device)));
	else if (NM_IS_DEVICE_WIFI (device))
		tmp = g_strdup (nm_device_wifi_get_hw_address (NM_DEVICE_WIFI (device)));

	if (tmp) {
		print_string ("HW Address", tmp);
		g_free (tmp);
	}

	/* Capabilities */
	caps = nm_device_get_capabilities (device);
	printf ("\n  Capabilities:\n");
	if (caps & NM_DEVICE_CAP_NM_SUPPORTED)
		print_string ("  Supported", "yes");
	else
		print_string ("  Supported", "no");
	if (caps & NM_DEVICE_CAP_CARRIER_DETECT)
		print_string ("  Carrier Detect", "yes");

	speed = 0;
	if (NM_IS_DEVICE_ETHERNET (device)) {
		/* Speed in Mb/s */
		speed = nm_device_ethernet_get_speed (NM_DEVICE_ETHERNET (device));
	} else if (NM_IS_DEVICE_WIFI (device)) {
		/* Speed in b/s */
		speed = nm_device_wifi_get_bitrate (NM_DEVICE_WIFI (device));
		speed /= 1000;
	}

	if (speed) {
		char *speed_string;

		speed_string = g_strdup_printf ("%u Mb/s", speed);
		print_string ("  Speed", speed_string);
		g_free (speed_string);
	}

	/* Wireless specific information */
	if ((NM_IS_DEVICE_WIFI (device))) {
		guint32 wcaps;
		NMAccessPoint *active_ap = NULL;
		const char *active_bssid = NULL;
		const GPtrArray *aps;

		printf ("\n  Wireless Settings\n");

		wcaps = nm_device_wifi_get_capabilities (NM_DEVICE_WIFI (device));

		if (wcaps & (NM_WIFI_DEVICE_CAP_CIPHER_WEP40 | NM_WIFI_DEVICE_CAP_CIPHER_WEP104))
			print_string ("  WEP Encryption", "yes");
		if (wcaps & NM_WIFI_DEVICE_CAP_WPA)
			print_string ("  WPA Encryption", "yes");
		if (wcaps & NM_WIFI_DEVICE_CAP_RSN)
			print_string ("  WPA2 Encryption", "yes");

		if (nm_device_get_state (device) == NM_DEVICE_STATE_ACTIVATED) {
			active_ap = nm_device_wifi_get_active_access_point (NM_DEVICE_WIFI (device));
			active_bssid = active_ap ? nm_access_point_get_hw_address (active_ap) : NULL;
		}

		printf ("\n  Wireless Access Points%s\n", active_ap ? "(* = Current AP)" : "");

		aps = nm_device_wifi_get_access_points (NM_DEVICE_WIFI (device));
		if (aps && aps->len)
			g_ptr_array_foreach ((GPtrArray *) aps, detail_access_point, (gpointer) active_bssid);
	} else if (NM_IS_DEVICE_ETHERNET (device)) {
		printf ("\n  Wired Settings\n");
		/* FIXME */
#if 0
		if (link_active)
			print_string ("  Hardware Link", "yes");
		else
			print_string ("  Hardware Link", "no");
#endif
	}

	/* IP Setup info */
	if (state == NM_DEVICE_STATE_ACTIVATED) {
		NMIP4Config *cfg = nm_device_get_ip4_config (device);
		GSList *iter;

		printf ("\n  IPv4 Settings:\n");

		for (iter = (GSList *) nm_ip4_config_get_addresses (cfg); iter; iter = g_slist_next (iter)) {
			NMIP4Address *addr = (NMIP4Address *) iter->data;
			guint32 prefix = nm_ip4_address_get_prefix (addr);
			char *tmp2;

			tmp = ip4_address_as_string (nm_ip4_address_get_address (addr));
			print_string ("  Address", tmp);
			g_free (tmp);

			tmp2 = ip4_address_as_string (nm_utils_ip4_prefix_to_netmask (prefix));
			tmp = g_strdup_printf ("%d (%s)", prefix, tmp2);
			g_free (tmp2);
			print_string ("  Prefix", tmp);
			g_free (tmp);

			tmp = ip4_address_as_string (nm_ip4_address_get_gateway (addr));
			print_string ("  Gateway", tmp);
			g_free (tmp);
			printf ("\n");
		}

		array = nm_ip4_config_get_nameservers (cfg);
		if (array) {
			int i;

			for (i = 0; i < array->len; i++) {
				tmp = ip4_address_as_string (g_array_index (array, guint32, i));
				print_string ("  DNS", tmp);
				g_free (tmp);
			}
		}
	}

	printf ("\n\n");
}


int
main (int argc, char *argv[])
{
	NMClient *client;
	const GPtrArray *devices;

	g_type_init ();

	client = nm_client_new ();
	if (!client) {
		exit (1);
	}

	printf ("\nNetworkManager Tool\n\n");

	if (!get_nm_state (client)) {
		fprintf (stderr, "\n\nNetworkManager appears not to be running (could not get its state).\n");
		exit (1);
	}

	devices = nm_client_get_devices (client);
	g_ptr_array_foreach ((GPtrArray *) devices, detail_device, client);

	g_object_unref (client);

	return 0;
}
