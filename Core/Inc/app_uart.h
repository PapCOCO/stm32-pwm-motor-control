#ifndef __APP_UART_H
#define __APP_UART_H

#include "main.h"

#include <stdint.h>

void AppUart_SendByte(UART_HandleTypeDef *Huart, uint8_t Byte, uint32_t TimeoutMs);
void AppUart_SendText(UART_HandleTypeDef *Huart, const char *Text, uint32_t TimeoutMs);

#endif
