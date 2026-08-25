/**
 * @file    dev_event_def.h
 * @brief   系统事件位定义（表驱动状态机移植）
 * @note    原样裁剪自控制器工程 dev_event_def.h，只保留系统状态机当前用到的事件位；
 *          轴事件为多轴预留（系统状态机 enter 回调向轴下发使能/保持）。
 */
#ifndef __DEV_EVENT_DEF_H__
#define __DEV_EVENT_DEF_H__

#include <stdint.h>

/* ===================== 系统事件组 ===================== */
#define EVT_SYS_INIT_DONE              (1U << 0)    /* 系统初始化完成 */
#define EVT_SYS_FAULT                  (1U << 1)    /* 系统故障 */
#define EVT_SYS_EMERGENCY              (1U << 2)    /* 系统急停 */
#define EVT_SYS_RECOVERY               (1U << 3)    /* 故障恢复 */
#define EVT_SYS_VOLT_NORMAL            (1U << 4)    /* 电压正常 */
#define EVT_SYS_VOLT_OVER              (1U << 5)    /* 过压 */
#define EVT_SYS_VOLT_UNDER             (1U << 6)    /* 欠压 */
#define EVT_SYS_CMD_WORK_ENABLE        (1U << 7)    /* 系统工作：使能 */
#define EVT_SYS_OVER_CURRENT          (1U << 8)    /* 过流 */
#define EVT_SYS_ST_WORK_ERROR          (1U << 13)   /* 系统工作：工作错误 */

/* ===================== 轴事件组（多轴预留） ===================== */
#define EVT_ACT_WORK_ENABLE            (1U << 0)    /* 推杆工作使能 */
#define EVT_ACT_WORK_DISABLE           (1U << 1)    /* 推杆工作禁止 */
#define EVT_ACT_HOLD                   (1U << 14)   /* 推杆保持/制动 */

#endif /* __DEV_EVENT_DEF_H__ */



