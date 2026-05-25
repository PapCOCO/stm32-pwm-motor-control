#ifndef __COMMAND_H
#define __COMMAND_H

#include "app_config.h"

#include <stdint.h>

#define COMMAND_ACK_TARGET_USART1 0x01U
#define COMMAND_ACK_TARGET_USART2 0x02U
#define COMMAND_ACK_KIND_T ((uint8_t)'T')
#define COMMAND_ACK_KIND_M ((uint8_t)'M')

typedef struct
{
  uint8_t Target;
  uint8_t Kind;
  uint32_t Value;
} CommandAck_t;

void Command_Init(void);
void Command_ParseByte(uint8_t Data, uint8_t AckTarget);
void Command_ProcessAck(void);
uint32_t Command_GetReportPeriodMs(void);
AppControlMode_t Command_GetControlMode(void);
uint8_t Command_GetAppLedOn(void);

#endif
