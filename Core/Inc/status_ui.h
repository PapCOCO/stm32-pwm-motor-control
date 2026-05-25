#ifndef __STATUS_UI_H
#define __STATUS_UI_H

#include "motor_control.h"

#include <stdint.h>

void StatusUi_Init(void);
void StatusUi_Task(uint32_t NowMs,
                   const MotorStatus_t *MotorStatus,
                   uint32_t ReportPeriodMs,
                   uint8_t AppLedOn);

#endif
