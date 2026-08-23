/**
 * @file    adc_drv.c
 * @brief   ADC 硬件驱动实现（HC32F460）
 * @note    基于 HB_chuchai v6.0.3 Adc.c 裁剪：去 DMA；阶段一软件触发（ADC_Start）
 */
#include "adc_drv.h"
#include <rtthread.h>
#include <string.h>

/* ============ 调试宏 ============ */
#ifdef ADC_DRV_DEBUG_ENABLE
#define ADC_DRV_DBG(fmt, ...)   rt_kprintf("[ADC_DRV] " fmt "\r\n", ##__VA_ARGS__)
#else
#define ADC_DRV_DBG(fmt, ...)   ((void)0)
#endif

/* ============ 硬件定义 ============ */
#define ADC_UNIT            (CM_ADC1)
#define ADC_PERIPH_CLK      (FCG3_PERIPH_ADC1)
#define ADC_VREF            (3.3F)
#define ADC_RES             (12U)
#define ADC_SEQA_INT_SRC    (INT_SRC_ADC1_EOCA)
#define ADC_SEQA_INT_IRQn   (INT116_IRQn)
#define ADC_SEQA_INT_PRIO   (DDL_IRQ_PRIO_06)

/* ============ 本地状态 ============ */
static adc_drv_inst_t *s_pstcInsts = NULL;
static uint8_t s_u8InstNum = 0;
static bool s_bInitialized = false;

/* ============ 本地函数 ============ */
static void AdcDrv_SetPinAnalogMode(uint8_t u8Port, uint16_t u16Pin)
{
    stc_gpio_init_t stcGpioInit;
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinAttr = PIN_ATTR_ANALOG;
    LL_PERIPH_WE(LL_PERIPH_GPIO);
    (void)GPIO_Init(u8Port, u16Pin, &stcGpioInit);
    LL_PERIPH_WP(LL_PERIPH_GPIO);
}

static void AdcDrv_IrqConfig(void)
{
    stc_irq_signin_config_t stcIrq;
    stcIrq.enIntSrc    = ADC_SEQA_INT_SRC;
    stcIrq.enIRQn      = ADC_SEQA_INT_IRQn;
    stcIrq.pfnCallback = &AdcDrv_ADC1_IRQHandler;
    LL_PERIPH_WE(LL_PERIPH_INTC);
    (void)INTC_IrqSignIn(&stcIrq);
    LL_PERIPH_WP(LL_PERIPH_INTC);
    NVIC_ClearPendingIRQ(stcIrq.enIRQn);
    NVIC_SetPriority(stcIrq.enIRQn, ADC_SEQA_INT_PRIO);
    NVIC_EnableIRQ(stcIrq.enIRQn);
    (void)ADC_IntCmd(ADC_UNIT, ADC_INT_EOCA, ENABLE);
}

/* ============ EOCA 中断处理 ============ */
void AdcDrv_ADC1_IRQHandler(void)
{
    (void)ADC_ClearStatus(ADC_UNIT, ADC_FLAG_EOCA);

    for (uint8_t i = 0U; i < s_u8InstNum; i++) {
        adc_drv_inst_t *pstc = &s_pstcInsts[i];
        uint16_t u16Raw = ADC_GetValue(ADC_UNIT, pstc->stcCh.u8Channel);
        pstc->u16LatestRaw = u16Raw;
        pstc->u32SampleCount++;

        float fVal = (float)u16Raw * ADC_VREF / (float)(1U << ADC_RES) * pstc->stcCh.fGain + pstc->stcCh.fOffset;
        if (pstc->stcCh.bAbs && fVal < 0.0f) {
            fVal = -fVal;
        }
        pstc->fLatest = fVal;
        pstc->afRing[pstc->u16WriteIdx] = fVal;
        pstc->u16WriteIdx = (uint16_t)((pstc->u16WriteIdx + 1U) % ADC_DRV_RING_SIZE);
        if (pstc->u16Count < ADC_DRV_RING_SIZE) {
            pstc->u16Count++;
        }
        if (pstc->stcCh.pfnCallback != NULL) {
            pstc->stcCh.pfnCallback(u16Raw, pstc->stcCh.u8Channel);
        }
    }
}

/* ============ 初始化 ============ */
int AdcDrv_Init(const adc_drv_ch_t *pstcChTable, uint8_t u8ChNum, adc_drv_inst_t *pstcInsts)
{
    if (pstcChTable == NULL || pstcInsts == NULL || u8ChNum == 0U) {
        return -1;
    }
    if (s_bInitialized) {
        return 0;   /* 幂等 */
    }

    s_pstcInsts = pstcInsts;
    s_u8InstNum = u8ChNum;
    (void)memset(s_pstcInsts, 0, sizeof(adc_drv_inst_t) * u8ChNum);
    for (uint8_t i = 0U; i < u8ChNum; i++) {
        s_pstcInsts[i].u8Id = i;
        s_pstcInsts[i].stcCh = pstcChTable[i];
    }

    /* ADC 时钟 */
    LL_PERIPH_WE(LL_PERIPH_FCG);
    FCG_Fcg3PeriphClockCmd(ADC_PERIPH_CLK, ENABLE);
    LL_PERIPH_WP(LL_PERIPH_FCG);

    /* ADC 初始化：SEQ_A 单次 */
    stc_adc_init_t stcAdcInit;
    (void)ADC_StructInit(&stcAdcInit);
    stcAdcInit.u16ScanMode = ADC_MD_SEQA_SINGLESHOT;
    (void)ADC_Init(ADC_UNIT, &stcAdcInit);

    /* 逐通道：引脚模拟模式 + 使能 SEQ_A */
    for (uint8_t i = 0U; i < u8ChNum; i++) {
        adc_drv_inst_t *pstc = &s_pstcInsts[i];
        AdcDrv_SetPinAnalogMode(pstc->stcCh.u8Port, pstc->stcCh.u16Pin);
        (void)ADC_ChCmd(ADC_UNIT, ADC_SEQ_A, pstc->stcCh.u8Channel, ENABLE);
    }

    /* EOCA 中断 */
    AdcDrv_IrqConfig();

    s_bInitialized = true;
    ADC_DRV_DBG("adc_drv init: %u channel(s)", u8ChNum);
    return 0;
}

void AdcDrv_SoftwareTrigger(void)
{
    if (!s_bInitialized) return;
    (void)ADC_Start(ADC_UNIT);
}

void AdcDrv_Start(void)
{
    /* Task 9：硬件触发时启动 TMR0_1 CH_B */
    ADC_DRV_DBG("adc_drv start (hw trig not yet enabled)");
}

void AdcDrv_Stop(void)
{
    /* Task 9：停止 TMR0 */
}

uint16_t AdcDrv_GetLatestRaw(uint8_t u8Id)
{
    if (u8Id >= s_u8InstNum) return 0U;
    return s_pstcInsts[u8Id].u16LatestRaw;
}

float AdcDrv_GetLatest(uint8_t u8Id)
{
    if (u8Id >= s_u8InstNum) return 0.0f;
    return s_pstcInsts[u8Id].fLatest;
}

uint16_t AdcDrv_ReadRing(uint8_t u8Id, float *pfBuf, uint16_t u16Max)
{
    if (u8Id >= s_u8InstNum || pfBuf == NULL || u16Max == 0U) return 0U;
    adc_drv_inst_t *pstc = &s_pstcInsts[u8Id];
    uint16_t u16N = (pstc->u16Count < u16Max) ? pstc->u16Count : u16Max;
    /* 从最新写入位置往前取 u16N 个（环形） */
    for (uint16_t i = 0U; i < u16N; i++) {
        uint16_t idx = (uint16_t)((pstc->u16WriteIdx + ADC_DRV_RING_SIZE - 1U - i) % ADC_DRV_RING_SIZE);
        pfBuf[i] = pstc->afRing[idx];
    }
    return u16N;
}

/* EOF */
