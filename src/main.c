/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(senao_app);

#ifdef CONFIG_SHELL_THREAD_PRIORITY
/* 回調函數用於查找 shell 線程 */
static void find_shell_thread_cb(const struct k_thread* thread,
                                 void* user_data) {
  const char* thread_name = k_thread_name_get((k_tid_t)thread);
  if (thread_name != NULL && strcmp(thread_name, "shell_uart") == 0) {
    k_tid_t* found_tid = (k_tid_t*)user_data;
    *found_tid = (k_tid_t)thread;
  }
}
#endif

int main(void) {
#ifdef CONFIG_SHELL_THREAD_PRIORITY
  k_tid_t shell_tid = NULL;
  /* 使用 k_thread_foreach 遍歷所有線程查找 shell 線程 */
  k_thread_foreach(find_shell_thread_cb, &shell_tid);

  if (shell_tid != NULL) {
    k_thread_priority_set(shell_tid, CONFIG_SHELL_THREAD_PRIORITY);
    printk("Shell thread priority set to %d\n", CONFIG_SHELL_THREAD_PRIORITY);
  } else {
    printk("Warning: Shell thread not found, priority not set\n");
  }
#endif

  printk("Senao Shell ready. Type 'help' for commands.\n");

  k_sleep(K_FOREVER);

  return 0;
}
