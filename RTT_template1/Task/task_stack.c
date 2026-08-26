/**
 * @file    task_stack.c
 * @brief   任务栈统一管理实现：集中登记各线程栈大小，打印总栈与堆余量
 * @note    所有栈大小唯一来源在 task_stack.h（TASK_STACK_*）；
 *          rtconfig.h / rod_task.h / di_task.h / led_task.h / dev_model.h / dev_registry.h 均引用它。
 */
#include "task_stack.h"
#include "applications/rtt_manager.h"
#include <rtthread.h>

/* ============ 任务栈登记表（新增线程在这里加一行） ============ */
static const TaskStackItem_t s_task_stack[] = {
    { "main",   (uint32_t)TASK_STACK_MAIN },
    { "sys_sm", (uint32_t)TASK_STACK_SYS_SM },
    { "dev",    (uint32_t)TASK_STACK_DEV },
    { "rod",    (uint32_t)TASK_STACK_ROD },
    { "di",     (uint32_t)TASK_STACK_DI },
    { "led",    (uint32_t)TASK_STACK_LED },
    { "finsh",  (uint32_t)TASK_STACK_FINSH },
    { "idle",   (uint32_t)TASK_STACK_IDLE },
    { "workq",  (uint32_t)TASK_STACK_WORKQ },
};
#define TASK_STACK_NUM  (sizeof(s_task_stack) / sizeof(s_task_stack[0]))

/* 打印所有任务栈大小 + 总栈 + 堆余量 */
void Task_Stack_Dump(void)
{
    uint32_t i;
    uint32_t total = 0U;
    rt_size_t h_total = 0U, h_used = 0U, h_max = 0U;

    TASK_STACK_PRINT("==== task stack list ====");
    for (i = 0U; i < (uint32_t)TASK_STACK_NUM; i++) {
        TASK_STACK_PRINT("%-8s = %u", s_task_stack[i].name, (unsigned)s_task_stack[i].stack);
        total += s_task_stack[i].stack;
    }
    TASK_STACK_PRINT("total stack = %u", (unsigned)total);

    /* 堆余量：线程栈来自堆，运行时最准 */
    rt_memory_info(&h_total, &h_used, &h_max);
    TASK_STACK_PRINT("heap total=%u used=%u max=%u free=%u",
                     (unsigned)h_total, (unsigned)h_used, (unsigned)h_max,
                     (unsigned)(h_total - h_used));
    TASK_STACK_PRINT("=========================");
}

/* EOF */
