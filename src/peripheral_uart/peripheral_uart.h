#ifndef __PERIPHERAL_UART_SEM_H_

#define __PERIPHERAL_UART_SEM_H_
extern struct k_sem peripheral_uart_sem;

int peripheral_start(void);
int peripheral_stop(void);

#endif