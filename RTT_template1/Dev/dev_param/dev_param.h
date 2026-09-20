/**
 * @file    dev_param.h
 * @brief   应用参数管理（param_manager 双实例）：慢块 A（配置参数）+ 快块 B（推杆行程）
 * @note    - 慢块 A：g_volt_cfg / g_cur_cfg 配置 + 软限位校准窗口 + 停止裕量 + 推杆机械
 *            参数（stroke/ratio/pulses/lead），改动少，secStart=62 / secEnd=61
 *            （0x7C000 起逆序，2 扇区），84B 记录，97 次/擦/扇区；
 *          - 快块 B：推杆当前行程，欠压急停时保存停车位置，secStart=60 / secEnd=51（0x78000
 *            起逆序，10 扇区），24B 记录，341 次/擦/扇区；
 *          - Dev_Param_Init() 上电由 main 调一次（扫 Flash 加载或写默认值，内部统一初始化
 *            A+B），不走 registry IDLE 复位；
 *          - 上电恢复分两步：扫描加载（main 内，早于 App_Model_Init）→ 应用到推杆位置模块
 *            （App_Model_Init 末尾调 Dev_Param_RodApply）；
 *          - 慢块保存需在安全时机（电机停止），运行中 Dev_Param_Save 拒绝并打印。
 */
#ifndef __DEV_PARAM_H__
#define __DEV_PARAM_H__

#include <stdint.h>
#include "Dev/dev_rod/dev_rod_position.h"

/*=============================================================================
 * 慢块 A（配置参数）：60B 记录
 *=============================================================================*/

/* Flash 存储参数记录结构体（布局即存储布局，修改需谨慎；4 字节对齐） */
#pragma pack(4)
typedef struct {
    /* 头部信息 */
    uint32_t head_magic;        /* 头部魔数 (0x55AA55AA) */
    uint32_t sequence_id;       /* 序列号 (每次保存递增) */
    uint32_t erase_count;       /* Flash 擦写次数记录 */

    /* 母线电压配置 (VoltCfg_t) */
    float    volt_over_th;      /* 过压阈值 V */
    float    volt_under_th;     /* 欠压阈值 V */
    float    volt_hyst;         /* 迟滞回差 V */
    uint32_t volt_recover_ms;   /* 恢复延时 ms */

    /* 电流传感器配置 (CurCfg_t) */
    float    cur_over_th_ma;    /* 过流阈值 mA */
    uint16_t cur_window_ms;     /* 过流判定窗口 ms */
    uint16_t cur_block_ms;      /* 方向变化过流屏蔽 ms（停止→正/反转、换向浪涌期不判定） */
    uint8_t  hall_dir_seq;      /* 霍尔判向相序：0=标准(B沿采样A,A高=FWD) 1=反置（原 uint16 reserved 拆分，偏移/大小不变，旧记录 CRC 兼容） */
    uint8_t  motor_dir_seq;     /* 电机输出相序：0=FWD→伸出(PB8) 1=交换FWD/REV输出 */

    /* 软限位校准窗口 (RodPosition_t.calib_win_a/b/c/d)，语义见 dev_rod_position.h
       下限使能：a<b ? pos∈[a,b] : pos<=a   上限使能：c>d ? pos∈[d,c] : pos>=c */
    float    rod_calib_win_a;   /* 下限窗口下界 a */
    float    rod_calib_win_b;   /* 下限窗口上界 b */
    float    rod_calib_win_c;   /* 上限窗口下界 c */
    float    rod_calib_win_d;   /* 上限窗口上界 d */
    float    rod_stop_margin;   /* 停止裕量（°，位置停车：pos>=行程-裕量即合成 AT_MAX） */

    /* 推杆机械参数（转动机构模式，数值单位=度）：Dev_Param_RodApply 调
       RodPosition_SetParams 应用（覆盖 dev_model.c 的 DFT 初值） */
    float    rod_stroke_mm;       /* 总行程（°） */
    float    rod_reduction_ratio; /* 减速比：电机轴转 N 圈输出轴转 1 圈 */
    float    rod_hall_pulses;     /* 电机轴每转霍尔脉冲数 */
    float    rod_screw_lead;      /* 输出轴每圈角度（°，转动模式=360）：脉冲->角度换算分子，
                                     非取模换算——每脉冲角度 = lead/(ratio*pulses) */

    /* 尾部信息 */
    uint32_t checksum;          /* CRC32 校验和 */
    uint32_t tail_magic;        /* 尾部魔数 (0xAA44AA44) */
} ParamRecord_t;
#pragma pack()

/* 存储记录全局可见（dev_param_rod.c 恢复/保存链路直接读取） */
extern ParamRecord_t g_param_record;

/*=============================================================================
 * 快块 B（推杆行程）：24B 记录
 *============================================================================*/

/* 快块魔数（区别于慢块：扇区已隔离，区分魔数属零成本防御） */
#define PARAM_ROD_MAGIC_HEAD    0x66AA66AAU
#define PARAM_ROD_MAGIC_TAIL    0xAA66AA66U

/* 欠压急停保存行程前的固定延时 ms：等推杆刹车滑行稳定（欠压掉电余量实测 270ms） */
#define DEV_PARAM_ROD_SAVE_DELAY_MS  20U

/* 快块记录：头尾 20B（引擎必需）+ 行程 float 4B = 24B，单扇区 8192/24 ≈ 341 次/擦 */
#pragma pack(4)
typedef struct {
    uint32_t head_magic;        /* 头部魔数 (0x66AA66AA) */
    uint32_t sequence_id;       /* 序列号 (每次保存递增) */
    uint32_t erase_count;       /* Flash 擦写次数记录 */
    float    position_mm;       /* 推杆当前行程 mm（可含容差负值） */
    uint32_t checksum;          /* CRC32 校验和 */
    uint32_t tail_magic;        /* 尾部魔数 (0xAA66AA66) */
} ParamStrokeRecord_t;
#pragma pack()

/*=============================================================================
 * 存储默认值宏（单点）：A 块全部默认值 + B 块行程默认值。
 * 全片擦除后首次上电由 Param_SetDefaults / Rod_SetDefaults 加载；
 * 其它模块 .c 一律 include 本头文件取宏，禁止再散落定义。
 *=============================================================================*/
#define VOL_OVER_TH_DFT             (26.0f)   /* 过压阈值 V */
#define VOL_UNDER_TH_DFT            (20.0f)   /* 欠压阈值 V（必须明显高于 VOL_OFFSET 1.2V 偏置，否则无 24V 时 SeenValid 误判） */
#define VOL_HYST_DFT                (1.0f)    /* 迟滞回差 V */
#define VOL_RECOVER_DELAY_MS_DFT    (500U)    /* 电压恢复延时 ms */

#define CUR_OVER_CUR_TH_MA_DFT      (1000.0f) /* 过流阈值 mA（直线推杆堵转判定，1.0A） */
#define CUR_OVER_WINDOW_MS_DFT      (30U)     /* 过流判定窗口 ms（连续 30ms 超阈值才判过流） */
#define CUR_BLOCK_MS_DFT            (30U)     /* 方向变化过流屏蔽 ms（停止→正/反转、换向浪涌期不判定，0=不屏蔽） */

/* 软限位校准窗口默认（直线推杆模式，单位 mm；窄窗形式，语义见 dev_rod_position.h）：
   a=b=1：pos<=1mm 允许 REV 过流校准（校准成功置 0mm）
   c=d=24：pos>=24mm 允许 FWD 过流校准（校准成功置 25mm） */
#define ROD_CALIB_WIN_A_DFT         (1.0f)    /* 下限窗口下界 a（mm）：a==b 时 pos<=a 判下限窗 */
#define ROD_CALIB_WIN_B_DFT         (1.0f)    /* 下限窗口上界 b（mm） */
#define ROD_CALIB_WIN_C_DFT         (24.0f)   /* 上限窗口下界 c（mm）：c==d 时 pos>=c 判上限窗 */
#define ROD_CALIB_WIN_D_DFT         (24.0f)   /* 上限窗口上界 d（mm） */
#define ROD_STOP_MARGIN_DFT         (0.5f)    /* 停止裕量（mm，位置停车：pos>=行程-裕量=24.5mm） */

/* 推杆机械参数默认（直线推杆模式，数值单位=mm；A 块存储，Dev_Param_RodApply 应用）：
   每脉冲位移 = lead/(ratio*pulses) = 3/(30.54*12) ≈ 0.008194mm/脉冲；行程 25mm ≈ 3054 脉冲
   双霍尔三对极：脉冲/电机转 = 3对极 × 2路霍尔 × 双沿 = 12（与 MOTOR_HALL 自动口径一致） */
#define ROD_STROKE_DFT              (25.0f)   /* 总行程（mm） */
#define ROD_REDUCTION_RATIO_DFT     (30.54f)  /* 减速比：电机轴转 N 圈丝杆转 1 圈 */
#define ROD_HALL_PULSES_DFT         (12.0f)   /* 电机轴每转霍尔脉冲数（双霍尔三对极双沿） */
#define ROD_SCREW_LEAD_DFT          (3.0f)    /* 丝杆导程（mm/圈）：换算分子，非取模 */

/* 方向相序默认（0=标准；板上实测相反时用 msh dir_set 改并保存） */
#define HALL_DIR_SEQ_DFT            (0U)      /* 霍尔判向：0=标准(B沿采样A,A高=FWD) 1=反置 */
#define MOTOR_DIR_SEQ_DFT           (0U)      /* 电机输出：0=FWD→PB8伸出 1=交换FWD/REV */

#define PARAM_ROD_STROKE_DFT        (0.0f)    /* 快块 B 行程默认 mm（0=无有效块走首次校准） */

/*=============================================================================
 * 外部接口
 *============================================================================*/

/**
 * @brief  上电初始化：扫 Flash 加载参数并应用到 g_volt_cfg / g_cur_cfg（慢块 A），
 *         并初始化快块 B（推杆行程加载到内部记录，待 Dev_Param_RodApply 应用）
 * @return PARAM_OK 成功（含首次写默认值）；其他为 PARAM_ERR_*
 * @note   main 调一次；IDLE 重入不得重复调用（内部有一次性保护）
 */
int32_t Dev_Param_Init(void);

/**
 * @brief  保存参数：从 g_volt_cfg / g_cur_cfg 同步到记录并写入 Flash（慢块 A）
 * @return PARAM_OK 成功；PARAM_ERR 失败（含电机运行中拒绝）
 */
int32_t Dev_Param_Save(void);

/**
 * @brief  float mm -> "[符号]整数.1位" 字符串（4 缓冲轮转；仅同一条打印内连用，勿跨打印保指针）
 */
const char *Dev_Param_MmFmtA(float v);

/**
 * @brief  调试：擦除 A+B 全部参数扇区并用默认值重新初始化（param_erase 命令用）
 */
void Dev_Param_EraseAll(void);

/**
 * @brief  测试：修改一个业务值（过压阈值 +0.5V 回绕）并保存一组，打印结果
 * @note   主循环 savelabel==1 触发；电机运行中会被 Dev_Param_Save 拒绝
 */
void Dev_Param_SaveTest(void);

/**
 * @brief  测试：连续保存，填满慢块当前扇区直到剩余空间只够再存两组
 * @note   主循环 savelabel==2 触发
 */
void Dev_Param_FillTest(void);

/*---------------------------------------------------------------------------
 * 快块 B（推杆行程）接口
 *---------------------------------------------------------------------------*/

/**
 * @brief  快块 B 初始化：扫 Flash 加载行程（由 Dev_Param_Init 内部调用，勿直接调）
 * @return PARAM_OK 成功；PARAM_ERR 失败
 * @note   加载结果暂存内部记录，待 App_Model_Init 末尾 Dev_Param_RodApply 应用
 */
int32_t Dev_Param_RodInit(void);

/**
 * @brief  把快块 B 加载的行程恢复到推杆位置模块（写 position_mm 并置 CALIBRATED）
 * @param  pos 推杆位置模块实例指针（同时被记录为欠压保存的取值来源）
 * @note   App_Model_Init 末尾（RodPosition_Init/SetParams 之后）调用；上电无有效块
 *         （首次写默认值）时跳过恢复，保持 NOT_CALIBRATED 走原校准流程
 */
void Dev_Param_RodApply(RodPosition_t *pos);

/**
 * @brief  欠压保存请求（欠压边沿调用，非阻塞、幂等；欠压边沿仅存一次）
 * @note   由 Sys_State_Dispatch 欠压分支调用；实际保存在 Rod_Task 的 PollSave 中
 *         延时 ≈20ms（等刹车滑行稳定）后执行
 */
void Dev_Param_RodSaveRequest(void);

/**
 * @brief  欠压保存状态机驱动（Rod_Task 10ms 周期调用，非阻塞）
 * @note   有保存请求时计数 2 个 tick（≈20ms）后取当前位置写入 Flash 快块
 */
void Dev_Param_RodPollSave(void);

/**
 * @brief  保存推杆当前行程到快块 B（纯写 ~0.3ms，扇区满含擦 ~8ms）
 * @param  mm 当前行程 mm
 * @return PARAM_OK 成功；PARAM_ERR 失败
 */
int32_t Dev_Param_RodSave(float mm);

/**
 * @brief  读取快块 B 的行程值（上电加载/最近保存的值）
 * @param  pmm 输出行程 mm
 * @return PARAM_OK 成功；PARAM_ERR_NOT_RDY 未初始化
 */
int32_t Dev_Param_RodGet(float *pmm);

/**
 * @brief  测试：连续保存指定行程，填满快块当前扇区直到剩余空间只够再存两组
 *         （主循环 savelabel==4 触发；savelabel==3 直接在 main 组合 RodSave+取值）
 */
void Dev_Param_RodFillTest(float mm);

/**
 * @brief  打印快块 B 记录与 runtime 状态（param_show 命令用）
 */
void Dev_Param_RodShow(void);

/**
 * @brief  调试：擦除快块 B 全部扇区并重写默认值（行程归零，不改动当前 RAM 位置）
 */
void Dev_Param_RodEraseAll(void);

#endif /* __DEV_PARAM_H__ */
