#include "app.h"

#include "app_config.h"
#include "motor.h"
#include "uart_debug.h"

#include "stm32f1xx_hal.h"

/*
 * 应用层调度模块
 * - app_init() 负责初始化各个子模块
 * - app_run() 负责按配置周期调度子模块任务
 */

void app_init(void)
{
  /* 初始化电机控制和串口调试模块 */
  motor_init();
  uart_debug_init();
}

void app_run(void)
{
  /* 保存上次调度时间，避免阻塞式延时 */
  static uint32_t last_motor_ms;
  static uint32_t last_uart_ms;
  uint32_t now_ms;

  now_ms = HAL_GetTick();

  /* 每 20ms 调度一次电机任务 */
  if ((now_ms - last_motor_ms) >= APP_MOTOR_TASK_PERIOD_MS)
  {
    last_motor_ms = now_ms;
    motor_task_step(now_ms);
  }

  /* 每 1 秒调度一次串口调试任务 */
  if ((now_ms - last_uart_ms) >= APP_UART_DEBUG_PERIOD_MS)
  {
    last_uart_ms = now_ms;
    uart_debug_task_step(now_ms);
  }
}
