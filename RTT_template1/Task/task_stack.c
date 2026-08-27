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
static TaskStackItem_t s_task_stack[] = {
    { "main",     (uint32_t)TASK_STACK_MAIN,  UINT32_MAX, 0U },
    { "sys_sm",   (uint32_t)TASK_STACK_SYS_SM, UINT32_MAX, 0U },
    { "dev",      (uint32_t)TASK_STACK_DEV,   UINT32_MAX, 0U },
    { "rod",      (uint32_t)TASK_STACK_ROD,   UINT32_MAX, 0U },
    { "di",       (uint32_t)TASK_STACK_DI,    UINT32_MAX, 0U },
    { "led",      (uint32_t)TASK_STACK_LED,   UINT32_MAX, 0U },
    { "tshell",   (uint32_t)TASK_STACK_FINSH, UINT32_MAX, 0U },
    { "tidle0",   (uint32_t)TASK_STACK_IDLE,  UINT32_MAX, 0U },
    { "sys workq", (uint32_t)TASK_STACK_WORKQ, UINT32_MAX, 0U },
    { "timer",    (uint32_t)TASK_STACK_TIMER, UINT32_MAX, 0U },
};
#define TASK_STACK_NUM  (sizeof(s_task_stack) / sizeof(s_task_stack[0]))

static uint32_t Task_Stack_Free(const struct rt_thread *t);

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

static TaskStackItem_t *Task_Stack_Find(const char *name)
{
    uint32_t i;

    if (name == RT_NULL) {
        return RT_NULL;
    }

    for (i = 0U; i < (uint32_t)TASK_STACK_NUM; i++) {
        if (rt_strcmp(s_task_stack[i].name, name) == 0) {
            return &s_task_stack[i];
        }
    }
    return RT_NULL;
}

/* 更新当前线程在登记表中的哨兵水位；未登记的系统线程返回 RT_NULL。 */
static TaskStackItem_t *Task_Stack_UpdateWatermark(struct rt_thread *t,
                                                   uint32_t free_bytes)
{
    TaskStackItem_t *item;

    if (t == RT_NULL || t->stack_addr == RT_NULL || t->stack_size == 0U) {
        return RT_NULL;
    }

    item = Task_Stack_Find(t->name);
    if (item == RT_NULL) {
        return RT_NULL;
    }

    if (item->tracked == 0U) {
        item->min_free = free_bytes;
    } else if (item->min_free > free_bytes) {
        item->min_free = free_bytes;
    }
    item->tracked = 1U;
    return item;
}

/* RT-Thread 线程初始化时把整栈填成 '#'(0x23)，Cortex-M 栈向低地址增长。
 * 从 stack_addr 向上找到第一个非 '#' 字节，其前面的连续哨兵数就是当前剩余量；
 * 该边界对应运行至今的最大用量，比瞬时 sp 更适合做高水位判断。
 */
static uint32_t Task_Stack_Free(const struct rt_thread *t)
{
    rt_uint8_t *p;

    if ((t == RT_NULL) || (t->stack_addr == RT_NULL) ||
        (t->stack_size == 0U)) {
        return 0U;
    }

    p = (rt_uint8_t *)t->stack_addr;
    while (((uint32_t)(p - (rt_uint8_t *)t->stack_addr) < t->stack_size) &&
           (*p == '#')) {
        p++;
    }
    return (uint32_t)(p - (rt_uint8_t *)t->stack_addr);
}

static uint32_t Task_Stack_Used(uint32_t size, uint32_t free_bytes)
{
    return (size > free_bytes) ? (size - free_bytes) : 0U;
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
        uint32_t used, size, pct, free_bytes;

        t = (struct rt_thread *)rt_list_entry(node, struct rt_object, list);
        if ((t->stat & RT_THREAD_STAT_MASK) == RT_THREAD_INIT) {
            continue;   /* 未启动的线程跳过 */
        }
        size = t->stack_size;
        if (size == 0U) {
            continue;
        }
        free_bytes = Task_Stack_Free(t);
        (void)Task_Stack_UpdateWatermark(t, free_bytes);
        used = Task_Stack_Used(size, free_bytes);
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
