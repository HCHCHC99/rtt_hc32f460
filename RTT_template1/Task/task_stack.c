/**
 * @file    task_stack.c
 * @brief   任务栈统一管理实现：集中登记各线程栈大小，打印总栈与堆余量
 * @note    所有栈大小唯一来源在 task_stack.h（TASK_STACK_*）；
 *          rtconfig.h / rod_task.h / di_task.h / led_task.h / dev_model.h / dev_registry.h 均引用它。
 */
#include "task_stack.h"
#include "applications/rtt_manager.h"
#include <rtthread.h>
#include <rthw.h>   /* rt_hw_interrupt_disable / enable */

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

/* ============ 哨兵水位监控 ============ */
/* 计算某线程栈已用字节：从 stack_addr 起扫描 '#'(0x23) 哨兵，第一个非 '#' 即水位。
 * 返回 0=栈满/异常，否则为已用字节数。线程被切出时 sp 可能停在栈内，扫描以哨兵为准。
 */
static uint32_t Task_Stack_Used(const struct rt_thread *t)
{
    rt_uint8_t *p;
    uint32_t used;

    if (t == RT_NULL || t->stack_addr == RT_NULL || t->stack_size == 0U) {
        return 0U;
    }
    p = (rt_uint8_t *)t->stack_addr;
    while ((uint32_t)(p - (rt_uint8_t *)t->stack_addr) < t->stack_size) {
        if (*p != '#') {
            break;
        }
        p++;
    }
    used = (uint32_t)(p - (rt_uint8_t *)t->stack_addr);
    /* 哨兵全保留 = 基本未用；已用 = 总大小 - 剩余哨兵 */
    return (t->stack_size - used);
}

/* 周期监控：遍历所有线程，超阈值才打印（1s 调用一次即可） */
void Task_Stack_Monitor(void)
{
    struct rt_object_information *info;
    struct rt_list_node *node;
    rt_base_t level;
    uint32_t cnt = 0U;

#if (TASK_STACK_WARN_PCT == 0U)
    return;
#endif

    info = rt_object_get_information(RT_Object_Class_Thread);
    if (info == RT_NULL) {
        return;
    }

    level = rt_hw_interrupt_disable();
    for (node = info->object_list.next; node != &(info->object_list); node = node->next) {
        struct rt_thread *t;
        uint32_t used, size, pct;

        t = (struct rt_thread *)rt_list_entry(node, struct rt_object, list);
        if ((t->stat & RT_THREAD_STAT_MASK) == RT_THREAD_INIT) {
            continue;   /* 未启动的线程跳过 */
        }
        size = t->stack_size;
        used = Task_Stack_Used(t);
        if (size == 0U) {
            continue;
        }
        pct = used * 100U / size;
        if (pct >= (uint32_t)TASK_STACK_WARN_PCT) {
            TASK_STACK_PRINT("WARN %s sp=0x%08x base=0x%08x size=%u used=%u(%u%%)",
                             t->name,
                             (unsigned)(rt_ubase_t)t->sp,
                             (unsigned)(rt_ubase_t)t->stack_addr,
                             (unsigned)size, (unsigned)used, (unsigned)pct);
            cnt++;
        }
    }
    rt_hw_interrupt_enable(level);

    if (cnt != 0U) {
        TASK_STACK_PRINT("warn thread count=%u", (unsigned)cnt);
    }
}

/* EOF */
