#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>

#include "shell_ipc_host.h"

#define HOST_WAIT_TIME 1000000

LOG_MODULE_REGISTER(remote_shell);

RING_BUF_DECLARE(rx_ringbuf, CONFIG_REMOTE_SHELL_RX_RING_BUFFER_SIZE);
RING_BUF_DECLARE(tx_ringbuf, CONFIG_REMOTE_SHELL_TX_RING_BUFFER_SIZE);

K_SEM_DEFINE(shell_ipc_write_sem, 0, 1);

static atomic_t stop_requested;
static const struct device *remote_shell_uart_dev;

static void ipc_prebind_drop_cb(const uint8_t *data, size_t len, void *context) {
  ARG_UNUSED(data);
  ARG_UNUSED(len);
  ARG_UNUSED(context);
}

/* Pre-bind "remote shell" endpoint at boot, so net core shell IPC init
 * does not block BT HCI IPC endpoint registration.
 */
static int remote_shell_ipc_prebind_init(void) {
  int err = shell_ipc_host_init(ipc_prebind_drop_cb, NULL);
  if (err) {
    LOG_WRN("IPC prebind failed: %d", err);
  }
  return 0;
}
SYS_INIT(remote_shell_ipc_prebind_init, APPLICATION, 1);

static const struct uart_irq_context {
  struct ring_buf* rx_buf;
  struct ring_buf* tx_buf;
  struct k_sem* write_sem;
} uart_context = {.rx_buf = &rx_ringbuf,
                  .tx_buf = &tx_ringbuf,
                  .write_sem = &shell_ipc_write_sem};

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
      /* Give CPU resources to low priority threads. */
      k_sleep(K_MSEC(100));
    }
  }
}

static void ipc_host_receive(const uint8_t* data, size_t len, void* context) {
  int recv_len;
  const struct device* uart_dev = context;

  if (!data || (len == 0)) {
    return;
  }

  recv_len = ring_buf_put(&tx_ringbuf, data, len);
  if (recv_len < len) {
    LOG_INF("TX ring buffer full. Dropping %d bytes", len - recv_len);
  }

  uart_irq_tx_enable(uart_dev);
}

static void uart_tx_procces(const struct device* dev,
                            const struct uart_irq_context* uart_ctrl) {
  int err;
  int send_len = 0;
  uint8_t* data;

  send_len =
      ring_buf_get_claim(uart_ctrl->tx_buf, &data, uart_ctrl->tx_buf->size);
  if (send_len) {
    if (IS_ENABLED(CONFIG_UART_LINE_CTRL)) {
      uart_dtr_wait(dev);
    }

    send_len = uart_fifo_fill(dev, data, send_len);
    err = ring_buf_get_finish(uart_ctrl->tx_buf, send_len);
    __ASSERT_NO_MSG(err == 0);
  } else {
    uart_irq_tx_disable(dev);
  }
}

static void uart_rx_process(const struct device* dev,
                            const struct uart_irq_context* uart_ctrl) {
  int recv_len = 0;
  uint8_t* data;

  recv_len =
      ring_buf_put_claim(uart_ctrl->rx_buf, &data, uart_ctrl->rx_buf->size);
  if (recv_len) {
    recv_len = uart_fifo_read(dev, data, recv_len);
    if (recv_len < 0) {
      LOG_ERR("Failed to read UART FIFO, err %d", recv_len);
      recv_len = 0;
    } else {
      ring_buf_put_finish(uart_ctrl->rx_buf, recv_len);
    }

    if (recv_len) {
      k_sem_give(uart_ctrl->write_sem);
    }
  }
}

static void uart_irq_handler(const struct device* dev, void* user_data) {
  const struct uart_irq_context* uart_ctrl = user_data;

  while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
    if (uart_irq_rx_ready(dev)) {
      uart_rx_process(dev, uart_ctrl);
    }

    if (uart_irq_tx_ready(dev)) {
      uart_tx_procces(dev, uart_ctrl);
    }
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
  remote_shell_uart_dev = dev;

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

    /* They are optional, we use them to test the interrupt endpoint */
    err = uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);
    if (err) {
      shell_warn(sh, "Failed to set DCD, err %d", err);
    }

    err = uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);
    if (err) {
      shell_warn(sh, "Failed to set DSR, err %d", err);
    }

    /* Wait 1 sec for the host to do all settings */
    k_busy_wait(HOST_WAIT_TIME);

    err = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
    if (err) {
      shell_warn(sh, "Failed to get baudrate, err %d", err);
    } else {
      shell_warn(sh, "Baudrate detected: %d", baudrate);
    }
  }

  uart_irq_callback_user_data_set(dev, uart_irq_handler, (void*)&uart_context);

  /* Enable rx interrupts */
  uart_irq_rx_enable(dev);

  shell_info(sh, "Connecting IPC shell (waiting for network core)…");

  err = shell_ipc_host_init(ipc_host_receive, (void*)dev);
  if (err) {
    shell_error(sh, "Shell IPC host initialization failed, err %d", err);
    shell_ipc_host_set_bridge_active(false);
    return err;
  }

  while (!atomic_get(&stop_requested)) {
    uint8_t* data;
    uint32_t data_size;

    /* Wake periodically to check stop condition. */
    (void)k_sem_take(&shell_ipc_write_sem, K_MSEC(200));

    if (!shell_ipc_host_is_connected()) {
      shell_warn(sh, "IPC disconnected, exiting bridge");
      break;
    }

    data_size = ring_buf_get_claim(&rx_ringbuf, &data, rx_ringbuf.size);

    if (data_size) {
      data_size = shell_ipc_host_write(data, data_size);
      if (data_size >= 0) {
        shell_print(sh, "Sent %d bytes", data_size);
        ring_buf_get_finish(&rx_ringbuf, data_size);
      } else {
        shell_warn(sh, "IPC write failed (%d), exiting bridge", data_size);
        break;
      }
    }
  }

  /* Best-effort cleanup */
  uart_irq_rx_disable(dev);
  uart_irq_tx_disable(dev);
  (void)uart_irq_callback_user_data_set(dev, NULL, NULL);
  remote_shell_uart_dev = NULL;

  (void)shell_ipc_host_deinit();
  shell_ipc_host_set_bridge_active(false);

  return 0;
}

int remote_shell_stop(const struct shell* sh) {
  ARG_UNUSED(sh);

  shell_ipc_host_set_bridge_active(false);
  atomic_set(&stop_requested, 1);
  k_sem_give(&shell_ipc_write_sem);

  if (remote_shell_uart_dev) {
    uart_irq_rx_disable(remote_shell_uart_dev);
    uart_irq_tx_disable(remote_shell_uart_dev);
    (void)uart_irq_callback_user_data_set(remote_shell_uart_dev, NULL, NULL);
  }

  (void)shell_ipc_host_deinit();
  return 0;
}
