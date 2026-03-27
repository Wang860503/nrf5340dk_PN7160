#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include "nfc_thread.h"
#include "peripheral_uart.h"
#include "shell_ipc_host.h"

LOG_MODULE_REGISTER(cmd);

K_THREAD_STACK_DEFINE(remote_shell_stack, 2048);
static struct k_thread remote_shell_thread;
static atomic_t remote_shell_running;

static void remote_shell_thread_fn(void* p1, void* p2, void* p3) {
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  const struct shell* sh = (const struct shell*)p1;
  int err = remote_shell_start(sh);
  if (err) {
    shell_error(sh, "remote_shell worker exited, err %d", err);
  } else {
    shell_warn(sh, "remote_shell worker exited");
  }

  atomic_clear(&remote_shell_running);
}

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
    if (k_sem_count_get(&peripheral_uart_sem) == 0) {
      shell_warn(sh, "Peripheral already initialized and enabled");
      return 0;
    }
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

static int cmd_remote_shell(const struct shell* sh, size_t argc, char** argv) {
  if (strcmp(argv[0], "start") == 0) {
    if (k_sem_count_get(&peripheral_uart_sem) == 0) {
      shell_warn(sh, "Peripheral uart already running");
      return 0;
    }

    if (!atomic_cas(&remote_shell_running, 0, 1)) {
      shell_warn(sh, "remote_shell already running");
      return 0;
    }

    shell_print(sh, "remote_shell: starting (async)...");

    k_thread_create(&remote_shell_thread, remote_shell_stack,
                    K_THREAD_STACK_SIZEOF(remote_shell_stack),
                    remote_shell_thread_fn, (void*)sh, NULL, NULL,
                    K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
    k_thread_name_set(&remote_shell_thread, "remote_shell");

  } else if (strcmp(argv[0], "stop") == 0) {
    int err = remote_shell_stop(sh);
    if (err) {
      shell_error(sh, "remote_shell stop failed, err %d", err);
      return err;
    }

    shell_print(sh, "remote_shell: stop requested");
  } else {
    shell_error(sh, "Usage: remote_shell <cmd>");
    shell_print(sh, "Commands:");
    shell_print(
        sh, "  start          - Bridge UART to network-core shell over IPC");
    shell_print(sh, "  stop           - Stop remote shell");
    shell_print(sh, "  (on net:~$)    - Back: Ctrl+A then Ctrl+X on UART");
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

/*remote_shell*/
SHELL_STATIC_SUBCMD_SET_CREATE(sub_remote_shell,
                               SHELL_CMD_ARG(start, NULL, "Start remote shell",
                                             cmd_remote_shell, 1, 0),
                               SHELL_CMD_ARG(stop, NULL, "Stop remote shell",
                                             cmd_remote_shell, 1, 0),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(remote_shell, &sub_remote_shell, "remote_shell",
                   cmd_remote_shell);