/**
 * @file    dev_config.h
 * @brief   设备层集中配置：功能开关 + 优先级常量
 * @note    由 Dev_RegisterAll() 使用；改这里即可裁剪/调整设备
 */
#ifndef __DEV_CONFIG_H__
#define __DEV_CONFIG_H__

/* ============ 设备功能开关（1=启用，0=裁剪） ============ */
#define DEV_ENABLE_ADC              1   /* ADC 设备（经 dev_adc_ops 访问底层驱动） */
#define DEV_ENABLE_CUR_SENSOR       1   /* 电流传感器设备 */
#define DEV_ENABLE_BUS_VOLTAGE      1   /* 母线电压设备 */
#define DEV_ENABLE_POLARITY         1   /* 电源极性设备（GPIO 双窗口，1ms ISR） */
#define DEV_ENABLE_MONITOR          1   /* 系统观测模块（Watch 用全局 g_monitor） */

/* ============ 设备优先级常量（数字越小优先级越高，与 dev_registry 语义一致） ============ */
#define DEV_PRIO_HIGH               2
#define DEV_PRIO_MID                3
#define DEV_PRIO_LOW                4
#define DEV_PRIO_MONITOR            DEV_PRIO_LOW

#endif /* __DEV_CONFIG_H__ */


