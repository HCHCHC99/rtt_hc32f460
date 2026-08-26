/**
 * @file    di_task.c
 * @brief   DI 采集任务实现（2ms 周期）
 * @note    执行模型：B→C（独立线程）；只调设备 Scan，不含业务判定/事件逻辑；
 *          电源极性消抖窗口 5 点 x 2ms 采样 = 10ms。
 */
#include "di_task.h"
#include "Dev/dev_power/dev_polarity.h"
#include <rtthread.h>

#define DI_SCAN_PERIOD_MS    (2U)     /* 2ms 采样周期：5 点窗口 => 10ms 消抖 */
#define DI_THREAD_STACK      (1024U)
#define DI_THREAD_PRIO       (22)     /* 中优先级，早于低优先级的 LED/监控 */
#define DI_THREAD_TICK       (10)

static void di_thread_entry(void *param)
{
    (void)param;
    while (1) {
        Polarity_Scan();              /* 设备扫描：内部读 GPIO + 窗口判定 + 跳变发事件 */
        /* 未来: Limit_Scan(); */
        rt_thread_mdelay(DI_SCAN_PERIOD_MS);
    }
}

void Di_Task_Start(void)
{
    rt_thread_t t = rt_thread_create("di", di_thread_entry, RT_NULL,
                                     DI_THREAD_STACK, DI_THREAD_PRIO, DI_THREAD_TICK);
    if (t != RT_NULL) {
        rt_thread_startup(t);
    }
}

/* EOF */

