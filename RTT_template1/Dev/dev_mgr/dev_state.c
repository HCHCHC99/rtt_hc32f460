/**
 * @file    dev_state.c
 * @brief   系统状态机实现（表驱动移植 + 故障联动）
 * @note    跳转表/函数表原样移植自控制器工程 sys_state.c；
 *          轮询分发改为 rt_event 事件驱动（Sys_State_Dispatch + 线程）；
 *          过压/欠压/过流 → EMERGENCY（急停动作）→ 末尾跳入 FAULT（稳定故障态）；
 *          日志全部 MAIN_D，英文，不打印浮点。
 */
#include "dev_state.h"
#include "dev_event_def.h"
#include "dev_model.h"
#include "rtt_manager.h"
#include "Utils/us_timer.h"
#include <rtthread.h>

/* 状态入口函数声明 */
static void sys_enter_init(void);
static void sys_enter_idle(void);
static void sys_enter_run(void);
static void sys_enter_stop(void);
static void sys_enter_fault(void);
static void sys_enter_emergency(void);

/* 系统状态跳转表 */
static const StateJumpTable_t sys_jump[] = {
    {SYS_STATE_INIT,        EVT_SYS_INIT_DONE,             SYS_STATE_IDLE},

    {SYS_STATE_IDLE,        EVT_SYS_CMD_WORK_ENABLE,       SYS_STATE_RUN},
    {SYS_STATE_IDLE,        EVT_SYS_FAULT,                 SYS_STATE_FAULT},
    {SYS_STATE_IDLE,        EVT_SYS_EMERGENCY,             SYS_STATE_EMERGENCY},
    {SYS_STATE_IDLE,        EVT_SYS_ST_WORK_ERROR,         SYS_STATE_STOP},
    {SYS_STATE_IDLE,        EVT_SYS_VOLT_OVER,             SYS_STATE_EMERGENCY},
    {SYS_STATE_IDLE,        EVT_SYS_VOLT_UNDER,            SYS_STATE_EMERGENCY},
    {SYS_STATE_IDLE,        EVT_SYS_OVER_CURRENT,          SYS_STATE_EMERGENCY},

    {SYS_STATE_RUN,         EVT_SYS_FAULT,                 SYS_STATE_FAULT},
    {SYS_STATE_RUN,         EVT_SYS_EMERGENCY,             SYS_STATE_EMERGENCY},
    {SYS_STATE_RUN,         EVT_SYS_ST_WORK_ERROR,         SYS_STATE_STOP},
    {SYS_STATE_RUN,         EVT_SYS_VOLT_OVER,             SYS_STATE_EMERGENCY},
    {SYS_STATE_RUN,         EVT_SYS_VOLT_UNDER,            SYS_STATE_EMERGENCY},
    {SYS_STATE_RUN,         EVT_SYS_OVER_CURRENT,          SYS_STATE_EMERGENCY},

    {SYS_STATE_STOP,        EVT_SYS_CMD_WORK_ENABLE,       SYS_STATE_RUN},
    {SYS_STATE_STOP,        EVT_SYS_RECOVERY,              SYS_STATE_IDLE},

    {SYS_STATE_FAULT,       EVT_SYS_RECOVERY,              SYS_STATE_IDLE},

    /* 急停后由 enter_emergency 末尾跳入 FAULT（稳定故障态） */
    {SYS_STATE_EMERGENCY,   EVT_SYS_FAULT,                 SYS_STATE_FAULT},
    {SYS_STATE_EMERGENCY,   EVT_SYS_RECOVERY,              SYS_STATE_IDLE},
};

/* 系统状态入口函数表 */
static const StateFuncTable_t sys_func[] = {
    {SYS_STATE_INIT,      sys_enter_init,      0},
    {SYS_STATE_IDLE,      sys_enter_idle,      0},
    {SYS_STATE_RUN,       sys_enter_run,       0},
    {SYS_STATE_STOP,      sys_enter_stop,      0},
    {SYS_STATE_FAULT,     sys_enter_fault,     0},
    {SYS_STATE_EMERGENCY, sys_enter_emergency, 0},
};

void Sys_State_Init(StateMachine_t *sm)
{
    sm->jump_table      = sys_jump;
    sm->jump_table_size = (uint16_t)(sizeof(sys_jump) / sizeof(StateJumpTable_t));
    sm->func_table      = sys_func;
    sm->func_table_size = (uint16_t)(sizeof(sys_func) / sizeof(StateFuncTable_t));
    sm->init_state      = SYS_STATE_INIT;
    StateMachine_Init(sm);
}

void Sys_State_Dispatch(rt_uint32_t bits)
{
    /* 故障事件：记录故障码后再投递（过压/欠压/过流先进 EMERGENCY，enter 末尾跳 FAULT）；
       检测在 1ms ISR 完成，打印挪到本线程上下文（ISR 不打印） */
    if (bits & EVT_SYS_OVER_CURRENT) {
        mySystem.error_code = SYS_ERR_OVER_CURRENT;
        POWER_PRINT("over curr");
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_OVER_CURRENT);
    }
    if (bits & EVT_SYS_VOLT_OVER) {
        mySystem.error_code = SYS_ERR_VOLT_OVER;
        POWER_PRINT("over volt");
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_VOLT_OVER);
    }
    if (bits & EVT_SYS_VOLT_UNDER) {
        mySystem.error_code = SYS_ERR_VOLT_UNDER;
        POWER_PRINT("under volt");
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_VOLT_UNDER);
    }
    if (bits & EVT_SYS_FAULT)         StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_FAULT);
    if (bits & EVT_SYS_EMERGENCY)     StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_EMERGENCY);
    if (bits & EVT_SYS_RECOVERY) {
        mySystem.error_code = SYS_ERR_NONE;
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_RECOVERY);
    }
    if (bits & EVT_SYS_INIT_DONE)     StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_INIT_DONE);
    if (bits & EVT_SYS_CMD_WORK_ENABLE) StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_CMD_WORK_ENABLE);
    if (bits & EVT_SYS_ST_WORK_ERROR) StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_ST_WORK_ERROR);
}

void Sys_Event_Send(rt_uint32_t bits)
{
    if (mySystem.sys_evt != RT_NULL) {
        (void)rt_event_send(mySystem.sys_evt, bits);
    }
}

void Sys_State_Recover(void)
{
    /* 手动恢复：清故障码 + 回 IDLE；如需回 RUN 再发 EVT_SYS_CMD_WORK_ENABLE */
    mySystem.error_code = SYS_ERR_NONE;
    Sys_Event_Send(EVT_SYS_RECOVERY);
}

/* 状态跳转打印：更新并输出全局 us 时间戳 */
static void sys_state_log_enter(const char *name)
{
    UsTimer_UpdateTimestamp();
    SYS_STATE_PRINT("enter %s t=%uus", name, (unsigned)UsTimer_GetTimestampUs());
}

/* ---- 状态入口函数 ---- */
static void sys_enter_init(void)
{
    sys_state_log_enter("INIT");
}
static void sys_enter_idle(void)
{
    sys_state_log_enter("IDLE");
}
static void sys_enter_run(void)
{
    int i;
    sys_state_log_enter("RUN");
    /* 多轴预留：仅使能已配置(ACT_DIR_NONE != dir)的轴 */
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        if (ACT_DIR_NONE != mySystem.axis[i].dir) {
            (void)rt_event_send(mySystem.axis[i].evt_act, EVT_ACT_WORK_ENABLE);
        }
    }
}
static void sys_enter_stop(void)
{
    int i;
    sys_state_log_enter("STOP");
    /* 修正原工程硬编码 axis[0]/axis[1] 的不对称：统一循环 */
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        (void)rt_event_send(mySystem.axis[i].evt_act, EVT_ACT_WORK_DISABLE);
    }
}
static void sys_enter_fault(void)
{
    /* 故障码为整型，可直接打印 */
    UsTimer_UpdateTimestamp();
    SYS_STATE_PRINT("enter FAULT code=%u t=%uus", (unsigned)mySystem.error_code, (unsigned)UsTimer_GetTimestampUs());
}
static void sys_enter_emergency(void)
{
    int i;
    UsTimer_UpdateTimestamp();
    SYS_STATE_PRINT("enter EMERGENCY code=%u t=%uus", (unsigned)mySystem.error_code, (unsigned)UsTimer_GetTimestampUs());
    /* 急停动作：全轴制动（多轴预留） */
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        (void)rt_event_send(mySystem.axis[i].evt_act, EVT_ACT_HOLD);
    }
    /* 急停后跳入 FAULT（稳定故障态） */
    StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_FAULT);
}

/* EOF */








