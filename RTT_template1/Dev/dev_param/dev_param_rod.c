/**
 * @file    dev_param_rod.c
 * @brief   快块 B（推杆行程掉电保存）：param_manager 第二实例
 * @note    - 存储区：secStart=60 / secEnd=51（0x78000 起逆序，10 扇区），24B 记录，
 *            单扇区 8192/24 ≈ 341 次/擦，MAX_ERASE_LIFE=10000 次擦/扇区；
 *          - 保存链路自包含：欠压边沿调 Dev_Param_RodSaveRequest() 置请求（幂等），
 *            Rod_Task 10ms 调 Dev_Param_RodPollSave() 驱动状态机：计 2 个 tick（≈20ms，
 *            等刹车滑行稳定）后取当前位置写入 Flash；欠压边沿仅存一次，零线程阻塞；
 *          - 上电恢复分两步：Dev_Param_RodInit（main 内扫描加载）→ Dev_Param_RodApply
 *            （App_Model_Init 末尾应用并注入位置实例引用，供 PollSave 取值）。
 *          - 本模块不依赖 mySystem：位置实例经 RodApply 注入，测试钩子传参。
 */
#include "dev_param.h"
#include "Utils/param_manager.h"
#include "applications/rtt_manager.h"
#include "rtthread.h"
#include "Dev/dev_config.h"
#include <stddef.h>
#include <string.h>

#if DEV_ENABLE_PARAM

/* 快块存储区：0x78000 起逆序使用（sec 60 → 51，10 扇区） */
#define ROD_SEC_START           (60U)
#define ROD_SEC_END             (51U)

/* 欠压保存延时 tick 数：Rod_Task 10ms 周期 × 2 ≈ 20ms（等刹车滑行稳定；
   欠压掉电余量实测 270ms，flash 纯写 ~0.3ms、扇区满含擦 ~8ms，均在余量内） */
#define ROD_SAVE_WAIT_TICKS     (2U)

/* 编译期断言：记录尺寸不超引擎缓冲上限、4 字节对齐 */
typedef char rod_rec_size_check[(sizeof(ParamStrokeRecord_t) <= PARAM_MAX_RECORD_SIZE) ? 1 : -1];
typedef char rod_rec_align_check[((sizeof(ParamStrokeRecord_t) % 4U) == 0U) ? 1 : -1];

/* Flash 存储记录 + 引擎实例 */
static ParamStrokeRecord_t s_rod_record;
static Param_Runtime_t s_rod_runtime;

static const Param_Config_t s_rod_config = {
    .pParamBuf      = &s_rod_record,
    .paramSize      = sizeof(ParamStrokeRecord_t),
    .magicHead      = PARAM_ROD_MAGIC_HEAD,
    .magicTail      = PARAM_ROD_MAGIC_TAIL,
    .checksumOffset = offsetof(ParamStrokeRecord_t, checksum),
    .seqOffset      = offsetof(ParamStrokeRecord_t, sequence_id),
    .eraseCntOffset = offsetof(ParamStrokeRecord_t, erase_count),
    .secStart       = ROD_SEC_START,
    .secEnd         = ROD_SEC_END,
};

/* 上电一次性保护 */
static uint8_t s_rod_inited = 0U;
/* 上电扫描是否走了"写默认值"路径：为 1 时 RodApply 跳过恢复（无有效基准可恢复） */
static uint8_t s_rod_defaults_written = 0U;

/* 位置实例引用（RodApply 注入；PollSave 取当前位置用，本模块不摸 mySystem） */
static RodPosition_t *s_rod_pos_ptr = NULL;

/* 欠压保存请求（Request 置位）+ tick 计数（PollSave 驱动） */
static volatile uint8_t s_rod_save_pending = 0U;
static uint8_t s_rod_save_wait = 0U;

/*=============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  打印规范：不打印浮点——float mm 拆为 [符号]整数.1位小数（0.1mm 精度）
 */
static char s_mm_a[12];

static void Rod_MmFmt(float v, char *pBuf, uint32_t bufSize)
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
 * @brief  设置默认值（上电无有效块时回调：行程归零，恢复时跳过保持原校准流程）
 */
static void Rod_SetDefaults(void)
{
    (void)memset(&s_rod_record, 0, sizeof(ParamStrokeRecord_t));
    s_rod_record.head_magic = PARAM_ROD_MAGIC_HEAD;
    s_rod_record.tail_magic = PARAM_ROD_MAGIC_TAIL;
    s_rod_record.position_mm = 0.0f;

    s_rod_defaults_written = 1U;
    PARAM_PRINT("[RODP] defaults set (stroke=0.0mm)");
}

/*=============================================================================
 * 外部接口
 *============================================================================*/

int32_t Dev_Param_RodInit(void)
{
    int32_t res;

    if (s_rod_inited != 0U) {
        return PARAM_OK;
    }

    s_rod_defaults_written = 0U;
    res = Param_Init(&s_rod_config, &s_rod_runtime, Rod_SetDefaults);
    if (res == PARAM_OK) {
        if (s_rod_defaults_written == 0U) {
            Rod_MmFmt(s_rod_record.position_mm, s_mm_a, sizeof(s_mm_a));
            PARAM_PRINT("[RODP] loaded: stroke=%smm (seq=%lu)",
                        s_mm_a, (unsigned long)s_rod_record.sequence_id);
        }
    } else {
        MAIN_D("[RODP] init FAILED res=%d (keep RAM defaults)", (int)res);
    }

    s_rod_inited = 1U;
    return res;
}

void Dev_Param_RodApply(RodPosition_t *pos)
{
    if ((s_rod_inited == 0U) || (pos == NULL)) {
        return;
    }

    /* 注入位置实例引用（欠压保存时 PollSave 取当前位置用；恢复与否都要注入） */
    s_rod_pos_ptr = pos;

    /* 软限位校准窗口（A 块数据）：无论 B 块有效与否都写入——B 无效走首次校准流程
       时窗口参数同样要就位（判定在 sys_sm 过流分支） */
    pos->calib_win_a = g_param_record.rod_calib_win_a;
    pos->calib_win_b = g_param_record.rod_calib_win_b;
    pos->calib_win_c = g_param_record.rod_calib_win_c;
    pos->calib_win_d = g_param_record.rod_calib_win_d;
    PARAM_PRINT("[RODP] calibWin a=%s b=%s c=%s d=%s",
                Dev_Param_MmFmtA(pos->calib_win_a), Dev_Param_MmFmtA(pos->calib_win_b),
                Dev_Param_MmFmtA(pos->calib_win_c), Dev_Param_MmFmtA(pos->calib_win_d));

    /* 上电无有效块（默认值路径）：不接管校准，保持原校准流程 */
    if (s_rod_defaults_written != 0U) {
        PARAM_PRINT("[RODP] apply skipped (no valid block)");
        return;
    }

    /* 恢复位置基准：写入加载的行程并置已校准，霍尔增量在此基准上继续累加 */
    pos->position_mm = s_rod_record.position_mm;
    pos->calib_state = POSITION_CALIBRATED;
    Rod_MmFmt(pos->position_mm, s_mm_a, sizeof(s_mm_a));
    PARAM_PRINT("[RODP] restored stroke=%smm (CALIBRATED)", s_mm_a);
}

void Dev_Param_RodSaveRequest(void)
{
    /* 幂等：欠压持续期间重复事件不重复置位（欠压边沿仅存一次） */
    if (s_rod_save_pending == 0U) {
        s_rod_save_pending = 1U;
        s_rod_save_wait = 0U;
    }
}

void Dev_Param_RodPollSave(void)
{
    if ((s_rod_save_pending == 0U) || (s_rod_inited == 0U)) {
        return;
    }

    /* tick 计数延时（Rod_Task 10ms 驱动）：等急停刹车滑行稳定，非阻塞 */
    s_rod_save_wait++;
    if (s_rod_save_wait < ROD_SAVE_WAIT_TICKS) {
        return;
    }

    s_rod_save_pending = 0U;
    s_rod_save_wait = 0U;

    if (s_rod_pos_ptr == NULL) {
        MAIN_D("[RODP] poll save FAILED: no position instance");
        return;
    }
    (void)Dev_Param_RodSave(RodPosition_GetCurrent(s_rod_pos_ptr));
}

int32_t Dev_Param_RodSave(float mm)
{
    int32_t res;

    if (s_rod_inited == 0U) {
        MAIN_D("[RODP] save refused: not inited");
        return PARAM_ERR_NOT_RDY;
    }

    s_rod_record.position_mm = mm;
    res = Param_Save(&s_rod_config, &s_rod_runtime);
    if (res == PARAM_OK) {
        Rod_MmFmt(mm, s_mm_a, sizeof(s_mm_a));
        PARAM_PRINT("[RODP] saved: stroke=%smm seq=%lu addr=0x%08lX",
                    s_mm_a, (unsigned long)s_rod_record.sequence_id,
                    (unsigned long)(s_rod_runtime.curr_addr - sizeof(ParamStrokeRecord_t)));
    } else {
        MAIN_D("[RODP] save FAILED res=%d", (int)res);
    }
    return res;
}

int32_t Dev_Param_RodGet(float *pmm)
{
    if (s_rod_inited == 0U) {
        return PARAM_ERR_NOT_RDY;
    }
    if (pmm == NULL) {
        return PARAM_ERR_INVD_PARAM;
    }
    *pmm = s_rod_record.position_mm;
    return PARAM_OK;
}

void Dev_Param_RodFillTest(float mm)
{
    uint32_t count = 0U;
    uint32_t remain;

    /* 连续保存指定行程，直到当前扇区剩余空间只够再存两组（之后再存将走擦除/换扇区路径）。
       1000 组为保险上限，正常单扇区最多 8192/24 ≈ 341 组，条件本身不会跨扇区。 */
    while (((s_rod_runtime.curr_sec + 1U) * PARAM_SECTOR_SIZE - s_rod_runtime.curr_addr)
           > (2U * sizeof(ParamStrokeRecord_t))) {
        if (Dev_Param_RodSave(mm) != PARAM_OK) {
            break;
        }
        count++;
        if (count >= 1000U) {
            break;
        }
    }

    remain = (s_rod_runtime.curr_sec + 1U) * PARAM_SECTOR_SIZE - s_rod_runtime.curr_addr;
    PARAM_PRINT("[RODP] FILL done: +%lu blocks, remain=%luB, next=0x%08lX (seq=%lu)",
                (unsigned long)count, (unsigned long)remain,
                (unsigned long)s_rod_runtime.curr_addr,
                (unsigned long)s_rod_record.sequence_id);
}

void Dev_Param_RodShow(void)
{
    Rod_MmFmt(s_rod_record.position_mm, s_mm_a, sizeof(s_mm_a));
    MAIN_D("[RODP] rec: magic=0x%08lX seq=%lu erase=%lu stroke=%smm",
           (unsigned long)s_rod_record.head_magic,
           (unsigned long)s_rod_record.sequence_id,
           (unsigned long)s_rod_record.erase_count,
           s_mm_a);
    MAIN_D("[RODP] runtime: sec=%u addr=0x%08lX saves=%lu last_res=%d",
           (unsigned)s_rod_runtime.curr_sec, (unsigned long)s_rod_runtime.curr_addr,
           (unsigned long)s_rod_runtime.save_count, (int)s_rod_runtime.last_res);
}

void Dev_Param_RodEraseAll(void)
{
    s_rod_defaults_written = 0U;
    Param_Debug_EraseAll(&s_rod_config, &s_rod_runtime, Rod_SetDefaults);
    /* 擦除后记录为默认值（0.0mm），不改动当前 RAM 位置，下次上电走原校准流程 */
}

#endif /* DEV_ENABLE_PARAM */
