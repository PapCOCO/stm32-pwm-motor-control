#include "motor.h"

#include "adc.h"
#include "app_config.h"
#include "button.h"
#include "tim.h"

/*
 * motor.c
 * 电机控制模块：
 * - 读取 ADC 输入值
 * - 计算目标 PWM
 * - 处理按钮输入
 * - 通过状态机控制电机启动、停机、换向
 */

static MotorStatus_t motor_status;
static uint32_t direction_wait_start_ms;

/* 获取 TIM3 PWM 的最大值 (ARR 自动重装载值) */
static uint16_t motor_get_pwm_max(void)
{
  return (uint16_t)__HAL_TIM_GET_AUTORELOAD(&htim3);
}

/* 将 PWM 限制到定时器允许的最大范围 */
static uint16_t motor_limit_pwm(uint16_t pwm)
{
  uint16_t max_pwm;

  max_pwm = motor_get_pwm_max();
  if (pwm > max_pwm)
  {
    return max_pwm;
  }
  return pwm;
}

/* 读取 ADC 值，返回最新的转换结果。如读取失败则保留上次值 */
static uint16_t motor_read_adc(void)
{
  if (HAL_ADC_PollForConversion(&hadc1, 1U) == HAL_OK)
  {
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
  }

  return motor_status.adc_value;
}

/* 将 ADC 值映射到 PWM 值范围 */
static uint16_t motor_adc_to_pwm(uint16_t adc_value)
{
  uint32_t pwm;

  pwm = ((uint32_t)adc_value * (uint32_t)motor_get_pwm_max()) / MOTOR_ADC_MAX_VALUE;
  return motor_limit_pwm((uint16_t)pwm);
}

/*
 * PWM 软变化函数：每次只允许向目标值逼近一个步长。
 * 这样可以避免 PWM 突变导致电机突然冲击。
 */
static uint16_t motor_step_toward(uint16_t current, uint16_t target)
{
  if (current < target)
  {
    uint16_t next;

    next = (uint16_t)(current + MOTOR_PWM_STEP_PER_TICK);
    if ((next < current) || (next > target))
    {
      return target;
    }
    return next;
  }

  if (current > target)
  {
    if ((current - target) <= MOTOR_PWM_STEP_PER_TICK)
    {
      return target;
    }
    return (uint16_t)(current - MOTOR_PWM_STEP_PER_TICK);
  }

  return current;
}

/*
 * 根据当前 PWM 值和方向，输出给 TIM3 的两个 PWM 通道。
 * forward_pwm 对应正转，reverse_pwm 对应反转。
 */
static void motor_apply_pwm(void)
{
  uint16_t forward_pwm;
  uint16_t reverse_pwm;

  forward_pwm = 0U;
  reverse_pwm = 0U;

  if (motor_status.current_pwm > 0U)
  {
    if (motor_status.output_direction == MOTOR_DIRECTION_FORWARD)
    {
      forward_pwm = motor_status.current_pwm;
    }
    else
    {
      reverse_pwm = motor_status.current_pwm;
    }
  }

  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, forward_pwm);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, reverse_pwm);
}

/*
 * 按键处理：
 * - RUN 按键切换启动/停止状态
 * - DIRECTION 按键切换目标转向
 */
static void motor_update_buttons(uint32_t now_ms)
{
  button_task_step(now_ms);

  if (button_consume_press(BUTTON_ID_RUN))
  {
    motor_status.run_enabled = !motor_status.run_enabled;
  }

  if (button_consume_press(BUTTON_ID_DIRECTION))
  {
    if (motor_status.requested_direction == MOTOR_DIRECTION_FORWARD)
    {
      motor_status.requested_direction = MOTOR_DIRECTION_REVERSE;
    }
    else
    {
      motor_status.requested_direction = MOTOR_DIRECTION_FORWARD;
    }
  }
}

/*
 * 状态机：根据按钮、目标 PWM 和当前阶段决定电机行为。
 * - STOPPED: 停机状态
 * - RUNNING: 正常运行状态
 * - RAMP_DOWN: 变向前先慢速下降 PWM
 * - CHANGE_WAIT: 变向等待时间
 */
static void motor_update_state(uint32_t now_ms)
{
  bool direction_change_requested;
  uint16_t desired_pwm;

  desired_pwm = motor_status.run_enabled ? motor_status.target_pwm : 0U;
  direction_change_requested =
      motor_status.run_enabled &&
      (motor_status.requested_direction != motor_status.output_direction);

  switch (motor_status.phase)
  {
  case MOTOR_PHASE_STOPPED:
    motor_status.current_pwm = 0U;
    if (direction_change_requested)
    {
      motor_status.output_direction = motor_status.requested_direction;
    }
    if (desired_pwm > 0U)
    {
      motor_status.phase = MOTOR_PHASE_RUNNING;
    }
    break;

  case MOTOR_PHASE_RUNNING:
    if (direction_change_requested)
    {
      motor_status.phase = MOTOR_PHASE_RAMP_DOWN;
      motor_status.current_pwm = motor_step_toward(motor_status.current_pwm, 0U);
    }
    else
    {
      motor_status.current_pwm = motor_step_toward(motor_status.current_pwm, desired_pwm);
      if ((desired_pwm == 0U) && (motor_status.current_pwm == 0U))
      {
        motor_status.phase = MOTOR_PHASE_STOPPED;
      }
    }
    break;

  case MOTOR_PHASE_RAMP_DOWN:
    motor_status.current_pwm = motor_step_toward(motor_status.current_pwm, 0U);
    if (motor_status.current_pwm == 0U)
    {
      direction_wait_start_ms = now_ms;
      motor_status.phase = MOTOR_PHASE_CHANGE_WAIT;
    }
    break;

  case MOTOR_PHASE_CHANGE_WAIT:
    if ((now_ms - direction_wait_start_ms) >= MOTOR_DIRECTION_CHANGE_DELAY_MS)
    {
      motor_status.output_direction = motor_status.requested_direction;
      motor_status.phase = motor_status.run_enabled ? MOTOR_PHASE_RUNNING : MOTOR_PHASE_STOPPED;
    }
    break;

  default:
    motor_status.phase = MOTOR_PHASE_STOPPED;
    motor_status.current_pwm = 0U;
    break;
  }
}

void motor_init(void)
{
  /* 初始化按键和电机状态 */
  button_init();

  motor_status.run_enabled = false;
  motor_status.requested_direction = MOTOR_DIRECTION_FORWARD;
  motor_status.output_direction = MOTOR_DIRECTION_FORWARD;
  motor_status.phase = MOTOR_PHASE_STOPPED;
  motor_status.adc_value = 0U;
  motor_status.target_pwm = 0U;
  motor_status.current_pwm = 0U;
  direction_wait_start_ms = 0U;

  /* 启动 ADC 和 PWM 输出 */
  (void)HAL_ADCEx_Calibration_Start(&hadc1);
  (void)HAL_ADC_Start(&hadc1);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
  motor_apply_pwm();
}

/* 电机主任务，每次调度读取输入并更新输出 */
void motor_task_step(uint32_t now_ms)
{
  motor_update_buttons(now_ms);
  motor_status.adc_value = motor_read_adc();
  motor_status.target_pwm = motor_adc_to_pwm(motor_status.adc_value);
  motor_update_state(now_ms);
  motor_apply_pwm();
}

/* 获取当前电机状态，用于调试或显示 */
void motor_get_status(MotorStatus_t *status)
{
  if (status != 0)
  {
    *status = motor_status;
  }
}
