/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic UART Bridge Service (NUS) sample
 */
#include "peripheral_uart.h"

#include <bluetooth/services/nus.h>
#include <dk_buttons_and_leds.h>
#include <soc.h>
#include <stdio.h>
#include <string.h>
#include <uart_async_adapter.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/types.h>
#include <zephyr/usb/usb_device.h>
#if defined(CONFIG_SOC_NRF53_CPUNET_MGMT)
#include <nrf53_cpunet_mgmt.h>
#endif

#if defined(NRF_RADIO)
#include <hal/nrf_radio.h>
#define PERIPHERAL_UART_HAS_RADIO_RESET 1
#else
#define PERIPHERAL_UART_HAS_RADIO_RESET 0
#endif

/* Forward declaration to avoid dependency */
extern void radio_test_deinit(void);

/* Define RADIO_IRQn based on SoC series */
#if PERIPHERAL_UART_HAS_RADIO_RESET && \
    (defined(CONFIG_SOC_SERIES_NRF54HX) || defined(CONFIG_SOC_SERIES_NRF54LX))
#define PERIPHERAL_RADIO_IRQn RADIO_0_IRQn
#elif PERIPHERAL_UART_HAS_RADIO_RESET && defined(CONFIG_SOC_SERIES_NRF53X)
#define PERIPHERAL_RADIO_IRQn RADIO_0_IRQn
#elif PERIPHERAL_UART_HAS_RADIO_RESET && defined(CONFIG_SOC_SERIES_NRF52X)
#define PERIPHERAL_RADIO_IRQn RADIO_IRQn
#elif PERIPHERAL_UART_HAS_RADIO_RESET
#define PERIPHERAL_RADIO_IRQn RADIO_0_IRQn
#endif

/* Simple function to reset radio hardware state
 * This is needed to allow MPSL to reinitialize properly
 * Similar to what radio_disable() does in radio_test
 */
static void peripheral_radio_reset(void) {
#if PERIPHERAL_UART_HAS_RADIO_RESET
  /* Disable radio interrupts */
  nrf_radio_int_disable(NRF_RADIO, ~0);

  /* Clear radio shorts */
  nrf_radio_shorts_set(NRF_RADIO, 0);

  /* Clear and trigger disable task */
  nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
  nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);

  /* Wait for radio to be disabled */
  while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_DISABLED)) {
    /* Do nothing */
  }
  nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);

  /* Disable radio IRQ */
  irq_disable(PERIPHERAL_RADIO_IRQn);
#endif
}

static void peripheral_cpunet_restart(void) {
#if defined(CONFIG_SOC_NRF53_CPUNET_MGMT)
  nrf53_cpunet_enable(false);
  k_sleep(K_MSEC(100));
  nrf53_cpunet_enable(true);
  /* Wait until net core boots and recreates IPC endpoints. */
  k_sleep(K_MSEC(600));
#endif
}

#define MODULE_NAME peripheral_uart
LOG_MODULE_REGISTER(MODULE_NAME);

/* peripheral_uart_sem: 用於標記 peripheral_uart 是否已啟用
 * 初始值為 1 (未啟用)
 * 啟用時: k_sem_take() 將值變為 0 (已啟用)
 * 停止時: k_sem_give() 將值變為 1 (未啟用)
 * 其他 test 可以通過 k_sem_count_get() 檢查: 0=已啟用, 1=未啟用
 */
K_SEM_DEFINE(peripheral_uart_sem, 1, 1);

#ifndef CONFIG_BT_NUS_THREAD_STACK_SIZE
#define CONFIG_BT_NUS_THREAD_STACK_SIZE 1024
#endif
#define STACKSIZE CONFIG_BT_NUS_THREAD_STACK_SIZE
#define PRIORITY 7

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED DK_LED1
#define RUN_LED_BLINK_INTERVAL 1000

#define CON_STATUS_LED DK_LED2

#define KEY_PASSKEY_ACCEPT DK_BTN1_MSK
#define KEY_PASSKEY_REJECT DK_BTN2_MSK

#ifndef CONFIG_BT_NUS_UART_BUFFER_SIZE
#define CONFIG_BT_NUS_UART_BUFFER_SIZE 20
#endif
#define UART_BUF_SIZE CONFIG_BT_NUS_UART_BUFFER_SIZE
#define UART_WAIT_FOR_BUF_DELAY K_MSEC(50)
#ifndef CONFIG_BT_NUS_UART_RX_WAIT_TIME
#define CONFIG_BT_NUS_UART_RX_WAIT_TIME 50
#endif
#define UART_WAIT_FOR_RX CONFIG_BT_NUS_UART_RX_WAIT_TIME

static K_SEM_DEFINE(ble_init_ok, 0, 1);

static struct bt_conn* current_conn;
static struct bt_conn* auth_conn;
static struct k_work adv_work;
static bool peripheral_initialized = false;
static bool bluetooth_enabled = false;
static bool peripheral_stopping = false;

struct uart_data_t {
  void* fifo_reserved;
  uint8_t data[UART_BUF_SIZE];
  uint16_t len;
};

static K_FIFO_DEFINE(fifo_uart_tx_data);
static K_FIFO_DEFINE(fifo_uart_rx_data);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

#ifdef CONFIG_UART_ASYNC_ADAPTER
UART_ASYNC_ADAPTER_INST_DEFINE(async_adapter);
#else
#define async_adapter NULL
#endif

static void adv_work_handler(struct k_work* work) {
  int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd,
                            ARRAY_SIZE(sd));

  if (err) {
    /* -EAGAIN can happen if start is requested too soon after stop. */
    if (err == -EAGAIN) {
      LOG_WRN("Advertising start busy, retrying...");
      k_sleep(K_MSEC(200));
      k_work_submit(&adv_work);
      return;
    }

    LOG_ERR("Advertising failed to start (err %d)", err);
    return;
  }

  LOG_INF("Advertising successfully started");
}

static void advertising_start(void) { k_work_submit(&adv_work); }

static void connected(struct bt_conn* conn, uint8_t err) {
  char addr[BT_ADDR_LE_STR_LEN];

  if (err) {
    LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
    return;
  }

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Connected %s", addr);

  current_conn = bt_conn_ref(conn);
#ifdef CONFIG_DK_LIBRARY
  dk_set_led_on(CON_STATUS_LED);
#endif
}

static void disconnected(struct bt_conn* conn, uint8_t reason) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason,
          bt_hci_err_to_str(reason));

  if (auth_conn) {
    bt_conn_unref(auth_conn);
    auth_conn = NULL;
  }

  if (current_conn) {
    bt_conn_unref(current_conn);
    current_conn = NULL;
#ifdef CONFIG_DK_LIBRARY
    dk_set_led_off(CON_STATUS_LED);
#endif
  }
}

static void recycled_cb(void) {
  LOG_INF(
      "Connection object available from previous conn. Disconnect is "
      "complete!");
  /* 如果正在停止外設，不要重新啟動廣告 */
  if (!peripheral_stopping) {
    advertising_start();
  }
}

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
static void security_changed(struct bt_conn* conn, bt_security_t level,
                             enum bt_security_err err) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  if (!err) {
    LOG_INF("Security changed: %s level %u", addr, level);
  } else {
    LOG_WRN("Security failed: %s level %u err %d %s", addr, level, err,
            bt_security_err_to_str(err));
  }
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = recycled_cb,
#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
    .security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn* conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn* conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];

  auth_conn = bt_conn_ref(conn);

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Passkey for %s: %06u", addr, passkey);

  if (IS_ENABLED(CONFIG_SOC_SERIES_NRF54HX) ||
      IS_ENABLED(CONFIG_SOC_SERIES_NRF54LX)) {
    LOG_INF("Press Button 0 to confirm, Button 1 to reject.");
  } else {
    LOG_INF("Press Button 1 to confirm, Button 2 to reject.");
  }
}

static void auth_cancel(struct bt_conn* conn) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing cancelled: %s", addr);
}

static void pairing_complete(struct bt_conn* conn, bool bonded) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}

static void pairing_failed(struct bt_conn* conn, enum bt_security_err reason) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing failed conn: %s, reason %d %s", addr, reason,
          bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
    .passkey_display = auth_passkey_display,
    .passkey_confirm = auth_passkey_confirm,
    .cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
    .pairing_complete = pairing_complete, .pairing_failed = pairing_failed};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void bt_receive_cb(struct bt_conn* conn, const uint8_t* const data,
                          uint16_t len) {
  /*Do nothing*/
}

static struct bt_nus_cb nus_cb = {
    .received = bt_receive_cb,
};

void error(void) {
#ifdef CONFIG_DK_LIBRARY
  dk_set_leds_state(DK_ALL_LEDS_MSK, DK_NO_LEDS_MSK);
#endif

  while (true) {
    /* Spin for ever */
    k_sleep(K_MSEC(1000));
  }
}

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
static void num_comp_reply(bool accept) {
  if (accept) {
    bt_conn_auth_passkey_confirm(auth_conn);
    LOG_INF("Numeric Match, conn %p", (void*)auth_conn);
  } else {
    bt_conn_auth_cancel(auth_conn);
    LOG_INF("Numeric Reject, conn %p", (void*)auth_conn);
  }

  bt_conn_unref(auth_conn);
  auth_conn = NULL;
}

void button_changed(uint32_t button_state, uint32_t has_changed) {
  uint32_t buttons = button_state & has_changed;

  if (auth_conn) {
    if (buttons & KEY_PASSKEY_ACCEPT) {
      num_comp_reply(true);
    }

    if (buttons & KEY_PASSKEY_REJECT) {
      num_comp_reply(false);
    }
  }
}
#endif /* CONFIG_BT_NUS_SECURITY_ENABLED */

int peripheral_start(void) {
  int err = 0;

  /* 清除停止標記 */
  peripheral_stopping = false;

  /* 如果已經初始化，檢查 semaphore 狀態 */
  if (peripheral_initialized) {
    /* 如果 semaphore 值為 1，表示未啟用，需要重新初始化 */
    printk("Peripheral initialized but not enabled, reinitializing...\n");
    peripheral_stop();
    k_sleep(K_MSEC(100)); /* 給系統一點時間清理 */
  }

  if (IS_ENABLED(CONFIG_BT_NUS_SECURITY_ENABLED)) {
    err = bt_conn_auth_cb_register(&conn_auth_callbacks);
    if (err && err != -EALREADY) {
      printk("Failed to register authorization callbacks. (err: %d)\n", err);
      return err;
    }

    err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
    if (err && err != -EALREADY) {
      printk("Failed to register authorization info callbacks. (err: %d)\n",
             err);
      return err;
    }
  }

  /* 檢查藍牙是否已經啟用 */
  if (!bluetooth_enabled) {
    err = bt_enable(NULL);
    if (err && err != -EALREADY) {
      printk("Failed to enable Bluetooth (err: %d)\n", err);
      return err;
    }
    if (err == -EALREADY) {
      printk("Bluetooth already enabled\n");
    } else {
      printk("Bluetooth initialized\n");
    }
    bluetooth_enabled = true;

  } else {
    printk("Bluetooth already enabled\n");
  }

  k_sem_give(&ble_init_ok);

  if (IS_ENABLED(CONFIG_SETTINGS)) {
    settings_load();
  }

  err = bt_nus_init(&nus_cb);
  if (err && err != -EALREADY) {
    printk("Failed to initialize UART service (err: %d)\n", err);
    return err;
  }

  k_work_init(&adv_work, adv_work_handler);
  advertising_start();

  /* 標記 peripheral_uart 已啟用 (take semaphore，值從 1 變為 0) */
  k_sem_take(&peripheral_uart_sem, K_NO_WAIT);

  peripheral_initialized = true;
  return 0;
}

int peripheral_stop(void) {
  int err = 0;

  if (!peripheral_initialized && !bluetooth_enabled) {
    printk("Peripheral not initialized, nothing to stop\n");
    return 0;
  }

  printk("Stopping peripheral...\n");

  /* 設置停止標記，防止回調中重新啟動廣告 */
  peripheral_stopping = true;

  if (current_conn) {
    err = bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
      printk("Failed to disconnect (err: %d)\n", err);
    } else {
      printk("Disconnected from peer\n");
    }
    /* 等待斷線完成，確保所有回調都執行完畢 */
    k_sleep(K_MSEC(200));
  }

  err = bt_le_adv_stop();
  if (err && err != -EALREADY) {
    printk("Failed to stop advertising (err: %d)\n", err);
    /* 這裡不 return，繼續嘗試斷線與關閉藍牙 */
  } else {
    printk("Advertising stopped\n");
  }

  /* 3. 再次確保廣告已停止（防止回調中重新啟動） */
  err = bt_le_adv_stop();
  if (err && err != -EALREADY) {
    /* 忽略錯誤，繼續執行 */
  }

  /* 4. 不走 bt_disable，直接重啟 net core 釋放 HCI/IPC 狀態 */
  if (bluetooth_enabled) {
    printk("Restarting net core...\n");
    peripheral_cpunet_restart();
    printk("Net core restarted\n");
    bluetooth_enabled = false;
    peripheral_radio_reset();
    k_sleep(K_MSEC(300));
  }

  peripheral_initialized = false;
  peripheral_stopping = false; /* 清除停止標記 */

  if (k_sem_count_get(&peripheral_uart_sem) == 0) {
    k_sem_give(&peripheral_uart_sem);
  }

  return 0;
}

int peripheral_bt_disable(void) {
  int err = 0;

  /* Stop adv + disconnect first */
  (void)peripheral_stop();

  return err;
}

void ble_write_thread(void) {
  /* Don't go any further until BLE is initialized */
  k_sem_take(&ble_init_ok, K_FOREVER);
  struct uart_data_t nus_data = {
      .len = 0,
  };

  for (;;) {
    /* Wait indefinitely for data to be sent over bluetooth */
    struct uart_data_t* buf = k_fifo_get(&fifo_uart_rx_data, K_FOREVER);

    int plen = MIN(sizeof(nus_data.data) - nus_data.len, buf->len);
    int loc = 0;

    while (plen > 0) {
      memcpy(&nus_data.data[nus_data.len], &buf->data[loc], plen);
      nus_data.len += plen;
      loc += plen;

      if (nus_data.len >= sizeof(nus_data.data) ||
          (nus_data.data[nus_data.len - 1] == '\n') ||
          (nus_data.data[nus_data.len - 1] == '\r')) {
        if (bt_nus_send(NULL, nus_data.data, nus_data.len)) {
          LOG_WRN("Failed to send data over BLE connection");
        }
        nus_data.len = 0;
      }

      plen = MIN(sizeof(nus_data.data), buf->len - loc);
    }

    k_free(buf);
  }
}

K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL,
                NULL, PRIORITY, 0, 0);
