/**
 * @file    app_model.c
 * @brief   顶层系统对象构建（表驱动状态机移植：对象模型 + 事件集）
 * @note    对应控制器工程 system.c 的 State_Init()；
 *          事件组用 RT-Thread rt_event 替代原自研 EventGroup。
 */
#include "dev_model.h"
#include "dev_state.h"
#include "dev_event_def.h"
#include <rtthread.h>

/* 全局系统对象 */
System_t mySystem;

void App_Model_Init(void)
{
    int i;

    /* 1. 系统状态机：填表 + 初始化（原样移植） */
    Sys_State_Init(&mySystem.sys_sm);

    /* 2. 系统事件集：替代原 EventGroup_Create */
    mySystem.sys_evt = rt_event_create("sys_evt", RT_IPC_FLAG_FIFO);
    mySystem.fault_bits = 0U;   /* 故障位图清零 */
    mySystem.prev_state  = (State_t)SYS_STATE_INIT;
    RT_ASSERT(mySystem.sys_evt != RT_NULL);

    /* 3. 轴对象初始化（多轴预留） */
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        Axis_t *ax = &mySystem.axis[i];
        char name[12];

        ax->id  = i;
        ax->dir = ACT_DIR_NONE;   /* 默认未配置；实际应来自机型/标定数据 */
        rt_snprintf(name, sizeof(name), "act%d_evt", i);
        ax->evt_act = rt_event_create(name, RT_IPC_FLAG_FIFO);

        /* 推杆位置 + 状态模块初始化（表驱动挂 sm_act；行程1000/减速比10/每转12脉冲/导程10/容差3） */
        RodPosition_Init(&ax->position);
        RodPosition_SetParams(&ax->position, 1000.0f, 10.0f, 12.0f, 10.0f, 3.0f);
        RodState_Init(&ax->sm_act, &ax->state, (uint8_t)i, &ax->position);
        RT_ASSERT(ax->evt_act != RT_NULL);
        /* sm_act 暂不挂表（Route B 调 Act_State_Init），保持清零即可 */
    }

    /* 4. 上电自动启动（对应原 State_Init 末尾的 EventGroup_Send） */
    Sys_Event_Send(EVT_SYS_INIT_DONE | EVT_SYS_CMD_WORK_ENABLE);
}

void Act_Event_Send(rt_uint32_t bits)
{
    int i;
    for (i = 0; i < MAX_AXIS_NUM; i++) {
        if (mySystem.axis[i].evt_act != RT_NULL) {
            (void)rt_event_send(mySystem.axis[i].evt_act, bits);
        }
    }
}

/* EOF */




