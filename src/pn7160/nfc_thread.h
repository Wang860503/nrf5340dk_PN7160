/*
 * NFC Thread header file
 */

#ifndef NFC_THREAD_H_
#define NFC_THREAD_H_

#include <zephyr/kernel.h>

/* 啟動 NFC 線程 */
void start_nfc_thread(void);

/* 外部變量聲明 */
extern struct k_sem nfc_start_sem;
extern bool nfc_run_flag;

#endif /* NFC_THREAD_H_ */
