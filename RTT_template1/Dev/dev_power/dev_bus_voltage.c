/**
 * @file    dev_bus_voltage.c
 * @brief   母线电压设备：读 10ms 滑动均值→过压/欠压（迟滞 + 恢复延时，1ms ISR 检测）
 */
#include "dev_bus_voltage.h"
#include "rtt_manager.h"
#include "Dev/dev_mgr/dev_state.h"
#include "Dev/dev_mgr/dev_event_def.h"
#include "Dev/dev_adc/dev_adc.h"
#include <rtthread.h>

#define VOL_OVER_TH          (23.0f)   /* 过压阈值 V */
#define VOL_UNDER_TH         (0.0f)    /* 欠压阈值 V */
#define VOL_HYST             (2.0f)    /* 迟滞回差 V */
#define VOL_RECOVER_DELAY_MS (3000U)   /* 恢复延时 ms（1ms 计数） */
#define VOL_OFFSET           (1200.0f) /* 偏置电压 mV：1200mV = 1.2V（仅加在本模块，不影响 ADC 层） */

static volatile float   s_fVolt;       /* 1ms ISR 写，主循环/GetInfo 读 */
static volatile uint8_t s_u8Status;    /* 0 正常 1 欠压 2 过压 */
static uint8_t s_u8Fault;
static uint8_t s_u8Waiting;
static uint32_t s_u32RecoverCnt;       /* 恢复延时累计 ms（仅 ISR） */



void BusVoltage_Init(void)
{
    s_fVolt = 0.0f;
    s_u8Status = 0U;
    s_u8Fault = 0U;
    s_u8Waiting = 0U;
    s_u32RecoverCnt = 0U;
}

/* 1ms ISR 检测（由 Dev_Power_Isr1ms 经 TMR0_2 心跳调用；ISR 内不打印） */
void BusVoltage_Isr1ms(void)
{
    uint8_t u8PrevFault;
    float fVolt = 0.0f;

    (void)Dev_Adc_GetMean(0U, &fVolt);   /* 10ms 滑动均值，单位 V（已含 150k:10k 分压换算） */
    fVolt += (VOL_OFFSET * 0.001f);      /* 偏置补偿：+1.2V（仅本模块，不影响 ADC 层） */
    s_fVolt = fVolt;

    u8PrevFault = s_u8Fault;

    if (s_u8Fault != 0U) {
        uint8_t u8Recover = (s_u8Fault == 2U) ? (fVolt < (VOL_OVER_TH - VOL_HYST))
                                              : (fVolt > (VOL_UNDER_TH + VOL_HYST));
        if (u8Recover != 0U) {
            if (s_u8Waiting == 0U) {
                s_u8Waiting = 1U;
                s_u32RecoverCnt = 0U;
            } else {
                s_u32RecoverCnt++;
                if (s_u32RecoverCnt >= VOL_RECOVER_DELAY_MS) {
                    s_u8Fault = 0U;
                    s_u8Waiting = 0U;
                    s_u32RecoverCnt = 0U;
                }
            }
        } else {
            s_u8Waiting = 0U;
            s_u32RecoverCnt = 0U;
        }
    } else {
        if (fVolt > VOL_OVER_TH) {
            s_u8Fault = 2U;
        } else if (fVolt < VOL_UNDER_TH) {
            s_u8Fault = 1U;
        }
    }
    s_u8Status = s_u8Fault;

    /* 故障跳变 -> 事件通知系统状态机（恢复不自动发事件，手动恢复；打印在 sys_sm 线程） */
    if (s_u8Fault != u8PrevFault) {
        if (s_u8Fault == 2U) {
            Sys_Event_Send(EVT_SYS_VOLT_OVER);
        } else if (s_u8Fault == 1U) {
            Sys_Event_Send(EVT_SYS_VOLT_UNDER);
        }
    }
}

void BusVoltage_GetInfo(float *pfVolt_V, uint8_t *pu8Status)
{
    if (pfVolt_V != NULL)  *pfVolt_V = s_fVolt;
    if (pu8Status != NULL) *pu8Status = s_u8Status;
}

/* EOF */












