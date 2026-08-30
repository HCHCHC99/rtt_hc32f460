/**
 * @file    dev_pwm.h
 * @brief   电机 PWM 输出设备：PHU(PB9/TMRA4-CH4) + PHV(PB7/TMRA4-CH2) 互补驱动
 * @note    仲裁输出接缝：Dev_Pwm_Init 里 Arb_BindOutputOps 绑定 fwd/rev/stop；
 *          正转=PHU 2% / PHV 98%，反转=互补，停止=双 0%（恒低，电机静止）。
 */
#ifndef __DEV_PWM_H__
#define __DEV_PWM_H__

/* 设备级可调项（硬件无关）：运行侧占空比 2%，互补侧 98%（hkb_1 实测配置） */
#define PWM_RUN_DUTY_PCT    20U

void Dev_Pwm_Init(void);       /* 注册表 init：通道创建 + 硬件初始化 + 绑定仲裁输出 */
void Dev_Pwm_Task(void);       /* 预留：非阻塞斜坡推进（当前未启用 ramp） */
int  Dev_PwmMotor_RunFwd(void);
int  Dev_PwmMotor_RunRev(void);
int  Dev_PwmMotor_Stop(void);

#endif /* __DEV_PWM_H__ */
