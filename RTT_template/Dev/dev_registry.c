/**
 * @file    dev_registry.c
 * @brief   设备注册管理：SysModule_t 注册表 + RT-Thread 设备管理线程
 * @note    参考源工程 sys_sched 的注册语义（注册即 init）；
 *          电源链（adc/cur/vm）已改为 ISR 驱动（TMR0_1 采样 + TMR0_2 1ms 检测），
 *          task 传 RT_NULL 仅保留初始化与注册打印；Dev_Start 线程留给未来线程型设备。
 */
#include "dev_registry.h"
#include "dev_config.h"
#include "rtt_manager.h"
#include "Dev/dev_adc/dev_adc.h"
#include "Dev/dev_power/dev_cur_sensor.h"
#include "Dev/dev_power/dev_bus_voltage.h"
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
    DEV_REG_PRINT("register %s", module->name);
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
#if DEV_ENABLE_ADC
    /* ISR 驱动：TMR0_1 硬件触发 200us 采样；Dev_Adc_Start() 启动 */
    static const SysModule_t s_adc_module =
        SYS_MODULE_REGISTER("adc", Dev_Adc_Init, RT_NULL, DEV_PRIO_HIGH, 0);
    Dev_Registry_Add(&s_adc_module);
#endif
#if DEV_ENABLE_CUR_SENSOR
    /* ISR 驱动：1ms 心跳 CurrentSensor_Isr1ms（经 Dev_Power_Isr1ms） */
    static const SysModule_t s_cur_module =
        SYS_MODULE_REGISTER("cur", CurrentSensor_Init, RT_NULL, DEV_PRIO_MID, 0);
    Dev_Registry_Add(&s_cur_module);
#endif
#if DEV_ENABLE_BUS_VOLTAGE
    /* ISR 驱动：1ms 心跳 BusVoltage_Isr1ms（经 Dev_Power_Isr1ms） */
    static const SysModule_t s_vm_module =
        SYS_MODULE_REGISTER("vm", BusVoltage_Init, RT_NULL, DEV_PRIO_MID, 0);
    Dev_Registry_Add(&s_vm_module);
#endif
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









