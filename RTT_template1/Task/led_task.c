/**
 * @file    led_task.c
 * @brief   LED 闪烁任务实现（PH2，1000ms 翻转）
 * @note    使用 Adp/hc32_drv_gpio 的 Output_GPIO_Init + Hc32_Gpio_Toggle；
 *          独立线程（C 模式），优先级取低值，栈 1024。
 */
#include "led_task.h"
#include "Adp/hc32_drv_gpio.h"
#include "rtt_manager.h"      /* LED_PRINT */
#include "Utils/us_timer.h"   /* 翻转打印带 us 时间戳 */
#include <rtthread.h>



static void led_thread_entry(void *param)
{
    uint8_t on;
    uint32_t t_us = 0U;

    (void)param;
    while (1) {
        Hc32_Gpio_Toggle(LED_PORT, LED_PIN);          /* 每 1000ms 翻转一次 */
        on = Hc32_Gpio_Read(LED_PORT, LED_PIN);       /* 翻转后读实际电平：1=亮 0=灭 */
        UsTimer_UpdateTimestamp();
        t_us = (uint32_t)UsTimer_GetTimestampUs();
        LED_PRINT("%s t=%uus", (on ? "on" : "off"), t_us);
        Task_Set_Beat();
        rt_thread_mdelay(LED_TOGGLE_MS);
    }
}

void Led_Task_Start(void)
{
    /* 初始化 LED GPIO：推挽输出，初始低电平 */
    Output_GPIO_Init(LED_PORT, LED_PIN, GPIO_INIT_LOW);

    (void)Task_Set_Create("led", led_thread_entry, RT_NULL,
                          LED_THREAD_STACK, LED_THREAD_PRIO, 2000U);
}

/* EOF */



