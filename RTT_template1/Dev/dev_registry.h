#ifndef __DEV_REGISTRY_H__
#define __DEV_REGISTRY_H__

#include <stdint.h>

/* 配置宏（统一放头文件） */
#define MAX_REG_MODULES         (16U)
#define DEV_THREAD_STACK_SIZE   (2048U)
#define DEV_THREAD_PRIORITY     (20)
#define DEV_THREAD_TICK         (10)

typedef struct {
    const char *name;
    void (*init)(void);
    void (*task)(void);
    uint8_t     prio;        /* 数字越小优先级越高 */
    uint16_t    period_ms;
    uint32_t    last_tick;
    uint8_t     enabled;
} SysModule_t;

#define SYS_MODULE_REGISTER(_name, _init, _task, _prio, _period) \
    { #_name, _init, _task, _prio, _period, 0, 1 }

int  Dev_Registry_Add(const SysModule_t *module);
void Dev_RegisterAll(void);   /* 集中注册：adc / cur / vm */
void Dev_Registry_InitAll(void);
void Dev_Registry_UpdateAll(void);
void Dev_Thread_Entry(void *param);
int  Dev_Start(void);

#endif /* __DEV_REGISTRY_H__ */
