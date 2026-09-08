/**
 * @file    dev_param.c
 * @brief   慢块 A：应用配置参数（g_volt_cfg / g_cur_cfg）掉电保存（param_manager 实例）
 * @note    上电 main 调 Dev_Param_Init() 一次（内部统一初始化慢块 A + 快块 B，见
 *          dev_param_rod.c）；保存走 Dev_Param_Save()（电机运行中拒绝）。
 */
#include "dev_param.h"
#include "Utils/param_manager.h"
#include "applications/rtt_manager.h"
#include "rtthread.h"
#include "Dev/dev_config.h"
#include "Dev/dev_power/dev_bus_voltage.h"
#include "Dev/dev_power/dev_cur_sensor.h"
#include "Dev/dev_act/dev_act.h"
#include <stddef.h>
#include <string.h>

#if DEV_ENABLE_PARAM

/* 慢块存储区：0x7C000 起逆序使用（sec 62 → 61，2 扇区；0x78000~0x79FFF 以下归快块 B） */
#define PARAM_SEC_START         (62U)
#define PARAM_SEC_END           (61U)

/* 编译期断言：记录尺寸不超引擎缓冲上限、4 字节对齐 */
typedef char param_size_check[(sizeof(ParamRecord_t) <= PARAM_MAX_RECORD_SIZE) ? 1 : -1];
typedef char param_align_check[((sizeof(ParamRecord_t) % 4U) == 0U) ? 1 : -1];

/* Flash 存储记录 + 引擎实例 */
ParamRecord_t g_param_record;
static Param_Runtime_t s_param_runtime;

static const Param_Config_t s_param_config = {
    .pParamBuf      = &g_param_record,
    .paramSize      = sizeof(ParamRecord_t),
    .magicHead      = PARAM_MAGIC_HEAD,
    .magicTail      = PARAM_MAGIC_TAIL,
    .checksumOffset = offsetof(ParamRecord_t, checksum),
    .seqOffset      = offsetof(ParamRecord_t, sequence_id),
    .eraseCntOffset = offsetof(ParamRecord_t, erase_count),
    .secStart       = PARAM_SEC_START,
    .secEnd         = PARAM_SEC_END,
};

/* 上电一次性保护：IDLE 复位误调时不再扫/写 Flash */
static uint8_t s_param_inited = 0U;

/*=============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  打印规范：不打印浮点——float 电压值拆为 [符号]整数.1位小数（0.1V 精度）
 */
static char s_volt_a[12], s_volt_b[12], s_volt_c[12];   /* Param_VoltFmt 输出缓冲 */

static void Param_VoltFmt(float v, char *pBuf, uint32_t bufSize)
{
    uint8_t neg = (v < 0.0f) ? 1U : 0U;
    uint32_t x10;

    if (neg) {
        v = -v;
    }
    x10 = (uint32_t)(v * 10.0f + 0.5f);
    rt_snprintf(pBuf, bufSize, "%s%lu.%01lu", (neg ? "-" : ""),
                (unsigned long)(x10 / 10U), (unsigned long)(x10 % 10U));
}

/**
 * @brief  设置默认值（Param_Init 未找到有效块时回调；默认宏见各设备 .h）
 */
static void Param_SetDefaults(void)
{
    (void)memset(&g_param_record, 0, sizeof(ParamRecord_t));
    g_param_record.head_magic = PARAM_MAGIC_HEAD;
    g_param_record.tail_magic = PARAM_MAGIC_TAIL;

    g_param_record.volt_over_th    = VOL_OVER_TH_DFT;
    g_param_record.volt_under_th   = VOL_UNDER_TH_DFT;
    g_param_record.volt_hyst       = VOL_HYST_DFT;
    g_param_record.volt_recover_ms = VOL_RECOVER_DELAY_MS_DFT;

    g_param_record.cur_over_th_ma = CUR_OVER_CUR_TH_MA_DFT;
    g_param_record.cur_window_ms  = (uint16_t)CUR_OVER_WINDOW_MS_DFT;
    g_param_record.reserved       = 0U;

    Param_VoltFmt(VOL_OVER_TH_DFT, s_volt_a, sizeof(s_volt_a));
    Param_VoltFmt(VOL_UNDER_TH_DFT, s_volt_b, sizeof(s_volt_b));
    Param_VoltFmt(VOL_HYST_DFT, s_volt_c, sizeof(s_volt_c));
    PARAM_PRINT("[PARAM] defaults set: over=%sV under=%sV hyst=%sV rec=%lums cur=%lumA win=%ums",
                s_volt_a, s_volt_b, s_volt_c,
                (unsigned long)VOL_RECOVER_DELAY_MS_DFT,
                (unsigned long)(CUR_OVER_CUR_TH_MA_DFT + 0.5f),
                (unsigned)CUR_OVER_WINDOW_MS_DFT);
}

/**
 * @brief  把 Flash 加载/默认值应用到各设备 RAM 配置（g_volt_cfg / g_cur_cfg）
 */
static void Param_ApplyToDevices(void)
{
    g_volt_cfg.over_th    = g_param_record.volt_over_th;
    g_volt_cfg.under_th   = g_param_record.volt_under_th;
    g_volt_cfg.hyst       = g_param_record.volt_hyst;
    g_volt_cfg.recover_ms = g_param_record.volt_recover_ms;

    g_cur_cfg.over_th_ma = g_param_record.cur_over_th_ma;
    g_cur_cfg.window_ms  = g_param_record.cur_window_ms;
}

/**
 * @brief  检查电机是否运行中（任一轴 MS_RUNNING 视为运行）
 */
static bool Param_IsMotorRunning(void)
{
    ArbData_t arb;
    for (uint8_t axis = 0U; axis < ARB_MAX_AXIS_NUM; axis++) {
        if (Arb_GetData(axis, &arb) == RT_EOK) {
            if (arb.state == MS_RUNNING) {
                return true;
            }
        }
    }
    return false;
}

/*=============================================================================
 * 外部接口
 *============================================================================*/

int32_t Dev_Param_Init(void)
{
    int32_t res;

    if (s_param_inited != 0U) {
        return PARAM_OK;
    }

    res = Param_Init(&s_param_config, &s_param_runtime, Param_SetDefaults);
    if (res == PARAM_OK) {
        Param_ApplyToDevices();
        Param_VoltFmt(g_param_record.volt_over_th, s_volt_a, sizeof(s_volt_a));
        Param_VoltFmt(g_param_record.volt_under_th, s_volt_b, sizeof(s_volt_b));
        Param_VoltFmt(g_param_record.volt_hyst, s_volt_c, sizeof(s_volt_c));
        PARAM_PRINT("[PARAM] loaded: over=%sV under=%sV hyst=%sV rec=%lums cur=%lumA win=%ums (seq=%lu)",
                    s_volt_a, s_volt_b, s_volt_c,
                    (unsigned long)g_param_record.volt_recover_ms,
                    (unsigned long)(g_param_record.cur_over_th_ma + 0.5f),
                    (unsigned)g_param_record.cur_window_ms,
                    (unsigned long)g_param_record.sequence_id);
    } else {
        MAIN_D("[PARAM] init FAILED res=%d (keep RAM defaults)", (int)res);
    }

    /* 快块 B（推杆行程）统一在此初始化：扫描加载，待 App_Model_Init 后 RodApply 恢复 */
    (void)Dev_Param_RodInit();

    s_param_inited = 1U;
    return res;
}

int32_t Dev_Param_Save(void)
{
    int32_t res;

    if (s_param_inited == 0U) {
        MAIN_D("[PARAM] save refused: not inited");
        return PARAM_ERR_NOT_RDY;
    }

    /* 安全检查：擦/写期间 BUS_HOLD 全系统停顿，电机运行时禁止 */
    if (Param_IsMotorRunning()) {
        MAIN_D("[PARAM] save refused: motor RUNNING (stop motor first)");
        return PARAM_ERR;
    }

    /* 从各设备 RAM 配置同步到记录 */
    g_param_record.volt_over_th    = g_volt_cfg.over_th;
    g_param_record.volt_under_th   = g_volt_cfg.under_th;
    g_param_record.volt_hyst       = g_volt_cfg.hyst;
    g_param_record.volt_recover_ms = g_volt_cfg.recover_ms;
    g_param_record.cur_over_th_ma  = g_cur_cfg.over_th_ma;
    g_param_record.cur_window_ms   = g_cur_cfg.window_ms;

    res = Param_Save(&s_param_config, &s_param_runtime);
    if (res != PARAM_OK) {
        MAIN_D("[PARAM] save FAILED res=%d", (int)res);
    }
    return res;
}

void Dev_Param_EraseAll(void)
{
    if (Param_IsMotorRunning()) {
        MAIN_D("[PARAM] erase refused: motor RUNNING");
        return;
    }
    Param_Debug_EraseAll(&s_param_config, &s_param_runtime, Param_SetDefaults);
    Param_ApplyToDevices();
    Dev_Param_RodEraseAll();
}

/*=============================================================================
 * Flash 存储测试（主循环 savelabel 触发，rtt debug 里改变量即可）
 *============================================================================*/

void Dev_Param_SaveTest(void)
{
    /* 修改其中一个值：过压阈值 +0.5V，[23.0, 27.0) 回绕，保证每组内容可预测且不同 */
    g_volt_cfg.over_th += 0.5f;
    if (g_volt_cfg.over_th >= 27.0f) {
        g_volt_cfg.over_th = 23.0f;
    }

    if (Dev_Param_Save() == PARAM_OK) {
        Param_VoltFmt(g_volt_cfg.over_th, s_volt_a, sizeof(s_volt_a));
        PARAM_PRINT("[PARAM] TEST saved: over_th=%sV seq=%lu addr=0x%08lX",
                    s_volt_a, (unsigned long)g_param_record.sequence_id,
                    (unsigned long)(s_param_runtime.curr_addr - sizeof(ParamRecord_t)));
    }
}

void Dev_Param_FillTest(void)
{
    uint32_t count = 0U;
    uint32_t remain;

    /* 连续保存，直到当前扇区剩余空间只够再存两组（之后再存将走擦除/换扇区路径）。
       500 组为保险上限，正常单扇区最多 8192/44 ≈ 186 组，条件本身不会跨扇区。 */
    while (((s_param_runtime.curr_sec + 1U) * PARAM_SECTOR_SIZE - s_param_runtime.curr_addr)
           > (2U * sizeof(ParamRecord_t))) {
        if (Dev_Param_Save() != PARAM_OK) {
            break;
        }
        count++;
        if (count >= 500U) {
            break;
        }
    }

    remain = (s_param_runtime.curr_sec + 1U) * PARAM_SECTOR_SIZE - s_param_runtime.curr_addr;
    PARAM_PRINT("[PARAM] FILL done: +%lu blocks, remain=%luB, next=0x%08lX (seq=%lu)",
                (unsigned long)count, (unsigned long)remain,
                (unsigned long)s_param_runtime.curr_addr,
                (unsigned long)g_param_record.sequence_id);
}

/*=============================================================================
 * MSH 调试命令
 *============================================================================*/
static void cmd_param_show(void)
{
    MAIN_D("[PARAM] rec: magic=0x%08lX seq=%lu erase=%lu",
           (unsigned long)g_param_record.head_magic,
           (unsigned long)g_param_record.sequence_id,
           (unsigned long)g_param_record.erase_count);
    Param_VoltFmt(g_volt_cfg.over_th, s_volt_a, sizeof(s_volt_a));
    Param_VoltFmt(g_volt_cfg.under_th, s_volt_b, sizeof(s_volt_b));
    Param_VoltFmt(g_volt_cfg.hyst, s_volt_c, sizeof(s_volt_c));
    MAIN_D("[PARAM] volt: over=%sV under=%sV hyst=%sV rec=%lums",
           s_volt_a, s_volt_b, s_volt_c, (unsigned long)g_volt_cfg.recover_ms);
    MAIN_D("[PARAM] cur: over=%lumA win=%ums",
           (unsigned long)(g_cur_cfg.over_th_ma + 0.5f), (unsigned)g_cur_cfg.window_ms);
    MAIN_D("[PARAM] runtime: sec=%u addr=0x%08lX saves=%lu last_res=%d",
           (unsigned)s_param_runtime.curr_sec, (unsigned long)s_param_runtime.curr_addr,
           (unsigned long)s_param_runtime.save_count, (int)s_param_runtime.last_res);
    Dev_Param_RodShow();
}
MSH_CMD_EXPORT_ALIAS(cmd_param_show, param_show, show stored param & runtime state);

static void cmd_param_save(void)
{
    int32_t res = Dev_Param_Save();
    if (res == PARAM_OK) {
        MAIN_D("[PARAM] save OK");
    }
    /* 失败原因已由 Dev_Param_Save 内打印 */
}
MSH_CMD_EXPORT_ALIAS(cmd_param_save, param_save, save g_volt_cfg/g_cur_cfg to flash);

static void cmd_param_erase(void)
{
    Dev_Param_EraseAll();
    MAIN_D("[PARAM] erased & re-inited with defaults");
}
MSH_CMD_EXPORT_ALIAS(cmd_param_erase, param_erase, erase all param sectors & reset defaults);

#endif /* DEV_ENABLE_PARAM */
