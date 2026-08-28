/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 * Copyright (c) 2022, Xiaohua Semiconductor Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-04-28     CDT          first version
 * 2026-08-24     (port)       接入电源采样设备（adc/cur/vm）+ power 命令
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "common.h"
#include "rtt_manager.h"
#include <stdlib.h>
#include "Dev/dev_registry.h"
#include "Dev/dev_mgr/dev_model.h"
#include "Dev/dev_mgr/dev_state.h"
#include "Dev/dev_adc/dev_adc.h"
#include "Adp/hc32_drv_adc.h"
#include "Adp/hc_drv_timer.h"
#include "Utils/us_timer.h"
#include "Dev/dev_power/dev_cur_sensor.h"
#include "Dev/dev_power/dev_bus_voltage.h"
#include "Dev/dev_power/dev_power_isr.h"
#include "Dev/dev_monitor/dev_monitor.h"
#include "Dev/dev_power/dev_polarity.h"
#include "Task/led_task.h"
#include "Task/di_task.h"
#include "Task/rod_task.h"
#include "Task/task_stack.h"
#include "Dev/dev_act/dev_act.h"


static void cmd_power(void)
{
    float v = 0.0f, c = 0.0f;
    uint8_t sv = 0U, sc = 0U;
    uint16_t v_i = 0U, v_d = 0U, c_i = 0U, c_d = 0U;

    BusVoltage_GetInfo(&v, &sv);
    CurrentSensor_GetInfo(&c, &sc);
    /* 不打印浮点：拆成整型，分别打印小数点前后 */
    v_i = (uint16_t)v;
    v_d = (uint16_t)((v - (float)v_i) * 100.0f);
    c_i = (uint16_t)c;
    c_d = (uint16_t)((c - (float)c_i) * 10.0f);
    MAIN_D("Vbus=%u.%02uV[%u] I=%u.%01umA[%u]", v_i, v_d, sv, c_i, c_d, sc);
}
MSH_CMD_EXPORT_ALIAS(cmd_power, power, show bus voltage & current);

static void cmd_sys_evt(int argc, char **argv)
{
    if (argc == 2) {
        Sys_Event_Send(strtoul(argv[1], RT_NULL, 0));
    } else {
        MAIN_D("usage: sys_evt <hex_bits>\r\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_sys_evt, sys_evt, send system event bits (hex));

/* 每 1s 打印采样结果：最新 AD 值、缓冲平均值、换算电流/电压（不打印浮点） */
static void monitor_sample_1s(void)
{
    uint16_t raw_v = 0U, raw_i = 0U;
    float v_avg = 0.0f, c_avg = 0.0f;
    float v_lst = 0.0f, c_lst = 0.0f;
    uint32_t v_avg_mv = 0U, v_lst_mv = 0U;
    uint16_t c_avg_i = 0U, c_avg_d = 0U, c_lst_i = 0U, c_lst_d = 0U;
    uint32_t t_us = 0U;

    UsTimer_UpdateTimestamp();
    Polarity_PrintPending();   /* 线程上下文：刷新未打印的极性跳变（调试） */
    Monitor_DumpStatus();    /* 1s：系统状态机 + 过压/欠压/过流状态 */
    t_us = (uint32_t)UsTimer_GetTimestampUs();

    Dev_Adc_GetRaw(0, &raw_v);
    Dev_Adc_GetRaw(1, &raw_i);
    BusVoltage_GetInfo(&v_avg, RT_NULL);
    CurrentSensor_GetInfo(&c_avg, RT_NULL);
    Dev_Adc_GetLatest(&v_lst, &c_lst);

    v_avg_mv = (uint32_t)(v_avg * 1000.0f);
    v_lst_mv  = (uint32_t)(v_lst * 1000.0f);
    c_avg_i = (uint16_t)c_avg;
    c_avg_d = (uint16_t)((c_avg - (float)c_avg_i) * 10.0f);
    c_lst_i = (uint16_t)c_lst;
    c_lst_d = (uint16_t)((c_lst - (float)c_lst_i) * 10.0f);

    /* ADC 层：原始 AD + 最近一次换算（无均值、无偏置） */
    MONITOR_PRINT("adc: t=%uus AD=%u/%u cur=%u.%01umA vol=%umV",
                  t_us, raw_v, raw_i, c_lst_i, c_lst_d, v_lst_mv);
    /* 设备层：电流传感器均值 + 母线电压均值（电压已含 VOL_OFFSET 偏置） */
    MONITOR_PRINT("dev: t=%uus cur=%u.%01umA vol=%umV",
                  t_us, c_avg_i, c_avg_d, v_avg_mv);

    Task_Stack_Monitor();      /* 1s：栈哨兵水位监控，超 75% 才打印 */
}
volatile int test = 0;
int main(void)
{
    /* 时钟频率打印 + TMR6 us 时间基准初始化 */
    Print_All_Clock_Freq();
    /* 通用 us 计时器：绑定 HC32 TMR6 驱动并启动 */
    UsTimer_Bind(&hc_us_timer_ops);
    UsTimer_Init();
    UsTimer_Start();
    rt_kprintf("aaaa");
    /* ADC 设备：绑定 HC32 底层驱动（dev_adc_ops），注册即 init（TMR0_1+AOS 已配置，未启动） */
    Dev_Adc_Bind(&hc32_adc_ops);
    Dev_RegisterAll();

    RttManager_DumpSwitches();

    /* 表驱动状态机：对象构建（先建事件组 sys_evt，检测 ISR 发事件依赖它） */
    App_Model_Init();
    Dev_Start();        /* registry 线程：驱动 monitor（B 模式 100ms 刷新 g_monitor） */


    Sys_Sm_Thread_Start();
    Led_Task_Start();    /* LED 闪烁任务（独立线程，1s 翻转，走 Adp GPIO） */
    Di_Task_Start();     /* DI 采集任务（10ms：电源极性扫描，事件在设备内发） */

    Act_Arbitrator_Init();    /* 仲裁占位：方向 g_act_dir（Watch 可改） */
    Rod_Task_Start();         /* 推杆位置/状态 10ms 更新 */

    Task_Stack_Dump();          /* 打印各线程栈大小 + 总栈 + 堆余量 */


    while (1)
    {
        test++;

        rt_thread_mdelay(1000);
        monitor_sample_1s();
    }
}





















