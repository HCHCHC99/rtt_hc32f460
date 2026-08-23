/**
 * @file    adc_drv.h
 * @brief   ADC 硬件驱动（HC32F460）— 多实例 + EOCA 中断 + 换算写环形缓冲
 * @note    基于 HB_chuchai v6.0.3 Adc.c/h 裁剪移植（去 DMA；阶段一软件触发）
 */
#ifndef __ADC_DRV_H__
#define __ADC_DRV_H__

#include "hc32_ll.h"
#include <stdint.h>
#include <stdbool.h>

/* 采样间隔宏（Task 9 硬件触发时使用；软件触发由 Dev_Adc_Task 周期调用） */
#define ADC_SAMPLE_INTERVAL_US      (50U)   /* 50us = 20kHz */

/* 环形缓冲容量 */
#ifndef ADC_DRV_RING_SIZE
#define ADC_DRV_RING_SIZE           (256U)
#endif

/* 调试翻转引脚（示波器验证采样间隔；注释掉则关闭） */
/* #define ADC_DRV_DEBUG_TOGGLE_ENABLE */
#ifdef ADC_DRV_DEBUG_TOGGLE_ENABLE
#define ADC_DRV_TOGGLE_PORT         (GPIO_PORT_A)
#define ADC_DRV_TOGGLE_PIN          (GPIO_PIN_07)
#endif

/* 回调：中断里每通道原始值 */
typedef void (*AdcDrvCallback_t)(uint16_t u16AdcValue, uint8_t u8Channel);

/* 通道配置 */
typedef struct {
    uint8_t             u8Channel;      /* ADC_CH4 / ADC_CH5 */
    uint8_t             u8Port;
    uint16_t            u16Pin;
    float               fGain;          /* 换算增益 */
    float               fOffset;        /* 换算偏移 */
    bool                bAbs;           /* 取绝对值（电流通道 true） */
    AdcDrvCallback_t    pfnCallback;
} adc_drv_ch_t;

/* 实例（对应一个物理采样点） */
typedef struct {
    uint8_t             u8Id;
    adc_drv_ch_t        stcCh;
    float               afRing[ADC_DRV_RING_SIZE];   /* 换算后工程值环形缓冲 */
    volatile uint16_t   u16WriteIdx;
    volatile uint16_t   u16Count;
    float               fLatest;        /* 最近一次换算值 */
    uint16_t            u16LatestRaw;
    uint32_t            u32SampleCount;
} adc_drv_inst_t;

/* 接口 */
int  AdcDrv_Init(const adc_drv_ch_t *pstcChTable, uint8_t u8ChNum, adc_drv_inst_t *pstcInsts);
void AdcDrv_Start(void);            /* 硬件触发：启动 TMR0（Task 9 后启用） */
void AdcDrv_SoftwareTrigger(void);  /* 软件触发：ADC_Start 单次（阶段一使用） */
void AdcDrv_Stop(void);
uint16_t AdcDrv_GetLatestRaw(uint8_t u8Id);
float    AdcDrv_GetLatest(uint8_t u8Id);
uint16_t AdcDrv_ReadRing(uint8_t u8Id, float *pfBuf, uint16_t u16Max);
void     AdcDrv_ADC1_IRQHandler(void);   /* EOCA 中断入口（DDL 分发） */

#endif /* __ADC_DRV_H__ */
