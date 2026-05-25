#ifndef UART_DEBUG_H
#define UART_DEBUG_H

#include <stdint.h>

/* 串口调试模块接口 */
void uart_debug_init(void);
void uart_debug_task_step(uint32_t now_ms);

#endif
