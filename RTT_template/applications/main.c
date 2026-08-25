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

/* defined the LED_GREEN pin: PD4 */
#define LED_GREEN_PIN GET_PIN(H, 2)

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

    MONITOR_PRINT("t=%uus AD=%u/%u avg=%umV/%u.%01umA cur=%u.%01umA vol=%umV",
                  t_us, raw_v, raw_i, v_avg_mv, c_avg_i, c_avg_d, c_lst_i, c_lst_d, v_lst_mv);
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

    /* ADC 设备：绑定 HC32 底层驱动（dev_adc_ops），注册即 init（TMR0_1+AOS 已配置，未启动） */
    Dev_Adc_Bind(&hc32_adc_ops);
    Dev_RegisterAll();

    RttManager_DumpSwitches();

    /* 表驱动状态机：对象构建（先建事件组 sys_evt，检测 ISR 发事件依赖它） */
    App_Model_Init();

    /* 1ms 检测心跳（TMR0_2）：电压/电流 ISR 检测 + rt_event 通知状态机 */
    HcDrv_Timer_Start1ms(Dev_Power_Isr1ms);
    /* 启动 200us 硬件触发采样（TMR0_1 + AOS） */
    Dev_Adc_Start();

    Sys_Sm_Thread_Start();

    /* set LED_GREEN_PIN pin mode to output */
    rt_pin_mode(LED_GREEN_PIN, PIN_MODE_OUTPUT);

    Output_GPIO_Init(GPIO_PORT_H, GPIO_PIN_02, GPIO_INIT_LOW);

    while (1)
    {
        test++;

        rt_thread_mdelay(500);
        rt_pin_write(LED_GREEN_PIN, PIN_LOW);
        rt_thread_mdelay(500);
        rt_pin_write(LED_GREEN_PIN, PIN_HIGH);
//        GPIO_TogglePins(GPIO_PORT_H, GPIO_PIN_02);
				monitor_sample_1s();
    }
}




















