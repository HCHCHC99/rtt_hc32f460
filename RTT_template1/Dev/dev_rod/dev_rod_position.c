/**
 * @file    dev_rod_position.c
 * @brief   推杆位置模块实现：脉冲累加、限位校准、校准区/允许标志管理
 */
#include "dev_rod_position.h"

void RodPosition_Init(RodPosition_t *pos)
{
    pos->position_mm       = 0.0f;
    pos->calib_state       = POSITION_NOT_CALIBRATED;
    pos->calib_pending     = true;
    pos->calib_allowed     = true;   /* 未校准：允许首次校准 */
    pos->in_calib_zone_min = false;
    pos->in_calib_zone_max = false;
    pos->min_limit_triggered = false;
    pos->max_limit_triggered = false;
    pos->stroke_mm         = 0.0f;
    pos->reduction_ratio   = 1.0f;
    pos->pulse_to_mm       = 0.0f;
    pos->calib_tolerance_mm = 0.0f;
}

void RodPosition_SetParams(RodPosition_t *pos, float stroke_mm, float reduction_ratio,
                           float hall_pulses_per_rev, float screw_lead_mm, float calib_tolerance_mm)
{
    pos->stroke_mm          = stroke_mm;
    pos->reduction_ratio    = reduction_ratio;
    pos->pulse_to_mm        = screw_lead_mm / (reduction_ratio * hall_pulses_per_rev);
    pos->calib_tolerance_mm = calib_tolerance_mm;
}

void RodPosition_Update(RodPosition_t *pos, int32_t delta_pulses)
{
    /* 1. 脉冲增量 -> 机械位移增量（mm） */
    float delta_mm = (float)delta_pulses * pos->pulse_to_mm;
    pos->position_mm += delta_mm;

    /* 2. 限位保护：防止累计误差溢出校准区判断范围 */
    if (pos->position_mm > pos->stroke_mm + pos->calib_tolerance_mm * 2.0f) {
        pos->position_mm = pos->stroke_mm + pos->calib_tolerance_mm * 2.0f;
    }
    if (pos->position_mm < -pos->calib_tolerance_mm * 2.0f) {
        pos->position_mm = -pos->calib_tolerance_mm * 2.0f;
    }

    /* 3. 更新校准区标志 */
    pos->in_calib_zone_min = (pos->position_mm >= -pos->calib_tolerance_mm) &&
                             (pos->position_mm <=  pos->calib_tolerance_mm);
    pos->in_calib_zone_max = (pos->position_mm >= pos->stroke_mm - pos->calib_tolerance_mm) &&
                             (pos->position_mm <= pos->stroke_mm + pos->calib_tolerance_mm);

    /* 4. 动态更新校准允许标志 */
    if (pos->calib_state == POSITION_NOT_CALIBRATED) {
        pos->calib_allowed = true;   /* 首次校准：始终允许 */
    } else {
        pos->calib_allowed = pos->in_calib_zone_min || pos->in_calib_zone_max;
    }
}

void RodPosition_OnMinLimit(RodPosition_t *pos, bool triggered)
{
    pos->min_limit_triggered = triggered;
    /* 下限位触发且允许校准 -> 重置位置为 0 */
    if (triggered && pos->calib_allowed) {
        pos->position_mm = 0.0f;
        pos->calib_state = POSITION_CALIBRATED;
        pos->calib_pending = false;
    }
}

void RodPosition_OnMaxLimit(RodPosition_t *pos, bool triggered)
{
    pos->max_limit_triggered = triggered;
    /* 上限位触发且允许校准 -> 重置位置为总行程 */
    if (triggered && pos->calib_allowed) {
        pos->position_mm = pos->stroke_mm;
        pos->calib_state = POSITION_CALIBRATED;
        pos->calib_pending = false;
    }
}

float RodPosition_GetCurrent(const RodPosition_t *pos)      { return pos->position_mm; }
bool  RodPosition_IsCalibrated(const RodPosition_t *pos)    { return (pos->calib_state == POSITION_CALIBRATED); }
bool  RodPosition_IsCalibAllowed(const RodPosition_t *pos)  { return pos->calib_allowed; }
bool  RodPosition_IsInCalibZoneMin(const RodPosition_t *pos){ return pos->in_calib_zone_min; }
bool  RodPosition_IsInCalibZoneMax(const RodPosition_t *pos){ return pos->in_calib_zone_max; }

/* EOF */
