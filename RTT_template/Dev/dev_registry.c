/**
 * @file    dev_registry.c
 * @brief   设备注册管理：SysModule_t 注册表 + RT-Thread 设备管理线程
 * @note    参考源工程 sys_sched 的注册语义（注册即 init），task 由 RT-Thread 线程承载
 */
#include "dev_registry.h"
#include "dev_adc.h"
#include "power/current_sensor.h"
#include "power/bus_voltage.h"
#include <rtthread.h>

#define MAX_REG_MODULES         (16U)
#define DEV_THREAD_STACK_SIZE   (2048U)
#define DEV_THREAD_PRIORITY     (20)
#define DEV_THREAD_TICK         (10)

static SysModule_t s_modules[MAX_REG_MODULES];
static uint16_t    s_module_num = 0U;

int Dev_Registry_Add(const SysModule_t *module)
{
    if (module == RT_NULL || s_module_num >= MAX_REG_MODULES) {
        return -1;
    }
    for (uint16_t i = 0U; i < s_module_num; i++) {
        if (rt_strcmp(s_modules[i].name, module->name) == 0) {
            return -1;   /* 重复注册 */
        }
    }
    s_modules[s_module_num++] = *module;
    if (module->init != RT_NULL) {
        module->init();
    }
    return 0;
}

void Dev_Registry_InitAll(void)
{
    /* init 在 Dev_Registry_Add 时已执行；本接口保留供显式初始化阶段使用 */
}

void Dev_Registry_UpdateAll(void)
{
    uint32_t tick = rt_tick_get();
    for (uint16_t i = 0U; i < s_module_num; i++) {
        SysModule_t *m = &s_modules[i];
        if (!m->enabled || m->task == RT_NULL) {
            continue;
        }
        if (m->period_ms == 0U || (tick - m->last_tick) >= m->period_ms) {
            m->task();
            m->last_tick = tick;
        }
    }
}

void Dev_Thread_Entry(void *param)
{
    (void)param;
    while (1) {
        Dev_Registry_UpdateAll();
        rt_thread_mdelay(1);
    }
}

void Dev_RegisterAll(void)
{
    Dev_Adc_Register();
    CurrentSensor_Register();
    BusVoltage_Register();
}

int Dev_Start(void)
{
    rt_thread_t t = rt_thread_create("dev", Dev_Thread_Entry, RT_NULL,
                                     DEV_THREAD_STACK_SIZE, DEV_THREAD_PRIORITY, DEV_THREAD_TICK);
    if (t != RT_NULL) {
        rt_thread_startup(t);
        return 0;
    }
    return -1;
}

/* EOF */
