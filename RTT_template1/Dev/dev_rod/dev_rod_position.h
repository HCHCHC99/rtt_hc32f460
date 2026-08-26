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

/* 推杆位置模块（每轴一个实例） */
typedef struct {
    /* 当前位置与校准状态 */
    float                position_mm;      /* 当前位置 mm */
    PositionCalibState_t calib_state;      /* 是否已校准 */
    bool                 calib_pending;    /* 等待首次校准标志 */
    bool                 calib_allowed;    /* 允许校准标志 */
    bool                 in_calib_zone_min;/* 在下限校准区内（-tol~+tol） */
    bool                 in_calib_zone_max;/* 在上限校准区内（stroke-tol~stroke+tol） */
    bool                 min_limit_triggered;
    bool                 max_limit_triggered;

    /* 物理参数（配置） */
    float                stroke_mm;          /* 总行程 mm（1000） */
    float                reduction_ratio;    /* 减速比（10） */
    float                pulse_to_mm;        /* 每霍尔脉冲位移 mm = 导程/(减速比×每转脉冲) */
    float                calib_tolerance_mm; /* 校准容差 mm（3） */
} RodPosition_t;

void  RodPosition_Init(RodPosition_t *pos);
void  RodPosition_SetParams(RodPosition_t *pos, float stroke_mm, float reduction_ratio,
                            float hall_pulses_per_rev, float screw_lead_mm, float calib_tolerance_mm);
void  RodPosition_Update(RodPosition_t *pos, int32_t delta_pulses);
void  RodPosition_OnMinLimit(RodPosition_t *pos, bool triggered);
void  RodPosition_OnMaxLimit(RodPosition_t *pos, bool triggered);
float RodPosition_GetCurrent(const RodPosition_t *pos);
bool  RodPosition_IsCalibrated(const RodPosition_t *pos);
bool  RodPosition_IsCalibAllowed(const RodPosition_t *pos);
bool  RodPosition_IsInCalibZoneMin(const RodPosition_t *pos);
bool  RodPosition_IsInCalibZoneMax(const RodPosition_t *pos);

#endif /* __DEV_ROD_POSITION_H__ */
