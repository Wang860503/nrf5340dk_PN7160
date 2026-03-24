#include <zephyr/kernel.h>

#include "nfc_task.h"

/* 1. 定義 Thread 堆疊與結構 */
#define NFC_STACK_SIZE 4096 /* 增加堆棧大小以避免溢出 */
#define NFC_PRIORITY 5      // 比 Shell 優先級低一點，避免卡死系統
K_THREAD_STACK_DEFINE(nfc_thread_stack, NFC_STACK_SIZE);
struct k_thread nfc_thread_data;
k_tid_t nfc_tid;

/* 啟動控制旗標 */
bool nfc_run_flag = false;
K_SEM_DEFINE(nfc_start_sem, 0, 1); /* 靜態初始化信號量 */

/* 2. NFC 任務主循環 */
void nfc_thread_entry(void* p1, void* p2, void* p3) {
  /* 信號量已靜態初始化，無需再次初始化 */
  static bool nfc_initialized = false;

  while (1) {
    // 等待 Shell 指令觸發
    k_sem_take(&nfc_start_sem, K_FOREVER);

    if (nfc_run_flag) {
      /* task_nfc() 內部有 while(1) 循環，只調用一次 */
      if (!nfc_initialized) {
/* 如果是 BOARD_NRF52840，需要先調用 task_nfc_init() */
#ifdef BOARD_NRF52840
        task_nfc_init();
#endif
        nfc_initialized = true;
      }

      /* task_nfc() 內部有無限循環，不會返回 */
      /* 注意：task_nfc() 會一直運行，直到 nfc_run_flag 被設置為 false */
      /* 但由於 task_nfc() 不會檢查 nfc_run_flag，我們需要在外部處理停止 */
      task_nfc();

      /* 如果 task_nfc() 返回（不應該發生），說明出錯了 */
      nfc_run_flag = false;
      nfc_initialized = false;
    }

    printk("[PN7160] Test Stopped.\n");
  }
}

/* 3. 在系統初始化時啟動 Thread (或在 main 中啟動) */
void start_nfc_thread(void) {
  nfc_tid =
      k_thread_create(&nfc_thread_data, nfc_thread_stack,
                      K_THREAD_STACK_SIZEOF(nfc_thread_stack), nfc_thread_entry,
                      NULL, NULL, NULL, NFC_PRIORITY, 0, K_NO_WAIT);
}