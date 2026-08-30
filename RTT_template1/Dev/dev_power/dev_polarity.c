/**
 * @file    dev_polarity.c
 * @brief   电源极性设备实现（GPIO 双窗口消抖 + 状态跳变事件）
 * @note    由 Task/di_task（2ms 线程）调用，不做打印/阻塞；
 *          事件经 Act_Event_Send 广播到各轴事件组（rt_event_send，ISR 安全），
 *          消费方为电机控制（轴仲裁）模块。
 */
#include "dev_polarity.h"
#include "Dev/dev_act/dev_act.h"
#include "Dev/dev_mgr/dev_model.h"
#include "Dev/dev_mgr/dev_event_def.h"
#include "drv_gpio.h"          /* GET_PIN / GPIO_PORT_B */
#include "rtt_manager.h"
#include "Utils/us_timer.h"
#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

/* ============ 引脚与窗口参数 ============ */

/* 双窗口：位掩码（bit0 最新）+ 已填点数 */
typedef struct {
    uint16_t win;
    uint8_t  cnt;
} PolarityWin_t;

/* ============ 本地状态 ============ */
static volatile PolarityState_t s_state = POLARITY_UNKNOWN;
static PolarityWin_t s_pWin;
static PolarityWin_t s_nWin;
static uint8_t s_bInit = 0U;
static volatile uint8_t s_u8PendingState = 0U;  /* 待打印的跳变状态（ISR 置位，线程清） */
static volatile uint32_t s_arb_send_fail_count = 0U;
volatile PolarityState_t g_pol_sim_state = POLARITY_UNKNOWN;   /* 模拟极性（POLARITY_SIM_MODE_EN=1 时生效） */

/* ============ 窗口操作 ============ */
static uint8_t Polarity_WinFull(const PolarityWin_t *w)
{
    return (w->cnt >= POLARITY_WIN_SIZE);
}

/* 推入一个采样点，环形覆盖最旧位 */
static void Polarity_WinPush(PolarityWin_t *w, uint8_t bit)
{
    w->win = (uint16_t)((w->win << 1) | (bit & 1U));
    if (w->cnt < POLARITY_WIN_SIZE) {
        w->cnt++;
    }
}

static uint8_t Polarity_WinAllZero(const PolarityWin_t *w)
{
    return (Polarity_WinFull(w) && (w->win == 0U));
}

static uint8_t Polarity_WinAllOne(const PolarityWin_t *w)
{
    return (Polarity_WinFull(w) && (w->win == (uint16_t)((1U << POLARITY_WIN_SIZE) - 1U)));
}

/* 极性命令是带数据通道；队列满只告警计数，不阻塞扫描线程 */
static void Polarity_SendArb(uint8_t device_id, uint8_t cmd_type, uint8_t duty_pct)
{
    rt_err_t ret = Arb_SendCommand(POLARITY_ARB_AXIS_ID,
                                   device_id,
                                   (uint8_t)PRIO_POWER,
                                   cmd_type,
                                   duty_pct,
                                   RT_FALSE);

    if (ret != RT_EOK) {
        s_arb_send_fail_count++;
        ARB_PRINT("send fail axis=%u dev=%u cmd=%u err=%d count=%u",
                  (unsigned)POLARITY_ARB_AXIS_ID,
                  (unsigned)device_id,
                  (unsigned)cmd_type,
                  (int)ret,
                  (unsigned)s_arb_send_fail_count);
    }
}

/* 稳定判定：窗口未满或任一窗口不稳定 -> UNKNOWN（保持上次稳定态） */
static PolarityState_t Polarity_Eval(void)
{
    uint8_t pAll0 = Polarity_WinAllZero(&s_pWin);
    uint8_t pAll1 = Polarity_WinAllOne(&s_pWin);
    uint8_t nAll0 = Polarity_WinAllZero(&s_nWin);
    uint8_t nAll1 = Polarity_WinAllOne(&s_nWin);

    if (pAll0 && nAll0) return POLARITY_UNPOWERED;  /* 掉电 */
    if (pAll1 && nAll0) return POLARITY_FWD;        /* 正向 */
    if (pAll0 && nAll1) return POLARITY_REV;        /* 反向 */
    if (pAll1 && nAll1) return POLARITY_ABNORMAL;   /* 异常 */
    return POLARITY_UNKNOWN;                        /* 不稳定 */
}

/* ============ 接口 ============ */
void Polarity_Init(void)
{
    s_state = POLARITY_UNKNOWN;
    s_pWin.win = 0U; s_pWin.cnt = 0U;
    s_nWin.win = 0U; s_nWin.cnt = 0U;
    s_u8PendingState = 0U;
    s_bInit = 1U;
    POLARITY_PRINT("init win=%u p=PB13 n=PB12", (unsigned)POLARITY_WIN_SIZE);
}

void Polarity_Scan(void)
{
    PolarityState_t st;

    if (!s_bInit) {
        return;
    }

#if POLARITY_SIM_MODE_EN
    st = g_pol_sim_state;           /* 模拟：直接取表达式窗口的状态 */
    if ((st == POLARITY_UNKNOWN) || ((uint8_t)st > (uint8_t)POLARITY_ABNORMAL)) {
        return;                     /* UNKNOWN/非法值：保持上次状态，不发事件 */
    }
#else
    uint8_t p, n;
    p = rt_pin_read(POWER_DIR_P_PIN) ? 1U : 0U;
    n = rt_pin_read(POWER_DIR_N_PIN) ? 1U : 0U;

    Polarity_WinPush(&s_pWin, p);
    Polarity_WinPush(&s_nWin, n);

    st = Polarity_Eval();
    if (st == POLARITY_UNKNOWN) {
        return;                     /* 未满窗/不稳定：保持上次状态，不发事件 */
    }
#endif
    if (st != s_state) {            /* 稳定状态跳变沿才发事件（不重复发相同事件） */
        s_state = st;
        s_u8PendingState = (uint8_t)st;   /* 由线程上下文 Polarity_PrintPending 打印 */
        switch (st) {
        case POLARITY_UNPOWERED:
            Act_Event_Send(EVT_ACT_POWER_LOST);
            Polarity_SendArb((uint8_t)DEV_ID_POWER_POS, (uint8_t)CMD_TYPE_CLEAR_ALLOW_FWD, 0U);
            Polarity_SendArb((uint8_t)DEV_ID_POWER_POS, (uint8_t)CMD_TYPE_CLEAR_ALLOW_REV, 0U);
            break;
        case POLARITY_FWD:
            Act_Event_Send(EVT_ACT_POLARITY_FWD);
            Polarity_SendArb((uint8_t)DEV_ID_POWER_POS,
                             (uint8_t)CMD_TYPE_RUN_FWD,
                             POLARITY_ARB_RUN_DUTY_PCT);
            break;
        case POLARITY_REV:
            Act_Event_Send(EVT_ACT_POLARITY_REV);
            Polarity_SendArb((uint8_t)DEV_ID_POWER_NEG,
                             (uint8_t)CMD_TYPE_RUN_REV,
                             POLARITY_ARB_RUN_DUTY_PCT);
            break;
        case POLARITY_ABNORMAL:
            Act_Event_Send(EVT_ACT_POWER_ABNORMAL);
            Polarity_SendArb((uint8_t)DEV_ID_POWER_POS, (uint8_t)CMD_TYPE_CLEAR_ALLOW_FWD, 0U);
            Polarity_SendArb((uint8_t)DEV_ID_POWER_POS, (uint8_t)CMD_TYPE_CLEAR_ALLOW_REV, 0U);
            break;
        default: break;
        }
    }
}

/* 线程上下文：打印未处理的极性跳变（ISR 只置位，这里才打，避免 ISR 内打印） */
void Polarity_PrintPending(void)
{
    rt_base_t level;
    uint8_t st;
    uint32_t t_us = 0U;

    level = rt_hw_interrupt_disable();   /* 原子读清（ISR 可能正在置位） */
    st = s_u8PendingState;
    s_u8PendingState = 0U;
    rt_hw_interrupt_enable(level);

    if (st == 0U) {
        return;
    }
    UsTimer_UpdateTimestamp();
    t_us = (uint32_t)UsTimer_GetTimestampUs();
    switch ((PolarityState_t)st) {
    case POLARITY_UNPOWERED: POLARITY_PRINT("power lost t=%uus", t_us); break;
    case POLARITY_FWD:       POLARITY_PRINT("fwd t=%uus", t_us);            break;
    case POLARITY_REV:       POLARITY_PRINT("rev t=%uus", t_us);            break;
    case POLARITY_ABNORMAL:  POLARITY_PRINT("abnormal t=%uus", t_us);       break;
    default: break;
    }
}

PolarityState_t Polarity_GetState(void)
{
    return s_state;
}

/* EOF */




