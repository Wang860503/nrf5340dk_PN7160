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

/***** NFC dedicated setting ****************************************/

/* Following definitions specifies which settings will apply when
 * NxpNci_ConfigureSettings() API is called from the application
 */
#include <stdint.h>

#define NXP_CORE_CONF 1
#define NXP_CORE_STANDBY 1
#define NXP_CORE_CONF_EXTN 1
#define NXP_CLK_CONF 1   // 1=Xtal, 2=PLL
#define NXP_TVDD_CONF 2  // 1=3.3V, 2=4.75V
#define NXP_RF_CONF 1

uint8_t NxpNci_SettingCurrentTS[32] = __TIMESTAMP__;

#if NXP_CORE_CONF
/* NCI standard dedicated settings
 * Refer to NFC Forum NCI standard for more details
 */
uint8_t NxpNci_CORE_CONF[] = {
    0x20, 0x02, 0x05, 0x01, /* CORE_SET_CONFIG_CMD */
    0x00, 0x02, 0xFE, 0x01  /* TOTAL_DURATION */
};
#endif

#if NXP_CORE_CONF_EXTN
/* NXP-NCI extension dedicated setting
 * Refer to NFC controller User Manual for more details
 */
uint8_t NxpNci_CORE_CONF_EXTN[] = {
    0x20, 0x02, 0x05, 0x01, /* CORE_SET_CONFIG_CMD */
    0xA0, 0x40, 0x01, 0x00  /* TAG_DETECTOR_CFG */
};
#endif

#if NXP_CORE_STANDBY
/* NXP-NCI standby enable setting
 * Refer to NFC controller User Manual for more details
 */
uint8_t NxpNci_CORE_STANDBY[] = {0x2F, 0x00, 0x01,
                                 0x00}; /* last byte indicates enable/disable */
#endif

#if NXP_CLK_CONF
/* NXP-NCI CLOCK configuration
 * Refer to NFC controller Hardware Design Guide document for more details
 */
#if (NXP_CLK_CONF == 1)
/* Xtal configuration */
uint8_t NxpNci_CLK_CONF[] = {
    0x20, 0x02, 0x05, 0x01, /* CORE_SET_CONFIG_CMD */
    0xA0, 0x03, 0x01, 0x08  /* CLOCK_SEL_CFG */
};
#else
/* PLL configuration */
uint8_t NxpNci_CLK_CONF[] = {
    0x20, 0x02, 0x09, 0x02, /* CORE_SET_CONFIG_CMD */
    0xA0, 0x03, 0x01, 0x11, /* CLOCK_SEL_CFG */
    0xA0, 0x04, 0x01, 0x01  /* CLOCK_TO_CFG */
};
#endif
#endif

#if NXP_TVDD_CONF
/* NXP-NCI TVDD configuration
 * Refer to NFC controller Hardware Design Guide document for more details
 */
#if (NXP_TVDD_CONF == 1)
/* TXLDO output voltage set to 3.3V */
uint8_t NxpNci_TVDD_CONF[] = {0x20, 0x02, 0x0F, 0x01, 0xA0, 0x0E,
                              0x0B, 0x11, 0x01, 0x01, 0x01, 0x00,
                              0x00, 0x00, 0x10, 0x00, 0xD0, 0x0C};
#else
/* TXLDO output voltage set to 4.75V */
uint8_t NxpNci_TVDD_CONF[] = {0x20, 0x02, 0x0F, 0x01, 0xA0, 0x0E,
                              0x0B, 0x11, 0x01, 0x01, 0x01, 0x00,
                              0x00, 0x00, 0x40, 0x00, 0xD0, 0x0C};
#endif
#endif

#if NXP_RF_CONF
/* NXP-NCI RF configuration
 * Refer to NFC controller Antenna Design and Tuning Guidelines document for
 * more details
 */
/* Following configuration relates to performance optimization of OM27160 NFC
 * Controller demo kit */
uint8_t NxpNci_RF_CONF[] = {
    0x20, 0x02, 0x4C, 0x09, 0xA0, 0x0D, 0x03, 0x78, 0x0D, 0x02, 0xA0, 0x0D,
    0x03, 0x78, 0x14, 0x02, 0xA0, 0x0D, 0x06, 0x4C, 0x44, 0x65, 0x09, 0x00,
    0x00, 0xA0, 0x0D, 0x06, 0x4C, 0x2D, 0x05, 0x35, 0x1E, 0x01, 0xA0, 0x0D,
    0x06, 0x82, 0x4A, 0x55, 0x07, 0x00, 0x07, 0xA0, 0x0D, 0x06, 0x44, 0x44,
    0x03, 0x04, 0xC4, 0x00, 0xA0, 0x0D, 0x06, 0x46, 0x30, 0x50, 0x00, 0x18,
    0x00, 0xA0, 0x0D, 0x06, 0x48, 0x30, 0x50, 0x00, 0x18, 0x00, 0xA0, 0x0D,
    0x06, 0x4A, 0x30, 0x50, 0x00, 0x08, 0x00};
#endif
