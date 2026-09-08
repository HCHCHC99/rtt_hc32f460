#ifndef __DEV_CUR_SENSOR_H__
#define __DEV_CUR_SENSOR_H__

#include <stdint.h>

/* 默认配置宏（统一放头文件） */
#define CUR_OVER_CUR_TH_MA_DFT  (5000.0f)  /* 过流阈值默认 mA（5A） */
#define CUR_OVER_WINDOW_MS_DFT  (50U)      /* 过流判定窗口默认 ms */

/* ===================== 差分放大器换算（0V 零点；总灵敏度=采样电阻×放大倍数） ===================== */
/* ADC 层 CH6 只输出电压 V，本模块把 V→mA 换算下沉至此（换传感器只改这里） */
#define CUR_SENSOR_ZERO_V        (0.0f)       /* 零点电压 V（差分放大器 0A 时输出 ≈0V） */
#define CUR_AMP_GAIN             (20.0f)      /* 差分放大器倍数（新板 20 倍；旧板 10 倍） */
#define CUR_SHUNT_MOHM           (10.0f)      /* 采样电阻 mΩ（旧板 10mΩ，新板未变则同值） */
#define CUR_SENSITIVITY_MA_PER_V (1000.0f * 1000.0f / (CUR_SHUNT_MOHM * CUR_AMP_GAIN))
/* 灵敏度 mA/V = 1e6/(R_mΩ×G)：10mΩ×20 倍 = 200mV/A → 5000 mA/V（旧板 10mΩ×10 倍 = 100mV/A → 10000） */

/* ===================== 模拟模式 ===================== */
/* 1=模拟：1ms 检测不读 ADC，改用 g_cur_sim_ma 直接赋值（表达式窗口实时可改，单位 mA） */
#define CUR_SIM_MODE_EN         (0)
extern volatile uint32_t g_cur_sim_ma;    /* 模拟电流 mA（初值见 .c，可实时改） */

/* 阈值配置（RAM 变量，debugger 可实时改；默认见 .c） */
typedef struct {
    float    over_th_ma;   /* 过流阈值 mA */
    uint16_t window_ms;   /* 过流判定窗口 ms */
} CurCfg_t;
extern volatile CurCfg_t g_cur_cfg;

void CurrentSensor_Init(void);
void CurrentSensor_Isr1ms(void); /* 1ms ISR 检测（TMR0_2 心跳调用） */
void CurrentSensor_GetInfo(float *pfCurr_mA, uint8_t *pu8Status);  /* 0=正常 1=过流 */
uint16_t CurrentSensor_GetOverMs(void);                    /* 当前超阈值累计时间（ms），观测用 */
float    CurrentSensor_GetFaultMa(void);                   /* 故障触发瞬间的电流快照 mA（sys_sm 打印用） */

#endif /* __DEV_CUR_SENSOR_H__ */










