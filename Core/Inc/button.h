#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  BUTTON_ID_RUN = 0,
  BUTTON_ID_DIRECTION,
  BUTTON_ID_COUNT
} ButtonId_t; /* 按键 ID，用于区分 RUN 和 DIRECTION */

void button_init(void);
void button_task_step(uint32_t now_ms);
bool button_consume_press(ButtonId_t id);

#endif
