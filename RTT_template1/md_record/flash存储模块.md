# Flash 存储模块设计（移植版）

> 记录日期：2026-09-08
> 状态：✅ 已实施 + 已测试（扇区内循环存储、磨损换扇区均验证通过）
> 来源：裸机工程 `D:\HB_chuchai_v.6.0.4`（hc32f46x_flash / param_manager / App_Params），芯片同为 HC32F460

---

## 一、需求背景

为 RT-Thread 工程增加**参数掉电保存**能力：把母线电压阈值配置（`g_volt_cfg`）与电流传感器阈值配置（`g_cur_cfg`）持久化到片内 Flash，上电自动加载。

**移植范围裁剪**（原 App_Params 为 Modbus 寄存器映射 + 实时数据 + 模拟，大部分不适用本项目，只移植存储内核）：

| 原文件 | 移植结果 | 位置 |
|---|---|---|
| hc32f46x_flash.c/.h | EFM 薄封装（重命名） | `Adp/hc32_drv_flash.c/.h` |
| param_manager.c/.h | 存储引擎（逻辑不动，适配 RTOS） | `Utils/param_manager.c/.h` |
| App_Params.c/.h | 参数记录定义 + 默认值 + 应用 | `Dev/dev_param/dev_param.c/.h`（新建模块） |

---

## 二、三层架构

```
applications/main.c        上电调 Dev_Param_Init()（一次性）
        │
Dev/dev_param/             应用层：ParamRecord_t 布局 + 默认值 + 应用到 g_volt_cfg/g_cur_cfg
        │                   保存入口 Dev_Param_Save()（电机运行中拒绝）
Utils/param_manager/       引擎层：顺序追加 + seq + 头尾魔数 + CRC32 + 回滚扫描 + 磨损均衡
        │                   多实例设计（Config + Runtime 由调用者持有，无静态业务状态）
Adp/hc32_drv_flash         适配层：EFM 解锁/擦除/写字/读字，调用方无需关心解锁
        │
hc32_ll_efm.c（DDL）        已在构建中（无需改 .cproject）
```

---

## 三、Flash 布局与记录结构

### 3.1 参数区（沿用裸机工程布局）

- 扇区范围：**secStart=62 → secEnd=56**（地址 0x7C000 起**逆序**使用，7 × 8KB = 56KB，即 0x70000~0x7FFFF）
- app 区可用至 0x6FFFF（448KB），与裸机工程完全一致
- 扇区号 = 绝对地址 / 0x2000（`PARAM_SECTOR_SIZE` 定义在 param_manager.h）

### 3.2 记录结构 ParamRecord_t（44 字节，pack(4)）

| 偏移 | 字段 | 说明 |
|---|---|---|
| 0 | head_magic | 0x55AA55AA |
| 4 | sequence_id | 每次保存 +1，回滚扫描选最大 |
| 8 | erase_count | 当前扇区擦写计数（磨损判断依据） |
| 12 | volt_over_th | 过压阈值 V（float） |
| 16 | volt_under_th | 欠压阈值 V |
| 20 | volt_hyst | 迟滞回差 V |
| 24 | volt_recover_ms | 恢复延时 ms |
| 28 | cur_over_th_ma | 过流阈值 mA |
| 32 | cur_window_ms | 过流判定窗口 ms + 保留 2B |
| 36 | checksum | CRC32 |
| 40 | tail_magic | 0xAA44AA44 |

- 校验覆盖：checksum 偏移之前的全部字节（36B）
- 上限保护：`PARAM_MAX_RECORD_SIZE = 256`，dev_param.c 内有编译期断言（尺寸超限/未 4 字节对齐直接编译报错）

### 3.3 引擎行为（param_manager）

- **追加写**：每次保存顺序写到 `curr_addr`，写完前移 44B；seq 在写入前 +1，CRC 重算
- **扇区溢出**：`curr_addr + 44 > 扇区尾` 时：
  - `erase_count < 10000`（MAX_ERASE_LIFE）→ **原地擦除**当前扇区，从头写，erase_count+1
  - `erase_count ≥ 10000` → **换下一扇区**（62→61→…→56，到 56 回卷 62），擦新扇区，erase_count 重置 1
- **上电扫描**（Param_Init）：从 secStart 向 secEnd 逐扇区扫描，头/尾魔数 + CRC 全通过视为有效块，**seq 最大者加载**；全无效则写默认值到 secStart
- **写失败重试**：最多 7 次（MAX_WRITE_RETRY），失败后写指针前移跳过坏点

---

## 四、RTOS / 构建适配决策

| # | 原实现 | 移植后 | 理由 |
|---|---|---|---|
| 1 | DDL 硬件 CRC（CRC_CRC32_AccumulateData） | **软件 CRC32**（CRC-32/ISO-HDLC，poly 0xEDB88320，init/xorout 0xFFFFFFFF） | hc32_ll_crc.c 被构建排除；软件实现零构建改动，结果与原硬件配置等价 |
| 2 | 裸机 `__disable_irq()/__enable_irq()` | `rt_hw_interrupt_disable()/enable()`（保存标志成对恢复） | RTOS 下无条件开中断会破坏嵌套状态 |
| 3 | 栈上 `tempBuf[256]` 硬编码 | 静态缓冲 + paramSize 上限校验 | 结构体超 256B 会爆栈，编译期即可拦住 |
| 4 | `.ramfunc` 段 | **去除** | BUS_HOLD 模式下擦/写期间 CPU 取指自动停等，无需 ramfunc；且 Keil scatter 无独立 ramfunc 执行域 |
| 5 | 裸机打印宏 | `rtt_manager.h` 的 `FLASH_PRINT`（默认关）/ `PARAM_PRINT`（开） | 打印开关统一管理 |

**解锁流程**（hc32_drv_flash.c 内部完成，调用方无感）：

```text
擦/写前：EFM_REG_Unlock → EFM_FWMC_Cmd(ENABLE) → EFM_SetBusStatus(EFM_BUS_HOLD) → EFM_ClearStatus
操作后：  EFM_ClearStatus → EFM_FWMC_Cmd(DISABLE) → EFM_REG_Lock
```

**关键约束**：单 bank Flash，擦/写期间（BUS_HOLD）全系统停顿数 ms，1ms 电源检测链会丢样本——**电机运行中禁止保存**（`Dev_Param_Save` 内检查任一仲裁轴 `MS_RUNNING` 即拒绝）。

---

## 五、对外接口

| 接口 | 说明 |
|---|---|
| `Dev_Param_Init()` | 上电扫 Flash 加载并应用（main.c 调一次，内部有一次性保护，IDLE 重入安全） |
| `Dev_Param_Save()` | 从 `g_volt_cfg`/`g_cur_cfg` 同步到记录并写入（电机运行中拒绝） |
| `Dev_Param_SaveTest()` | 测试：over_th +0.5V（[23,27) 回绕）后保存一组 |
| `Dev_Param_FillTest()` | 测试：连续保存填满当前扇区，剩余空间只够再存 2 组停止（500 组保险上限） |
| `Dev_Param_EraseAll()` | 擦除全部参数扇区并重写默认值 |

**MSH 命令**：`param_show`（记录+runtime 状态）/ `param_save` / `param_erase`

**主循环测试开关**（main.c，volatile 全局，debug Expressions 直接改值）：

```text
savelabel = 1   修改一个业务值并保存一组
savelabel = 2   连续保存填满当前扇区（单扇区约 8192/44 ≈ 186 组）
```

---

## 六、测试记录（2026-09-08）

| 测试项 | 方法 | 结果 |
|---|---|---|
| 首次上电写默认值 | 上电看 `[PARAM] defaults set` + 写入 | ✅ |
| 扇区内循环追加 | savelabel=1 连续触发，seq 递增、地址 +44 | ✅ |
| 扇区填充 | savelabel=2，填到剩余 ≤2 组字节 | ✅ |
| 磨损换扇区 | 溢出边缘 + `g_param_record.erase_count=10000` → savelabel=1，保存地址跳到 0x7A000（61 扇区），erase_count 重置 1 | ✅ |
| 复位回滚加载 | 重启后 `[PARAM] loaded` 显示最大 seq 的块 | ✅ |
| 编译 | Studio GCC（0E/0W）；Keil AC5 命令行（0E） | ✅ |

**换扇区模拟法**（将来回归用）：先 `savelabel=2` 填满 → 两次 `savelabel=1` 把剩余消耗到 <44B → debug 改 `g_param_record.erase_count = 10000` → `savelabel=1`，观察保存地址跳至下一扇区起点。若改完计数后地址仍在原扇区，说明当时未溢出（剩余 ≥44B），再触发一次即可。

---

## 七、注意事项 / 踩坑记录

1. **include 路径**：Studio GCC 的 `-I` 列表只有工程根与 applications 等，**没有 Utils**。Dev/Adp/Utils 三层的跨目录 include 一律用目录前缀（如 `"applications/rtt_manager.h"`、`"Utils/param_manager.h"`），同目录文件才允许裸文件名。
2. **打印浮点**：阈值是 float，打印按规范拆整型（`Param_VoltFmt` 拆为 `符号整数.1位小数`），不使用 `%f`。
3. **测试副作用**：`Dev_Param_SaveTest` 会改动 over_th 并持久化，测完用 `param_erase` 恢复默认。
4. **erase_count 在 RAM**：磨损判断读记录结构体字段（非 flash 回读），断电丢失的是最近一次扇区切换前的累计值，属可接受误差。
5. **构建清单**：实际构建以 RT-Thread Studio 为准（新 .c 自动发现）；`project - 副本.uvprojx` 已同步新文件，根目录 `project.uvprojx` 已损坏（二进制）勿用。
