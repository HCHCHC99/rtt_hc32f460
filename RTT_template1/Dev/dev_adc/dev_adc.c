/**
 * @file    dev_adc.c
 * @brief   ADC 设备（Dev 层）：通道配置表 + 接口表调用（注册由 Dev_RegisterAll 集中管理）
 * @note    底层驱动通过 dev_adc_ops 注入，Dev 层不依赖具体芯片；
 *          电压 CH6 / 电流 CH5 同一 SEQ_A（时间对齐）；
 *          采样由 TMR0_1 硬件触发 500us（2kHz），EOCA 中断维护环形缓冲 + 滑动平均窗口；
 *          1ms 检测 ISR 经 Dev_Adc_GetMean 读 10ms 滑动均值。
 */
#include "dev_adc.h"
#include "dev_adc_ops.h"
#include <stddef.h>

/* 全局接口表指针（由 Dev_Adc_Bind 注入） */
static const struct dev_adc_ops *g_adc_ops = NULL;

/* 通道配置表（芯片无关：只含换算；端口/引脚在 HC32 实现层映射） */
static const dev_adc_ch_cfg_t s_astcChTable[] = {
    { 6,      16.0f,      0.0f,      false },   /* CH6 电压：PA6，150k:10k 分压，gain=16，满量程 52.8V */
    { 5,   1.0f,        0.0f,        false },   /* CH5 电流通道：ADC 层仅出电压 V，差分放大器 V→mA 换算在 dev_cur_sensor */
};


void Dev_Adc_Bind(const struct dev_adc_ops *ops)
{
    g_adc_ops = ops;
}


void Dev_Adc_Init(void)
{
    if (g_adc_ops != NULL && g_adc_ops->init != NULL) {
        (void)g_adc_ops->init(s_astcChTable,
                              (uint8_t)(sizeof(s_astcChTable) / sizeof(s_astcChTable[0])));
    }
}

void Dev_Adc_Start(void)
{
    if (g_adc_ops != NULL && g_adc_ops->start != NULL) {
        g_adc_ops->start();
    }
}

void Dev_Adc_Stop(void)
{
    if (g_adc_ops != NULL && g_adc_ops->stop != NULL) {
        g_adc_ops->stop();
    }
}

int Dev_Adc_GetLatest(float *pfVolt, float *pfCurr)
{
    if (pfVolt == NULL || pfCurr == NULL ||
        g_adc_ops == NULL || g_adc_ops->get_latest == NULL) {
        return -1;
    }
    (void)g_adc_ops->get_latest(0, pfVolt);
    (void)g_adc_ops->get_latest(1, pfCurr);
    return 0;
}

uint16_t Dev_Adc_ReadRing(uint8_t id, float *pfBuf, uint16_t u16Max)
{
    if (pfBuf == NULL || u16Max == 0U ||
        g_adc_ops == NULL || g_adc_ops->read_ring == NULL) {
        return 0U;
    }
    return g_adc_ops->read_ring(id, pfBuf, u16Max);
}

int Dev_Adc_GetRaw(uint8_t id, uint16_t *pu16Raw)
{
    if (pu16Raw == NULL || g_adc_ops == NULL || g_adc_ops->get_raw == NULL) {
        return -1;
    }
    return g_adc_ops->get_raw(id, pu16Raw);
}

int Dev_Adc_GetMean(uint8_t id, float *pfVal)
{
    if (pfVal == NULL || g_adc_ops == NULL || g_adc_ops->get_mean == NULL) {
        return -1;
    }
    return g_adc_ops->get_mean(id, pfVal);
}

/* EOF */







