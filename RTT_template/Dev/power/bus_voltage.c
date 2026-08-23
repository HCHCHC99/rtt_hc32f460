/**
 * @file    bus_voltage.c
 * @brief   母线电压设备：读环形缓冲→均值→过压/欠压（迟滞 + 恢复延时）
 */
#include "bus_voltage.h"
#include "dev_registry.h"
#include "adc_drv.h"
#include <rtthread.h>

#define VOL_FILTER_N         (20U)
#define VOL_OVER_TH          (32.0f)   /* 过压阈值 V（待整定） */
#define VOL_UNDER_TH         (8.0f)    /* 欠压阈值 V（待整定） */
#define VOL_HYST             (2.0f)    /* 迟滞回差 V */
#define VOL_RECOVER_DELAY_MS (3000U)   /* 恢复延时 ms */

static float   s_fVolt;
static uint8_t s_u8Status;       /* 0 正常 1 欠压 2 过压 */
static uint8_t s_u8Fault;
static uint8_t s_u8Waiting;
static uint32_t s_u32RecoverStart;

static const SysModule_t s_vm_module = SYS_MODULE_REGISTER("vm", BusVoltage_Init, BusVoltage_Task, 3, 20);

void BusVoltage_Register(void)
{
    (void)Dev_Registry_Add(&s_vm_module);
}

void BusVoltage_Init(void)
{
    s_fVolt = 0.0f;
    s_u8Status = 0U;
    s_u8Fault = 0U;
    s_u8Waiting = 0U;
    s_u32RecoverStart = 0U;
}

void BusVoltage_Task(void)
{
    float fBuf[VOL_FILTER_N];
    uint16_t u16N = AdcDrv_ReadRing(0U, fBuf, VOL_FILTER_N);
    float fSum = 0.0f;
    for (uint16_t i = 0U; i < u16N; i++) {
        fSum += fBuf[i];
    }
    if (u16N > 0U) {
        s_fVolt = fSum / (float)u16N;
    }

    uint32_t now = rt_tick_get();
    if (s_u8Fault != 0U) {
        uint8_t recover = (s_u8Fault == 2U) ? (s_fVolt < (VOL_OVER_TH - VOL_HYST))
                                            : (s_fVolt > (VOL_UNDER_TH + VOL_HYST));
        if (recover != 0U) {
            if (s_u8Waiting == 0U) {
                s_u8Waiting = 1U;
                s_u32RecoverStart = now;
            } else if ((now - s_u32RecoverStart) >= VOL_RECOVER_DELAY_MS) {
                s_u8Fault = 0U;
                s_u8Waiting = 0U;
            }
        } else {
            s_u8Waiting = 0U;
        }
    } else {
        if (s_fVolt > VOL_OVER_TH) {
            s_u8Fault = 2U;
        } else if (s_fVolt < VOL_UNDER_TH) {
            s_u8Fault = 1U;
        }
    }
    s_u8Status = s_u8Fault;
}

void BusVoltage_GetInfo(float *pfVolt_V, uint8_t *pu8Status)
{
    if (pfVolt_V != NULL)  *pfVolt_V = s_fVolt;
    if (pu8Status != NULL) *pu8Status = s_u8Status;
}

/* EOF */
