/**
 * @file    led_task.h
 * @brief   LED 闪烁任务：独立线程，每 1000ms 翻转一次（走 Adp/hc32_drv_gpio 函数）
 */
#ifndef __LED_TASK_H__

/* 默认配置宏（统一放头文件） */
#define LED_TOGGLE_MS    (1000U)  /* LED 翻转周期 ms */
#define LED_PORT        (PH2_PORT)      /* GPIO_PORT_H */
#define LED_PIN         (PH2_PIN)       /* GPIO_PIN_02 */
#define LED_THREAD_STACK    (1024U)
#define LED_THREAD_PRIO     (25)        /* 低优先级，不影响电源/控制 */
#define LED_THREAD_TICK     (10)
#define __LED_TASK_H__

/* 初始化 LED GPIO 并启动闪烁线程 */
void Led_Task_Start(void);

#endif /* __LED_TASK_H__ */

