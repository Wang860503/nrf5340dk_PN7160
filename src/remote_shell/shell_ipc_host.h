/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SHELL_IPC_HOST_H_
#define SHELL_IPC_HOST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/shell/shell.h>

#ifdef __cplusplus
extern "C" {
#endif

/**@brief IPC shell data received callback function.
 *
 * @param[in] data Pointer to the received data.
 * @param[in] len Received data length.
 * @param[in] context Pointer to user context, set by @ref shell_ipc_host_init.
 */
typedef void (*shell_ipc_host_recv_cb)(const uint8_t* data, size_t len,
                                       void* context);

/**@brief Initializes the Shell over IPC service.
 *
 * @param[in] cb The IPC shell data received callback.
 * @param[in] context User context data.
 *
 * @retval 0 If all operations are successful.
 *           Otherwise, a (negative) error code is returned.
 */
int shell_ipc_host_init(shell_ipc_host_recv_cb cb, void* context);

/**@brief Deinitializes the Shell over IPC service (best-effort).
 *
 * Deregisters the "remote shell" endpoint only (shared IPC instance may stay open).
 */
int shell_ipc_host_deinit(void);

/**@brief Returns true if IPC endpoint is currently bound. */
bool shell_ipc_host_is_connected(void);

/**@brief Returns true if remote_shell bridge thread is running. */
bool shell_ipc_host_is_bridge_active(void);

/**@brief Sets remote_shell bridge running state. */
void shell_ipc_host_set_bridge_active(bool active);

/**@brief Re-register the IPC endpoint with a dummy callback if needed. */
int shell_ipc_host_prebind(void);

/**@brief Writes data to the remote shell over the IPC Service.
 *
 * @param[in] data Data to write.
 * @paaram[in] len Data length.
 *
 * @retval 0 If all operations are successful.
 *           Otherwise, a (negative) error code is returned.
 */
int shell_ipc_host_write(const uint8_t* data, size_t len);

int remote_shell_start(const struct shell* sh);
int remote_shell_stop(const struct shell* sh);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_IPC_HOST_H_ */
