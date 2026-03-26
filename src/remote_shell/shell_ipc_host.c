/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "shell_ipc_host.h"

LOG_MODULE_REGISTER(shell_ipc_host, CONFIG_LOG_DEFAULT_LEVEL);

struct shell_ipc_host {
	struct ipc_ept ept;
	struct ipc_ept_cfg ept_cfg;
	struct k_sem ipc_bond_sem;
	atomic_t connected;
	const struct device *instance;
	shell_ipc_host_recv_cb cb;
	void *context;
};

static struct shell_ipc_host ipc_host;
static atomic_t bridge_active;

static void prebind_drop(const uint8_t *data, size_t len, void *context)
{
	(void)data;
	(void)len;
	(void)context;
}

static void bound(void *priv)
{
	struct shell_ipc_host *ipc = priv;

	atomic_set(&ipc->connected, 1);
	k_sem_give(&ipc->ipc_bond_sem);
}

static void unbound(void *priv)
{
	struct shell_ipc_host *ipc = priv;

	atomic_clear(&ipc->connected);
	LOG_WRN("IPC endpoint unbound (remote reset/disconnect)");
}

static void received(const void *data, size_t len, void *priv)
{
	struct shell_ipc_host *ipc = priv;

	if (!data || (len == 0)) {
		return;
	}

	if (ipc->cb) {
		ipc->cb(data, len, ipc->context);
	}
}

static int ipc_init(void)
{
	int err;
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_ipc));

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	ipc_host.instance = dev;

	err = ipc_service_open_instance(dev);
	if (err && (err != -EALREADY)) {
		return err;
	}

	err = k_sem_init(&ipc_host.ipc_bond_sem, 0, 1);
	if (err) {
		return err;
	}

	ipc_host.ept_cfg.name = "remote shell";
	ipc_host.ept_cfg.cb.bound = bound;
	ipc_host.ept_cfg.cb.unbound = unbound;
	ipc_host.ept_cfg.cb.received = received;
	ipc_host.ept_cfg.priv = &ipc_host;

	err = ipc_service_register_endpoint(dev, &ipc_host.ept, &ipc_host.ept_cfg);
	if (err) {
		return err;
	}

	LOG_WRN("Waiting for network core IPC endpoint \"%s\"…",
		ipc_host.ept_cfg.name);

	err = k_sem_take(&ipc_host.ipc_bond_sem, K_MSEC(2000));
	if (err) {
		LOG_ERR("IPC endpoint bind timeout (%d)", err);
		(void)ipc_service_deregister_endpoint(&ipc_host.ept);
		ipc_host.ept.instance = NULL;
		ipc_host.ept.token = NULL;
		ipc_host.instance = NULL;
		atomic_clear(&ipc_host.connected);
		return -ETIMEDOUT;
	}

	return 0;
}

int shell_ipc_host_init(shell_ipc_host_recv_cb cb, void *context)
{
	if (!cb) {
		return -EINVAL;
	}

	ipc_host.cb = cb;
	ipc_host.context = context;

	/* Already initialized: only update callback/context. */
	if (ipc_host.ept.instance) {
		return 0;
	}

	return ipc_init();
}

int shell_ipc_host_write(const uint8_t *data, size_t len)
{
	if (!data || (len == 0)) {
		return -EINVAL;
	}

	__ASSERT_NO_MSG(ipc_host.ept.instance);

	return ipc_service_send(&ipc_host.ept, data, len);
}

int shell_ipc_host_deinit(void)
{
	int err = 0;

	if (ipc_host.ept.instance) {
		int derr = ipc_service_deregister_endpoint(&ipc_host.ept);
		if (derr && derr != -ENOENT) {
			err = derr;
		}
	}

	ipc_host.ept.instance = NULL;
	ipc_host.ept.token = NULL;
	ipc_host.instance = NULL;
	atomic_clear(&ipc_host.connected);

	return err;
}

bool shell_ipc_host_is_connected(void)
{
	return atomic_get(&ipc_host.connected) != 0;
}

bool shell_ipc_host_is_bridge_active(void)
{
	return atomic_get(&bridge_active) != 0;
}

void shell_ipc_host_set_bridge_active(bool active)
{
	if (active) {
		atomic_set(&bridge_active, 1);
	} else {
		atomic_clear(&bridge_active);
	}
}

int shell_ipc_host_prebind(void)
{
	/* Avoid overriding callbacks while remote_shell bridge is running. */
	if (shell_ipc_host_is_bridge_active()) {
		return 0;
	}

	/* (Re-)register endpoint with a dummy RX callback. */
	return shell_ipc_host_init(prebind_drop, NULL);
}
