/**
 * @file    dev_adc.c
 * @brief   ADC 设备：通道表 + 周期触发 + 注册条目
 * @note    电压 CH4 / 电流 CH5 同放 SEQ_A（时间对齐）
 */
#include "dev_adc.h"
#include "dev_registry.h"
#include "adc_drv.h"

/* 通道表：电压 CH4 / 电流 CH5，同一 SEQ_A（时间对齐） */
static const adc_drv_ch_t s_astcChTable[] = {
    { ADC_CH4, GPIO_PORT_A, GPIO_PIN_04, 31.303f,   0.0f,    false, NULL },
    { ADC_CH5, GPIO_PORT_A, GPIO_PIN_05, 3787.88f, -6250.0f, true,  NULL },
};
static adc_drv_inst_t s_astcInsts[2];

static const SysModule_t s_adc_module = SYS_MODULE_REGISTER("adc", Dev_Adc_Init, Dev_Adc_Task, 2, 1);

void Dev_Adc_Register(void)
{
    (void)Dev_Registry_Add(&s_adc_module);
}

void Dev_Adc_Init(void)
{
    (void)AdcDrv_Init(s_astcChTable, 2U, s_astcInsts);
}

void Dev_Adc_Task(void)
{
    AdcDrv_SoftwareTrigger();   /* 阶段一：软件触发单次转换；Task 9 改 AdcDrv_Start() */
}

int Dev_Adc_GetLatest(float *pfVolt, float *pfCurr)
{
    if (pfVolt == NULL || pfCurr == NULL) {
        return -1;
    }
    *pfVolt = AdcDrv_GetLatest(0);
    *pfCurr = AdcDrv_GetLatest(1);
    return 0;
}

/* EOF */
