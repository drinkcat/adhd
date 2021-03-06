/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_DEVICE_H_
#define CRAS_BT_DEVICE_H_

#include <dbus/dbus.h>

struct cras_bt_adapter;
struct cras_bt_device;

enum cras_bt_device_profile {
	CRAS_BT_DEVICE_PROFILE_HEADSET         = (1 << 0),
	CRAS_BT_DEVICE_PROFILE_HEADSET_GATEWAY = (1 << 1),
	CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE     = (1 << 2),
	CRAS_BT_DEVICE_PROFILE_A2DP_SINK       = (1 << 3),
	CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE    = (1 << 4),
	CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET    = (1 << 5)
};

enum cras_bt_device_profile cras_bt_device_profile_from_uuid(const char *uuid);

struct cras_bt_device *cras_bt_device_create(const char *object_path);
void cras_bt_device_destroy(struct cras_bt_device *device);
void cras_bt_device_reset();

struct cras_bt_device *cras_bt_device_get(const char *object_path);
size_t cras_bt_device_get_list(struct cras_bt_device ***device_list_out);

const char *cras_bt_device_object_path(const struct cras_bt_device *device);
struct cras_bt_adapter *cras_bt_device_adapter(
	const struct cras_bt_device *device);
const char *cras_bt_device_address(const struct cras_bt_device *device);
const char *cras_bt_device_name(const struct cras_bt_device *device);
int cras_bt_device_paired(const struct cras_bt_device *device);
int cras_bt_device_trusted(const struct cras_bt_device *device);
int cras_bt_device_connected(const struct cras_bt_device *device);

int cras_bt_device_supports_profile(const struct cras_bt_device *device,
				    enum cras_bt_device_profile profile);

void cras_bt_device_update_properties(struct cras_bt_device *device,
				      DBusMessageIter *properties_array_iter,
				      DBusMessageIter *invalidated_array_iter);

#endif /* CRAS_BT_DEVICE_H_ */
