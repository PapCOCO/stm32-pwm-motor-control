#include "button.h"

#include "app_config.h"
#include "main.h"

/*
 * button.c
 * 按键去抖和按下事件处理模块。
 * RUN 和 DIRECTION 两个按键采样 PB12/PB13，按下后生成一次性 press 事件。
 */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState raw_state;
  GPIO_PinState stable_state;
  uint32_t changed_ms;
  bool press_pending;
} ButtonState_t;

static ButtonState_t buttons[BUTTON_ID_COUNT] = {
    [BUTTON_ID_RUN] = {GPIOB, GPIO_PIN_12, GPIO_PIN_SET, GPIO_PIN_SET, 0U, false},
    [BUTTON_ID_DIRECTION] = {GPIOB, GPIO_PIN_13, GPIO_PIN_SET, GPIO_PIN_SET, 0U, false},
};

static bool button_is_valid(ButtonId_t id)
{
  return ((uint32_t)id < (uint32_t)BUTTON_ID_COUNT);
}

/* 初始化按键状态，读取当前 GPIO 电平并清除待处理按压 */
void button_init(void)
{
  uint32_t now_ms;

  now_ms = HAL_GetTick();
  for (uint32_t i = 0U; i < (uint32_t)BUTTON_ID_COUNT; ++i)
  {
    buttons[i].raw_state = HAL_GPIO_ReadPin(buttons[i].port, buttons[i].pin);
    buttons[i].stable_state = buttons[i].raw_state;
    buttons[i].changed_ms = now_ms;
    buttons[i].press_pending = false;
  }
}

/*
 * 按键去抖任务，需要周期调用。
 * 先检测 GPIO 电平变化，再等待 debounce 时间，最后确认按下事件。
 */
void button_task_step(uint32_t now_ms)
{
  for (uint32_t i = 0U; i < (uint32_t)BUTTON_ID_COUNT; ++i)
  {
    GPIO_PinState raw_state;

    raw_state = HAL_GPIO_ReadPin(buttons[i].port, buttons[i].pin);
    if (raw_state != buttons[i].raw_state)
    {
      buttons[i].raw_state = raw_state;
      buttons[i].changed_ms = now_ms;
    }

    if ((raw_state != buttons[i].stable_state) &&
        ((now_ms - buttons[i].changed_ms) >= BUTTON_DEBOUNCE_MS))
    {
      buttons[i].stable_state = raw_state;
      if (raw_state == GPIO_PIN_RESET)
      {
        buttons[i].press_pending = true;
      }
    }
  }
}

/*
 * 获取并清除按键按下事件。
 * 只有当按键从未按下到按下时才会产生一次 press_pending。
 */
bool button_consume_press(ButtonId_t id)
{
  bool pressed;

  if (!button_is_valid(id))
  {
    return false;
  }

  pressed = buttons[id].press_pending;
  buttons[id].press_pending = false;
  return pressed;
}
