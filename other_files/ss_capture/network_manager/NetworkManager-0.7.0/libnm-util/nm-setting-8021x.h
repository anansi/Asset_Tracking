/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
 * Dan Williams <dcbw@redhat.com>
 * Tambet Ingo <tambet@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2007 - 2008 Red Hat, Inc.
 * (C) Copyright 2007 - 2008 Novell, Inc.
 */

#ifndef NM_SETTING_8021X_H
#define NM_SETTING_8021X_H

#include <nm-setting.h>

G_BEGIN_DECLS

#define NM_TYPE_SETTING_802_1X            (nm_setting_802_1x_get_type ())
#define NM_SETTING_802_1X(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_SETTING_802_1X, NMSetting8021x))
#define NM_SETTING_802_1X_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_SETTING_802_1X, NMSetting8021xClass))
#define NM_IS_SETTING_802_1X(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_SETTING_802_1X))
#define NM_IS_SETTING_802_1X_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_SETTING_802_1X))
#define NM_SETTING_802_1X_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_SETTING_802_1X, NMSetting8021xClass))

#define NM_SETTING_802_1X_SETTING_NAME "802-1x"

typedef enum
{
	NM_SETTING_802_1X_ERROR_UNKNOWN = 0,
	NM_SETTING_802_1X_ERROR_INVALID_PROPERTY,
	NM_SETTING_802_1X_ERROR_MISSING_PROPERTY
} NMSetting8021xError;

#define NM_TYPE_SETTING_802_1X_ERROR (nm_setting_802_1x_error_get_type ()) 
GType nm_setting_802_1x_error_get_type (void);

#define NM_SETTING_802_1X_ERROR nm_setting_802_1x_error_quark ()
GQuark nm_setting_802_1x_error_quark (void);


#define NM_SETTING_802_1X_EAP "eap"
#define NM_SETTING_802_1X_IDENTITY "identity"
#define NM_SETTING_802_1X_ANONYMOUS_IDENTITY "anonymous-identity"
#define NM_SETTING_802_1X_CA_CERT "ca-cert"
#define NM_SETTING_802_1X_CA_PATH "ca-path"
#define NM_SETTING_802_1X_CLIENT_CERT "client-cert"
#define NM_SETTING_802_1X_PHASE1_PEAPVER "phase1-peapver"
#define NM_SETTING_802_1X_PHASE1_PEAPLABEL "phase1-peaplabel"
#define NM_SETTING_802_1X_PHASE1_FAST_PROVISIONING "phase1-fast-provisioning"
#define NM_SETTING_802_1X_PHASE2_AUTH "phase2-auth"
#define NM_SETTING_802_1X_PHASE2_AUTHEAP "phase2-autheap"
#define NM_SETTING_802_1X_PHASE2_CA_CERT "phase2-ca-cert"
#define NM_SETTING_802_1X_PHASE2_CA_PATH "phase2-ca-path"
#define NM_SETTING_802_1X_PHASE2_CLIENT_CERT "phase2-client-cert"
#define NM_SETTING_802_1X_PASSWORD "password"
#define NM_SETTING_802_1X_PRIVATE_KEY "private-key"
#define NM_SETTING_802_1X_PHASE2_PRIVATE_KEY "phase2-private-key"
#define NM_SETTING_802_1X_PIN "pin"
#define NM_SETTING_802_1X_PSK "psk"

typedef struct {
	NMSetting parent;
} NMSetting8021x;

typedef struct {
	NMSettingClass parent;
} NMSetting8021xClass;

GType nm_setting_802_1x_get_type (void);

NMSetting *nm_setting_802_1x_new (void);

guint32           nm_setting_802_1x_get_num_eap_methods              (NMSetting8021x *setting);
const char *      nm_setting_802_1x_get_eap_method                   (NMSetting8021x *setting, guint32 i);
gboolean          nm_setting_802_1x_add_eap_method                   (NMSetting8021x *setting, const char *eap);
void              nm_setting_802_1x_remove_eap_method                (NMSetting8021x *setting, guint32 i);
void              nm_setting_802_1x_clear_eap_methods                (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_identity                     (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_anonymous_identity           (NMSetting8021x *setting);

const GByteArray *nm_setting_802_1x_get_ca_cert                      (NMSetting8021x *setting);
const char *      nm_setting_802_1x_get_ca_path                      (NMSetting8021x *setting);
gboolean          nm_setting_802_1x_set_ca_cert_from_file            (NMSetting8021x *setting,
                                                                      const char *filename,
                                                                      GError **err);

const GByteArray *nm_setting_802_1x_get_client_cert                  (NMSetting8021x *setting);
gboolean          nm_setting_802_1x_set_client_cert_from_file        (NMSetting8021x *setting,
                                                                      const char *filename,
                                                                      GError **err);

const char *      nm_setting_802_1x_get_phase1_peapver               (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_phase1_peaplabel             (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_phase1_fast_provisioning     (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_phase2_auth                  (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_phase2_autheap               (NMSetting8021x *setting);

const GByteArray *nm_setting_802_1x_get_phase2_ca_cert               (NMSetting8021x *setting);
const char *      nm_setting_802_1x_get_phase2_ca_path               (NMSetting8021x *setting);
gboolean          nm_setting_802_1x_set_phase2_ca_cert_from_file     (NMSetting8021x *setting,
                                                                      const char *filename,
                                                                      GError **err);

const GByteArray *nm_setting_802_1x_get_phase2_client_cert           (NMSetting8021x *setting);
gboolean          nm_setting_802_1x_set_phase2_client_cert_from_file (NMSetting8021x *setting,
                                                                      const char *filename,
                                                                      GError **err);

const char *      nm_setting_802_1x_get_password                     (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_pin                          (NMSetting8021x *setting);

const char *      nm_setting_802_1x_get_psk                          (NMSetting8021x *setting);

const GByteArray *nm_setting_802_1x_get_private_key                  (NMSetting8021x *setting);
gboolean          nm_setting_802_1x_set_private_key_from_file        (NMSetting8021x *setting,
                                                                      const char *filename,
                                                                      const char *password,
                                                                      GError **err);

const GByteArray *nm_setting_802_1x_get_phase2_private_key           (NMSetting8021x *setting);
gboolean          nm_setting_802_1x_set_phase2_private_key_from_file (NMSetting8021x *setting,
                                                                      const char *filename,
                                                                      const char *password,
                                                                      GError **err);

G_END_DECLS

#endif /* NM_SETTING_8021X_H */
