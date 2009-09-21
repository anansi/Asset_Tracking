/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dbus/dbus-glib.h>
#include <nm-setting.h>
#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-vpn.h>
#include <nm-utils.h>
#include <string.h>
#include <arpa/inet.h>

#include "nm-dbus-glib-types.h"
#include "writer.h"
#include "reader.h"

static gboolean
write_array_of_uint (GKeyFile *file,
                     NMSetting *setting,
                     const char *key,
                     const GValue *value)
{
	GArray *array;
	int i;

	array = (GArray *) g_value_get_boxed (value);
	if (!array || !array->len)
		return TRUE;

	if (NM_IS_SETTING_IP4_CONFIG (setting) && !strcmp (key, NM_SETTING_IP4_CONFIG_DNS)) {
		char **list;

		list = g_new0 (char *, array->len + 1);

		for (i = 0; i < array->len; i++) {
			char buf[INET_ADDRSTRLEN + 1];
			struct in_addr addr;

			addr.s_addr = g_array_index (array, guint32, i);
			if (!inet_ntop (AF_INET, &addr, buf, sizeof (buf))) {
				nm_warning ("%s: error converting IP4 address 0x%X",
				            __func__, ntohl (addr.s_addr));
				list[i] = NULL;
			} else {
				list[i] = g_strdup (buf);
			}
		}

		g_key_file_set_string_list (file, nm_setting_get_name (setting), key, (const char **) list, array->len);
		g_strfreev (list);
	} else {
		int *tmp_array;

		tmp_array = g_new (gint, array->len);
		for (i = 0; i < array->len; i++)
			tmp_array[i] = g_array_index (array, int, i);

		g_key_file_set_integer_list (file, nm_setting_get_name (setting), key, tmp_array, array->len);
		g_free (tmp_array);
	}

	return TRUE;
}

static void
write_ip4_values (GKeyFile *file,
                  const char *setting_name,
                  const char *key,
                  GPtrArray *array,
                  guint32 tuple_len,
                  guint32 addr1_pos,
                  guint32 addr2_pos)
{
	char **list = NULL;
	int i, j;

	list = g_malloc (tuple_len);

	for (i = 0, j = 0; i < array->len; i++, j++) {
		GArray *tuple = g_ptr_array_index (array, i);
		gboolean success = TRUE;
		char *key_name;
		int k;

		memset (list, 0, tuple_len);

		for (k = 0; k < tuple_len; k++) {
			if (k == addr1_pos || k == addr2_pos) {
				char buf[INET_ADDRSTRLEN + 1];
				struct in_addr addr;

				/* IP addresses */
				addr.s_addr = g_array_index (tuple, guint32, k);
				if (!inet_ntop (AF_INET, &addr, buf, sizeof (buf))) {
					nm_warning ("%s: error converting IP4 address 0x%X",
					            __func__, ntohl (addr.s_addr));
					success = FALSE;
					break;
				} else {
					list[k] = g_strdup (buf);
				}
			} else {
				/* prefix, metric */
				list[k] = g_strdup_printf ("%d", g_array_index (tuple, guint32, k));
			}
		}

		if (success) {
			key_name = g_strdup_printf ("%s%d", key, j + 1);
			g_key_file_set_string_list (file, setting_name, key_name, (const char **) list, tuple_len);
			g_free (key_name);
		}

		for (k = 0; k < tuple_len; k++)
			g_free (list[k]);
	}
	g_free (list);
}

static gboolean
write_array_of_array_of_uint (GKeyFile *file,
                              NMSetting *setting,
                              const char *key,
                              const GValue *value)
{
	GPtrArray *array;

	/* Only handle IPv4 addresses and routes for now */
	if (!NM_IS_SETTING_IP4_CONFIG (setting))
		return FALSE;

	array = (GPtrArray *) g_value_get_boxed (value);
	if (!array || !array->len)
		return TRUE;

	if (!strcmp (key, NM_SETTING_IP4_CONFIG_ADDRESSES))
		write_ip4_values (file, nm_setting_get_name (setting), key, array, 3, 0, 2);
	else if (!strcmp (key, NM_SETTING_IP4_CONFIG_ROUTES))
		write_ip4_values (file, nm_setting_get_name (setting), key, array, 4, 0, 2);

	return TRUE;
}

typedef struct {
	GKeyFile *file;
	const char *setting_name;
} WriteStringHashInfo;

static void
write_hash_of_string_helper (gpointer key, gpointer data, gpointer user_data)
{
	WriteStringHashInfo *info = (WriteStringHashInfo *) user_data;
	const char *property = (const char *) key;
	const char *value = (const char *) data;

	g_key_file_set_string (info->file,
	                       info->setting_name,
	                       property,
	                       value);
}

static void
write_hash_of_string (GKeyFile *file,
                      NMSetting *setting,
                      const char *key,
                      const GValue *value)
{
	GHashTable *hash = g_value_get_boxed (value);
	WriteStringHashInfo info;

	info.file = file;

	/* Write VPN secrets out to a different group to keep them separate */
	if (   (G_OBJECT_TYPE (setting) == NM_TYPE_SETTING_VPN)
	    && !strcmp (key, NM_SETTING_VPN_SECRETS)) {
		info.setting_name = VPN_SECRETS_GROUP;
	} else
		info.setting_name = nm_setting_get_name (setting);

	g_hash_table_foreach (hash, write_hash_of_string_helper, &info);
}

static void
write_setting_value (NMSetting *setting,
				 const char *key,
				 const GValue *value,
				 gboolean secret,
				 gpointer user_data)
{
	GKeyFile *file = (GKeyFile *) user_data;
	const char *setting_name;
	GType type;

	type = G_VALUE_TYPE (value);

	/* Setting name gets picked up from the keyfile's section name instead */
	if (!strcmp (key, NM_SETTING_NAME))
		return;

	/* Don't write the NMSettingConnection object's 'read-only' property */
	if (   NM_IS_SETTING_CONNECTION (setting)
	    && !strcmp (key, NM_SETTING_CONNECTION_READ_ONLY))
		return;

	setting_name = nm_setting_get_name (setting);

	if (type == G_TYPE_STRING) {
		const char *str;

		str = g_value_get_string (value);
		if (str)
			g_key_file_set_string (file, setting_name, key, str);
	} else if (type == G_TYPE_UINT)
		g_key_file_set_integer (file, setting_name, key, (int) g_value_get_uint (value));
	else if (type == G_TYPE_INT)
		g_key_file_set_integer (file, setting_name, key, g_value_get_int (value));
	else if (type == G_TYPE_UINT64) {
		char *numstr;

		numstr = g_strdup_printf ("%" G_GUINT64_FORMAT, g_value_get_uint64 (value));
		g_key_file_set_value (file, setting_name, key, numstr);
		g_free (numstr);
	} else if (type == G_TYPE_BOOLEAN) {
		g_key_file_set_boolean (file, setting_name, key, g_value_get_boolean (value));
	} else if (type == G_TYPE_CHAR) {
		g_key_file_set_integer (file, setting_name, key, (int) g_value_get_char (value));
	} else if (type == DBUS_TYPE_G_UCHAR_ARRAY) {
		GByteArray *array;

		array = (GByteArray *) g_value_get_boxed (value);
		if (array && array->len > 0) {
			int *tmp_array;
			int i;

			tmp_array = g_new (gint, array->len);
			for (i = 0; i < array->len; i++)
				tmp_array[i] = (int) array->data[i];

			g_key_file_set_integer_list (file, setting_name, key, tmp_array, array->len);
			g_free (tmp_array);
		}
	} else if (type == dbus_g_type_get_collection ("GSList", G_TYPE_STRING)) {
		GSList *list;
		GSList *iter;

		list = (GSList *) g_value_get_boxed (value);
		if (list) {
			char **array;
			int i = 0;

			array = g_new (char *, g_slist_length (list));
			for (iter = list; iter; iter = iter->next)
				array[i++] = iter->data;

			g_key_file_set_string_list (file, setting_name, key, (const gchar **const) array, i);
			g_free (array);
		}
	} else if (type == dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_STRING)) {
		write_hash_of_string (file, setting, key, value);
	} else if (type == DBUS_TYPE_G_UINT_ARRAY) {
		if (!write_array_of_uint (file, setting, key, value)) {
			g_warning ("Unhandled setting property type (write) '%s/%s' : '%s'", 
					 setting_name, key, g_type_name (type));
		}
	} else if (type == DBUS_TYPE_G_ARRAY_OF_ARRAY_OF_UINT) {
		if (!write_array_of_array_of_uint (file, setting, key, value)) {
			g_warning ("Unhandled setting property type (write) '%s/%s' : '%s'", 
					 setting_name, key, g_type_name (type));
		}
	} else {
		g_warning ("Unhandled setting property type (write) '%s/%s' : '%s'", 
				 setting_name, key, g_type_name (type));
	}
}

char *
writer_id_to_filename (const char *id)
{
	char *filename, *f;
	const char *i = id;

	f = filename = g_malloc0 (strlen (id) + 1);

	/* Convert '/' to '*' */
	while (*i) {
		if (*i == '/')
			*f++ = '*';
		else
			*f++ = *i;
		i++;
	}

	return filename;
}

gboolean
write_connection (NMConnection *connection, char **out_path, GError **error)
{
	NMSettingConnection *s_con;
	GKeyFile *key_file;
	char *data;
	gsize len;
	gboolean success = FALSE;
	char *filename, *path;

	if (out_path)
		g_return_val_if_fail (*out_path == NULL, FALSE);

	s_con = NM_SETTING_CONNECTION (nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION));
	if (!s_con)
		return success;

	key_file = g_key_file_new ();
	nm_connection_for_each_setting_value (connection, write_setting_value, key_file);
	data = g_key_file_to_data (key_file, &len, error);
	if (!data)
		goto out;

	filename = writer_id_to_filename (nm_setting_connection_get_id (s_con));
	path = g_build_filename (KEYFILE_DIR, filename, NULL);
	g_free (filename);

	g_file_set_contents (path, data, len, error);
	chmod (path, S_IRUSR | S_IWUSR);
	if (chown (path, 0, 0) < 0) {
		g_warning ("Error chowning '%s': %d", path, errno);
		unlink (path);
	} else {
		if (out_path)
			*out_path = g_strdup (path);
		success = TRUE;
	}
	g_free (path);

out:
	g_free (data);
	g_key_file_free (key_file);
	return success;
}
