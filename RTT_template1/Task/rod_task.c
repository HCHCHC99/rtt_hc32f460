/**
 * @file    rod_task.c
 * @brief   推杆 10ms 纯编排：电机霍尔 Task → 位置 → 限位校准 → 状态机
 * @note    本文件不摸 GPIO、不判故障；硬件细节在 dev_hall_motor / dev_hall_rod。
 *          限位源二选一（dev_config.h DEV_ENABLE_HALL_ROD）：
 *          1=旧板推杆霍尔稳态；0=新板软限位（sys_sm 过流判定写 calib_req，
 *            本任务执行位置重置，状态机消费请求进限位态并通知仲裁）。
 */
#include "rod_task.h"
#include "Dev/dev_mgr/dev_model.h"
#include "Dev/dev_rod/dev_rod_position.h"
#include "Dev/dev_rod/dev_rod_state.h"
#include "Dev/dev_config.h"
#if DEV_ENABLE_HALL_ROD
#include "Dev/dev_hall_rod/dev_hall_rod.h"
#endif
#include "Dev/dev_hall_motor/dev_hall_motor.h"
#include "Dev/dev_act/dev_act.h"
#if DEV_ENABLE_PARAM
#include "Dev/dev_param/dev_param.h"
#endif
#include <rtthread.h>

static void Actuator_Tick(uint32_t tick)
{
    uint8_t i;

    /* 1. 电机霍尔：测速/堵转观测/霍尔状态/增量累积（对应参考 motor_hall_update） */
    MotorHall_Task();

#if DEV_ENABLE_HALL_ROD
    /* 2. 推杆霍尔：消抖 + 双高故障沿检测（故障沿内部发 EVT_SYS_ROD_LIMIT_FAULT） */
    RodHall_Scan();
#endif

    for (i = 0U; i < MAX_AXIS_NUM; i++) {
        Axis_t *axis = &mySystem.axis[i];
        int32_t delta;
        ArbData_t arb;
        RodDirection_t dir = ROD_DIR_STOP;

        if (axis->dir == ACT_DIR_NONE) {
            continue;   /* 未配置轴跳过 */
        }

        /* 3. 位置积分（带符号增量，读清） */
        delta = MotorHall_GetDeltaPulses(i);
        RodPosition_Update(&axis->position, delta);

        /* 4. 限位注入位置模块（自动校准） */
#if DEV_ENABLE_HALL_ROD
        /* 旧板：推杆霍尔稳态作为限位源 */
        RodPosition_OnMinLimit(&axis->position, RodHall_IsAtMin());
        RodPosition_OnMaxLimit(&axis->position, RodHall_IsAtMax());
#else
        /* 新板软限位：sys_sm 过流判定写入的校准请求 -> 执行位置重置；
           请求本身由 RodState_Update 消费（进限位态并通知仲裁），此处仅重置位置。
           OnXxxLimit 内部按 calib_allowed 门控（sys_sm 下发请求前已判窗口/未校准） */
        if (axis->state.calib_req == ROD_CALIB_REQ_MIN) {
            (void)RodPosition_OnMinLimit(&axis->position, true);
        } else if (axis->state.calib_req == ROD_CALIB_REQ_MAX) {
            (void)RodPosition_OnMaxLimit(&axis->position, true);
        }
#endif

        /* 5. 方向指令（仲裁；禁用/读取失败一律视为停止）+ 状态更新 */
        if ((Arb_GetData(i, &arb) == RT_EOK) && (arb.enable != 0U)) {
            if (arb.active_dir == DIR_FWD) {
                dir = ROD_DIR_FWD;
            } else if (arb.active_dir == DIR_REV) {
                dir = ROD_DIR_REV;
            }
        }
        RodState_Update(&axis->sm_act, &axis->state, dir, tick);
    }
}

static void rod_thread_entry(void *param)
{
    (void)param;
    while (1) {
        Actuator_Tick(rt_tick_get());
#if DEV_ENABLE_PARAM
        /* 欠压行程保存状态机驱动：请求后计 2 tick（≈20ms 刹车滑行稳定）写入 Flash */
        Dev_Param_RodPollSave();
#endif
        Task_Set_Beat();
        rt_thread_mdelay(ROD_SCAN_PERIOD_MS);
    }
}

void Rod_Task_Start(void)
{
    (void)Task_Set_Create("rod", rod_thread_entry, RT_NULL,
                          ROD_THREAD_STACK, ROD_THREAD_PRIO, 100U);
}

/* EOF */
