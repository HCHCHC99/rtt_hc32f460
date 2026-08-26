/**
 * @file    rod_task.c
 * @brief   推杆位置/状态 10ms 周期更新（Actuator_Tick）
 * @note    霍尔占位：Hall_GetDeltaPulses 为弱定义，电机霍尔模块提供后自动覆盖。
 */
#include "rod_task.h"
#include "Dev/dev_mgr/dev_model.h"
#include "Dev/dev_rod/dev_rod_position.h"
#include "Dev/dev_rod/dev_rod_state.h"
#include "Dev/dev_act/dev_act.h"
#include "Adp/hc32_drv_gpio.h"
#include <rtthread.h>

/* 占位：电机霍尔脉冲（每机械转 12 边沿，导程10mm/圈，减速比10 —— 模块后续提供，覆盖此弱定义） */
RT_WEAK int32_t Hall_GetDeltaPulses(uint8_t axis_id)
{
    (void)axis_id;
    return 0;
}

static void Actuator_Tick(uint32_t tick)
{
    for (uint8_t i = 0U; i < MAX_AXIS_NUM; i++) {
        Axis_t *axis = &mySystem.axis[i];
        int32_t delta;

        if (axis->dir == ACT_DIR_NONE) {
            continue;   /* 未配置轴跳过 */
        }

        /* 1. 霍尔脉冲增量（占位 0） -> 位置更新 */
        delta = Hall_GetDeltaPulses(i);
        RodPosition_Update(&axis->position, delta);

        /* 2. 读限位霍尔电平（高=触发；上 PB2 / 下 PB10） */
        axis->state.max_limit_switch = (Hc32_Gpio_Read(ROD_MAX_LIMIT_PORT, ROD_MAX_LIMIT_PIN) != 0U);
        axis->state.min_limit_switch = (Hc32_Gpio_Read(ROD_MIN_LIMIT_PORT, ROD_MIN_LIMIT_PIN) != 0U);

        /* 3. 限位注入位置模块（自动校准） */
        RodPosition_OnMaxLimit(&axis->position, axis->state.max_limit_switch);
        RodPosition_OnMinLimit(&axis->position, axis->state.min_limit_switch);

        /* 4. 上下霍尔双高 = 传感器异常（触发系统 Emergency + 霍尔故障置位） */
        RodState_SetSensorFault(&axis->state,
                                (axis->state.max_limit_switch && axis->state.min_limit_switch));

        /* 5. 方向指令（仲裁占位）+ 状态更新 */
        RodState_Update(&axis->sm_act, &axis->state, Arbitrator_GetDirection(i), tick);
    }
}

static void rod_thread_entry(void *param)
{
    (void)param;
    while (1) {
        Actuator_Tick(rt_tick_get());
        rt_thread_mdelay(ROD_SCAN_PERIOD_MS);
    }
}

void Rod_Task_Start(void)
{
    rt_thread_t t = rt_thread_create("rod", rod_thread_entry, RT_NULL,
                                     ROD_THREAD_STACK, ROD_THREAD_PRIO, ROD_THREAD_TICK);
    if (t != RT_NULL) {
        rt_thread_startup(t);
    }
}

/* EOF */
