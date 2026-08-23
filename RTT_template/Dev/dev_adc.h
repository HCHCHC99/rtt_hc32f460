#ifndef __DEV_ADC_H__
#define __DEV_ADC_H__

void Dev_Adc_Init(void);   /* 注册表 init */
void Dev_Adc_Task(void);   /* 注册表 task：周期软件触发 */
void Dev_Adc_Register(void); /* 注册条目 */
int  Dev_Adc_GetLatest(float *pfVolt, float *pfCurr);   /* 上层轮询 */

#endif /* __DEV_ADC_H__ */
