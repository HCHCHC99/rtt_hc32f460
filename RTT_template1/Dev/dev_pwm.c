/**
 * @file    dev_pwm.c
 * @brief   电机 PWM 输出设备实现（移植自 hkb_1 dev_pwm 的双通道互补语义）
 */
#include "dev_pwm.h"
#include "Adp/hc32_drv_pwm.h"
#include "applications/rtt_manager.h"
#include "Dev/dev_act/dev_act.h"

typedef struct {
    uint8_t  port;
    uint16_t pins;
    CM_TMRA_TypeDef *tmra;
    uint32_t ch;
    float    duty;             /* 当前占空比（观测用） */
} DevPwmCh_t;

static DevPwmCh_t s_phu;
static DevPwmCh_t s_phv;
static uint8_t s_inited = 0U;

static int PwmMotor_Duty(uint8_t phu_pct, uint8_t phv_pct)
{
    if (s_inited == 0U) {
        return -1;
    }
    PwmHw_SetDutyPct(s_phu.tmra, s_phu.ch, (float)phu_pct);
    PwmHw_SetDutyPct(s_phv.tmra, s_phv.ch, (float)phv_pct);
    s_phu.duty = (float)phu_pct;
    s_phv.duty = (float)phv_pct;
    return 0;
}

int Dev_PwmMotor_RunFwd(void)
{
    return PwmMotor_Duty(PWM_RUN_DUTY_PCT, 100U - PWM_RUN_DUTY_PCT);   /* PHU 2 / PHV 98 */
}

int Dev_PwmMotor_RunRev(void)
{
    return PwmMotor_Duty(100U - PWM_RUN_DUTY_PCT, PWM_RUN_DUTY_PCT);   /* PHU 98 / PHV 2 */
}

int Dev_PwmMotor_Stop(void)
{
    return PwmMotor_Duty(0U, 0U);                                       /* 双 0% = 恒低静止 */
}

/* 仲裁输出 ops：axis_id 暂不区分（单轴），duty_pct 暂未映射速度（hkb_1 即固定 2/98） */
static void Pwm_OpFwd(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;
    (void)Dev_PwmMotor_RunFwd();
}

static void Pwm_OpRev(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;
    (void)Dev_PwmMotor_RunRev();
}

static void Pwm_OpStop(uint8_t axis_id)
{
    (void)axis_id;
    (void)Dev_PwmMotor_Stop();
}

static const ArbOutputOps_t s_pwm_out_ops = {
    Pwm_OpFwd,
    Pwm_OpRev,
    Pwm_OpStop,
};

void Dev_Pwm_Init(void)
{
    if (s_inited != 0U) {
        return;     /* IDLE 重入：跳过（硬件配置不变，无需重复初始化） */
    }

    s_phu.port = PWM_PHU_PORT;
    s_phu.pins = PWM_PHU_PINS;
    s_phu.tmra = PWM_PHU_TMRA;
    s_phu.ch   = PWM_PHU_CH;
    s_phv.port = PWM_PHV_PORT;
    s_phv.pins = PWM_PHV_PINS;
    s_phv.tmra = PWM_PHV_TMRA;
    s_phv.ch   = PWM_PHV_CH;

    /* 上电安全态：占空比 0% = 输出恒低，电机静止 */
    PwmHw_ChannelInit(s_phu.port, s_phu.pins, s_phu.tmra, s_phu.ch, 0.0f, 1U);
    PwmHw_ChannelInit(s_phv.port, s_phv.pins, s_phv.tmra, s_phv.ch, 0.0f, 1U);

    /* 绑定仲裁输出：此后 arb 决策直接驱动真实 PWM */
    (void)Arb_BindOutputOps(&s_pwm_out_ops);

    s_inited = 1U;
    ARB_PRINT("pwm init phu=PB9/CH4 phv=PB7/CH2 duty=0");
}

void Dev_Pwm_Task(void)
{
    /* 预留：非阻塞斜坡推进（当前正反转为瞬占空比切换，无 ramp） */
}

/* EOF */
