/**
 * @file    hc32_drv_pwm.c
 * @brief   TMRA PWM 硬件适配实现（移植自 hkb_1 adapter_pwm）
 * @note    语义与 hkb_1 一致：u8LowActive=1 时，占空比 = 输出低电平时间百分比
 *          （CompareMatch=LOW / Period=HIGH）；duty 0% = 恒低（电机静止安全态）。
 */
#include "hc32_drv_pwm.h"
#include "hc32_ll_pwc.h"
#include "hc32_ll_fcg.h"
#include <rtthread.h>

/* 每个 TMRA 单元记录当前周期值（set duty 换算用；同单元重复初始化幂等） */
typedef struct {
    CM_TMRA_TypeDef *tmra;
    uint32_t period;
} PwmTimerPeriod_t;

static PwmTimerPeriod_t s_timer_period[2];
static uint8_t s_timer_num = 0U;

static uint32_t *PwmHw_PeriodOf(CM_TMRA_TypeDef *tmra)
{
    uint8_t i;

    for (i = 0U; i < s_timer_num; i++) {
        if (s_timer_period[i].tmra == tmra) {
            return &s_timer_period[i].period;
        }
    }
    if (s_timer_num < (uint8_t)(sizeof(s_timer_period) / sizeof(s_timer_period[0]))) {
        s_timer_period[s_timer_num].tmra = tmra;
        s_timer_period[s_timer_num].period = PWM_PERIOD_DFT;
        return &s_timer_period[s_timer_num++].period;
    }
    return RT_NULL;
}

void PwmHw_ChannelInit(uint8_t u8Port, uint16_t u16Pin, CM_TMRA_TypeDef *TMRAx,
                       uint32_t u32Ch, float fInitDutyPct, uint8_t u8LowActive)
{
    stc_tmra_init_t tmra_init;
    stc_tmra_pwm_init_t pwm_init;
    uint32_t *period = PwmHw_PeriodOf(TMRAx);
    uint16_t cmp_val;

    if (period == RT_NULL) {
        return;
    }

    /* 外设时钟门控（与 hc_drv_timer 同一惯例） */
    if (TMRAx == CM_TMRA_1) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_1, ENABLE);
    } else if (TMRAx == CM_TMRA_2) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_2, ENABLE);
    } else if (TMRAx == CM_TMRA_3) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_3, ENABLE);
    } else if (TMRAx == CM_TMRA_4) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_4, ENABLE);
    } else {
        return;
    }

    /* 引脚复用到 TMRA（功能码 4，与 hkb_1 的 Func_Tima0 等值） */
    GPIO_SetFunc(u8Port, u16Pin, PWM_GPIO_FUNC_TIMA);

    /* TMRA 基础：PCLK 直驱、锯齿波、向上计数 */
    tmra_init.sw_count.u16ClockDiv = TMRA_CLK_DIV1;
    tmra_init.sw_count.u16CountMode = TMRA_MD_SAWTOOTH;
    tmra_init.sw_count.u16CountDir = TMRA_DIR_UP;
    tmra_init.hw_count.u16CountUpCond = TMRA_CNT_UP_COND_INVD;
    tmra_init.hw_count.u16CountDownCond = TMRA_CNT_UP_COND_INVD;
    tmra_init.u32PeriodValue = *period;
    (void)TMRA_Init(TMRAx, &tmra_init);

    /* PWM 通道：低有效（比较匹配=低，周期匹配=高，起停=低），占空比=低电平占比 */
    cmp_val = (uint16_t)(((uint32_t)(*period + 1U) * (uint32_t)fInitDutyPct) / 100U);
    pwm_init.u32CompareValue = cmp_val;
    if (u8LowActive != 0U) {
        pwm_init.u16StartPolarity = TMRA_PWM_LOW;
        pwm_init.u16StopPolarity = TMRA_PWM_LOW;
        pwm_init.u16CompareMatchPolarity = TMRA_PWM_LOW;
        pwm_init.u16PeriodMatchPolarity = TMRA_PWM_HIGH;
    } else {
        pwm_init.u16StartPolarity = TMRA_PWM_HIGH;
        pwm_init.u16StopPolarity = TMRA_PWM_HIGH;
        pwm_init.u16CompareMatchPolarity = TMRA_PWM_HIGH;
        pwm_init.u16PeriodMatchPolarity = TMRA_PWM_LOW;
    }
    (void)TMRA_PWM_Init(TMRAx, u32Ch, &pwm_init);
    TMRA_PWM_OutputCmd(TMRAx, u32Ch, ENABLE);

    TMRA_Start(TMRAx);
}

void PwmHw_SetDutyPct(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, float fDutyPct)
{
    uint32_t *period = PwmHw_PeriodOf(TMRAx);
    uint32_t cmp_val;

    if ((period == RT_NULL) || (fDutyPct < 0.0f) || (fDutyPct > 100.0f)) {
        return;
    }

    if (fDutyPct <= 0.0f) {
        cmp_val = 0U;                                   /* 恒低 */
    } else if (fDutyPct >= 100.0f) {
        cmp_val = *period;                              /* 恒高 */
    } else {
        cmp_val = (uint32_t)(((uint32_t)*period * (uint32_t)fDutyPct) / 100U);
    }
    TMRA_SetCompareValue(TMRAx, u32Ch, cmp_val);
}

void PwmHw_CompareEnable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch)
{
    TMRA_PWM_OutputCmd(TMRAx, u32Ch, ENABLE);
}

void PwmHw_CompareDisable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch)
{
    TMRA_PWM_OutputCmd(TMRAx, u32Ch, DISABLE);
}

/* EOF */
