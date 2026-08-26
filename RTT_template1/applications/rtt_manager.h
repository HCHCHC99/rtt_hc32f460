/**
 * @file    rtt_manager.h
 * @brief   RTT 打印统一开关管理（各模块打印宏）
 * @note    在"开关区"把对应 _EN 置 0 即可关闭该模块打印，无需改业务代码；
 *          所有打印宏最终落到 rtt_log.h 的 MAIN_D（英文、不打印浮点）。
 */
#ifndef __RTT_MANAGER_H__
#define __RTT_MANAGER_H__

#include "RTT/rtt_log.h"

/* ===================== 开关区（1=启用 0=关闭） ===================== */
#define SYS_STATE_PRINT_EN      1   /* 系统状态机：跳转/入口 */
#define DEV_REG_PRINT_EN        1   /* 设备注册 */
#define POWER_PRINT_EN          1   /* 电源设备：过压/欠压/过流 */
#define POLARITY_PRINT_EN       1   /* 电源极性：跳变/初始化 */
#define LED_PRINT_EN              1   /* LED：翻转打印（1s 一次，带 us 时间戳） */
#define MONITOR_PRINT_EN        1   /* 采样监视（1s 周期打印） */
#define QUEUE_INIT_PRINT_EN     0   /* 队列初始化（示例，默认关闭） */

/* ===================== 各模块打印宏封装 ===================== */
#if SYS_STATE_PRINT_EN
#define SYS_STATE_PRINT(fmt, ...)   MAIN_D("[SYS_STATE] " fmt, ##__VA_ARGS__)
#else
#define SYS_STATE_PRINT(fmt, ...)   ((void)0)
#endif

#if DEV_REG_PRINT_EN
#define DEV_REG_PRINT(fmt, ...)     MAIN_D("[DEV_REG] " fmt, ##__VA_ARGS__)
#else
#define DEV_REG_PRINT(fmt, ...)     ((void)0)
#endif

#if POWER_PRINT_EN
#define POWER_PRINT(fmt, ...)       MAIN_D("[POWER] " fmt, ##__VA_ARGS__)
#else
#define POWER_PRINT(fmt, ...)       ((void)0)
#endif

#if POLARITY_PRINT_EN
#define POLARITY_PRINT(fmt, ...)    MAIN_D("[POLARITY] " fmt, ##__VA_ARGS__)
#else
#define POLARITY_PRINT(fmt, ...)    ((void)0)
#endif

#if LED_PRINT_EN
#define LED_PRINT(fmt, ...)         MAIN_D("[LED] " fmt, ##__VA_ARGS__)
#else
#define LED_PRINT(fmt, ...)         ((void)0)
#endif

#if QUEUE_INIT_PRINT_EN
#define QUEUE_INIT_PRINT(fmt, ...)  MAIN_D("[QUEUE_INIT] " fmt, ##__VA_ARGS__)
#else
#define QUEUE_INIT_PRINT(fmt, ...)  ((void)0)
#endif

#if MONITOR_PRINT_EN
#define MONITOR_PRINT(fmt, ...)     MAIN_D("[MON] " fmt, ##__VA_ARGS__)
#else
#define MONITOR_PRINT(fmt, ...)     ((void)0)
#endif

/* 打印当前开关状态（调试用） */
void RttManager_DumpSwitches(void);

#endif /* __RTT_MANAGER_H__ */




