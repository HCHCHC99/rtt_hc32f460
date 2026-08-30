/**
 * @file    dev_rod_state.c
 * @brief   推杆状态模块实现：表驱动状态机 + 事件合成 + 限位事件发送
 * @note    StateMachine 实例用 Axis_t.sm_act；事件由 RodState_Update 每 10ms 合成一次。
 */
#include "dev_rod_state.h"
#include "Dev/dev_mgr/dev_model.h"       /* mySystem / Act_Event_Send */
#include "Dev/dev_mgr/dev_event_def.h"
#include "Dev/dev_hall_rod/dev_hall_rod.h" /* 限位/故障稳态源（推杆霍尔设备） */
#include "applications/rtt_manager.h"
#include <rtthread.h>

/* ============ 跳转表 ============ */
static const StateJumpTable_t s_rod_jump[] = {
    /* 未知（未校准）：允许运动以寻找限位，碰到限位即校准 */
    {ROD_STATE_UNKNOWN,     ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_UNKNOWN,     ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_UNKNOWN,     ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
    {ROD_STATE_UNKNOWN,     ROD_EVT_AT_MAX,        ROD_STATE_EXT_LIMIT},
    {ROD_STATE_UNKNOWN,     ROD_EVT_AT_MIN,        ROD_STATE_RET_LIMIT},

    {ROD_STATE_STOPPED,     ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_STOPPED,     ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
    {ROD_STATE_STOPPED,     ROD_EVT_AT_MAX,        ROD_STATE_EXT_LIMIT},
    {ROD_STATE_STOPPED,     ROD_EVT_AT_MIN,        ROD_STATE_RET_LIMIT},

    {ROD_STATE_EXTENDING,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_EXTENDING,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
    {ROD_STATE_EXTENDING,   ROD_EVT_AT_MAX,        ROD_STATE_EXT_LIMIT},
    {ROD_STATE_EXTENDING,   ROD_EVT_TIMEOUT,       ROD_STATE_EXT_FAULT},
    {ROD_STATE_EXTENDING,   ROD_EVT_SENSOR_FAULT,  ROD_STATE_EXT_FAULT},

    {ROD_STATE_RETRACTING,  ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_RETRACTING,  ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_RETRACTING,  ROD_EVT_AT_MIN,        ROD_STATE_RET_LIMIT},
    {ROD_STATE_RETRACTING,  ROD_EVT_TIMEOUT,       ROD_STATE_RET_FAULT},
    {ROD_STATE_RETRACTING,  ROD_EVT_SENSOR_FAULT,  ROD_STATE_RET_FAULT},

    {ROD_STATE_EXT_LIMIT,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_EXT_LIMIT,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},

    {ROD_STATE_RET_LIMIT,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_RET_LIMIT,   ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},

    {ROD_STATE_EXT_FAULT,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_EXT_FAULT,   ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_EXT_FAULT,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},

    {ROD_STATE_RET_FAULT,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_RET_FAULT,   ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_RET_FAULT,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
};

/* 状态名（日志用，英文） */
static const char *const s_rod_state_name[] = {
    "UNKNOWN", "STOPPED", "EXTENDING", "EXT_LIMIT", "EXT_FAULT",
    "RETRACTING", "RET_LIMIT", "RET_FAULT",
};

void RodState_Init(StateMachine_t *sm, RodStateCtx_t *ctx, uint8_t axis_id, const RodPosition_t *pos)
{
    sm->jump_table      = s_rod_jump;
    sm->jump_table_size = (uint16_t)(sizeof(s_rod_jump) / sizeof(StateJumpTable_t));
    sm->func_table      = RT_NULL;   /* 侧效应在 RodState_Update 处理 */
    sm->func_table_size = 0U;
    sm->init_state      = ROD_STATE_UNKNOWN;
    StateMachine_Init(sm);

    ctx->axis_id           = axis_id;
    ctx->position          = pos;
    ctx->direction         = ROD_DIR_STOP;
    ctx->fault_code        = 0U;
    ctx->move_start_tick   = 0U;
    ctx->move_timeout_ms   = 5000U;
    ctx->limit_ext_sent    = false;
    ctx->limit_ret_sent    = false;
    ctx->extend_count      = 0U;
    ctx->retract_count     = 0U;
    ctx->fault_count       = 0U;
    ctx->limit_reach_count = 0U;
}

/* 事件合成：优先级 传感器异常 > 限位 > 超时 > 方向指令（限位/故障源 = 推杆霍尔稳态） */
static RodEvent_t RodState_SynthesizeEvent(const RodStateCtx_t *ctx, RodState_t st,
                                           RodDirection_t dir, uint32_t tick)
{
    bool at_max = RodHall_IsAtMax();
    bool at_min = RodHall_IsAtMin();

    if (RodHall_IsFault()) {
        return ROD_EVT_SENSOR_FAULT;
    }
    if (at_max && (st == ROD_STATE_UNKNOWN || st == ROD_STATE_STOPPED ||
                   st == ROD_STATE_EXTENDING || st == ROD_STATE_EXT_LIMIT)) {
        return ROD_EVT_AT_MAX;
    }
    if (at_min && (st == ROD_STATE_UNKNOWN || st == ROD_STATE_STOPPED ||
                   st == ROD_STATE_RETRACTING || st == ROD_STATE_RET_LIMIT)) {
        return ROD_EVT_AT_MIN;
    }
    if ((st == ROD_STATE_EXTENDING || st == ROD_STATE_RETRACTING) &&
        ctx->move_timeout_ms != 0U && (tick - ctx->move_start_tick) >= ctx->move_timeout_ms) {
        return ROD_EVT_TIMEOUT;
    }
    if (dir == ROD_DIR_FWD)  return ROD_EVT_CMD_EXTEND;
    if (dir == ROD_DIR_REV)  return ROD_EVT_CMD_RETRACT;
    return ROD_EVT_CMD_STOP;
}

void RodState_Update(StateMachine_t *sm, RodStateCtx_t *ctx, RodDirection_t dir, uint32_t tick)
{
    RodState_t prev, cur;
    RodEvent_t evt;

    ctx->direction = dir;
    prev = (RodState_t)StateMachine_GetState(sm);

    evt = RodState_SynthesizeEvent(ctx, prev, dir, tick);
    StateMachine_SendEvent(sm, (Event_t)evt);
    cur = (RodState_t)StateMachine_GetState(sm);

    /* ---- 状态侧效应（按跳变处理） ---- */
    if (cur == ROD_STATE_EXTENDING) {
        if (prev != ROD_STATE_EXTENDING) {
            ctx->extend_count++;
            ctx->move_start_tick = tick;
        }
        if (prev == ROD_STATE_EXT_LIMIT) {
            ctx->limit_ext_sent = false;
            Act_Event_Send(EVT_ROD_LIMIT_RELEASED);   /* 离开上限位 */
        }
    } else if (cur == ROD_STATE_RETRACTING) {
        if (prev != ROD_STATE_RETRACTING) {
            ctx->retract_count++;
            ctx->move_start_tick = tick;
        }
        if (prev == ROD_STATE_RET_LIMIT) {
            ctx->limit_ret_sent = false;
            Act_Event_Send(EVT_ROD_LIMIT_RELEASED);   /* 离开下限位 */
        }
    } else if (cur == ROD_STATE_EXT_LIMIT) {
        if (prev != ROD_STATE_EXT_LIMIT) {
            ctx->limit_reach_count++;
            if (!ctx->limit_ext_sent) {
                Act_Event_Send(EVT_ROD_LIMIT_EXTEND); /* 到上限位 -> 通知仲裁 */
                ctx->limit_ext_sent = true;
            }
        }
    } else if (cur == ROD_STATE_RET_LIMIT) {
        if (prev != ROD_STATE_RET_LIMIT) {
            ctx->limit_reach_count++;
            if (!ctx->limit_ret_sent) {
                Act_Event_Send(EVT_ROD_LIMIT_RETRACT); /* 到下限位 -> 通知仲裁 */
                ctx->limit_ret_sent = true;
            }
        }
    } else if (cur == ROD_STATE_EXT_FAULT || cur == ROD_STATE_RET_FAULT) {
        if (prev != cur) {
            ctx->fault_count++;
        }
    }

    /* 状态跳变日志 */
    if (cur != prev) {
        ROD_PRINT("rod%u state=%s", (unsigned)ctx->axis_id, s_rod_state_name[cur]);
    }
}

RodState_t RodState_Get(StateMachine_t *sm)
{
    return (RodState_t)StateMachine_GetState(sm);
}

/* EOF */


