/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dbus/dbus.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include "cras_bt_device.h"
#include "cras_bt_endpoint.h"
#include "cras_bt_transport.h"
#include "cras_bt_constants.h"
#include "utlist.h"

#define BTPROTO_SCO 2


struct cras_bt_transport {
	DBusConnection *conn;
	char *object_path;
	struct cras_bt_device *device;
	enum cras_bt_device_profile profile;
	int codec;
	void *configuration;
	int configuration_len;
	enum cras_bt_transport_state state;
	int fd;
	uint16_t read_mtu;
	uint16_t write_mtu;

	struct cras_bt_endpoint *endpoint;
	struct cras_bt_transport *prev, *next;
};

static struct cras_bt_transport *transports;

struct cras_bt_transport *cras_bt_transport_create(DBusConnection *conn,
						   const char *object_path)
{
	struct cras_bt_transport *transport;

	transport = calloc(1, sizeof(*transport));
	if (transport == NULL)
		return NULL;

	transport->object_path = strdup(object_path);
	if (transport->object_path == NULL) {
		free(transport);
		return NULL;
	}

	transport->conn = conn;
	dbus_connection_ref(transport->conn);

	transport->fd = -1;

	DL_APPEND(transports, transport);

	return transport;
}

void cras_bt_transport_set_endpoint(struct cras_bt_transport *transport,
				    struct cras_bt_endpoint *endpoint) {
	transport->endpoint = endpoint;
}

void cras_bt_transport_destroy(struct cras_bt_transport *transport)
{
	DL_DELETE(transports, transport);

	dbus_connection_unref(transport->conn);

	if (transport->fd >= 0)
		close(transport->fd);

	free(transport->object_path);
	free(transport->configuration);
	free(transport);
}

void cras_bt_transport_reset()
{
	while (transports) {
		syslog(LOG_INFO, "Bluetooth Transport: %s removed",
		       transports->object_path);
		cras_bt_transport_destroy(transports);
	}
}


struct cras_bt_transport *cras_bt_transport_get(const char *object_path)
{
	struct cras_bt_transport *transport;

	DL_FOREACH(transports, transport) {
		if (strcmp(transport->object_path, object_path) == 0)
			return transport;
	}

	return NULL;
}

size_t cras_bt_transport_get_list(
	struct cras_bt_transport ***transport_list_out)
{
	struct cras_bt_transport *transport;
	struct cras_bt_transport **transport_list = NULL;
	size_t num_transports = 0;

	DL_FOREACH(transports, transport) {
		struct cras_bt_transport **tmp;

		tmp = realloc(transport_list,
			      sizeof(transport_list[0]) * (num_transports + 1));
		if (!tmp) {
			free(transport_list);
			return -ENOMEM;
		}

		transport_list = tmp;
		transport_list[num_transports++] = transport;
	}

	*transport_list_out = transport_list;
	return num_transports;
}

const char *cras_bt_transport_object_path(
	const struct cras_bt_transport *transport)
{
	return transport->object_path;
}

struct cras_bt_device *cras_bt_transport_device(
	const struct cras_bt_transport *transport)
{
	return transport->device;
}

enum cras_bt_device_profile cras_bt_transport_profile(
	const struct cras_bt_transport *transport)
{
	return transport->profile;
}

int cras_bt_transport_codec(const struct cras_bt_transport *transport)
{
	return transport->codec;
}

int cras_bt_transport_configuration(const struct cras_bt_transport *transport,
				    void *configuration, int len)
{
	if (len < transport->configuration_len)
		return -ENOSPC;

	memcpy(configuration, transport->configuration,
	       transport->configuration_len);

	return 0;
}

enum cras_bt_transport_state cras_bt_transport_state(
	const struct cras_bt_transport *transport)
{
	return transport->state;
}

struct cras_bt_endpoint *cras_bt_transport_endpoint(
	const struct cras_bt_transport *transport)
{
	return transport->endpoint;
}

int cras_bt_transport_fd(const struct cras_bt_transport *transport)
{
	return transport->fd;
}

uint16_t cras_bt_transport_read_mtu(const struct cras_bt_transport *transport)
{
	return transport->read_mtu;
}

uint16_t cras_bt_transport_write_mtu(const struct cras_bt_transport *transport)
{
	return transport->write_mtu;
}

static enum cras_bt_transport_state cras_bt_transport_state_from_string(
	const char *value)
{
	if (strcmp("idle", value) == 0)
		return CRAS_BT_TRANSPORT_STATE_IDLE;
	else if (strcmp("pending", value) == 0)
		return CRAS_BT_TRANSPORT_STATE_PENDING;
	else if (strcmp("active", value) == 0)
		return CRAS_BT_TRANSPORT_STATE_ACTIVE;
	else
		return CRAS_BT_TRANSPORT_STATE_IDLE;
}

static const char *cras_bt_transport_state_to_string(
		enum cras_bt_transport_state state)
{
	switch (state) {
	case CRAS_BT_TRANSPORT_STATE_IDLE:
		return "idle";
	case CRAS_BT_TRANSPORT_STATE_PENDING:
		return "pending";
	case CRAS_BT_TRANSPORT_STATE_ACTIVE:
		return "active";
	default:
		return "(unknown)";
	}
}

static void cras_bt_transport_state_changed(struct cras_bt_transport *transport)
{
	if (!transport->endpoint)
		return;

	/* Transport state change is an asynchronous notification while the
	 * stream fd is updated in blocking calls (acquire and release).
	 *
	 * Don't modify the endpoint at here, since the transport state and
	 * stream fd could not be synced.  The state of transport is handled in
	 * life cycle of cras_bt_endpoint. Place transport state and fd in
	 * debug log to monitor the state transitionfor debugging.
	 *
	 * TODO(hychao): remove debug log when a2dp issues are fixed.
	 */
	syslog(LOG_ERR, "Transport state changed to %s, while stream fd %d",
			cras_bt_transport_state_to_string(transport->state),
			transport->fd);
}

void cras_bt_transport_fill_properties(struct cras_bt_transport *transport,
				       int fd, const char *uuid)
{
	transport->device = cras_bt_device_get(transport->object_path);
	transport->profile = cras_bt_device_profile_from_uuid(uuid);
	free(transport->configuration);

	/* For HFP, the configuration is just the file descriptor of
	 * the rfcomm socket */
	transport->configuration = (int *)malloc(sizeof(fd));
	memcpy(transport->configuration, &fd, sizeof(fd));
	transport->configuration_len = sizeof(fd);
}

/* Converts bluetooth address string into sockaddr structure. The address
 * string is expected of the form 1A:2B:3C:4D:5E:6F, and each of the six
 * hex values will be parsed into sockaddr in inverse order.
 * Args:
 *    str - The string version of bluetooth address
 *    addr - The struct to be filled with converted address
 */
static int bt_address(const char *str, struct sockaddr *addr)
{
	int i;

	if (strlen(str) != 17) {
		syslog(LOG_ERR, "Invalid bluetooth address %s", str);
		return -1;
	}

	memset(addr, 0, sizeof(*addr));
	addr->sa_family = AF_BLUETOOTH;
	for (i = 5; i >= 0; i--) {
		addr->sa_data[i] = (unsigned char)strtol(str, NULL, 16);
		str += 3;
	}

	return 0;
}

int cras_bt_transport_sco_connect(struct cras_bt_transport *transport)
{
	int sk, err;
	struct sockaddr addr;

	struct cras_bt_device *device = transport->device;
	struct cras_bt_adapter *adapter;

	adapter = cras_bt_device_adapter(device);

	sk = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO);
	if (sk < 0) {
		syslog(LOG_ERR, "Failed to create socket: %s (%d)",
				strerror(errno), errno);
		return -errno;
	}

	/* Bind to local address */
	if (bt_address(cras_bt_adapter_address(adapter), &addr))
		goto error;
	if (bind(sk, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		syslog(LOG_ERR, "Failed to bind socket: %s (%d)",
				strerror(errno), errno);
		goto error;
	}

	/* Connect to remote */
	if (bt_address(cras_bt_device_address(device), &addr))
		goto error;
	err = connect(sk, (struct sockaddr *) &addr, sizeof(addr));
	if (err < 0 && !(errno == EAGAIN || errno == EINPROGRESS)) {
		syslog(LOG_ERR, "Failed to connect: %s (%d)",
				strerror(errno), errno);
		goto error;
	}

	return sk;

error:
	return -1;
}

void cras_bt_transport_update_properties(
	struct cras_bt_transport *transport,
	DBusMessageIter *properties_array_iter,
	DBusMessageIter *invalidated_array_iter)
{
	while (dbus_message_iter_get_arg_type(properties_array_iter) !=
	       DBUS_TYPE_INVALID) {
		DBusMessageIter properties_dict_iter, variant_iter;
		const char *key;
		int type;

		dbus_message_iter_recurse(properties_array_iter,
					  &properties_dict_iter);

		dbus_message_iter_get_basic(&properties_dict_iter, &key);
		dbus_message_iter_next(&properties_dict_iter);

		dbus_message_iter_recurse(&properties_dict_iter, &variant_iter);
		type = dbus_message_iter_get_arg_type(&variant_iter);

		if (type == DBUS_TYPE_STRING) {
			const char *value;

			dbus_message_iter_get_basic(&variant_iter, &value);

			if (strcmp(key, "UUID") == 0) {
				transport->profile =
					cras_bt_device_profile_from_uuid(value);

			} else if (strcmp(key, "State") == 0) {
				enum cras_bt_transport_state old_state =
					transport->state;
				transport->state =
					cras_bt_transport_state_from_string(
						value);
				if (transport->state != old_state)
					cras_bt_transport_state_changed(
						transport);
			}

		} else if (type == DBUS_TYPE_BYTE) {
			int value;

			dbus_message_iter_get_basic(&variant_iter, &value);

			if (strcmp(key, "Codec") == 0)
				transport->codec = value;
		} else if (type == DBUS_TYPE_OBJECT_PATH) {
			const char *obj_path;

			dbus_message_iter_get_basic(&variant_iter, &obj_path);

			if (strcmp(key, "Device") == 0)
				transport->device =
					cras_bt_device_get(obj_path);
		} else if (strcmp(
				dbus_message_iter_get_signature(&variant_iter),
				"ay") == 0 &&
			   strcmp(key, "Configuration") == 0) {
			DBusMessageIter value_iter;
			char *value;
			int len;

			dbus_message_iter_recurse(&variant_iter, &value_iter);
			dbus_message_iter_get_fixed_array(&value_iter,
							  &value, &len);

			free(transport->configuration);
			transport->configuration_len = 0;

			transport->configuration = malloc(len);
			if (transport->configuration) {
				memcpy(transport->configuration, value, len);
				transport->configuration_len = len;
			}

		}

		dbus_message_iter_next(properties_array_iter);
	}

	while (invalidated_array_iter &&
	       dbus_message_iter_get_arg_type(invalidated_array_iter) !=
	       DBUS_TYPE_INVALID) {
		const char *key;

		dbus_message_iter_get_basic(invalidated_array_iter, &key);

		if (strcmp(key, "Device") == 0) {
			transport->device = NULL;
		} else if (strcmp(key, "UUID") == 0) {
			transport->profile = 0;
		} else if (strcmp(key, "State") == 0) {
			transport->state = CRAS_BT_TRANSPORT_STATE_IDLE;
		} else if (strcmp(key, "Codec") == 0) {
			transport->codec = 0;
		} else if (strcmp(key, "Configuration") == 0) {
			free(transport->configuration);
			transport->configuration = NULL;
			transport->configuration_len = 0;
		}

		dbus_message_iter_next(invalidated_array_iter);
	}
}

int cras_bt_transport_acquire(struct cras_bt_transport *transport)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	if (transport->fd >= 0)
		return 0;

	method_call = dbus_message_new_method_call(
		BLUEZ_SERVICE,
		transport->object_path,
		BLUEZ_INTERFACE_MEDIA_TRANSPORT,
		"Acquire");
	if (!method_call)
		return -ENOMEM;

	dbus_error_init(&dbus_error);

	reply = dbus_connection_send_with_reply_and_block(
		transport->conn,
		method_call,
		DBUS_TIMEOUT_USE_DEFAULT,
		&dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to acquire transport %s: %s",
		       transport->object_path, dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "Acquire returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}

	if (!dbus_message_get_args(reply, &dbus_error,
				   DBUS_TYPE_UNIX_FD, &(transport->fd),
				   DBUS_TYPE_UINT16, &(transport->read_mtu),
				   DBUS_TYPE_UINT16, &(transport->write_mtu),
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_ERR, "Bad Acquire reply received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(reply);
		return -EINVAL;
	}

	dbus_message_unref(reply);
	return 0;
}

int cras_bt_transport_release(struct cras_bt_transport *transport)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	if (transport->fd < 0)
		return 0;

	/* Close the transport on our end no matter whether or not the server
	 * gives us an error.
	 */
	close(transport->fd);
	transport->fd = -1;

	method_call = dbus_message_new_method_call(
		BLUEZ_SERVICE,
		transport->object_path,
		BLUEZ_INTERFACE_MEDIA_TRANSPORT,
		"Release");
	if (!method_call)
		return -ENOMEM;

	dbus_error_init(&dbus_error);

	reply = dbus_connection_send_with_reply_and_block(
		transport->conn,
		method_call,
		DBUS_TIMEOUT_USE_DEFAULT,
		&dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to release transport %s: %s",
		       transport->object_path, dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "Release returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}

	dbus_message_unref(reply);
	return 0;
}
