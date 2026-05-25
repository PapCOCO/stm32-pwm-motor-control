/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "app.h"
#include "gpio.h"
#include "tim.h"
#include "usart.h"

/*
 * main.c
 * 入口文件：
 * - 初始化 HAL 库
 * - 配置系统时钟
 * - 初始化 GPIO、ADC、定时器、USART
 * - 调用应用层初始化和循环调度
 */

void SystemClock_Config(void);

int main(void)
{
  /* 初始化 HAL 库，设置中断优先级分组等底层系统 */
  HAL_Init();
  /* 配置 MCU 时钟，使 CPU 和外设进入目标频率 */
  SystemClock_Config();

  /* 初始化硬件外设 */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();

  /* 初始化应用逻辑 */
  app_init();

  /* 主循环：周期性调度应用任务 */
  while (1)
  {
    app_run();
  }
}

void SystemClock_Config(void)
{
  /*
   * 系统时钟配置：
   * - 使用内部 HSI 8MHz 作为时钟源
   * - 通过 PLL x16 提升到 64MHz
   * - APB1 降频 2 倍，APB2 保持 1 倍
   * - ADC 时钟使用 PCLK2 / 6
   */
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

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

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
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

void Error_Handler(void)
{
  /* 当 HAL 库发生错误时进入此函数，关闭中断并停机 */
  __disable_irq();
  while (1)
  {
    /* 这里可以加入故障指示，例如闪烁 LED */
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
