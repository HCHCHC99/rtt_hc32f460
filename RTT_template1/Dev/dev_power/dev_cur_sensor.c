/**
 * @file    dev_cur_sensor.c
 * @brief   电流传感器设备：读 ADC 10ms 滑动电压均值→差分放大器换算→时间窗口过流（1ms ISR 检测）
 * @note    电流换算在本模块（ADC 层 CH5 只出电压 V）：
 *          差分放大器 0V 零点 100mV/A → mA = (V - 0) × 10000；
 *          换传感器只改本模块宏（CUR_SENSOR_ZERO_V / CUR_SENSITIVITY_MA_PER_V），不动 ADC 层。
 *          过流事件 ISR 直发 rt_event（ISR 安全），打印在 sys_sm 线程。
 */
#include "dev_cur_sensor.h"
#include "rtt_manager.h"
#include "Dev/dev_mgr/dev_state.h"
#include "Dev/dev_mgr/dev_event_def.h"
#include "Dev/dev_adc/dev_adc.h"
#include <rtthread.h>


/* 阈值配置全局变量（类型/声明见 dev_cur_sensor.h；debugger 改 g_cur_cfg 实时生效） */
volatile CurCfg_t g_cur_cfg = {
    CUR_OVER_CUR_TH_MA_DFT, CUR_OVER_WINDOW_MS_DFT,
};
volatile uint32_t g_cur_sim_ma = 500U;    /* 模拟电流 mA（CUR_SIM_MODE_EN=1 时生效） */

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

#if CUR_SIM_MODE_EN
    fCurrMa = (float)g_cur_sim_ma;         /* 模拟：直接赋值（ADC 读取与换算一并旁路） */
#else
    float fVoltV = 0.0f;
    (void)Dev_Adc_GetMean(1U, &fVoltV);     /* 10ms 滑动均值，单位 V（ADC 层仅出电压，不换算） */
    fCurrMa = (fVoltV - CUR_SENSOR_ZERO_V) * CUR_SENSITIVITY_MA_PER_V;  /* 差分放大器 V→mA */
#endif
    s_fCurrMa = fCurrMa;

    u8Prev = s_u8Status;
    if (fCurrMa > g_cur_cfg.over_th_ma) {
        if (s_u16OverMs < g_cur_cfg.window_ms) {
            s_u16OverMs++;
        }
        if (s_u16OverMs >= g_cur_cfg.window_ms) {
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

uint16_t CurrentSensor_GetOverMs(void)
{
    return s_u16OverMs;
}

/* EOF */












