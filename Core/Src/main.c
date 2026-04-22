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
#include "oled.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  MOTOR_STOP = 0,
  MOTOR_FORWARD,
  MOTOR_REVERSE
} MotorState_t;

typedef enum
{
  CONTROL_KEYS = 0,
  CONTROL_FORCE_FORWARD,
  CONTROL_FORCE_REVERSE,
  CONTROL_FORCE_STOP,
  CONTROL_TEST_FORWARD_100,
  CONTROL_TEST_REVERSE_100
} ControlMode_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LED_RUN_PIN GPIO_PIN_8
#define LED_ALARM_PIN GPIO_PIN_7
#define LED_APP_PIN GPIO_PIN_6
#define LED_GPIO_PORT GPIOA
#define LED_ON GPIO_PIN_RESET
#define LED_OFF GPIO_PIN_SET

#define KEY_RUN_PIN GPIO_PIN_12
#define KEY_DIR_PIN GPIO_PIN_13
#define KEY_GPIO_PORT GPIOB
#define KEY_PRESSED GPIO_PIN_RESET

#define ADC_MAX_VALUE 4095U
#define VREF_MV 3300U
#define PWM_MAX_DUTY 100U
#define MOTOR_MIN_START_DUTY 40U
#define MOTOR_REVERSE_DELAY_MS 50U
#define REPORT_PERIOD_DEFAULT_MS 1000U
#define REPORT_PERIOD_MIN_MS 300U
#define REPORT_PERIOD_MAX_MS 2000U
#define CONTROL_PERIOD_MS 20U
#define OLED_UPDATE_PERIOD_MS 100U
#define ADC_FILTER_SHIFT 2U
#define ALARM_BLINK_HALF_PERIOD_MS 50U

#define ACK_TARGET_USART1 0x01U
#define ACK_TARGET_USART2 0x02U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static volatile uint8_t UsbRxByte;
static volatile uint8_t BluetoothRxByte;
static volatile uint8_t CommandMode;
static volatile uint32_t CommandValue;
static volatile uint32_t ReportPeriodMs = REPORT_PERIOD_DEFAULT_MS;
static volatile uint8_t CommandAckPending;
static volatile uint8_t CommandAckTarget;
static volatile uint8_t CommandAckKind;
static volatile uint32_t CommandAckValue;
static volatile uint8_t AppLedOn;
static volatile ControlMode_t ControlMode = CONTROL_KEYS;

static uint16_t AdcRaw;
static uint32_t AdcFiltered;
static uint8_t AdcFilterReady;
static uint16_t PotVoltageMv;
static uint8_t PwmDuty;
static MotorState_t MotorState = MOTOR_STOP;
static char OledLineCache[4][17];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint32_t ClampReportPeriod(uint32_t Value);
static void ParseCommandByte(uint8_t Data, uint8_t AckTarget);
static void ProcessControl(void);
static void ProcessIndicators(uint32_t NowMs);
static void ProcessStatusOutput(uint32_t NowMs);
static void ProcessCommandAck(void);
static void Motor_Set(MotorState_t State, uint8_t Duty);
static void LED_Set(uint16_t Pin, uint8_t On);
static void OLED_ShowPadded(uint8_t Line, const char *Text);
static const char *MotorStateText(MotorState_t State);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

static uint32_t ClampReportPeriod(uint32_t Value)
{
  if (Value < REPORT_PERIOD_MIN_MS)
  {
    return REPORT_PERIOD_MIN_MS;
  }
  if (Value > REPORT_PERIOD_MAX_MS)
  {
    return REPORT_PERIOD_MAX_MS;
  }
  return Value;
}

static void LED_Set(uint16_t Pin, uint8_t On)
{
  HAL_GPIO_WritePin(LED_GPIO_PORT, Pin, On ? LED_ON : LED_OFF);
}

static const char *MotorStateText(MotorState_t State)
{
  if (State == MOTOR_FORWARD)
  {
    return "FORWARD";
  }
  if (State == MOTOR_REVERSE)
  {
    return "REVERSE";
  }
  return "STOP";
}

static void Motor_Set(MotorState_t State, uint8_t Duty)
{
  static MotorState_t LastState = MOTOR_STOP;
  uint32_t Compare;

  if (Duty > PWM_MAX_DUTY)
  {
    Duty = PWM_MAX_DUTY;
  }
  if ((State != MOTOR_STOP) && (Duty > 0U) && (Duty < MOTOR_MIN_START_DUTY))
  {
    Duty = MOTOR_MIN_START_DUTY;
  }

  if (((LastState == MOTOR_FORWARD) && (State == MOTOR_REVERSE)) ||
      ((LastState == MOTOR_REVERSE) && (State == MOTOR_FORWARD)))
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);
    HAL_Delay(MOTOR_REVERSE_DELAY_MS);
  }

  Compare = Duty;
  if (State == MOTOR_FORWARD)
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, Compare);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);
  }
  else if (State == MOTOR_REVERSE)
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, Compare);
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);
  }

  LastState = State;
}

static void ProcessControl(void)
{
  GPIO_PinState RunKey;
  GPIO_PinState DirKey;
  uint8_t OutputDuty;

  if (HAL_ADC_PollForConversion(&hadc1, 2) == HAL_OK)
  {
    AdcRaw = (uint16_t)HAL_ADC_GetValue(&hadc1);
  }

  if (AdcFilterReady == 0U)
  {
    AdcFiltered = AdcRaw;
    AdcFilterReady = 1U;
  }
  else
  {
    AdcFiltered = (uint32_t)((int32_t)AdcFiltered +
                  (((int32_t)AdcRaw - (int32_t)AdcFiltered) >> ADC_FILTER_SHIFT));
  }

  PotVoltageMv = (uint16_t)((AdcFiltered * VREF_MV) / ADC_MAX_VALUE);
  PwmDuty = (uint8_t)((AdcFiltered * PWM_MAX_DUTY) / ADC_MAX_VALUE);
  OutputDuty = PwmDuty;

  if (ControlMode == CONTROL_FORCE_FORWARD)
  {
    MotorState = MOTOR_FORWARD;
  }
  else if (ControlMode == CONTROL_FORCE_REVERSE)
  {
    MotorState = MOTOR_REVERSE;
  }
  else if (ControlMode == CONTROL_FORCE_STOP)
  {
    MotorState = MOTOR_STOP;
  }
  else if (ControlMode == CONTROL_TEST_FORWARD_100)
  {
    MotorState = MOTOR_FORWARD;
    PwmDuty = PWM_MAX_DUTY;
    OutputDuty = PWM_MAX_DUTY;
  }
  else if (ControlMode == CONTROL_TEST_REVERSE_100)
  {
    MotorState = MOTOR_REVERSE;
    PwmDuty = PWM_MAX_DUTY;
    OutputDuty = PWM_MAX_DUTY;
  }
  else
  {
    RunKey = HAL_GPIO_ReadPin(KEY_GPIO_PORT, KEY_RUN_PIN);
    DirKey = HAL_GPIO_ReadPin(KEY_GPIO_PORT, KEY_DIR_PIN);

    if (RunKey != KEY_PRESSED)
    {
      MotorState = MOTOR_STOP;
    }
    else if (DirKey == KEY_PRESSED)
    {
      MotorState = MOTOR_FORWARD;
    }
    else
    {
      MotorState = MOTOR_REVERSE;
    }
  }

  if (MotorState == MOTOR_STOP)
  {
    PwmDuty = 0U;
    OutputDuty = 0U;
  }
  else if (OutputDuty > 0U)
  {
    OutputDuty = (uint8_t)(MOTOR_MIN_START_DUTY +
                 ((((uint32_t)OutputDuty - 1U) * (PWM_MAX_DUTY - MOTOR_MIN_START_DUTY)) /
                  (PWM_MAX_DUTY - 1U)));
  }

  Motor_Set(MotorState, OutputDuty);
}

static void ProcessIndicators(uint32_t NowMs)
{
  static uint32_t LastRunBlinkMs;
  static uint32_t LastAlarmBlinkMs;
  static uint8_t RunLedOn;
  static uint8_t AlarmLedOn;
  uint32_t PeriodMs;

  PeriodMs = ReportPeriodMs;
  if ((NowMs - LastRunBlinkMs) >= PeriodMs)
  {
    LastRunBlinkMs = NowMs;
    RunLedOn = !RunLedOn;
    LED_Set(LED_RUN_PIN, RunLedOn);
  }

  if (PotVoltageMv >= 3000U)
  {
    if ((NowMs - LastAlarmBlinkMs) >= ALARM_BLINK_HALF_PERIOD_MS)
    {
      LastAlarmBlinkMs = NowMs;
      AlarmLedOn = !AlarmLedOn;
      LED_Set(LED_ALARM_PIN, AlarmLedOn);
    }
  }
  else
  {
    AlarmLedOn = 0;
    LED_Set(LED_ALARM_PIN, 0);
  }

  LED_Set(LED_APP_PIN, AppLedOn);
}

static void OLED_ShowPadded(uint8_t Line, const char *Text)
{
  char Buffer[17];
  uint8_t i;

  if ((Line == 0U) || (Line > 4U))
  {
    return;
  }

  for (i = 0; i < 16U; i++)
  {
    if ((Text != NULL) && (Text[i] != '\0'))
    {
      Buffer[i] = Text[i];
    }
    else
    {
      Buffer[i] = ' ';
    }
  }
  Buffer[16] = '\0';

  if (strcmp(OledLineCache[Line - 1U], Buffer) == 0)
  {
    return;
  }

  memcpy(OledLineCache[Line - 1U], Buffer, sizeof(Buffer));
  OLED_ShowLine(Line, Buffer);
}

static void ProcessStatusOutput(uint32_t NowMs)
{
  static uint32_t LastReportMs;
  static uint32_t LastOledUpdateMs;
  char Text[80];
  char Line[17];
  uint32_t PeriodMs;

  PeriodMs = ReportPeriodMs;
  if ((NowMs - LastOledUpdateMs) >= OLED_UPDATE_PERIOD_MS)
  {
    LastOledUpdateMs = NowMs;

    snprintf(Line, sizeof(Line), "%s", MotorStateText(MotorState));
    OLED_ShowPadded(1, Line);
    snprintf(Line, sizeof(Line), "V:%lu.%03luV",
             (unsigned long)(PotVoltageMv / 1000U),
             (unsigned long)(PotVoltageMv % 1000U));
    OLED_ShowPadded(2, Line);
    snprintf(Line, sizeof(Line), "DUTY:%u%%", PwmDuty);
    OLED_ShowPadded(3, Line);
    snprintf(Line, sizeof(Line), "T:%lums", (unsigned long)PeriodMs);
    OLED_ShowPadded(4, Line);
  }

  if ((NowMs - LastReportMs) >= PeriodMs)
  {
    LastReportMs = NowMs;
    snprintf(Text, sizeof(Text), "STATE:%s,V:%lu.%03lu,DUTY:%u%%,T:%lums\r\n",
             MotorStateText(MotorState),
             (unsigned long)(PotVoltageMv / 1000U),
             (unsigned long)(PotVoltageMv % 1000U),
             PwmDuty,
             (unsigned long)PeriodMs);
    HAL_UART_Transmit(&huart1, (uint8_t *)Text, strlen(Text), 100);
    HAL_UART_Transmit(&huart2, (uint8_t *)Text, strlen(Text), 100);
  }
}

static void ProcessCommandAck(void)
{
  char Text[32];
  uint32_t PeriodMs;
  uint8_t Kind;
  uint8_t Target;
  uint32_t Value;

  if (CommandAckPending == 0U)
  {
    return;
  }

  CommandAckPending = 0U;
  Target = CommandAckTarget;
  CommandAckTarget = 0U;
  Kind = CommandAckKind;
  Value = CommandAckValue;
  PeriodMs = ReportPeriodMs;

  if (Kind == 'M')
  {
    snprintf(Text, sizeof(Text), "OK,M:%lu\r\n", (unsigned long)Value);
  }
  else
  {
    snprintf(Text, sizeof(Text), "OK,T:%lums\r\n", (unsigned long)PeriodMs);
  }
  if ((Target & ACK_TARGET_USART1) != 0U)
  {
    HAL_UART_Transmit(&huart1, (uint8_t *)Text, strlen(Text), 100);
  }
  if ((Target & ACK_TARGET_USART2) != 0U)
  {
    HAL_UART_Transmit(&huart2, (uint8_t *)Text, strlen(Text), 100);
  }
}

static void ParseCommandByte(uint8_t Data, uint8_t AckTarget)
{
  if ((Data == 'T') || (Data == 't'))
  {
    CommandMode = 'T';
    CommandValue = 0;
  }
  else if ((CommandMode == 'T') && (Data >= '0') && (Data <= '9'))
  {
    CommandValue = CommandValue * 10U + (uint32_t)(Data - '0');
    ReportPeriodMs = ClampReportPeriod(CommandValue);
    CommandAckTarget |= AckTarget;
    CommandAckKind = 'T';
    CommandAckValue = ReportPeriodMs;
    CommandAckPending = 1U;
  }
  else if ((Data == 'L') || (Data == 'l'))
  {
    CommandMode = 'L';
  }
  else if ((CommandMode == 'L') && ((Data == '0') || (Data == '1')))
  {
    AppLedOn = (Data == '1') ? 1U : 0U;
    CommandMode = 0;
  }
  else if ((Data == 'M') || (Data == 'm'))
  {
    CommandMode = 'M';
  }
  else if ((CommandMode == 'M') && (Data >= '0') && (Data <= '5'))
  {
    ControlMode = (ControlMode_t)(Data - '0');
    CommandAckTarget |= AckTarget;
    CommandAckKind = 'M';
    CommandAckValue = (uint32_t)ControlMode;
    CommandAckPending = 1U;
    CommandMode = 0;
  }
  else if ((Data == '\r') || (Data == '\n') || (Data == ' '))
  {
    CommandMode = 0;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    ParseCommandByte(UsbRxByte, ACK_TARGET_USART1);
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&UsbRxByte, 1);
  }
  else if (huart->Instance == USART2)
  {
    ParseCommandByte(BluetoothRxByte, ACK_TARGET_USART2);
    HAL_UART_Receive_IT(&huart2, (uint8_t *)&BluetoothRxByte, 1);
  }
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
  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_ADC_Start(&hadc1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&UsbRxByte, 1);
  HAL_UART_Receive_IT(&huart2, (uint8_t *)&BluetoothRxByte, 1);

  Motor_Set(MOTOR_STOP, 0);
  LED_Set(LED_RUN_PIN, 0);
  LED_Set(LED_ALARM_PIN, 0);
  LED_Set(LED_APP_PIN, 0);

  OLED_Init();
  OLED_ShowPadded(1, "Motor Control");
  OLED_ShowPadded(2, "USART1/2 CMD");
  OLED_ShowPadded(3, "CMD: T1000");
  OLED_ShowPadded(4, "Ready");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    static uint32_t LastControlMs;
    uint32_t NowMs;

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    NowMs = HAL_GetTick();
    if ((NowMs - LastControlMs) >= CONTROL_PERIOD_MS)
    {
      LastControlMs = NowMs;
      ProcessControl();
    }

    ProcessIndicators(NowMs);
    ProcessCommandAck();
    ProcessStatusOutput(NowMs);
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
