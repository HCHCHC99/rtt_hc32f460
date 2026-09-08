/**
 * @file    dev_rod_position.h
 * @brief   推杆位置模块：霍尔脉冲→mm、校准管理（与 dev_rod_state 组合，挂 Axis_t）
 */
#ifndef __DEV_ROD_POSITION_H__
#define __DEV_ROD_POSITION_H__

#include <stdint.h>
#include <stdbool.h>

/* 位置校准状态 */
typedef enum {
    POSITION_NOT_CALIBRATED = 0,   /* 未建立绝对位置基准（上电初始态） */
    POSITION_CALIBRATED,           /* 已建立绝对位置基准 */
} PositionCalibState_t;

/* 软限位校准窗口默认值（上电无有效 flash A 块时 Init 使用；实际值以 A 块加载、
   Dev_Param_RodApply 写入为准）。默认 998 依赖默认行程 1000mm，行程变更时同步修改。 */
#define ROD_CALIB_WIN_A_DFT     (2.0f)    /* 下限窗口下界 a：a<b 时使能区间 [a,b] */
#define ROD_CALIB_WIN_B_DFT     (2.0f)    /* 下限窗口上界 b：a==b（含 a>b 宽容）时使能 pos<=a（半开） */
#define ROD_CALIB_WIN_C_DFT     (998.0f)  /* 上限窗口下界 c：c==d（含 c<d 宽容）时使能 pos>=c（半开） */
#define ROD_CALIB_WIN_D_DFT     (998.0f)  /* 上限窗口上界 d：c>d 时使能区间 [d,c] */

/* 推杆位置模块（每轴一个实例） */
typedef struct {
    /* 当前位置与校准状态 */
    float                position_mm;      /* 当前位置 mm */
    PositionCalibState_t calib_state;      /* 是否已校准 */
    bool                 calib_pending;    /* 等待首次校准标志 */
    bool                 calib_allowed;    /* 允许校准标志（下/上限窗口任一命中） */
    bool                 in_calib_zone_min;/* 在下限校准使能窗口内（窗口参数 calib_win_a/b） */
    bool                 in_calib_zone_max;/* 在上限校准使能窗口内（窗口参数 calib_win_c/d） */
    bool                 min_limit_triggered;
    bool                 max_limit_triggered;

    /* 物理参数（配置） */
    float                stroke_mm;          /* 总行程 mm（1000） */
    float                reduction_ratio;    /* 减速比（10） */
    float                pulse_to_mm;        /* 每霍尔脉冲位移 mm = 导程/(减速比×每转脉冲) */
    float                calib_tolerance_mm; /* 位置钳位 margin mm（3，不再用于校准窗口） */

    /* 软限位校准窗口（flash A 块加载，RodApply 写入；语义见文件顶部宏注释）
       下限使能：a<b ? pos∈[a,b] : pos<=a   上限使能：c>d ? pos∈[d,c] : pos>=c */
    float                calib_win_a;
    float                calib_win_b;
    float                calib_win_c;
    float                calib_win_d;
} RodPosition_t;

void  RodPosition_Init(RodPosition_t *pos);
void  RodPosition_SetParams(RodPosition_t *pos, float stroke_mm, float reduction_ratio,
                            float hall_pulses_per_rev, float screw_lead_mm, float calib_tolerance_mm);
void  RodPosition_Update(RodPosition_t *pos, int32_t delta_pulses);
bool  RodPosition_OnMinLimit(RodPosition_t *pos, bool triggered);   /* 返回 true=已重置为 0 */
bool  RodPosition_OnMaxLimit(RodPosition_t *pos, bool triggered);   /* 返回 true=已重置为行程 */
float RodPosition_GetCurrent(const RodPosition_t *pos);
bool  RodPosition_IsCalibrated(const RodPosition_t *pos);
bool  RodPosition_IsCalibAllowed(const RodPosition_t *pos);
bool  RodPosition_IsInCalibZoneMin(const RodPosition_t *pos);
bool  RodPosition_IsInCalibZoneMax(const RodPosition_t *pos);

#endif /* __DEV_ROD_POSITION_H__ */
