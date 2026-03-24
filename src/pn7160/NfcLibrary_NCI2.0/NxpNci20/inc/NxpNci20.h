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

#include <Nfc.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>

#ifndef REMOVE_CARDEMU_SUPPORT
#include <T4T_NDEF_emu.h>
#endif  // #ifndef REMOVE_CARDEMU_SUPPORT

#ifndef REMOVE_P2P_SUPPORT
#include <P2P_NDEF.h>
#endif  // #ifndef REMOVE_P2P_SUPPORT

#ifndef REMOVE_RW_SUPPORT
#include <RW_NDEF.h>
#include <RW_NDEF_T3T.h>
#endif  // #ifndef REMOVE_RW_SUPPORT

#define NXPNCI_SUCCESS NFC_SUCCESS
#define NXPNCI_ERROR NFC_ERROR

#ifdef NCI_DEBUG
#define NCI_PRINT(...)   \
  do {                   \
    printk(__VA_ARGS__); \
  } while (0)

static inline void _nci_print_buf(const char* tag, const uint8_t* buf,
                                  size_t len) {
  printk("%s: ", tag);
  for (size_t i = 0; i < len; i++) {
    printk("%02X ", buf[i]);
  }
  printk("\n");
}

#define NCI_PRINT_BUF(x, y, z)               \
  do {                                       \
    _nci_print_buf(x, (const uint8_t*)y, z); \
  } while (0)
#else
#define NCI_PRINT(...)
#define NCI_PRINT_BUF(x, y, z)
#endif
