/**
 * @file    di_task.h
 * @brief   DI 采集任务：10ms 周期驱动数字输入设备扫描（电源极性 + 未来限位）
 * @note    事件逻辑在设备内（dev_polarity 等），本任务只做"定时调用设备 Scan"。
 */
#ifndef __DI_TASK_H__
#define __DI_TASK_H__

/* 初始化并启动 DI 采集线程 */
void Di_Task_Start(void);

#endif /* __DI_TASK_H__ */
