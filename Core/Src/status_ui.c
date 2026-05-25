#include "status_ui.h"

#include "app_config.h"
#include "app_uart.h"
#include "oled.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define STATUS_UI_LEFT_BLOCK_WIDTH (APP_OLED_LEFT_STATUS_WIDTH + 1U)

/* 缓存上一帧 OLED 的内容，避免重复刷屏。 */
static char OledLineCache[4][17];

/* LED 是低电平点亮，具体电平含义已经在 app_config.h 里封装好。 */
static void StatusUi_SetLed(uint16_t Pin, uint8_t On)
{
  HAL_GPIO_WritePin(APP_LED_GPIO_PORT, Pin, On ? APP_LED_ON : APP_LED_OFF);
}

/* 整行显示 16 个字符，不足补空格。 */
static void StatusUi_ShowPadded(uint8_t Line, const char *Text, uint8_t Force)
{
  char Buffer[17];
  uint8_t i;
  uint8_t TextEnded;

  if ((Line == 0U) || (Line > 4U))
  {
    return;
  }

  TextEnded = (Text == NULL) ? 1U : 0U;
  for (i = 0U; i < 16U; i++)
  {
    if (TextEnded != 0U)
    {
      Buffer[i] = ' ';
    }
    else if (Text[i] == '\0')
    {
      TextEnded = 1U;
      Buffer[i] = ' ';
    }
    else
    {
      Buffer[i] = Text[i];
    }
  }
  Buffer[16] = '\0';

  if ((Force == 0U) && (strcmp(OledLineCache[Line - 1U], Buffer) == 0))
  {
    return;
  }

  (void)memcpy(OledLineCache[Line - 1U], Buffer, sizeof(Buffer));
  OLED_ShowLine(Line, Buffer);
}

/* 只刷新左侧状态区，右侧姓名区保持不动。 */
static void StatusUi_ShowLeftPadded(uint8_t Line, const char *Text, uint8_t Width, uint8_t Force)
{
  char Buffer[STATUS_UI_LEFT_BLOCK_WIDTH + 1U];
  uint8_t i;
  uint8_t TextEnded;

  if ((Line == 0U) || (Line > 4U))
  {
    return;
  }
  if (Width > APP_OLED_LEFT_STATUS_WIDTH)
  {
    Width = APP_OLED_LEFT_STATUS_WIDTH;
  }

  TextEnded = (Text == NULL) ? 1U : 0U;
  for (i = 0U; i < Width; i++)
  {
    if (TextEnded != 0U)
    {
      Buffer[i] = ' ';
    }
    else if (Text[i] == '\0')
    {
      TextEnded = 1U;
      Buffer[i] = ' ';
    }
    else
    {
      Buffer[i] = Text[i];
    }
  }
  while (i < STATUS_UI_LEFT_BLOCK_WIDTH)
  {
    Buffer[i] = ' ';
    i++;
  }
  Buffer[STATUS_UI_LEFT_BLOCK_WIDTH] = '\0';

  if ((Force != 0U) || (strncmp(OledLineCache[Line - 1U], Buffer, STATUS_UI_LEFT_BLOCK_WIDTH) != 0))
  {
    (void)memcpy(OledLineCache[Line - 1U], Buffer, STATUS_UI_LEFT_BLOCK_WIDTH);
    OLED_ShowFixedString(Line, 1U, Buffer, STATUS_UI_LEFT_BLOCK_WIDTH);
  }
}

/* 清掉左侧状态区和右侧姓名区之间的分隔空白列。 */
static void StatusUi_ClearSeparator(void)
{
  uint8_t Line;

  for (Line = 1U; Line <= 4U; Line++)
  {
    OLED_ClearArea16(Line, APP_OLED_SEPARATOR_X, APP_OLED_SEPARATOR_WIDTH);
  }
}

/* OLED 上直接显示完整状态词。 */
static const char *StatusUi_OledStateText(AppMotorState_t State)
{
  return MotorControl_StateText(State);
}

/* 更新 3 个指示灯：运行灯、报警灯、应用灯。 */
static void StatusUi_ProcessIndicators(uint32_t NowMs,
                                       const MotorStatus_t *MotorStatus,
                                       uint32_t ReportPeriodMs,
                                       uint8_t AppLedOn)
{
  static uint32_t LastRunBlinkMs;
  static uint32_t LastAlarmBlinkMs;
  static uint8_t RunLedOn;
  static uint8_t AlarmLedOn;

  if ((NowMs - LastRunBlinkMs) >= ReportPeriodMs)
  {
    LastRunBlinkMs = NowMs;
    RunLedOn = !RunLedOn;
    StatusUi_SetLed(APP_LED_RUN_PIN, RunLedOn);
  }

  if ((MotorStatus != NULL) && (MotorStatus->VoltageMv >= APP_ALARM_VOLTAGE_MV))
  {
    if ((NowMs - LastAlarmBlinkMs) >= APP_ALARM_BLINK_HALF_PERIOD_MS)
    {
      LastAlarmBlinkMs = NowMs;
      AlarmLedOn = !AlarmLedOn;
      StatusUi_SetLed(APP_LED_ALARM_PIN, AlarmLedOn);
    }
  }
  else
  {
    AlarmLedOn = 0U;
    StatusUi_SetLed(APP_LED_ALARM_PIN, 0U);
  }

  StatusUi_SetLed(APP_LED_APP_PIN, AppLedOn);
}

/* 周期刷新 OLED 左侧状态区，并定时强制全量重画关键区域。 */
static void StatusUi_ProcessOled(uint32_t NowMs,
                                 const MotorStatus_t *MotorStatus,
                                 uint32_t ReportPeriodMs)
{
  static uint32_t LastOledStatusMs;
  static uint32_t LastOledForceRefreshMs;
  char Line[17];
  uint8_t ForceRefresh;

  if (MotorStatus == NULL)
  {
    return;
  }

  ForceRefresh = 0U;
  if ((NowMs - LastOledForceRefreshMs) >= APP_OLED_FORCE_REFRESH_PERIOD_MS)
  {
    LastOledForceRefreshMs = NowMs;
    ForceRefresh = 1U;
    OLED_ShowNameLabels();
    StatusUi_ClearSeparator();
  }

  if (((NowMs - LastOledStatusMs) < APP_OLED_STATUS_UPDATE_PERIOD_MS) && (ForceRefresh == 0U))
  {
    return;
  }

  LastOledStatusMs = NowMs;

  /* 第 1 行显示方向；后 3 行显示电压、目标占空比、串口上报周期。 */
  (void)snprintf(Line, sizeof(Line), "%s", StatusUi_OledStateText(MotorStatus->State));
  StatusUi_ShowLeftPadded(1U, Line, APP_OLED_LEFT_STATUS_WIDTH, ForceRefresh);

  (void)snprintf(Line, sizeof(Line), "V:%lu.%03luV",
                 (unsigned long)(MotorStatus->VoltageMv / 1000U),
                 (unsigned long)(MotorStatus->VoltageMv % 1000U));
  StatusUi_ShowLeftPadded(2U, Line, APP_OLED_LEFT_STATUS_WIDTH, ForceRefresh);

  (void)snprintf(Line, sizeof(Line), "DUTY:%u%%", MotorStatus->TargetDuty);
  StatusUi_ShowLeftPadded(3U, Line, APP_OLED_LEFT_STATUS_WIDTH, ForceRefresh);

  (void)snprintf(Line, sizeof(Line), "T:%lums", (unsigned long)ReportPeriodMs);
  StatusUi_ShowLeftPadded(4U, Line, APP_OLED_LEFT_STATUS_WIDTH, ForceRefresh);
}

/* 定时把状态文本发到两个串口，便于串口助手或蓝牙端观察。 */
static void StatusUi_ProcessSerialReport(uint32_t NowMs,
                                         const MotorStatus_t *MotorStatus,
                                         uint32_t ReportPeriodMs)
{
  static uint32_t LastReportMs;
  char Text[80];

  if ((MotorStatus == NULL) || ((NowMs - LastReportMs) < ReportPeriodMs))
  {
    return;
  }

  LastReportMs = NowMs;
  (void)snprintf(Text, sizeof(Text), "STATE:%s,V:%lu.%03lu,DUTY:%u%%,T:%lums\r\n",
                 MotorControl_StateText(MotorStatus->State),
                 (unsigned long)(MotorStatus->VoltageMv / 1000U),
                 (unsigned long)(MotorStatus->VoltageMv % 1000U),
                 MotorStatus->TargetDuty,
                 (unsigned long)ReportPeriodMs);
  AppUart_SendText(&huart1, Text, 100U);
  AppUart_SendText(&huart2, Text, 100U);
}

/* 上电后先显示欢迎/提示文本，再切到实时状态界面。 */
void StatusUi_Init(void)
{
  StatusUi_SetLed(APP_LED_RUN_PIN, 0U);
  StatusUi_SetLed(APP_LED_ALARM_PIN, 0U);
  StatusUi_SetLed(APP_LED_APP_PIN, 0U);

  (void)memset(OledLineCache, 0, sizeof(OledLineCache));
  OLED_Init();
  StatusUi_ShowPadded(1U, "Motor Control", 1U);
  StatusUi_ShowPadded(2U, "USART1/2 CMD", 1U);
  StatusUi_ShowPadded(3U, "CMD: T1000", 1U);
  StatusUi_ShowPadded(4U, "Ready", 1U);
  OLED_ShowNameLabels();
  StatusUi_ClearSeparator();
}

/* UI 模块的统一入口：灯、OLED、串口上报都在这里推进。 */
void StatusUi_Task(uint32_t NowMs,
                   const MotorStatus_t *MotorStatus,
                   uint32_t ReportPeriodMs,
                   uint8_t AppLedOn)
{
  StatusUi_ProcessIndicators(NowMs, MotorStatus, ReportPeriodMs, AppLedOn);
  StatusUi_ProcessOled(NowMs, MotorStatus, ReportPeriodMs);
  StatusUi_ProcessSerialReport(NowMs, MotorStatus, ReportPeriodMs);
}
