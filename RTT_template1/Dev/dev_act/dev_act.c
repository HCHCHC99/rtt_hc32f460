/**
 * @file    dev_act.c
 * @brief   电机仲裁系统（预留占位）
 * @note    g_act_dir 供调试（Watch 修改 / 后续指令写入）；真实仲裁后续实现。
 */
#include "dev_act.h"
#include <rtthread.h>

volatile RodDirection_t g_act_dir = ROD_DIR_STOP;

void Act_Arbitrator_Init(void)
{
    g_act_dir = ROD_DIR_STOP;
}

RodDirection_t Arbitrator_GetDirection(uint8_t axis_id)
{
    (void)axis_id;
    return g_act_dir;   /* 占位：真实仲裁（极性×指令×限位×故障）后续实现 */
}

/* EOF */
