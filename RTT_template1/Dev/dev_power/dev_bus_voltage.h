#ifndef __DEV_BUS_VOLTAGE_H__
#define __DEV_BUS_VOLTAGE_H__

#include <stdint.h>

/* 默认配置宏（统一放头文件） */
#define VOL_OVER_TH_DFT          (23.0f)   /* 过压阈值默认 V */

#define VOL_UNDER_TH_DFT         (0.0f)    /* 欠压阈值默认 V */
#define VOL_HYST_DFT             (1.0f)    /* 迟滞回差默认 V（1V） */
#define VOL_RECOVER_DELAY_MS_DFT (500U)    /* 恢复延时默认 ms */
#define VOL_OFFSET               (1200.0f) /* 偏置电压 mV：1200mV = 1.2V */

/* 阈值配置（RAM 变量，debugger 可实时改；默认见 .c） */
typedef struct {
    float    over_th;      /* 过压阈值 V */
    float    under_th;     /* 欠压阈值 V */
    float    hyst;         /* 迟滞回差 V */
    uint32_t recover_ms;   /* 恢复延时 ms */
} VoltCfg_t;
extern volatile VoltCfg_t g_volt_cfg;

void BusVoltage_Init(void);
void BusVoltage_Isr1ms(void); /* 1ms ISR 检测（TMR0_2 心跳调用） */
void BusVoltage_GetInfo(float *pfVolt_V, uint8_t *pu8Status);  /* 0=正常 1=欠压 2=过压 */

#endif /* __DEV_BUS_VOLTAGE_H__ */





