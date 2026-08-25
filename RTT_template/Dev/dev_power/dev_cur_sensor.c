/**
 * @file    dev_cur_sensor.c
 * @brief   电流传感器设备：读 10ms 滑动均值→时间窗口过流（1ms ISR 检测）
 * @note    电流换算在 ADC 驱动（已取绝对值）；本设备 1ms ISR 消费均值并判定；
 *          过流事件 ISR 直发 rt_event（ISR 安全），打印在 sys_sm 线程。
 */
#include "dev_cur_sensor.h"
#include "rtt_manager.h"
#include "Dev/dev_mgr/dev_state.h"
#include "Dev/dev_mgr/dev_event_def.h"
#include "Dev/dev_adc/dev_adc.h"
#include <rtthread.h>

#define CUR_OVER_CUR_TH_MA  (5000.0f)    /* 过流阈值 5A */
#define CUR_OVER_WINDOW_MS  (50U)        /* 判定时间窗口 ms：10ms 均值超阈值累计 50ms 才报 */

static volatile float    s_fCurrMa;      /* 1ms ISR 写，主循环/GetInfo 读 */
static volatile uint8_t  s_u8Status;     /* 0 正常 1 过流 */
static uint16_t s_u16OverMs;             /* 超阈值累计 ms（仅 ISR） */



void CurrentSensor_Init(void)
{
    s_fCurrMa = 0.0f;
    s_u8Status = 0U;
    s_u16OverMs = 0U;
}

/* 1ms ISR 检测（由 Dev_Power_Isr1ms 经 TMR0_2 心跳调用；ISR 内不打印） */
void CurrentSensor_Isr1ms(void)
{
    uint8_t u8Prev;
    float fCurrMa = 0.0f;

    (void)Dev_Adc_GetMean(1U, &fCurrMa);   /* 10ms 滑动均值，单位 mA（已取绝对值） */
    s_fCurrMa = fCurrMa;

    u8Prev = s_u8Status;
    if (fCurrMa > CUR_OVER_CUR_TH_MA) {
        if (s_u16OverMs < CUR_OVER_WINDOW_MS) {
            s_u16OverMs++;
        }
        if (s_u16OverMs >= CUR_OVER_WINDOW_MS) {
            s_u8Status = 1U;
        }
    } else {
        s_u16OverMs = 0U;
        s_u8Status = 0U;
    }

    /* 过流跳变 -> 事件通知系统状态机（打印在 sys_sm 线程） */
    if (s_u8Status != u8Prev) {
        if (s_u8Status == 1U) {
            Sys_Event_Send(EVT_SYS_OVER_CURRENT);
        }
    }
}

void CurrentSensor_GetInfo(float *pfCurr_mA, uint8_t *pu8Status)
{
    if (pfCurr_mA != NULL)  *pfCurr_mA = s_fCurrMa;
    if (pu8Status != NULL) *pu8Status = s_u8Status;
}

/* EOF */










