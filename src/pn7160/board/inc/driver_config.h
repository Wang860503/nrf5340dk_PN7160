/***********************************************************************************************
 *   driver_config.h:  Type definition Header file for NXP Family
 *   Microprocessors
 *
 *   Copyright(C) 2006, NXP Semiconductor
 *   All rights reserved.
 *
 *   History:		Created on: Aug 31, 2010
 *
 ***********************************************************************************************/
#ifndef __DRIVER_CONFIG_H__
#define __DRIVER_CONFIG_H__

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

/***********************************************************************************************
 **	Global macros and definitions
 ***********************************************************************************************/
#define PN7160_NODE DT_NODELABEL(pn7160)
// SPI MODE 0
#define PN7160_SPI_OPERATION \
  (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER | SPI_LINES_SINGLE)
static const struct spi_dt_spec pn7160_spi =
    SPI_DT_SPEC_GET(PN7160_NODE,          /* 參數 1: Devicetree 節點識別碼 */
                    PN7160_SPI_OPERATION, /* 參數 2: SPI 傳輸模式與字長 */
                    0 /* 參數 3: 選擇片選後的延遲時間 (微秒) */
    );

static const struct gpio_dt_spec pn7160_reset =
    GPIO_DT_SPEC_GET(PN7160_NODE, enable_gpios);
static const struct gpio_dt_spec pn7160_irq =
    GPIO_DT_SPEC_GET(PN7160_NODE, irq_gpios);
static const struct gpio_dt_spec pn7160_dwl =
    GPIO_DT_SPEC_GET(PN7160_NODE, dwl_gpios);

#define VO_LED_NODE DT_ALIAS(voleden)
// const struct gpio_dt_spec PORT_VO_LED_EN = GPIO_DT_SPEC_GET(VO_LED_NODE,
// gpios);

#define LED0_NODE DT_ALIAS(led0)

#define CONFIG_ENABLE_DRIVER_CRP 1
#define CONFIG_CRP_SETTING_NO_CRP 1

/* definitions for i2c link */
#define DEFAULT_I2C I2C0
#define CONFIG_ENABLE_DRIVER_I2C 1
#define CONFIG_I2C_DEFAULT_I2C_IRQHANDLER 1

/* definitions for SPI link */
#define CONFIG_ENABLE_DRIVER_SSP 1

/* definitions for GPIO */
#define CONFIG_ENABLE_DRIVER_GPIO 1

/* definitions for UART */
#define CONFIG_ENABLE_DRIVER_UART 1
#define CONFIG_UART_DEFAULT_UART_IRQHANDLER 1
#define CONFIG_UART_ENABLE_INTERRUPT 1
#define CONFIG_UART_ENABLE_TX_INTERRUPT 1

#define CONFIG_ENABLE_DRIVER_PRINTF 1

#define BOARD_NXPNCI_I2C_ADDR 0x28U

#define PORT0 0
#define PORT1 1

/* P1.10 and P1.08 worked  on nrf52840-dk */
/* P0.04 and P1.08 didn't work */
#define PORT_IRQ PORT0
#define PORT_VEN PORT1
#define PIN_IRQ 4  // P0.6 <- P1.010
#define PIN_VEN 8  // P0.7 <- P1.08

#define SET_OUT 1
#define SET_IN 0

#define HIGH 1
#define LOW 0

/***********************************************************************************************
 **	Global variables
 ***********************************************************************************************/

/***********************************************************************************************
 **	Global function prototypes
 ***********************************************************************************************/

#endif  // __DRIVER_CONFIG_H__

/***********************************************************************************************
 **                            End Of File
 ***********************************************************************************************/
