/**
 * @file    led_task.h
 * @brief   LED 闪烁任务：独立线程，每 1000ms 翻转一次（走 Adp/hc32_drv_gpio 函数）
 */
#ifndef __LED_TASK_H__
#define __LED_TASK_H__

#include "Task/task_set.h"     /* 栈大小/优先级统一管理 */

/* 默认配置宏（统一放头文件） */
#define LED_TOGGLE_MS    (1000U)  /* LED 翻转周期 ms */
#define LED_PORT        (PH2_PORT)      /* GPIO_PORT_H */
#define LED_PIN         (PH2_PIN)       /* GPIO_PIN_02 */
#define LED_THREAD_STACK    TASK_STACK_LED   /* 引用 task_set.h 唯一来源 */
#define LED_THREAD_PRIO     TASK_PRIO_LED        /* 低优先级，不影响电源/控制 */
#define LED_THREAD_TICK     (10)

/* 初始化 LED GPIO 并启动闪烁线程 */
void Led_Task_Start(void);

#endif /* __LED_TASK_H__ */
