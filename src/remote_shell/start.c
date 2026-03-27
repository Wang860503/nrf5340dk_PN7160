#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>

#include "shell_ipc_host.h"

#define HOST_WAIT_TIME 1000000

LOG_MODULE_REGISTER(remote_shell);

RING_BUF_DECLARE(rx_ringbuf, CONFIG_REMOTE_SHELL_RX_RING_BUFFER_SIZE);

K_SEM_DEFINE(shell_ipc_write_sem, 0, 1);

static atomic_t stop_requested;

/* Exit bridge (not sent to net): Ctrl+A (0x01) then Ctrl+X (0x18). */

static uint8_t bridge_esc_state;

#define BRIDGE_ESC_BYTE1 0x01 /* Ctrl+A */

#define BRIDGE_ESC_BYTE2 0x18 /* Ctrl+X */

static void ipc_prebind_drop_cb(const uint8_t* data, size_t len,

                                void* context) {
  ARG_UNUSED(data);

  ARG_UNUSED(len);

  ARG_UNUSED(context);
}

static int remote_shell_ipc_prebind_init(void) {
  int err = shell_ipc_host_init(ipc_prebind_drop_cb, NULL);

  if (err) {
    LOG_WRN("IPC prebind failed: %d", err);
  }

  return 0;
}

SYS_INIT(remote_shell_ipc_prebind_init, APPLICATION, 1);

static void uart_dtr_wait(const struct device* dev) {
  if (IS_ENABLED(CONFIG_UART_LINE_CTRL)) {
    int dtr, err;

    while (true) {
      err = uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);

      if (err == -ENOSYS || err == -ENOTSUP) {
        break;
      }

      if (dtr) {
        break;
      }

      k_sleep(K_MSEC(100));
    }
  }
}

/* net -> host UART (polling; safe with shell owning IRQ/callback). */

static void ipc_host_receive(const uint8_t* data, size_t len, void* context) {
  ARG_UNUSED(context);

  if (!data || (len == 0)) {
    return;
  }

  const struct device* uart_dev =

      DEVICE_DT_GET(DT_CHOSEN(ncs_remote_shell_uart));

  if (IS_ENABLED(CONFIG_UART_LINE_CTRL)) {
    uart_dtr_wait(uart_dev);
  }

  for (size_t i = 0; i < len; i++) {
    uart_poll_out(uart_dev, data[i]);
  }
}

/* Shell UART RX arrives here (same UART as zephyr,shell-uart); do not hijack

 * uart_irq_callback — that fights the shell backend and breaks escape keys.

 */

static void remote_shell_bypass_cb(const struct shell* sh, uint8_t* recv,

                                   size_t len) {
  ARG_UNUSED(sh);

  bool wake = false;

  for (size_t i = 0; i < len; i++) {
    uint8_t b = recv[i];

    if (bridge_esc_state == 0) {
      if (b == BRIDGE_ESC_BYTE1) {
        bridge_esc_state = 1;

        continue;
      }

      if (ring_buf_put(&rx_ringbuf, &b, 1) > 0) {
        wake = true;
      }

    } else {
      bridge_esc_state = 0;

      if (b == BRIDGE_ESC_BYTE2) {
        atomic_set(&stop_requested, 1);

        wake = true;

        continue;
      }

      uint8_t c = BRIDGE_ESC_BYTE1;

      if (ring_buf_put(&rx_ringbuf, &c, 1) > 0) {
        wake = true;
      }

      if (b == BRIDGE_ESC_BYTE1) {
        bridge_esc_state = 1;

      } else if (ring_buf_put(&rx_ringbuf, &b, 1) > 0) {
        wake = true;
      }
    }
  }

  if (wake) {
    k_sem_give(&shell_ipc_write_sem);
  }
}

int remote_shell_start(const struct shell* sh) {
  int err;

  const struct device* dev = DEVICE_DT_GET(DT_CHOSEN(ncs_remote_shell_uart));

  uint32_t baudrate = 0;

  if (!sh) {
    return -EIO;
  }

  shell_ipc_host_set_bridge_active(true);

  atomic_clear(&stop_requested);

  bridge_esc_state = 0;

  if (!device_is_ready(dev)) {
    shell_error(sh, "Device not ready");

    return -ENODEV;
  }

  if (IS_ENABLED(CONFIG_USB_DEVICE_STACK)) {
    err = usb_enable(NULL);

    if (err != 0) {
      shell_error(sh, "Failed to enable USB, err %d", err);

      return err;
    }
  }

  if (IS_ENABLED(CONFIG_UART_LINE_CTRL)) {
    shell_info(sh, "Wait for DTR");

    uart_dtr_wait(dev);

    shell_info(sh, "DTR set");

    err = uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);

    if (err) {
      shell_warn(sh, "Failed to set DCD, err %d", err);
    }

    err = uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);

    if (err) {
      shell_warn(sh, "Failed to set DSR, err %d", err);
    }

    k_busy_wait(HOST_WAIT_TIME);

    err = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);

    if (err) {
      shell_warn(sh, "Failed to get baudrate, err %d", err);

    } else {
      shell_warn(sh, "Baudrate detected: %d", baudrate);
    }
  }

  shell_info(sh, "Connecting IPC shell (waiting for network core)…");

  shell_info(sh, "Back to uart:~$ : Ctrl+A then Ctrl+X");

  err = shell_ipc_host_init(ipc_host_receive, NULL);

  if (err) {
    shell_error(sh, "Shell IPC host initialization failed, err %d", err);

    shell_ipc_host_set_bridge_active(false);

    return err;
  }

  shell_set_bypass(sh, remote_shell_bypass_cb);

  while (!atomic_get(&stop_requested)) {
    uint8_t* data;

    uint32_t data_size;

    (void)k_sem_take(&shell_ipc_write_sem, K_MSEC(200));

    if (!shell_ipc_host_is_connected()) {
      shell_warn(sh, "IPC disconnected, exiting bridge");

      break;
    }

    data_size = ring_buf_get_claim(&rx_ringbuf, &data, rx_ringbuf.size);

    if (data_size) {
      int w = shell_ipc_host_write(data, data_size);

      if (w >= 0) {
        ring_buf_get_finish(&rx_ringbuf, (uint32_t)w);

      } else {
        shell_warn(sh, "IPC write failed (%d), exiting bridge", w);

        break;
      }
    }
  }

  shell_set_bypass(sh, NULL);

  /* Keep endpoint alive but drop payload when bridge is not running. */
  (void)shell_ipc_host_prebind();

  shell_ipc_host_set_bridge_active(false);

  return 0;
}

int remote_shell_stop(const struct shell* sh) {
  if (sh) {
    shell_set_bypass(sh, NULL);
  }

  bridge_esc_state = 0;

  shell_ipc_host_set_bridge_active(false);

  atomic_set(&stop_requested, 1);

  k_sem_give(&shell_ipc_write_sem);

  /* Ensure callback is reverted from bridge RX handler to drop mode. */
  (void)shell_ipc_host_prebind();

  return 0;
}
