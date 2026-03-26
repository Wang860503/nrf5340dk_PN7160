#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include "nfc_thread.h"
#include "peripheral_uart.h"

LOG_MODULE_REGISTER(cmd);

static int cmd_pn7160_test(const struct shell* sh, size_t argc, char** argv) {
  if (strcmp(argv[0], "start") == 0) {
    /* 確保線程已啟動 */
    static bool thread_started = false;
    if (!thread_started) {
      start_nfc_thread();
      thread_started = true;
      k_msleep(10); /* 給線程一點時間啟動 */
    }

    if (!nfc_run_flag) {
      nfc_run_flag = true;
      k_sem_give(&nfc_start_sem);
    }
    return 0;

  } else if (strcmp(argv[0], "stop") == 0) {
    nfc_run_flag = false;
    shell_print(sh, "[PN7161] Stopping...");
    return 0;
  } else {
    shell_error(sh, "Usage: pn7161_test <cmd>");
    shell_print(sh, "Commands:");
    shell_print(sh, "  start          - Start PN7161 test");
    shell_print(sh, "  stop          - Stop PN7161 test");
    return -EINVAL;
  }
}

static int cmd_peripheral_uart(const struct shell* sh, size_t argc,
                               char** argv) {
  if (strcmp(argv[0], "start") == 0) {
    peripheral_start();
  } else if (strcmp(argv[0], "stop") == 0) {
    if (k_sem_count_get(&peripheral_uart_sem) != 0) {
      shell_warn(sh, "peripheral uart test is not running");
      return 0;
    }

    peripheral_stop();
  } else {
    shell_error(sh, "Usage: peripheral_uart_test <cmd>");
    shell_print(sh, "Commands:");
    shell_print(sh, "  start          - Start peripheral uart test");
    shell_print(sh, "  stop           - Stop peripheral uart test");
    return -EINVAL;
  }
  return 0;
}

/*PN7161 cmd*/
SHELL_STATIC_SUBCMD_SET_CREATE(sub_pn7160,
                               SHELL_CMD_ARG(start, NULL, "Start PN7160 test",
                                             cmd_pn7160_test, 1, 0),
                               SHELL_CMD_ARG(stop, NULL, "Stop PN7160 test",
                                             cmd_pn7160_test, 1, 0),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(PN7160_test, &sub_pn7160, "PN7160 test commands",
                   cmd_pn7160_test);

/*Peripheral uart test*/
SHELL_STATIC_SUBCMD_SET_CREATE(sub_peripheral_uart_test,
                               SHELL_CMD_ARG(start, NULL,
                                             "Start Peripheral uart",
                                             cmd_peripheral_uart, 1, 0),
                               SHELL_CMD_ARG(stop, NULL, "Stop Peripheral uart",
                                             cmd_peripheral_uart, 1, 0),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(peripheral_uart_test, &sub_peripheral_uart_test,
                   "Peripheral uart test", cmd_peripheral_uart);