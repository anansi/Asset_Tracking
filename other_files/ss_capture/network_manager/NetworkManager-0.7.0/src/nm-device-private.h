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
 * (C) Copyright 2005 Red Hat, Inc.
 */

#ifndef NM_DEVICE_PRIVATE_H
#define NM_DEVICE_PRIVATE_H

#include "nm-device.h"

void nm_device_set_ip_iface (NMDevice *self, const char *iface);

void nm_device_set_device_type (NMDevice *dev, NMDeviceType type);

void nm_device_activate_schedule_stage3_ip_config_start (NMDevice *device);

void nm_device_state_changed (NMDevice *device,
                              NMDeviceState state,
                              NMDeviceStateReason reason);

gboolean nm_device_hw_bring_up (NMDevice *self, gboolean wait, gboolean *no_firmware);

void nm_device_hw_take_down (NMDevice *self, gboolean block);

void nm_device_handle_autoip4_event (NMDevice *self,
                                     const char *event,
                                     const char *address);

#endif	/* NM_DEVICE_PRIVATE_H */
