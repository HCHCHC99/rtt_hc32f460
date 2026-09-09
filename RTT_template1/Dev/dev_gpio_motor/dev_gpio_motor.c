/**
 * @file    dev_gpio_motor.c
 * @brief   电机 GPIO 输出设备实现（新板）：双脚方向控制，无调速/无线程
 * @note    命令接缝沿用 Arb_BindOutputOps 绑定 fwd/rev/stop，无缓启动状态机：
 *          ops 收到即成对写 GPIO（关中断保证两脚同步翻转 +
 *          线程/ISR 上下文写不交错；双高非法按停止处理）。
 *          duty_pct 被忽略（GPIO 无调速，任意非零 = 全速输出）。
 *          停止/急停 = 双低（用户：双低应为刹车态，待实测）。
 */
#include "dev_gpio_motor.h"
#include "applications/rtt_manager.h"
#include "Adp/hc32_drv_gpio.h"
#include "Dev/dev_act/dev_act.h"
#include <rtthread.h>
#include <rthw.h>   /* rt_hw_interrupt_disable / enable */

static uint8_t s_inited = 0U;
static volatile uint8_t s_dir_invert = 0U;   /* 1=交换 FWD/REV 输出（Flash A 块 motor_dir_seq 应用，Init 不复位） */

/* ===================== 成对写 GPIO（线程/ISR 上下文均可调） ===================== */
static void MotorGpio_WritePair(uint8_t fwd_en, uint8_t rev_en)
{
    rt_base_t level = rt_hw_interrupt_disable();

    if (s_dir_invert != 0U) {                       /* 相序反置：交换 FWD/REV 语义 */
        uint8_t t = fwd_en;
        fwd_en = rev_en;
        rev_en = t;
    }

    if ((fwd_en != 0U) && (rev_en == 0U)) {         /* 正转：FWD=1 REV=0 */
        Hc32_Gpio_Set(MOTOR_GPIO_FWD_PORT, MOTOR_GPIO_FWD_PIN);
        Hc32_Gpio_Reset(MOTOR_GPIO_REV_PORT, MOTOR_GPIO_REV_PIN);
    } else if ((rev_en != 0U) && (fwd_en == 0U)) {  /* 反转：FWD=0 REV=1 */
        Hc32_Gpio_Reset(MOTOR_GPIO_FWD_PORT, MOTOR_GPIO_FWD_PIN);
        Hc32_Gpio_Set(MOTOR_GPIO_REV_PORT, MOTOR_GPIO_REV_PIN);
    } else {                                        /* 停止 + 双高防御：双低 */
        Hc32_Gpio_Reset(MOTOR_GPIO_FWD_PORT, MOTOR_GPIO_FWD_PIN);
        Hc32_Gpio_Reset(MOTOR_GPIO_REV_PORT, MOTOR_GPIO_REV_PIN);
    }
    rt_hw_interrupt_enable(level);
}

/* ===================== 仲裁输出 ops（act 线程上下文调用） ===================== */
static void MotorGpio_OpFwd(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;     /* GPIO 无调速：任意非零 duty = 全速 */
    MotorGpio_WritePair(1U, 0U);
}

static void MotorGpio_OpRev(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    (void)duty_pct;
    MotorGpio_WritePair(0U, 1U);
}

static void MotorGpio_OpStop(uint8_t axis_id)
{
    (void)axis_id;
    MotorGpio_WritePair(0U, 0U);
}

static const ArbOutputOps_t s_gpio_out_ops = {
    MotorGpio_OpFwd,
    MotorGpio_OpRev,
    MotorGpio_OpStop,
};

/* ===================== 公开 API ===================== */
void Dev_MotorGpio_SetDirInvert(uint8_t invert)
{
    s_dir_invert = (invert != 0U) ? 1U : 0U;
}

int Dev_MotorGpio_RunFwd(void)
{
    if (s_inited == 0U) {
        return -1;
    }
    MotorGpio_WritePair(1U, 0U);
    return 0;
}

int Dev_MotorGpio_RunRev(void)
{
    if (s_inited == 0U) {
        return -1;
    }
    MotorGpio_WritePair(0U, 1U);
    return 0;
}

int Dev_MotorGpio_Stop(void)
{
    if (s_inited == 0U) {
        return -1;
    }
    MotorGpio_WritePair(0U, 0U);
    return 0;
}

int Dev_MotorGpio_EStop(void)
{
    /* 急停：立即双低（双低 = 刹车态待实测）；关中断序列，ISR 上下文可调 */
    MotorGpio_WritePair(0U, 0U);
    return 0;
}

/* ===================== 初始化 ===================== */
void Dev_MotorGpio_Init(void)
{
    if (s_inited != 0U) {
        return;     /* IDLE 重入：跳过（硬件配置不变） */
    }

    /* 两脚输出 + 上电安全态 = 双低（停止） */
    Output_GPIO_Init(MOTOR_GPIO_FWD_PORT, MOTOR_GPIO_FWD_PIN, GPIO_INIT_LOW);
    Output_GPIO_Init(MOTOR_GPIO_REV_PORT, MOTOR_GPIO_REV_PIN, GPIO_INIT_LOW);

    /* 绑定仲裁输出：此后 arb 决策经 ops 直接写 GPIO（无线程中转） */
    (void)Arb_BindOutputOps(&s_gpio_out_ops);

    s_inited = 1U;
    MOTOR_GPIO_PRINT("init gpio fwd/rev pair, both-low=stop");
}

/* EOF */
