#include "motor_control.h"

#include "adc.h"
#include "gpio.h"
#include "tim.h"

/* 保存一个按键去抖后的稳定状态。 */
typedef struct
{
  GPIO_PinState StableLevel;
  GPIO_PinState LastRawLevel;
  uint32_t LastRawChangeMs;
} DebouncedKey_t;

static DebouncedKey_t RunKey;
static DebouncedKey_t DirKey;

/* ADC 原始值、滤波值和健康状态。 */
static uint16_t AdcRaw;
static uint32_t AdcFiltered;
static uint8_t AdcFilterReady;
static uint8_t AdcFailCount;

/* 下面这些变量描述“用户想要什么”和“电机当前真正输出了什么”。 */
static uint16_t PotVoltageMv;
static uint8_t TargetDuty;
static uint8_t RequestedOutputDuty;
static uint8_t ActualOutputDuty;
static AppMotorState_t RequestedState = APP_MOTOR_STOP;
static AppMotorState_t OutputState = APP_MOTOR_STOP;
static AppMotorState_t PendingState = APP_MOTOR_STOP;
static uint8_t ReverseWaitActive;
static uint32_t ReverseWaitStartMs;

/* 按当前方向和占空比把 PWM 真正写到 TIM3 的两个通道上。 */
static void MotorControl_WritePwm(AppMotorState_t State, uint8_t DutyPercent)
{
  uint32_t PeriodCounts;
  uint32_t Compare;

  if (DutyPercent > APP_PWM_MAX_DUTY)
  {
    DutyPercent = APP_PWM_MAX_DUTY;
  }

  /*
   * 用 ARR+1 参与计算，而不是把 100 写死。
   * 这样以后即使修改了定时器周期，占空比换算仍然是对的。
   */
  PeriodCounts = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U;
  Compare = (PeriodCounts * (uint32_t)DutyPercent) / APP_PWM_MAX_DUTY;

  if ((State == APP_MOTOR_FORWARD) && (DutyPercent > 0U))
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, Compare);
  }
  else if ((State == APP_MOTOR_REVERSE) && (DutyPercent > 0U))
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, Compare);
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0U);
  }
}

/* 对机械按键做最基础的时间去抖。 */
static GPIO_PinState MotorControl_UpdateKey(DebouncedKey_t *Key, GPIO_PinState RawLevel, uint32_t NowMs)
{
  if (RawLevel != Key->LastRawLevel)
  {
    Key->LastRawLevel = RawLevel;
    Key->LastRawChangeMs = NowMs;
  }

  /*
   * 只有电平稳定持续一段时间，才承认按键状态真的变了。
   * 这样能避免按下/松开瞬间的抖动被误判。
   */
  if ((NowMs - Key->LastRawChangeMs) >= APP_KEY_DEBOUNCE_MS)
  {
    Key->StableLevel = Key->LastRawLevel;
  }

  return Key->StableLevel;
}

/* 让占空比按固定步进逐渐靠近目标值，实现软启动和软减速。 */
static uint8_t MotorControl_RampDuty(uint8_t Current, uint8_t Target)
{
  if (Current < Target)
  {
    uint8_t Delta = (uint8_t)(Target - Current);
    return (Delta > APP_MOTOR_RAMP_STEP_DUTY) ? (uint8_t)(Current + APP_MOTOR_RAMP_STEP_DUTY) : Target;
  }

  if (Current > Target)
  {
    uint8_t Delta = (uint8_t)(Current - Target);
    return (Delta > APP_MOTOR_RAMP_STEP_DUTY) ? (uint8_t)(Current - APP_MOTOR_RAMP_STEP_DUTY) : Target;
  }

  return Current;
}

/* 把电位器得到的 1..100% 映射成更适合电机起转的输出占空比。 */
static uint8_t MotorControl_MapDutyForRunning(uint8_t Duty)
{
  if (Duty == 0U)
  {
    return 0U;
  }

  /*
   * 低占空比时电机可能只会发热不转，所以把用户的 1..100%
   * 映射到真正输出的较高范围。OLED 仍显示用户目标值，便于理解。
   */
  return (uint8_t)(APP_MOTOR_MIN_START_DUTY +
                  ((((uint32_t)Duty - 1U) * (APP_PWM_MAX_DUTY - APP_MOTOR_MIN_START_DUTY)) /
                   (APP_PWM_MAX_DUTY - 1U)));
}

/* 读取 ADC，并做一个轻量的一阶低通滤波。 */
static void MotorControl_ReadAdc(void)
{
  if (HAL_ADC_PollForConversion(&hadc1, 2U) == HAL_OK)
  {
    AdcRaw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    AdcFailCount = 0U;

    if (AdcFilterReady == 0U)
    {
      AdcFiltered = AdcRaw;
      AdcFilterReady = 1U;
    }
    else
    {
      AdcFiltered = (uint32_t)((int32_t)AdcFiltered +
                    (((int32_t)AdcRaw - (int32_t)AdcFiltered) >> APP_ADC_FILTER_SHIFT));
    }

    PotVoltageMv = (uint16_t)((AdcFiltered * APP_VREF_MV) / APP_ADC_MAX_VALUE);
    TargetDuty = (uint8_t)((AdcFiltered * APP_PWM_MAX_DUTY) / APP_ADC_MAX_VALUE);
  }
  else if (AdcFailCount < APP_ADC_FAIL_LIMIT)
  {
    AdcFailCount++;
  }
}

/* 根据串口模式和按键状态，决定“希望电机往哪转、转多快”。 */
static void MotorControl_UpdateRequest(uint32_t NowMs, AppControlMode_t ControlMode)
{
  GPIO_PinState RunLevel;
  GPIO_PinState DirLevel;

  RequestedOutputDuty = TargetDuty;

  if (ControlMode == APP_CONTROL_FORCE_FORWARD)
  {
    RequestedState = APP_MOTOR_FORWARD;
  }
  else if (ControlMode == APP_CONTROL_FORCE_REVERSE)
  {
    RequestedState = APP_MOTOR_REVERSE;
  }
  else if (ControlMode == APP_CONTROL_FORCE_STOP)
  {
    RequestedState = APP_MOTOR_STOP;
  }
  else if (ControlMode == APP_CONTROL_TEST_FORWARD_100)
  {
    RequestedState = APP_MOTOR_FORWARD;
    TargetDuty = APP_PWM_MAX_DUTY;
    RequestedOutputDuty = APP_PWM_MAX_DUTY;
  }
  else if (ControlMode == APP_CONTROL_TEST_REVERSE_100)
  {
    RequestedState = APP_MOTOR_REVERSE;
    TargetDuty = APP_PWM_MAX_DUTY;
    RequestedOutputDuty = APP_PWM_MAX_DUTY;
  }
  else
  {
    RunLevel = MotorControl_UpdateKey(&RunKey, HAL_GPIO_ReadPin(APP_KEY_GPIO_PORT, APP_KEY_RUN_PIN), NowMs);
    DirLevel = MotorControl_UpdateKey(&DirKey, HAL_GPIO_ReadPin(APP_KEY_GPIO_PORT, APP_KEY_DIR_PIN), NowMs);

    if (RunLevel != APP_KEY_PRESSED)
    {
      RequestedState = APP_MOTOR_STOP;
    }
    else if (DirLevel == APP_KEY_PRESSED)
    {
      RequestedState = APP_MOTOR_FORWARD;
    }
    else
    {
      RequestedState = APP_MOTOR_REVERSE;
    }
  }

  if ((AdcFailCount >= APP_ADC_FAIL_LIMIT) || (RequestedState == APP_MOTOR_STOP))
  {
    TargetDuty = 0U;
    RequestedOutputDuty = 0U;
    RequestedState = APP_MOTOR_STOP;
  }
  else
  {
    RequestedOutputDuty = MotorControl_MapDutyForRunning(RequestedOutputDuty);
  }
}

/* 处理正反转切换的保护逻辑，避免电机直接硬切换方向。 */
static uint8_t MotorControl_UpdateDirectionState(uint32_t NowMs)
{
  uint8_t RampTargetDuty;

  RampTargetDuty = RequestedOutputDuty;

  if ((RequestedState == APP_MOTOR_STOP) || (RequestedOutputDuty == 0U))
  {
    ReverseWaitActive = 0U;
    PendingState = APP_MOTOR_STOP;
    RampTargetDuty = 0U;
  }
  else if (ReverseWaitActive != 0U)
  {
    PendingState = RequestedState;
    RampTargetDuty = 0U;
    if ((NowMs - ReverseWaitStartMs) >= APP_MOTOR_REVERSE_DELAY_MS)
    {
      OutputState = PendingState;
      ReverseWaitActive = 0U;
      RampTargetDuty = RequestedOutputDuty;
    }
  }
  else if ((OutputState == APP_MOTOR_STOP) && (ActualOutputDuty == 0U))
  {
    OutputState = RequestedState;
  }
  else if (OutputState != RequestedState)
  {
    /*
     * 换向时不能直接从正转跳到反转。
     * 这里先把当前 PWM 缓慢降到 0，等待一小段时间，再允许新方向重新启动。
     */
    PendingState = RequestedState;
    RampTargetDuty = 0U;
    if (ActualOutputDuty == 0U)
    {
      OutputState = APP_MOTOR_STOP;
      ReverseWaitActive = 1U;
      ReverseWaitStartMs = NowMs;
    }
  }

  return RampTargetDuty;
}

/* 初始化运行时状态，并确保上电时 PWM 为 0。 */
void MotorControl_Init(void)
{
  GPIO_PinState RunRaw;
  GPIO_PinState DirRaw;

  RunRaw = HAL_GPIO_ReadPin(APP_KEY_GPIO_PORT, APP_KEY_RUN_PIN);
  DirRaw = HAL_GPIO_ReadPin(APP_KEY_GPIO_PORT, APP_KEY_DIR_PIN);
  RunKey.StableLevel = RunRaw;
  RunKey.LastRawLevel = RunRaw;
  RunKey.LastRawChangeMs = 0U;
  DirKey.StableLevel = DirRaw;
  DirKey.LastRawLevel = DirRaw;
  DirKey.LastRawChangeMs = 0U;

  AdcRaw = 0U;
  AdcFiltered = 0U;
  AdcFilterReady = 0U;
  AdcFailCount = 0U;
  PotVoltageMv = 0U;
  TargetDuty = 0U;
  RequestedOutputDuty = 0U;
  ActualOutputDuty = 0U;
  RequestedState = APP_MOTOR_STOP;
  OutputState = APP_MOTOR_STOP;
  PendingState = APP_MOTOR_STOP;
  ReverseWaitActive = 0U;
  ReverseWaitStartMs = 0U;

  MotorControl_WritePwm(APP_MOTOR_STOP, 0U);
}

/* 每个控制周期调用一次，推进整个电机控制状态机。 */
void MotorControl_Task(uint32_t NowMs, AppControlMode_t ControlMode)
{
  uint8_t RampTargetDuty;

  MotorControl_ReadAdc();
  MotorControl_UpdateRequest(NowMs, ControlMode);
  RampTargetDuty = MotorControl_UpdateDirectionState(NowMs);
  ActualOutputDuty = MotorControl_RampDuty(ActualOutputDuty, RampTargetDuty);

  if (ActualOutputDuty == 0U)
  {
    MotorControl_WritePwm(APP_MOTOR_STOP, 0U);
    if ((RequestedState == APP_MOTOR_STOP) && (ReverseWaitActive == 0U))
    {
      OutputState = APP_MOTOR_STOP;
    }
  }
  else
  {
    MotorControl_WritePwm(OutputState, ActualOutputDuty);
  }
}

/* 把当前内部状态打包成一个稳定的快照，供别的模块读取。 */
void MotorControl_GetStatus(MotorStatus_t *Status)
{
  if (Status == NULL)
  {
    return;
  }

  Status->State = (ActualOutputDuty == 0U) ? APP_MOTOR_STOP : OutputState;
  Status->VoltageMv = PotVoltageMv;
  Status->TargetDuty = TargetDuty;
  Status->OutputDuty = ActualOutputDuty;
  Status->AdcHealthy = (AdcFailCount < APP_ADC_FAIL_LIMIT) ? 1U : 0U;
}

/* 把状态枚举转成给 OLED/串口显示的英文字符串。 */
const char *MotorControl_StateText(AppMotorState_t State)
{
  if (State == APP_MOTOR_FORWARD)
  {
    return "FORWARD";
  }
  if (State == APP_MOTOR_REVERSE)
  {
    return "REVERSE";
  }
  return "STOP";
}
