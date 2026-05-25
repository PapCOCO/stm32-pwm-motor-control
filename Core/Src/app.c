#include "app.h"

#include "app_uart.h"
#include "app_config.h"
#include "adc.h"
#include "command.h"
#include "main.h"
#include "motor_control.h"
#include "status_ui.h"
#include "tim.h"
#include "usart.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "stm32f1xx_hal.h"
#include "task.h"

#include <stdio.h>

/*
 * 应用层调度模块（FreeRTOS）
 * - app_init() 负责初始化各个子模块
 * - app_start_scheduler() 负责创建任务并启动调度器
 */

#define APP_MOTOR_TASK_PRIORITY (tskIDLE_PRIORITY + 3U)
#define APP_COMMAND_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)
#define APP_STATUS_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)

#define APP_MOTOR_TASK_STACK_WORDS 192U
#define APP_COMMAND_TASK_STACK_WORDS 256U
#define APP_STATUS_TASK_STACK_WORDS 384U

#define APP_UART_RX_QUEUE_LENGTH 64U
#define APP_STATUS_TASK_PERIOD_MS APP_ALARM_BLINK_HALF_PERIOD_MS

typedef struct
{
  uint8_t byte;
  uint8_t ack_target;
} AppUartRxEvent_t;

static volatile uint8_t UsbRxByte;
static volatile uint8_t BluetoothRxByte;
static QueueHandle_t UartRxQueue;

static void MotorTask(void *argument);
static void CommandTask(void *argument);
static void StatusTask(void *argument);
static void App_RestartUartReceive(UART_HandleTypeDef *huart);
static void app_boot_log(const char *msg);

int fputc(int ch, FILE *f)
{
  uint8_t byte;

  (void)f;
  byte = (uint8_t)ch;
  AppUart_SendByte(&huart1, byte, 0xFFFFU);
  return ch;
}

void app_init(void)
{
  UartRxQueue = xQueueCreate(APP_UART_RX_QUEUE_LENGTH, sizeof(AppUartRxEvent_t));
  if (UartRxQueue == NULL)
  {
    Error_Handler();
  }

  Command_Init();

  (void)HAL_ADCEx_Calibration_Start(&hadc1);
  (void)HAL_ADC_Start(&hadc1);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  MotorControl_Init();
  App_RestartUartReceive(&huart1);
  App_RestartUartReceive(&huart2);
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

  created = xTaskCreate(CommandTask,
                        "CommandTask",
                        APP_COMMAND_TASK_STACK_WORDS,
                        NULL,
                        APP_COMMAND_TASK_PRIORITY,
                        NULL);
  if (created != pdPASS)
  {
    Error_Handler();
  }

  created = xTaskCreate(StatusTask,
                        "StatusTask",
                        APP_STATUS_TASK_STACK_WORDS,
                        NULL,
                        APP_STATUS_TASK_PRIORITY,
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
  const TickType_t period_ticks = pdMS_TO_TICKS(APP_CONTROL_PERIOD_MS);

  (void)argument;
  last_wake_tick = xTaskGetTickCount();

  for (;;)
  {
    MotorControl_Task(HAL_GetTick(), Command_GetControlMode());
    vTaskDelayUntil(&last_wake_tick, period_ticks);
  }
}

static void CommandTask(void *argument)
{
  AppUartRxEvent_t event;

  (void)argument;

  for (;;)
  {
    if (xQueueReceive(UartRxQueue, &event, portMAX_DELAY) == pdPASS)
    {
      Command_ParseByte(event.byte, event.ack_target);
    }
  }
}

static void StatusTask(void *argument)
{
  TickType_t last_wake_tick;
  const TickType_t period_ticks = pdMS_TO_TICKS(APP_STATUS_TASK_PERIOD_MS);

  (void)argument;
  last_wake_tick = xTaskGetTickCount();
  StatusUi_Init();
  app_boot_log("FreeRTOS motor control started");

  for (;;)
  {
    MotorStatus_t status;
    uint32_t now_ms;
    uint32_t report_period_ms;

    now_ms = HAL_GetTick();
    report_period_ms = Command_GetReportPeriodMs();

    Command_ProcessAck();
    MotorControl_GetStatus(&status);
    StatusUi_Task(now_ms, &status, report_period_ms, Command_GetAppLedOn());

    vTaskDelayUntil(&last_wake_tick, period_ticks);
  }
}

static void App_RestartUartReceive(UART_HandleTypeDef *huart)
{
  if ((huart != NULL) && (huart->Instance == USART1))
  {
    (void)HAL_UART_Receive_IT(&huart1, (uint8_t *)&UsbRxByte, 1U);
  }
  else if ((huart != NULL) && (huart->Instance == USART2))
  {
    (void)HAL_UART_Receive_IT(&huart2, (uint8_t *)&BluetoothRxByte, 1U);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  AppUartRxEvent_t event;
  BaseType_t higher_priority_task_woken;

  if (huart == NULL)
  {
    return;
  }

  event.byte = 0U;
  event.ack_target = 0U;
  if (huart->Instance == USART1)
  {
    event.byte = UsbRxByte;
    event.ack_target = COMMAND_ACK_TARGET_USART1;
  }
  else if (huart->Instance == USART2)
  {
    event.byte = BluetoothRxByte;
    event.ack_target = COMMAND_ACK_TARGET_USART2;
  }

  if ((event.ack_target != 0U) && (UartRxQueue != NULL))
  {
    higher_priority_task_woken = pdFALSE;
    (void)xQueueSendFromISR(UartRxQueue, &event, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }

  App_RestartUartReceive(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  App_RestartUartReceive(huart);
}

static void app_boot_log(const char *msg)
{
  const char newline[] = "\r\n";
  uint16_t len;

  if (msg == NULL)
  {
    return;
  }

  len = 0U;
  while ((msg[len] != '\0') && (len < 160U))
  {
    len++;
  }

  if (len > 0U)
  {
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)(const void *)msg, len, 100U);
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)(const void *)newline, 2U, 100U);
  }
}
