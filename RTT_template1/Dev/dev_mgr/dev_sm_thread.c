/**
 * @file    dev_sm_thread.c
 * @brief   系统状态机线程（事件驱动）
 * @note    替代原 Sys_State_Task 轮询：rt_event_recv 阻塞 -> Sys_State_Dispatch
 */
#include "dev_model.h"
#include "dev_state.h"
#include "dev_event_def.h"
#include "rtt_manager.h"
#include <rtthread.h>

/* 线程等待的系统事件位（与 sys_jump 表使用的事件一致） */


static void sys_sm_thread_entry(void *param)
{
    rt_uint32_t e;

    (void)param;
    while (1) {
        if (rt_event_recv(mySystem.sys_evt, SYS_SM_WAIT_EVENTS,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &e) == RT_EOK) {
            Sys_State_Dispatch(e);
        }
    }
}

void Sys_Sm_Thread_Start(void)
{
    rt_thread_t th = rt_thread_create("sys_sm",
                                      sys_sm_thread_entry,
                                      RT_NULL,
                                      SYS_SM_THREAD_STACK,
                                      SYS_SM_THREAD_PRIO,
                                      SYS_SM_THREAD_TICK);
    if (th != RT_NULL) {
        rt_thread_startup(th);
    } else {
        SYS_STATE_PRINT("thread create failed");
    }
}

/* EOF */







