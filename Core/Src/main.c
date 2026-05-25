/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_config.h"
#include "app_uart.h"
#include "command.h"
#include "motor_control.h"
#include "status_ui.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 串口中断每次只收 1 个字节，收到后再交给命令解析器逐字节处理。 */
static volatile uint8_t UsbRxByte;
static volatile uint8_t BluetoothRxByte;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void App_StartPeripherals(void);
static void App_RestartUartReceive(UART_HandleTypeDef *Huart);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 把 printf 的输出重定向到 USART1，方便接电脑串口调试。 */
int fputc(int ch, FILE *f)
{
  uint8_t Byte;

  (void)f;
  Byte = (uint8_t)ch;
  AppUart_SendByte(&huart1, Byte, 0xFFFFU);
  return ch;
}

/* 重新挂起一次 1 字节中断接收，保证串口能一直持续收数据。 */
static void App_RestartUartReceive(UART_HandleTypeDef *Huart)
{
  if ((Huart != NULL) && (Huart->Instance == USART1))
  {
    (void)HAL_UART_Receive_IT(&huart1, (uint8_t *)&UsbRxByte, 1U);
  }
  else if ((Huart != NULL) && (Huart->Instance == USART2))
  {
    (void)HAL_UART_Receive_IT(&huart2, (uint8_t *)&BluetoothRxByte, 1U);
  }
}

static void App_StartPeripherals(void)
{
  /* 先准备命令解析模块，后面串口一来数据就能直接处理。 */
  Command_Init();

  /*
   * 外设引脚和时钟都由 CubeMX 生成代码配置，这里只启动已经配置好的功能。
   * TIM3_CH3/PB0 和 TIM3_CH4/PB1 仍然是电机正反转 PWM 输出。
   */
  (void)HAL_ADCEx_Calibration_Start(&hadc1);
  (void)HAL_ADC_Start(&hadc1);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  /* 再启动应用层模块：电机控制、串口接收和 OLED 界面。 */
  MotorControl_Init();
  App_RestartUartReceive(&huart1);
  App_RestartUartReceive(&huart2);
  StatusUi_Init();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return;
  }

  /* 两个串口共用这个回调，所以先判断数据来自哪个串口。 */
  if (huart->Instance == USART1)
  {
    Command_ParseByte(UsbRxByte, COMMAND_ACK_TARGET_USART1);
  }
  else if (huart->Instance == USART2)
  {
    Command_ParseByte(BluetoothRxByte, COMMAND_ACK_TARGET_USART2);
  }

  /* 当前 1 字节处理完后，立刻准备接收下一个字节。 */
  App_RestartUartReceive(huart);
}

/* 串口异常后也要重新打开接收，否则后续命令可能一直收不到。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  /*
   * 串口偶发溢出、噪声或帧错误后，HAL 可能停止本次中断接收。
   * 这里重新挂起 1 字节接收，保证后续 T/L/M 命令还能继续进入解析器。
   */
  App_RestartUartReceive(huart);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* 启动真正的业务逻辑。前面的 MX_*_Init() 只是把参数配置好。 */
  App_StartPeripherals();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 上一次执行电机控制任务的时间戳。 */
    static uint32_t LastControlMs;
    /* 当前电机状态快照，给 OLED 和串口上报使用。 */
    MotorStatus_t MotorStatus;
    /* 当前系统运行时间，单位 ms。 */
    uint32_t NowMs;

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    NowMs = HAL_GetTick();

    /* 控制任务按固定周期运行，不让主循环速度直接影响控制节奏。 */
    if ((NowMs - LastControlMs) >= APP_CONTROL_PERIOD_MS)
    {
      LastControlMs = NowMs;
      MotorControl_Task(NowMs, Command_GetControlMode());
    }

    /* 每圈循环都更新状态、处理回包，并刷新 UI。 */
    MotorControl_GetStatus(&MotorStatus);
    Command_ProcessAck();
    StatusUi_Task(NowMs, &MotorStatus, Command_GetReportPeriodMs(), Command_GetAppLedOn());
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* 发生严重错误时停在这里，方便连调试器定位问题。 */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
