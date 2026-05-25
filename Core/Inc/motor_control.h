#ifndef __MOTOR_CONTROL_H
#define __MOTOR_CONTROL_H

#include "app_config.h"

#include <stdint.h>

typedef enum
{
  APP_MOTOR_STOP = 0,
  APP_MOTOR_FORWARD,
  APP_MOTOR_REVERSE
} AppMotorState_t;

typedef struct
{
  AppMotorState_t State;
  uint16_t VoltageMv;
  uint8_t TargetDuty;
  uint8_t OutputDuty;
  uint8_t AdcHealthy;
} MotorStatus_t;

void MotorControl_Init(void);
void MotorControl_Task(uint32_t NowMs, AppControlMode_t ControlMode);
void MotorControl_GetStatus(MotorStatus_t *Status);
const char *MotorControl_StateText(AppMotorState_t State);

#endif
