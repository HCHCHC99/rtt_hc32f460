/**
 * @file    hc32_drv_pwm.h
 * @brief   HC32F460 TMRA PWM 硬件适配（移植自 hkb_1 adapter_pwm，DDL 换为 hc32_ll_tmra）
 * @note    时钟：TMRA4 = PCLK2（本工程 50MHz，CLK_DIV1 → 计数时钟 50MHz）；
 *          上电安全态：比较输出使能但占空比 0% = 恒低（电机静止）。
 */
#ifndef __HC32_DRV_PWM_H__
#define __HC32_DRV_PWM_H__

#include <stdint.h>
#include "hc32_ll_tmra.h"
#include "hc32_ll_gpio.h"

#define PWM_TMRA_CLK_HZ     (50000000UL)    /* TMRA4 计数时钟 = PCLK2（CLK_DIV1）；示波器实测校准 */
#define PWM_FREQ_DFT_HZ     (6500U)         /* 默认 PWM 频率（hkb_1 实测配置） */
#define PWM_FREQ_MIN_HZ     (10000U)        /* 运行中改频下限（仅 PwmHw_SetFreq 用） */
#define PWM_FREQ_MAX_HZ     (20000U)        /* 运行中改频上限 */
#define PWM_GPIO_FUNC_TIMA  (4U)            /* PB7/PB9 复用功能码 4 = TMRA（旧 DDL Func_Tima0 同值） */

/* ===================== 电机 PWM 引脚/通道硬件绑定（hkb_1 hardware.h 同配置；换芯片改这里） ===================== */
#define PWM_PHU_PORT        GPIO_PORT_B     /* PHU = PB9 */
#define PWM_PHU_PINS        GPIO_PIN_09
#define PWM_PHU_TMRA        CM_TMRA_4
#define PWM_PHU_CH          TMRA_CH4

#define PWM_PHV_PORT        GPIO_PORT_B     /* PHV = PB7 */
#define PWM_PHV_PINS        GPIO_PIN_07
#define PWM_PHV_TMRA        CM_TMRA_4
#define PWM_PHV_CH          TMRA_CH2

#define PWM_MOTOR_LOW_ACTIVE 1U             /* 1=低有效：占空比 = 输出低电平时间百分比（hkb_1 实测配置） */

/* 默认周期值（= PWM_TMRA_CLK_HZ / PWM_FREQ_DFT_HZ - 1；改频率请用 PwmHw 后续接口，此处仅初值） */
#define PWM_PERIOD_DFT      ((PWM_TMRA_CLK_HZ / PWM_FREQ_DFT_HZ) - 1U)

/* 通道初始化：引脚复用 + TMRA 基础配置 + PWM 极性/比较值 + 输出使能 + 启动 */
void PwmHw_ChannelInit(uint8_t u8Port, uint16_t u16Pin, CM_TMRA_TypeDef *TMRAx,
                       uint32_t u32Ch, float fInitDutyPct, uint8_t u8LowActive);
void PwmHw_SetDutyPct(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, float fDutyPct);
void PwmHw_CompareEnable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch);
void PwmHw_CompareDisable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch);

#endif /* __HC32_DRV_PWM_H__ */
