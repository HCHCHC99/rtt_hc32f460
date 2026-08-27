/**
 * @file    task_stack.h
 * @brief   任务栈统一管理：所有线程栈大小的唯一权威来源（Single Source of Truth）
 * @note    栈大小数值只在本文件改；各 task 头文件与 rtconfig.h 均用宏引用（宏引用宏）。
 *          打印走 TASK_STACK_PRINT（MAIN_D，英文整型，开关在 rtt_manager.h）。
 */
#ifndef __TASK_STACK_H__
#define __TASK_STACK_H__

#include <stdint.h>

/* ===================== 线程栈大小统一管理（唯一权威来源） ===================== */
#define TASK_STACK_MAIN     (4096U)   /* main 主线程：初始化 + 1s 采样打印 + MSH */
#define TASK_STACK_SYS_SM   (2048U)   /* sys_sm 系统状态机事件线程 */
#define TASK_STACK_DEV      (2048U)   /* dev 设备管理线程 */
#define TASK_STACK_ROD      (2048U)   /* rod 推杆位置/状态（含 float+RTT 打印+中断嵌套；1024 曾溢出） */
#define TASK_STACK_DI       (1024U)   /* di DI 采集（2ms 极性扫描） */
#define TASK_STACK_LED      (2048U)   /* led LED 1s 翻转 */
#define TASK_STACK_FINSH    (4096U)   /* finsh MSH shell */
#define TASK_STACK_IDLE     (256U)    /* idle 空闲线程 */
#define TASK_STACK_WORKQ    (2048U)   /* workq 系统工作队列 */
#define TASK_STACK_TIMER    (512U)    /* timer 软定时器线程（RT-Thread 默认 512B） */

/* 任务栈登记项 */
typedef struct {
    const char *name;      /* 线程名（与 rt_thread_create 的 name 一致） */
    uint32_t    stack;     /* 栈大小（字节） */
    uint32_t    min_free;  /* 运行期哨兵扫描到的最低剩余字节 */
    uint8_t     tracked;   /* 是否已在运行线程列表中匹配到该线程 */
} TaskStackItem_t;

/* 打印所有任务栈大小 + 总栈 + 堆余量（MAIN_D，英文整型） */
void Task_Stack_Dump(void);

/* 哨兵水位告警阈值：线程栈已用 >= 该百分比时打印（%），0=关闭 */
#define TASK_STACK_WARN_PCT   (75U)

/*
 * 周期监控所有线程栈：扫描 '#' 哨兵水位，超过 TASK_STACK_WARN_PCT 才打印
 * （打印 name / sp / stack_addr / stack_size / used%%），避免刷屏。
 * 建议在 1s 周期任务里调用；内部关中断遍历，可在线程上下文调用。
 */
void Task_Stack_Monitor(void);

#endif /* __TASK_STACK_H__ */
