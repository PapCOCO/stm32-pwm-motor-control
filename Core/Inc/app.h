#ifndef APP_H
#define APP_H

/* 应用层接口：初始化与 FreeRTOS 调度启动 */
void app_init(void);
void app_run(void);
void app_start_scheduler(void);

#endif
