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
#include "dev_registry.h"
#include "dev_adc.h"
#include "power/current_sensor.h"
#include "power/bus_voltage.h"

/* defined the LED_GREEN pin: PD4 */
#define LED_GREEN_PIN GET_PIN(H, 2)

static void cmd_power(void)
{
    float v = 0.0f, c = 0.0f;
    uint8_t sv = 0U, sc = 0U;

    BusVoltage_GetInfo(&v, &sv);
    CurrentSensor_GetInfo(&c, &sc);
    rt_kprintf("Vbus=%.2fV[%u] I=%.1fmA[%u]\r\n", v, sv, c, sc);
}
MSH_CMD_EXPORT_ALIAS(cmd_power, power, show bus voltage & current);

int main(void)
{
    /* 注册并启动电源采样设备（adc / cur / vm） */
    Dev_RegisterAll();
    Dev_Start();

    /* set LED_GREEN_PIN pin mode to output */
    rt_pin_mode(LED_GREEN_PIN, PIN_MODE_OUTPUT);

    while (1)
    {
        rt_pin_write(LED_GREEN_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED_GREEN_PIN, PIN_LOW);
        rt_thread_mdelay(500);
    }
}
