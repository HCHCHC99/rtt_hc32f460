/**
 * @file    dev_param.h
 * @brief   应用参数管理：掉电保存 g_volt_cfg / g_cur_cfg（经 param_manager 磨损均衡存储）
 * @note    - Dev_Param_Init() 上电由 main 调一次（扫 Flash 加载或写默认值），不走 registry IDLE 复位；
 *          - 参数区沿用裸机工程：secStart=62 / secEnd=56（0x7C000 起逆序，7 扇区）；
 *          - 保存需在安全时机（电机停止），运行中 Dev_Param_Save 拒绝并打印。
 */
#ifndef __DEV_PARAM_H__
#define __DEV_PARAM_H__

#include <stdint.h>

/* Flash 存储参数记录结构体（布局即存储布局，修改需谨慎；4 字节对齐） */
#pragma pack(4)
typedef struct {
    /* 头部信息 */
    uint32_t head_magic;        /* 头部魔数 (0x55AA55AA) */
    uint32_t sequence_id;       /* 序列号 (每次保存递增) */
    uint32_t erase_count;       /* Flash 擦写次数记录 */

    /* 母线电压配置 (VoltCfg_t) */
    float    volt_over_th;      /* 过压阈值 V */
    float    volt_under_th;     /* 欠压阈值 V */
    float    volt_hyst;         /* 迟滞回差 V */
    uint32_t volt_recover_ms;   /* 恢复延时 ms */

    /* 电流传感器配置 (CurCfg_t) */
    float    cur_over_th_ma;    /* 过流阈值 mA */
    uint16_t cur_window_ms;     /* 过流判定窗口 ms */
    uint16_t reserved;          /* 保留对齐 */

    /* 尾部信息 */
    uint32_t checksum;          /* CRC32 校验和 */
    uint32_t tail_magic;        /* 尾部魔数 (0xAA44AA44) */
} ParamRecord_t;
#pragma pack()

/**
 * @brief  上电初始化：扫 Flash 加载参数并应用到 g_volt_cfg / g_cur_cfg
 * @return PARAM_OK 成功（含首次写默认值）；其他为 PARAM_ERR_*
 * @note   main 调一次；IDLE 重入不得重复调用（内部有一次性保护）
 */
int32_t Dev_Param_Init(void);

/**
 * @brief  保存参数：从 g_volt_cfg / g_cur_cfg 同步到记录并写入 Flash
 * @return PARAM_OK 成功；PARAM_ERR 失败（含电机运行中拒绝）
 */
int32_t Dev_Param_Save(void);

/**
 * @brief  调试：擦除全部参数扇区并用默认值重新初始化（param_erase 命令用）
 */
void Dev_Param_EraseAll(void);

/**
 * @brief  测试：修改一个业务值（过压阈值 +0.5V 回绕）并保存一组，打印结果
 * @note   主循环 savelabel==1 触发；电机运行中会被 Dev_Param_Save 拒绝
 */
void Dev_Param_SaveTest(void);

/**
 * @brief  测试：连续保存，填满当前扇区直到剩余空间只够再存两组
 * @note   主循环 savelabel==2 触发；结束后剩余 < 2 组字节（再存将触发擦除/换扇区路径）
 */
void Dev_Param_FillTest(void);

#endif /* __DEV_PARAM_H__ */
