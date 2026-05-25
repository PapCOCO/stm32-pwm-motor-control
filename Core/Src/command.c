#include "command.h"

#include "app_uart.h"
#include "usart.h"

#include <stdio.h>

/* 这些变量组成一个极简串口命令状态机。 */
static volatile uint8_t CommandMode;
static volatile uint32_t CommandValue;
static volatile uint32_t ReportPeriodMs = APP_REPORT_PERIOD_DEFAULT_MS;
static volatile uint8_t CommandAckPending;
static volatile uint8_t CommandAckTarget;
static volatile uint8_t CommandAckKind;
static volatile uint32_t CommandAckValue;
static volatile uint8_t AppLedOn;
static volatile uint8_t ControlMode = (uint8_t)APP_CONTROL_KEYS;

/* 把串口设置的上报周期限制在允许范围内。 */
static uint32_t Command_ClampReportPeriod(uint32_t Value)
{
  if (Value < APP_REPORT_PERIOD_MIN_MS)
  {
    return APP_REPORT_PERIOD_MIN_MS;
  }
  if (Value > APP_REPORT_PERIOD_MAX_MS)
  {
    return APP_REPORT_PERIOD_MAX_MS;
  }
  return Value;
}

/* 把需要回复给串口的 ACK 暂存起来，交给主循环统一发送。 */
static void Command_SetAck(uint8_t Target, uint8_t Kind, uint32_t Value)
{
  CommandAckTarget |= Target;
  CommandAckKind = Kind;
  CommandAckValue = Value;
  CommandAckPending = 1U;
}

static uint8_t Command_TakeAck(CommandAck_t *Ack)
{
  uint32_t Primask;

  if (Ack == NULL)
  {
    return 0U;
  }

  /*
   * ACK 在串口中断里写入、在主循环里读走。
   * 这里短暂关中断，保证“复制 ACK + 清空标志”是一个原子动作。
   */
  Primask = __get_PRIMASK();
  __disable_irq();

  if (CommandAckPending == 0U)
  {
    if (Primask == 0U)
    {
      __enable_irq();
    }
    return 0U;
  }

  Ack->Target = CommandAckTarget;
  Ack->Kind = CommandAckKind;
  Ack->Value = CommandAckValue;
  CommandAckPending = 0U;
  CommandAckTarget = 0U;

  if (Primask == 0U)
  {
    __enable_irq();
  }
  return 1U;
}

void Command_Init(void)
{
  CommandMode = 0U;
  CommandValue = 0U;
  ReportPeriodMs = APP_REPORT_PERIOD_DEFAULT_MS;
  CommandAckPending = 0U;
  CommandAckTarget = 0U;
  CommandAckKind = 0U;
  CommandAckValue = 0U;
  AppLedOn = 0U;
  ControlMode = (uint8_t)APP_CONTROL_KEYS;
}

/* 串口每收到 1 个字节就调用一次，用简单状态机解析命令。 */
void Command_ParseByte(uint8_t Data, uint8_t AckTarget)
{
  /* T1000: 设置状态上报周期，单位 ms。 */
  if ((Data == (uint8_t)'T') || (Data == (uint8_t)'t'))
  {
    CommandMode = (uint8_t)'T';
    CommandValue = 0U;
  }
  else if ((CommandMode == (uint8_t)'T') && (Data >= (uint8_t)'0') && (Data <= (uint8_t)'9'))
  {
    uint32_t Digit;

    /*
     * T 后面可以连续输入多位数字，例如 T1000。
     * 每收到一位就更新一次数值，并顺手处理 uint32_t 溢出。
     */
    Digit = (uint32_t)(Data - (uint8_t)'0');
    if (CommandValue <= ((UINT32_MAX - Digit) / 10U))
    {
      CommandValue = (CommandValue * 10U) + Digit;
    }
    else
    {
      CommandValue = APP_REPORT_PERIOD_MAX_MS;
    }

    ReportPeriodMs = Command_ClampReportPeriod(CommandValue);
    Command_SetAck(AckTarget, COMMAND_ACK_KIND_T, ReportPeriodMs);
  }
  /* L0 / L1: 控制应用指示灯灭/亮。 */
  else if ((Data == (uint8_t)'L') || (Data == (uint8_t)'l'))
  {
    CommandMode = (uint8_t)'L';
  }
  else if ((CommandMode == (uint8_t)'L') && ((Data == (uint8_t)'0') || (Data == (uint8_t)'1')))
  {
    AppLedOn = (Data == (uint8_t)'1') ? 1U : 0U;
    CommandMode = 0U;
  }
  /* M0..M5: 切换电机控制模式。 */
  else if ((Data == (uint8_t)'M') || (Data == (uint8_t)'m'))
  {
    CommandMode = (uint8_t)'M';
  }
  else if ((CommandMode == (uint8_t)'M') && (Data >= (uint8_t)'0') && (Data <= (uint8_t)'5'))
  {
    ControlMode = (uint8_t)(Data - (uint8_t)'0');
    Command_SetAck(AckTarget, COMMAND_ACK_KIND_M, (uint32_t)ControlMode);
    CommandMode = 0U;
  }
  /* 回车、换行、空格都视为一次命令输入结束。 */
  else if ((Data == (uint8_t)'\r') || (Data == (uint8_t)'\n') || (Data == (uint8_t)' '))
  {
    CommandMode = 0U;
  }
}

/* 主循环里统一发送 ACK，避免在中断里直接做较慢的串口发送。 */
void Command_ProcessAck(void)
{
  CommandAck_t Ack;
  char Text[32];

  if (Command_TakeAck(&Ack) == 0U)
  {
    return;
  }

  if (Ack.Kind == COMMAND_ACK_KIND_M)
  {
    (void)snprintf(Text, sizeof(Text), "OK,M:%lu\r\n", (unsigned long)Ack.Value);
  }
  else
  {
    (void)snprintf(Text, sizeof(Text), "OK,T:%lums\r\n", (unsigned long)ReportPeriodMs);
  }

  if ((Ack.Target & COMMAND_ACK_TARGET_USART1) != 0U)
  {
    AppUart_SendText(&huart1, Text, 100U);
  }
  if ((Ack.Target & COMMAND_ACK_TARGET_USART2) != 0U)
  {
    AppUart_SendText(&huart2, Text, 100U);
  }
}

uint32_t Command_GetReportPeriodMs(void)
{
  return ReportPeriodMs;
}

/* 供控制任务读取当前控制模式。 */
AppControlMode_t Command_GetControlMode(void)
{
  return (AppControlMode_t)ControlMode;
}

/* 供 UI 读取应用指示灯状态。 */
uint8_t Command_GetAppLedOn(void)
{
  return AppLedOn;
}
