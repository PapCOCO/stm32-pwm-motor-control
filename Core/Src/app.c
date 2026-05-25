#include "app.h"

#include "app_config.h"
#include "main.h"
#include "motor.h"
#include "uart_debug.h"

#include "FreeRTOS.h"
#include "stm32f1xx_hal.h"
#include "task.h"

/*
 * 应用层调度模块（FreeRTOS）
 * - app_init() 负责初始化各个子模块
 * - app_start_scheduler() 负责创建任务并启动调度器
 */

#define APP_MOTOR_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)
#define APP_UART_DEBUG_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)

#define APP_MOTOR_TASK_STACK_WORDS 192U
#define APP_UART_DEBUG_TASK_STACK_WORDS 256U

static void MotorTask(void *argument);
static void UartDebugTask(void *argument);

void app_init(void)
{
  /* 初始化电机控制和串口调试模块 */
  motor_init();
  uart_debug_init();
}

void app_run(void)
{
  /* 兼容旧接口：业务调度已迁移到 FreeRTOS 任务。 */
}

void app_start_scheduler(void)
{
  BaseType_t created;

  created = xTaskCreate(MotorTask,
                        "MotorTask",
                        APP_MOTOR_TASK_STACK_WORDS,
                        NULL,
                        APP_MOTOR_TASK_PRIORITY,
                        NULL);
  if (created != pdPASS)
  {
    Error_Handler();
  }

  created = xTaskCreate(UartDebugTask,
                        "UartDebugTask",
                        APP_UART_DEBUG_TASK_STACK_WORDS,
                        NULL,
                        APP_UART_DEBUG_TASK_PRIORITY,
                        NULL);
  if (created != pdPASS)
  {
    Error_Handler();
  }

  vTaskStartScheduler();

  Error_Handler();
}

static void MotorTask(void *argument)
{
  TickType_t last_wake_tick;
  const TickType_t period_ticks = pdMS_TO_TICKS(APP_MOTOR_TASK_PERIOD_MS);

  (void)argument;
  last_wake_tick = xTaskGetTickCount();

  for (;;)
  {
    motor_task_step(HAL_GetTick());
    vTaskDelayUntil(&last_wake_tick, period_ticks);
  }
}

static void UartDebugTask(void *argument)
{
  TickType_t last_wake_tick;
  const TickType_t period_ticks = pdMS_TO_TICKS(APP_UART_DEBUG_PERIOD_MS);

  (void)argument;
  last_wake_tick = xTaskGetTickCount();

  for (;;)
  {
    uart_debug_task_step(HAL_GetTick());
    vTaskDelayUntil(&last_wake_tick, period_ticks);
  }
}
