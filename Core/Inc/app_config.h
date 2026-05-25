#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_MOTOR_TASK_PERIOD_MS 20U /* 电机控制任务周期 */
#define APP_UART_DEBUG_PERIOD_MS 1000U /* 串口调试输出周期 */

#define BUTTON_DEBOUNCE_MS 30U /* 按键去抖时间 */

#define MOTOR_DIRECTION_CHANGE_DELAY_MS 50U /* 方向切换等待时间 */
#define MOTOR_ADC_MAX_VALUE 4095U /* 12 位 ADC 最大值 */
#define MOTOR_PWM_STEP_PER_TICK 5U /* PWM 变化步长 */

#endif
