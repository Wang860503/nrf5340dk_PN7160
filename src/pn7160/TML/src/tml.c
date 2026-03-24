/*
 *         Copyright (c), NXP Semiconductors Caen / France
 *
 *                     (C)NXP Semiconductors
 *       All rights are reserved. Reproduction in whole or in part is
 *      prohibited without the written consent of the copyright owner.
 *  NXP reserves the right to make changes without notice at any time.
 * NXP makes no warranty, expressed, implied or statutory, including but
 * not limited to any implied warranty of merchantability or fitness for any
 *particular purpose, or that the use will not infringe any third party patent,
 * copyright or trademark. NXP must not be liable for any loss or damage
 *                          arising from its use.
 */

#include "tml.h"

#include <stdint.h>
#include <tool.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include "driver_config.h"
#include "types.h"

static uint8_t tml_Init(void) {
  /* SPI */
  if (!spi_is_ready_dt(&pn7160_spi)) {
    printk("[PN7160] SPI bus %s is not ready!\n\r", pn7160_spi.bus->name);
    return -1;
  } else {
    printk("[PN7160] SPI bus %s is ready!\n\r", pn7160_spi.bus->name);
  }
  /* Configure GPIO for IRQ pin */
  if (device_is_ready(pn7160_irq.port)) {
    int ret = gpio_pin_configure_dt(&pn7160_irq, GPIO_INPUT);
    if (ret < 0) {
      printk("[PN7160] IRQ setting input fail\n\r");
    }
    printk("[PN7160] IRQ setting input Success\n\r");
  }
  /* Configure GPIO for RESET pin */
  if (device_is_ready(pn7160_reset.port)) {
    int ret = gpio_pin_configure_dt(&pn7160_reset, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
      printk("[PN7160] RESET setting ouput fail\n\r");
    }
    printk("[PN7160] RESET setting ouput Success\n\r");
  }
  /* Configure GPIO for DWL pin */
  if (device_is_ready(pn7160_dwl.port)) {
    int ret = gpio_pin_configure_dt(&pn7160_dwl, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
      printk("[PN7160] DWL setting ouput fail\n\r");
    }
    printk("[PN7160] DWL setting ouput Success\n\r");
  }

  return SUCCESS;
}

static uint8_t tml_Reset(void) {
  /*Avoid to Download mode*/
  gpio_pin_set_dt(&pn7160_dwl, LOW);
  /* Apply VEN reset */
  gpio_pin_set_dt(&pn7160_reset, HIGH);
  k_msleep(10);
  gpio_pin_set_dt(&pn7160_reset, LOW);
  k_msleep(10);
  gpio_pin_set_dt(&pn7160_reset, HIGH);
  printk("[PN7160] RESET Success\n\r");
  k_msleep(3);
  return SUCCESS;
}

static uint8_t tml_Tx(uint8_t* pBuff, uint16_t buffLen) {
  uint8_t tx_full_buf[buffLen + 1];
  uint8_t rx_dummy_buf[buffLen + 1];

  tx_full_buf[0] = WRITE_PREFIX_ON_WRITE;  // WRITE_PREFIX_ON_WRITE
  memcpy(&tx_full_buf[1], pBuff, buffLen);

  const struct spi_buf tx_spi = {.buf = tx_full_buf, .len = buffLen + 1};
  const struct spi_buf rx_spi = {.buf = rx_dummy_buf, .len = buffLen + 1};

  const struct spi_buf_set tx_set = {.buffers = &tx_spi, .count = 1};
  const struct spi_buf_set rx_set = {.buffers = &rx_spi, .count = 1};

  // 使用 transceive 因為需要檢查 MISO 的第一個 byte
  if (spi_transceive_dt(&pn7160_spi, &tx_set, &rx_set) == 0) {
    if (rx_dummy_buf[0] == MISO_VAL_ON_WRITE_SUCCESS) {
      return SUCCESS;
    }
  }
  return ERROR;
}

static uint8_t tml_Rx(uint8_t* pBuff, uint16_t buffLen, uint16_t* pBytesRead) {
  int ret;

  /* * 1. 第一次讀取：讀取 3 位元組的 Header
   * SPI 是全雙工，我們送出 1 byte prefix，晶片會回傳 1 byte dummy，
   * 接著我們再送出 3 bytes dummy clock 來換取 3 bytes Header。
   */
  uint8_t tx_header[4] = {WRITE_PREFIX_ON_READ, 0x00, 0x00, 0x00};
  uint8_t rx_header[4] = {0};

  struct spi_buf tx_buf = {.buf = tx_header, .len = 4};
  struct spi_buf rx_buf = {.buf = rx_header, .len = 4};
  struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
  struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

  ret = spi_transceive_dt(&pn7160_spi, &tx_set, &rx_set);

  if (ret == 0) {
    /* rx_header[0] 是對應 prefix 的 dummy，資料從 rx_header[1] 開始 */
    memcpy(pBuff, &rx_header[1], 3);
    uint8_t payload_len = pBuff[2];  // NCI 協定的 Payload Length 位元組

    if (payload_len > 0) {
      /* 安全檢查：避免緩衝區溢位 */
      if ((3 + payload_len) > buffLen) {
        *pBytesRead = 0;
        return -1;
      }

      /* * 2. 第二次讀取：讀取剩餘的 Payload
       * 根據 PN7160 SPI 規範，每次讀取事務通常都要重新發送 0xFF
       */
      uint8_t tx_payload[payload_len + 1];
      uint8_t rx_payload[payload_len + 1];
      memset(tx_payload, 0, sizeof(tx_payload));
      tx_payload[0] = WRITE_PREFIX_ON_READ;

      struct spi_buf tx_p_buf = {.buf = tx_payload, .len = payload_len + 1};
      struct spi_buf rx_p_buf = {.buf = rx_payload, .len = payload_len + 1};
      struct spi_buf_set tx_p_set = {.buffers = &tx_p_buf, .count = 1};
      struct spi_buf_set rx_p_set = {.buffers = &rx_p_buf, .count = 1};

      ret = spi_transceive_dt(&pn7160_spi, &tx_p_set, &rx_p_set);

      if (ret == 0) {
        /* 將讀到的 Payload 接在 pBuff[3] 之後 */
        memcpy(&pBuff[3], &rx_payload[1], payload_len);
        *pBytesRead = 3 + payload_len;
      } else {
        *pBytesRead = 0;
      }
    } else {
      *pBytesRead = 3;
    }
  } else {
    *pBytesRead = 0;
    printk("[PN7160] SPI Read Header Failed: %d\n\r", ret);
  }

  return (ret == 0) ? 0 : -1;
}

static uint8_t tml_WaitForRx(uint16_t timeout) {
  if (timeout == 0) {
    int16_t to = 3000; /* 3 seconds */
    while (gpio_pin_get_dt(&pn7160_irq) == LOW) {
      k_busy_wait(1000);
      to -= 1;
      if (to <= 0) {
        printk("IRQ Timeout (3s)!\n");
        return 0xFF;
      }
    }
  } else {
    int16_t to = timeout;
    while ((gpio_pin_get_dt(&pn7160_irq) == LOW)) {
      k_busy_wait(1000);
      to -= 1;
      if (to <= 0) {
        printk("IRQ Timeout (%dms)!\n", timeout);
        return ERROR;
      }
    }
  }
  return SUCCESS;
}

void tml_Connect(void) {
  tml_Init();
  tml_Reset();
}

void tml_Disconnect(void) { return; }

void tml_Send(uint8_t* pBuffer, uint16_t BufferLen, uint16_t* pBytesSent) {
  if (tml_Tx(pBuffer, BufferLen) != SUCCESS) {
    *pBytesSent = 0;
  } else {
    *pBytesSent = BufferLen;
  }
}

void tml_Receive(uint8_t* pBuffer, uint16_t BufferLen, uint16_t* pBytes,
                 uint16_t timeout) {
  k_msleep(100);
  uint8_t ret = tml_WaitForRx(timeout);

  if (ret == ERROR)
    *pBytes = 0;
  else if (ret == 0xFF) {
    *pBytes = 0xFFFF;
  } else {
    tml_Rx(pBuffer, BufferLen, pBytes);
  }
}
