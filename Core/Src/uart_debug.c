#include "uart_debug.h"

#include "motor.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

/*
 * uart_debug.c
 * 串口调试输出模块：周期打印当前电机状态，便于观察运行、目标和 PWM 值。
 */

static const char *uart_debug_direction_name(MotorDirection_t direction)
{
  return (direction == MOTOR_DIRECTION_FORWARD) ? "forward" : "reverse";
}

static const char *uart_debug_phase_name(MotorPhase_t phase)
{
  switch (phase)
  {
  case MOTOR_PHASE_STOPPED:
    return "stopped";
  case MOTOR_PHASE_RUNNING:
    return "running";
  case MOTOR_PHASE_RAMP_DOWN:
    return "ramp_down";
  case MOTOR_PHASE_CHANGE_WAIT:
    return "change_wait";
  default:
    return "unknown";
  }
}

void uart_debug_init(void)
{
  /* 当前模块不需要特殊初始化，USART 已在主程序中完成 */
}

/*
 * 调试任务：构造状态字符串并通过 USART1 发送。
 * 输出示例：t=123 run=1 req=forward out=forward phase=running adc=1023 target=99 pwm=43
 */
void uart_debug_task_step(uint32_t now_ms)
{
  MotorStatus_t status;
  char line[160];
  int len;

  motor_get_status(&status);
  len = snprintf(line, sizeof(line),
                 "t=%lu run=%u req=%s out=%s phase=%s adc=%u target=%u pwm=%u\r\n",
                 (unsigned long)now_ms,
                 status.run_enabled ? 1U : 0U,
                 uart_debug_direction_name(status.requested_direction),
                 uart_debug_direction_name(status.output_direction),
                 uart_debug_phase_name(status.phase),
                 status.adc_value,
                 status.target_pwm,
                 status.current_pwm);

  if (len > 0)
  {
    size_t tx_len;

    tx_len = (size_t)len;
    if (tx_len >= sizeof(line))
    {
      tx_len = sizeof(line) - 1U;
    }
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)tx_len, 100U);
  }
}
