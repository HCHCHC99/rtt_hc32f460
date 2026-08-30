#ifndef __DEV_BUS_VOLTAGE_H__
#define __DEV_BUS_VOLTAGE_H__

#include <stdint.h>

/* 默认配置宏（统一放头文件） */
#define VOL_OVER_TH_DFT          (25.0f)   /* 过压阈值默认 V */

#define VOL_UNDER_TH_DFT         (0.0f)    /* 欠压阈值默认 V */
#define VOL_HYST_DFT             (1.0f)    /* 迟滞回差默认 V（1V） */
#define VOL_RECOVER_DELAY_MS_DFT (500U)    /* 恢复延时默认 ms */
#define VOL_OFFSET               (1200.0f) /* 偏置电压 mV：1200mV = 1.2V */

/* ===================== 模拟模式 ===================== */
/* 1=模拟：1ms 检测不读 ADC，改用 g_volt_sim_mv 直接赋值（表达式窗口实时可改）。
   模拟值 = 模块看到的最终电压 mV（ADC 换算与 +1.2V 偏置一并旁路） */
#define VOLT_SIM_MODE_EN        (1)
extern volatile uint32_t g_volt_sim_mv;   /* 模拟母线电压 mV（初值见 .c，可实时改） */

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









