#ifndef __CURRENT_SENSOR_H__
#define __CURRENT_SENSOR_H__

#include <stdint.h>

void CurrentSensor_Init(void);
void CurrentSensor_Task(void);
void CurrentSensor_Register(void);
void CurrentSensor_GetInfo(float *pfCurr_mA, uint8_t *pu8Status);  /* 0=正常 1=过流 */

#endif /* __CURRENT_SENSOR_H__ */
