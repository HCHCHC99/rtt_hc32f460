/**
 * @file    hc32_drv_adc.h
 * @brief   ADC 硬件驱动（HC32F460）— 多实例 + EOCA 中断 + 换算写环形缓冲
 * @note    基于 HB_chuchai v6.0.4 Adc.c/h 裁剪移植（去 DMA；TMR0_1 硬件触发 500us + AOS 事件路由 + EOCA 滑动窗口）
 */
#ifndef __HC32_DRV_ADC_H__
#define __HC32_DRV_ADC_H__

#include "hc32_ll.h"
#include <stdint.h>
#include <stdbool.h>

/* ============ 采样触发：TMR0_1 CH_B 硬件触发 + AOS 事件路由 ============ */
#define ADC_SAMPLE_INTERVAL_US      (500U)            /* 采样间隔 500us = 2kHz */
#define ADC_TRIG_TMR_UNIT           (CM_TMR0_1)
#define ADC_TRIG_TMR_CH             (TMR0_CH_B)
#define ADC_TRIG_TMR_CLK            (FCG2_PERIPH_TMR0_1)
#define ADC_TRIG_TMR_CLK_DIV        (TMR0_CLK_DIV256)
#define ADC_TRIG_TMR_DIV_VAL        (256UL)
#define ADC_TRIG_AOS_SEL            (AOS_ADC1_0)           /* AOS 目标：ADC1 触发输入 0 */
#define ADC_TRIG_AOS_EVT            (EVT_SRC_TMR0_1_CMP_B) /* AOS 事件源：TMR0_1 CMP_B */
#define ADC_TRIG_HARD_SEL           (ADC_HARDTRIG_EVT0)    /* ADC SEQ_A 硬件触发选择（内部事件 EVT0） */

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

    /* 滑动平均窗口（原始值；EOCA 中断维护，1ms 检测 ISR 读均值） */
    uint32_t           *pu32MeanWin;   /* 指向静态窗口数组（.c 内分配，长度 ADC_MEAN_WINDOW_SAMPLES） */
    uint32_t            u32MeanSum;
    uint16_t            u16MeanIdx;
    uint16_t            u16MeanCnt;
} adc_drv_inst_t;

/* 接口 */
int  AdcDrv_Init(const adc_drv_ch_t *pstcChTable, uint8_t u8ChNum, adc_drv_inst_t *pstcInsts);
void AdcDrv_Start(void);            /* 硬件触发：启动 TMR0_1 开始 500us 采样 */
void AdcDrv_SoftwareTrigger(void);  /* 软件触发：ADC_Start 单次（阶段一使用） */
void AdcDrv_Stop(void);
uint16_t AdcDrv_GetLatestRaw(uint8_t u8Id);
float    AdcDrv_GetLatest(uint8_t u8Id);
uint16_t AdcDrv_ReadRing(uint8_t u8Id, float *pfBuf, uint16_t u16Max);
float    AdcDrv_GetMean(uint8_t u8Id);       /* 滑动窗口平均（换算后工程值） */
void     AdcDrv_ADC1_IRQHandler(void);   /* EOCA 中断入口（DDL 分发） */

/* ==================== dev_adc_ops 接口表（Dev 层注入用） ==================== */
#define HC32_ADC_MAX_INST       (4U)

struct dev_adc_ops;   /* 前向声明，避免依赖 Dev 层头文件 */
extern const struct dev_adc_ops hc32_adc_ops;


/* 调试宏（统一放头文件；ADC_DRV_DEBUG_ENABLE 为总开关） */
#ifdef ADC_DRV_DEBUG_ENABLE
#define ADC_DRV_DBG(fmt, ...)   rt_kprintf("[ADC_DRV] " fmt "\r\n", ##__VA_ARGS__)
#else
#define ADC_DRV_DBG(fmt, ...)   ((void)0)
#endif

/* 硬件定义（统一放头文件） */
#define ADC_UNIT            (CM_ADC1)
#define ADC_PERIPH_CLK      (FCG3_PERIPH_ADC1)
#define ADC_VREF            (3.3F)
#define ADC_RES             (12U)
#define ADC_SEQA_INT_SRC    (INT_SRC_ADC1_EOCA)
#define ADC_SEQA_INT_IRQn   (INT116_IRQn)
#define ADC_SEQA_INT_PRIO   (DDL_IRQ_PRIO_06)
#endif /* __HC32_DRV_ADC_H__ */







