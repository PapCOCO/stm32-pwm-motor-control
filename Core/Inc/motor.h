#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  MOTOR_DIRECTION_FORWARD = 0,
  MOTOR_DIRECTION_REVERSE
} MotorDirection_t; /* 电机转向：正转或反转 */

typedef enum
{
  MOTOR_PHASE_STOPPED = 0,
  MOTOR_PHASE_RUNNING,
  MOTOR_PHASE_RAMP_DOWN,
  MOTOR_PHASE_CHANGE_WAIT
} MotorPhase_t; /* 电机运行阶段，用于变向和停机控制 */

typedef struct
{
  bool run_enabled; /* 是否处于运行模式 */
  MotorDirection_t requested_direction; /* 按键选择的目标转向 */
  MotorDirection_t output_direction; /* 当前实际输出方向 */
  MotorPhase_t phase; /* 当前状态机阶段 */
  uint16_t adc_value; /* 最新 ADC 采样值 */
  uint16_t target_pwm; /* 目标 PWM 经过 ADC 映射后的值 */
  uint16_t current_pwm; /* 当前实际输出 PWM */
} MotorStatus_t;

void motor_init(void);
void motor_task_step(uint32_t now_ms);
void motor_get_status(MotorStatus_t *status);

#endif
