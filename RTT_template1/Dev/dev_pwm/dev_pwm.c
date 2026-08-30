/**
 * @file    dev_pwm.c
 * @brief   电机 PWM 输出设备实现：PHU/PHV 同比较值 + 反相极性 → 严格互补输出
 * @note    严格互补原理：两通道共用同一比较值 CMP，但极性相反——
 *          PHU 高电平区间 [0,CMP) ⟺ PHV 低电平区间 [0,CMP)，任意时刻严格互反，无双高/双低。
 *          停止用硬件强制极性（FORCE_LOW）而非比较值，无毛刺。
 */
#include "dev_pwm.h"
#include "Adp/hc32_drv_pwm.h"
#include "applications/rtt_manager.h"
#include "Dev/dev_act/dev_act.h"

/* 电机状态：0=停止 1=正转(伸出) 2=反转(缩回) */
static uint8_t s_motor_mode = 0U;

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

/* 严格互补驱动：两通道共用同一比较值 CMP（极性相反）
   duty_pct = PHU 高电平时间占比（2=正转伸出 98=反转缩回） */
static void PwmMotor_ApplyDuty(uint8_t duty_pct)
{
    uint32_t cmp = ((uint32_t)(PWM_PERIOD_DFT + 1U) * duty_pct) / 100U;
    if (cmp > 0U) {
        cmp--;                       /* 官方公式：CmpVal = ((PeriodVal+1)*Duty) - 1 */
    }
    PwmHw_SetCompareValue(PWM_PHU_TMRA, PWM_PHU_CH, cmp);
    PwmHw_SetCompareValue(PWM_PHV_TMRA, PWM_PHV_CH, cmp);
}

/* 停止：硬件强制双低（FORCE 极性，无毛刺；解除需 RunFwd/RunRev） */
static void PwmMotor_ForceStop(void)
{
    PwmHw_SetForcePolarity(PWM_PHU_TMRA, PWM_PHU_CH, TMRA_PWM_FORCE_LOW);
    PwmHw_SetForcePolarity(PWM_PHV_TMRA, PWM_PHV_CH, TMRA_PWM_FORCE_LOW);
}

/* 解除硬件强制（恢复正常比较输出） */
static void PwmMotor_Release(void)
{
    PwmHw_SetForcePolarity(PWM_PHU_TMRA, PWM_PHU_CH, TMRA_PWM_FORCE_INVD);
    PwmHw_SetForcePolarity(PWM_PHV_TMRA, PWM_PHV_CH, TMRA_PWM_FORCE_INVD);
}

int Dev_PwmMotor_RunFwd(void)
{
    if ((s_inited == 0U) || (s_motor_mode == 1U)) {
        return (s_inited == 0U) ? -1 : 0;
    }
    PwmMotor_Release();
    PwmMotor_ApplyDuty(PWM_RUN_DUTY_PCT);
    s_motor_mode = 1U;
    return 0;
}

int Dev_PwmMotor_RunRev(void)
{
    if ((s_inited == 0U) || (s_motor_mode == 2U)) {
        return (s_motor_mode == 2U) ? 0 : -1;
    }
    PwmMotor_Release();
    PwmMotor_ApplyDuty(100U - PWM_RUN_DUTY_PCT);
    s_motor_mode = 2U;
    return 0;
}

int Dev_PwmMotor_Stop(void)
{
    if (s_inited == 0U) {
        return -1;
    }
    PwmMotor_ForceStop();
    s_motor_mode = 0U;
    return 0;
}

/* 仲裁输出 ops：axis_id 暂不区分（单轴），duty_pct 暂未映射速度（hkb_1 即固定 2/98） */
static void Pwm_OpFwd(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;
    ARB_PRINT("motor fwd (PHU=%u PHV=%u)", PWM_RUN_DUTY_PCT, 100U - PWM_RUN_DUTY_PCT);
    (void)Dev_PwmMotor_RunFwd();
}

static void Pwm_OpRev(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;
    ARB_PRINT("motor rev (PHU=%u PHV=%u)", 100U - PWM_RUN_DUTY_PCT, PWM_RUN_DUTY_PCT);
    (void)Dev_PwmMotor_RunRev();
}

static void Pwm_OpStop(uint8_t axis_id)
{
    (void)axis_id;
    ARB_PRINT("motor stop");
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
    rt_thread_mdelay(1);
    /* 时钟诊断：实测计数时钟 vs 50M 假设（示波器频率不符时核对此行） */
    ARB_PRINT("tmra probe: cnt_clk=%u Hz (period=%u, meas over 100us)",
              (unsigned)PwmHw_ProbeCountClock(PWM_PHU_TMRA, PWM_PERIOD_DFT),
              (unsigned)PWM_PERIOD_DFT);
    rt_thread_mdelay(1);
    s_inited = 1U;
    ARB_PRINT("pwm init phu=PB9/CH4 phv=PB7/CH2 duty=0");
    rt_thread_mdelay(1);
}

void Dev_Pwm_Task(void)
{
    /* 预留：非阻塞斜坡推进（当前正反转为瞬占空比切换，无 ramp） */
}

/* EOF */
