#ifndef __DEV_CUR_SENSOR_H__
#define __DEV_CUR_SENSOR_H__

#include <stdint.h>

void CurrentSensor_Init(void);
void CurrentSensor_Isr1ms(void); /* 1ms ISR 检测（TMR0_2 心跳调用） */
void CurrentSensor_GetInfo(float *pfCurr_mA, uint8_t *pu8Status);  /* 0=正常 1=过流 */

#endif /* __DEV_CUR_SENSOR_H__ */




