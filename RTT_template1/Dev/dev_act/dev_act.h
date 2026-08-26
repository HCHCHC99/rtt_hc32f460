/**
 * @file    dev_act.h
 * @brief   电机仲裁系统（预留占位）：多源输入整合，决策推杆运动方向
 * @note    当前为占位：Arbitrator_GetDirection 返回测试方向 g_act_dir；
 *          后续接入 极性/限位事件/过流过压/指令 后实现真实仲裁。
 */
#ifndef __DEV_ACT_H__
#define __DEV_ACT_H__

#include "Dev/dev_rod/dev_rod_state.h"   /* RodDirection_t */

/* 测试方向（Watch 可直接改：0=停止 1=伸出 -1=缩回） */
extern volatile RodDirection_t g_act_dir;

/* 仲裁系统初始化（预留） */
void Act_Arbitrator_Init(void);

/* 获取轴 i 的仲裁后方向（占位：返回 g_act_dir） */
RodDirection_t Arbitrator_GetDirection(uint8_t axis_id);

#endif /* __DEV_ACT_H__ */
