/**
 * @file    dev_polarity.h
 * @brief   电源极性设备：GPIO 双窗口消抖 + 状态判定 + 跳变发事件（1ms ISR）
 * @note    POWER_DIR_P=PB13 / POWER_DIR_N=PB12；各 5 点窗口（Task/di_task 2ms 采样 => 10ms 消抖）；
 *          仅在稳定状态跳变沿发轴事件（EVT_ACT_POLARITY_FWD/REV/POWER_ABNORMAL/POWER_LOST）。
 */
#ifndef __DEV_POLARITY_H__
#define __DEV_POLARITY_H__

#include <stdint.h>

/* 引脚配置（统一放头文件） */
#define POWER_DIR_P_PIN     GET_PIN(C, 14)   /* 正极线检测 */
#define POWER_DIR_N_PIN     GET_PIN(C, 15)   /* 负极线检测 */

/* 默认配置宏（统一放头文件） */
#define POLARITY_WIN_SIZE   (5U)             /* 双窗口各 5 点：di_task 2ms 采样 => 10ms 消抖 */

/* 仲裁命令映射（当前唯一实体电机轴；axis 1 预留） */
#define POLARITY_ARB_AXIS_ID        (0U)
#define POLARITY_ARB_RUN_DUTY_PCT   (85U)   /* 后续按电机负载和温升调整 */

/* 电源极性状态 */
typedef enum {
    POLARITY_UNKNOWN = 0,   /* 窗口未满 / 不稳定：保持上次稳定态，不发事件 */
    POLARITY_UNPOWERED,     /* 掉电：P=0 N=0 */
    POLARITY_FWD,           /* 正向：P=1 N=0 */
    POLARITY_REV,           /* 反向：P=0 N=1 */
    POLARITY_ABNORMAL,      /* 异常：P=1 N=1 */
} PolarityState_t;

/* ===================== 模拟模式 ===================== */
/* 1=模拟：Polarity_Scan 不读 GPIO/不做窗口消抖，改用 g_pol_sim_state 直接判定
   （表达式窗口实时可改：0=UNKNOWN 保持上次 1=UNPOWERED 2=FWD 3=REV 4=ABNORMAL） */
#define POLARITY_SIM_MODE_EN    (0)
extern volatile PolarityState_t g_pol_sim_state;   /* 模拟极性状态（含义见 PolarityState_t；UNKNOWN=保持不发事件） */

void Polarity_Init(void);              /* 注册表 init：复位窗口与状态 */
void Polarity_Scan(void);              /* 扫描（di_task 10ms 调）：读 GPIO + 推窗 + 判定 + 跳变发轴事件 */
PolarityState_t Polarity_GetState(void); /* 查询上次稳定状态（电机控制/监控用） */
void Polarity_PrintPending(void);    /* 线程上下文：打印未处理的极性跳变（调试，走 POLARITY_PRINT） */

#endif /* __DEV_POLARITY_H__ */














