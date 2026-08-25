#ifndef __DEV_BUS_VOLTAGE_H__
#define __DEV_BUS_VOLTAGE_H__

#include <stdint.h>

void BusVoltage_Init(void);
void BusVoltage_Isr1ms(void); /* 1ms ISR 检测（TMR0_2 心跳调用） */
void BusVoltage_GetInfo(float *pfVolt_V, uint8_t *pu8Status);  /* 0=正常 1=欠压 2=过压 */

#endif /* __DEV_BUS_VOLTAGE_H__ */




