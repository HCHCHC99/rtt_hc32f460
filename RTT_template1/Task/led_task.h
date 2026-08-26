/**
 * @file    led_task.h
 * @brief   LED 闪烁任务：独立线程，每 1000ms 翻转一次（走 Adp/hc32_drv_gpio 函数）
 */
#ifndef __LED_TASK_H__
#define __LED_TASK_H__

/* 初始化 LED GPIO 并启动闪烁线程 */
void Led_Task_Start(void);

#endif /* __LED_TASK_H__ */
