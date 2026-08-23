#ifndef __BUS_VOLTAGE_H__
#define __BUS_VOLTAGE_H__

#include <stdint.h>

void BusVoltage_Init(void);
void BusVoltage_Task(void);
void BusVoltage_Register(void);
void BusVoltage_GetInfo(float *pfVolt_V, uint8_t *pu8Status);  /* 0=正常 1=欠压 2=过压 */

#endif /* __BUS_VOLTAGE_H__ */
