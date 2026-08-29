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
#include "Dev/dev_registry.h"
#include "Adp/hc_drv_timer.h"
#include "Dev/dev_adc/dev_adc.h"
#include "Dev/dev_power/dev_power_isr.h"
#include "Dev/dev_act/dev_act.h"
#include "Dev/dev_power/dev_polarity.h"
#include <rtthread.h>

static volatile uint32_t s_arb_cmd_send_fail_count = 0U;

/* 状态入口函数声明 */
static void sys_enter_init(void);
static void sys_enter_idle(void);
static void sys_enter_run(void);
static void sys_enter_stop(void);
static void sys_enter_fault(void);
static void sys_enter_emergency(void);
static void Sys_State_ArbSendCommand(uint8_t axis_id,
                                     uint8_t device_id,
                                     uint8_t priority,
                                     uint8_t cmd_type,
                                     uint8_t duty_pct,
                                     rt_bool_t urgent);
static void Sys_State_SetArbEnable(uint8_t axis_id, rt_bool_t enable);
static void Sys_State_ResyncActFromFault(void);
static const StateJumpTable_t sys_jump[] = {
    {SYS_STATE_INIT,        EVT_SYS_INIT_DONE,             SYS_STATE_IDLE},

    {SYS_STATE_IDLE,        EVT_SYS_CMD_WORK_ENABLE,       SYS_STATE_RUN},
    {SYS_STATE_IDLE,        EVT_SYS_FAULT,                 SYS_STATE_FAULT},
    {SYS_STATE_IDLE,        EVT_SYS_EMERGENCY,             SYS_STATE_EMERGENCY},
    {SYS_STATE_IDLE,        EVT_SYS_ST_WORK_ERROR,         SYS_STATE_STOP},
    {SYS_STATE_IDLE,        EVT_SYS_VOLT_OVER,             SYS_STATE_EMERGENCY},
    {SYS_STATE_IDLE,        EVT_SYS_VOLT_UNDER,            SYS_STATE_EMERGENCY},
    {SYS_STATE_IDLE,        EVT_SYS_OVER_CURRENT,          SYS_STATE_EMERGENCY},
    {SYS_STATE_IDLE,        EVT_SYS_ROD_LIMIT_FAULT,       SYS_STATE_EMERGENCY},

    {SYS_STATE_RUN,         EVT_SYS_FAULT,                 SYS_STATE_FAULT},
    {SYS_STATE_RUN,         EVT_SYS_EMERGENCY,             SYS_STATE_EMERGENCY},
    {SYS_STATE_RUN,         EVT_SYS_ST_WORK_ERROR,         SYS_STATE_STOP},
    {SYS_STATE_RUN,         EVT_SYS_VOLT_OVER,             SYS_STATE_EMERGENCY},
    {SYS_STATE_RUN,         EVT_SYS_VOLT_UNDER,            SYS_STATE_EMERGENCY},
    {SYS_STATE_RUN,         EVT_SYS_OVER_CURRENT,          SYS_STATE_EMERGENCY},
    {SYS_STATE_RUN,         EVT_SYS_ROD_LIMIT_FAULT,       SYS_STATE_EMERGENCY},

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
        if (mySystem.fault_bits == 0U) { mySystem.prev_state = (State_t)StateMachine_GetState(&mySystem.sys_sm); }
        mySystem.fault_bits |= (1U << 0);   /* 过流故障置位 */
        POWER_PRINT("over curr");
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_OVER_CURRENT);
    }
    if (bits & EVT_SYS_ROD_LIMIT_FAULT) {
        mySystem.error_code = SYS_ERR_ROD_LIMIT;
        mySystem.fault_bits |= (1U << 3);   /* 推杆上下霍尔故障置位 */
        ROD_PRINT("rod limit fault");
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_ROD_LIMIT_FAULT);
    }
    if (bits & EVT_SYS_VOLT_OVER) {
        mySystem.error_code = SYS_ERR_VOLT_OVER;
        if (mySystem.fault_bits == 0U) { mySystem.prev_state = (State_t)StateMachine_GetState(&mySystem.sys_sm); }
        mySystem.fault_bits |= (1U << 1);   /* 过压故障置位 */
        POWER_PRINT("over volt");
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_VOLT_OVER);
    }
    if (bits & EVT_SYS_VOLT_UNDER) {
        mySystem.error_code = SYS_ERR_VOLT_UNDER;
        if (mySystem.fault_bits == 0U) { mySystem.prev_state = (State_t)StateMachine_GetState(&mySystem.sys_sm); }
        mySystem.fault_bits |= (1U << 2);   /* 欠压故障置位 */
        POWER_PRINT("under volt");
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_VOLT_UNDER);
    }
    if (bits & EVT_SYS_FAULT)         StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_FAULT);
    if (bits & EVT_SYS_EMERGENCY)     StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_EMERGENCY);
    if (bits & EVT_SYS_RECOVERY) {
        mySystem.error_code = SYS_ERR_NONE;
        mySystem.fault_bits = 0U;   /* 手动恢复清全部故障位 */
        StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_RECOVERY);
    }
    /* 电压恢复正常：清电压故障位；全部故障清除 -> 自动恢复（FAULT/EMERGENCY -> IDLE） */
    if (bits & EVT_SYS_VOLT_NORMAL) {
        mySystem.fault_bits &= ~((1U << 1) | (1U << 2));   /* 清过压/欠压位 */
        if (mySystem.fault_bits == 0U) {
            mySystem.error_code = SYS_ERR_NONE;
            POWER_PRINT("volt normal, auto recover");
            StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_RECOVERY);
        if (mySystem.prev_state == (State_t)SYS_STATE_RUN) {
            StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_CMD_WORK_ENABLE);   /* 回故障前状态：RUN */
        }
        }
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
    /* 手动恢复：清码/故障位 + 回故障前状态（RUN->RUN，否则->IDLE） */
    mySystem.error_code = SYS_ERR_NONE;
    mySystem.fault_bits = 0U;
    Sys_Event_Send(EVT_SYS_RECOVERY);
    if (mySystem.prev_state == (State_t)SYS_STATE_RUN) {
        Sys_Event_Send(EVT_SYS_CMD_WORK_ENABLE);
    }
}

/* 状态跳转打印：更新并输出全局 us 时间戳 */
static void sys_state_log_enter(const char *name)
{
    UsTimer_UpdateTimestamp();
    MAIN_D_SYNC("[SYS_STATE] enter %s t=%uus", name, (unsigned)UsTimer_GetTimestampUs());
}

static void Sys_State_ArbSendCommand(uint8_t axis_id,
                                     uint8_t device_id,
                                     uint8_t priority,
                                     uint8_t cmd_type,
                                     uint8_t duty_pct,
                                     rt_bool_t urgent)
{
    rt_err_t ret = Arb_SendCommand(axis_id, device_id, priority,
                                   cmd_type, duty_pct, urgent);
    if (ret != RT_EOK) {
        s_arb_cmd_send_fail_count++;
        ARB_PRINT("send fail axis=%u dev=%u cmd=%u err=%d count=%u",
                  (unsigned)axis_id, (unsigned)device_id,
                  (unsigned)cmd_type, (int)ret,
                  (unsigned)s_arb_cmd_send_fail_count);
    }
}

static void Sys_State_SetArbEnable(uint8_t axis_id, rt_bool_t enable)
{
    rt_err_t ret = Arb_SetEnable(axis_id, enable);
    if (ret != RT_EOK) {
        ARB_PRINT("enable fail axis=%u enable=%u err=%d",
                  (unsigned)axis_id, (unsigned)(enable != RT_FALSE), (int)ret);
    }
}

/* FAULT->RUN skips IDLE/InitAll, so arbitration must be re-enabled explicitly. */
static void Sys_State_ResyncActFromFault(void)
{
    int i;
    PolarityState_t polarity = Polarity_GetState();

    SYS_STATE_PRINT("act resync from fault");
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        Sys_State_SetArbEnable((uint8_t)i, RT_TRUE);
    }

    if (polarity == POLARITY_FWD) {
        Sys_State_ArbSendCommand(POLARITY_ARB_AXIS_ID,
                                 (uint8_t)DEV_ID_POWER_POS,
                                 (uint8_t)PRIO_POWER,
                                 (uint8_t)CMD_TYPE_RUN_FWD,
                                 POLARITY_ARB_RUN_DUTY_PCT,
                                 RT_FALSE);
    }
    else if (polarity == POLARITY_REV) {
        Sys_State_ArbSendCommand(POLARITY_ARB_AXIS_ID,
                                 (uint8_t)DEV_ID_POWER_NEG,
                                 (uint8_t)PRIO_POWER,
                                 (uint8_t)CMD_TYPE_RUN_REV,
                                 POLARITY_ARB_RUN_DUTY_PCT,
                                 RT_FALSE);
    }
    else {
        Sys_State_ArbSendCommand(POLARITY_ARB_AXIS_ID,
                                 (uint8_t)DEV_ID_POWER_POS,
                                 (uint8_t)PRIO_POWER,
                                 (uint8_t)CMD_TYPE_CLEAR_ALLOW_FWD,
                                 0U,
                                 RT_FALSE);
        Sys_State_ArbSendCommand(POLARITY_ARB_AXIS_ID,
                                 (uint8_t)DEV_ID_POWER_POS,
                                 (uint8_t)PRIO_POWER,
                                 (uint8_t)CMD_TYPE_CLEAR_ALLOW_REV,
                                 0U,
                                 RT_FALSE);
    }
}

/* ---- 状态入口函数 ---- */
static void sys_enter_init(void)
{
    sys_state_log_enter("INIT");
}
static void sys_enter_idle(void)
{
    static uint8_t s_hw_started = 0U;
    sys_state_log_enter("IDLE");

    /* 业务初始化/复位：每次进 IDLE 统一执行（上电一次初始化；故障恢复/回 IDLE 时清业务状态） */
    Dev_Registry_InitAll();

    /* 硬件触发只启动一次（首次进 IDLE；后续回 IDLE 不重复启动硬件） */
    if (s_hw_started == 0U) {
        HcDrv_Timer_Start1ms(Dev_Power_Isr1ms);   /* TMR0_2：1ms 检测心跳 */
        Dev_Adc_Start();                            /* TMR0_1：启动 500us 采样 */
        s_hw_started = 1U;
    }
}
static void sys_enter_run(void)
{
    int i;
    sys_state_log_enter("RUN");

    if (mySystem.sys_sm.previous_state == (State_t)SYS_STATE_FAULT) {
        Sys_State_ResyncActFromFault();
    }

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
    int i;

    /* 故障码为整型，可直接打印 */
    UsTimer_UpdateTimestamp();
    SYS_STATE_PRINT("enter FAULT code=%u t=%uus", (unsigned)mySystem.error_code, (unsigned)UsTimer_GetTimestampUs());
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        Sys_State_SetArbEnable((uint8_t)i, RT_FALSE);
        /* 直入 FAULT（不经 EMERGENCY）时也必须停硬件输出：清允许并触发决策 */
        Sys_State_ArbSendCommand((uint8_t)i,
                                 (uint8_t)DEV_ID_EMERGENCY,
                                 (uint8_t)PRIO_EMERGENCY,
                                 (uint8_t)CMD_TYPE_EMERGENCY_STOP,
                                 0U,
                                 RT_TRUE);
    }
}
static void sys_enter_emergency(void)
{
    int i;
    UsTimer_UpdateTimestamp();
    SYS_STATE_PRINT("enter EMERGENCY code=%u t=%uus", (unsigned)mySystem.error_code, (unsigned)UsTimer_GetTimestampUs());
    /* 急停动作：全轴制动（多轴预留） */
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        (void)rt_event_send(mySystem.axis[i].evt_act, EVT_ACT_HOLD);
        Sys_State_ArbSendCommand((uint8_t)i,
                                 (uint8_t)DEV_ID_EMERGENCY,
                                 (uint8_t)PRIO_EMERGENCY,
                                 (uint8_t)CMD_TYPE_EMERGENCY_STOP,
                                 0U,
                                 RT_TRUE);
    }
    /* 急停后跳入 FAULT（稳定故障态） */
    StateMachine_SendEvent(&mySystem.sys_sm, EVT_SYS_FAULT);
}

/* EOF */









