/**
 * @file    current_sensor.c
 * @brief   电流传感器设备：读环形缓冲→均值→时间窗口过流
 * @note    电流已在 adc_drv 中取绝对值；本设备只做消费与状态判定
 */
#include "current_sensor.h"
#include "dev_registry.h"
#include "adc_drv.h"
#include <rtthread.h>

#define CUR_FILTER_N        (8U)
#define CUR_OVER_CUR_TH_MA  (10000.0f)   /* 过流阈值 10A（待整定） */
#define CUR_OVER_WINDOW_MS  (50U)        /* 判定时间窗口 ms（待整定） */

static float    s_fCurrMa;
static uint8_t  s_u8Status;
static uint16_t s_u16OverMs;
static uint32_t s_u32LastTick;

static const SysModule_t s_cur_module = SYS_MODULE_REGISTER("cur", CurrentSensor_Init, CurrentSensor_Task, 3, 5);

void CurrentSensor_Register(void)
{
    (void)Dev_Registry_Add(&s_cur_module);
}

void CurrentSensor_Init(void)
{
    s_fCurrMa = 0.0f;
    s_u8Status = 0U;
    s_u16OverMs = 0U;
    s_u32LastTick = rt_tick_get();
}

void CurrentSensor_Task(void)
{
    float fBuf[CUR_FILTER_N];
    uint16_t u16N = AdcDrv_ReadRing(1U, fBuf, CUR_FILTER_N);
    float fSum = 0.0f;
    for (uint16_t i = 0U; i < u16N; i++) {
        fSum += fBuf[i];
    }
    if (u16N > 0U) {
        s_fCurrMa = fSum / (float)u16N;
    }

    /* 时间窗口过流：连续超阈值累计达到窗口时间才报 */
    uint32_t now = rt_tick_get();
    uint32_t dt = now - s_u32LastTick;
    s_u32LastTick = now;

    if (s_fCurrMa > CUR_OVER_CUR_TH_MA) {
        s_u16OverMs = (uint16_t)(s_u16OverMs + dt);
        if (s_u16OverMs >= CUR_OVER_WINDOW_MS) {
            s_u8Status = 1U;
        }
    } else {
        s_u16OverMs = 0U;
        s_u8Status = 0U;
    }
}

void CurrentSensor_GetInfo(float *pfCurr_mA, uint8_t *pu8Status)
{
    if (pfCurr_mA != NULL)  *pfCurr_mA = s_fCurrMa;
    if (pu8Status != NULL) *pu8Status = s_u8Status;
}

/* EOF */
