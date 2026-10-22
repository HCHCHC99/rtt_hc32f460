# RT_HC32F460 推杆控制项目 — 软件架构梳理 & -准备

> 适用工程：`D:\RT_HC32F460_v.1.0.0\RTT_template1`（RT-Thread + HC32F460 / Cortex-M4）
> 依据：`Dev/ Adp/ Task/ applications/ Utils/` 全部 .h 头文件 + `applications/main.c` + `rtconfig.h`

---

## 一、项目一句话概述

**基于 RT-Thread 的 24V 直线推杆电机控制器**：ADC 硬件触发采样母线电压/电流 → 1ms ISR 故障判定（过压/欠压/过流，迟滞+确认窗）→ 事件驱动系统状态机（INIT/IDLE/RUN/FAULT/EMERGENCY/STOP）→ 消息队列驱动的**电机方向仲裁内核**（多命令源按优先级竞争单一电机）→ PWM/可切 GPIO 两种 H 桥输出（接缝绑定）→ 双霍尔测速/限位 + 过流软限位校准 + Flash 磨损均衡掉电行程保存。

关键数字：
| 项 | 值 |
|---|---|
| 采样 | TMR0_1 硬件触发 500us（2kHz），EOCA 中断，环形缓冲 256，10ms 滑动均值（20 点） |
| 故障判定 | TMR0_2 1ms 心跳 ISR（读 10ms 均值），迟滞回差 + 确认窗 + 恢复延时 |
| 控制周期 | di_task 2ms（极性）、rod_task 10ms（位置/状态）、pwm ramp 10ms、dev 线程 10ms |
| PWM | TMR4_3 四输出 PB6~PB9，6.5kHz，低有效，停止=混合极性 50/50，换向只在端压差=0 时刻 |
| 仲裁 | rt_mq 深度 16，每轴 4 条优先级队列（block/allow × fwd/rev），MAX_CMD_QUEUE_SIZE=10 |
| 存储 | 双实例 param_manager：慢块 A 92B（sec62→61）、快块 B 24B（sec60→51），追加写+CRC32 |
| 掉电余量 | 实测 270ms；急停→等 20ms 刹车→写 Flash（纯写 0.3ms，含擦 8ms） |
| 线程 | main=10 / act=15 / pwm=16 / dev·rod·di=18 / led·sys_sm=19 / tshell=20 / canary=2 |

---

## 二、分层架构

```
applications/   main 启动链 + 1s 监控采样 + MSH 命令(power/sys_evt) + rtt_manager.h 打印开关
      │
Task/           任务编排层：task_set.h（栈/优先级唯一权威 SSOT + 饿死金丝雀 + 栈哨兵监控）
      │         di_task(2ms→极性扫描)  rod_task(10ms→霍尔/位置/状态/保存)  led_task(1s兜底)
      │
Dev/            设备层（业务语义，基本芯片无关）
      ├─ dev_mgr   dev_model(System_t/Axis_t 对象模型) + dev_state(系统状态机) + dev_event_def(事件位)
      │            dev_registry(模块注册表：B=协作周期 / C=独立线程，MAX_REG_MODULES=16)
      ├─ dev_adc(+dev_adc_ops 接口表) / dev_cur_sensor / dev_bus_voltage / dev_polarity / dev_power_isr
      ├─ dev_act(电机仲裁内核：mq+4队列+仲裁线程+ArbOutputOps 输出接缝)
      ├─ dev_pwm(缓启动 ramp 调速线程) / dev_gpio_motor(双脚方向, 新板)
      ├─ dev_hall_motor(EXTI 测速, 仅观测) / dev_hall_rod(限位消抖) / dev_monitor(g_monitor Watch)
      └─ dev_rod(dev_rod_position 脉冲→mm + dev_rod_state 表驱动状态机 + dev_rod_calib 过流判决策)
      │         dev_param(参数管理：param_manager 双实例 A/B + 默认值单点宏 + 热改限值)
      │
Utils/          通用引擎（横切，完全业务无关）
      ├─ state_engine(表驱动状态机引擎：跳转表+Enter/Exit，多实例)
      ├─ us_timer(µs 时基接口表，芯片无关)
      └─ param_manager(Flash 磨损均衡引擎：追加写+seq+CRC32+回滚扫描，rt 临界区适配)
      │
Adp/            芯片适配层（唯一允许出现 HC32 LL 库头文件的地方）
      └─ hc32_drv_gpio / hc32_drv_timer(TMR6 µs 时基 + TMR0_2 1ms 心跳回调) /
         hc32_drv_pwm(TMR4_3，保留 TMRA4 旧实现可切) / hc32_drv_adc(多实例+EOCA+滑动窗) /
         hc32_drv_flash(EFM 解锁/擦/写薄封装)
      │
HC32 LL 库 + CMSIS + RT-Thread 内核/组件（rtconfig.h：semaphore/mutex/event/mailbox/msgqueue/
                mempool/小内存堆/device+console"uart4"/msh+finsh/serial v1/pin/spi/workqueue）
```

**依赖方向单向**：applications → Task → Dev → Adp → LL 库；Utils 被下层复用但不含业务。
**配置集中**：`dev_config.h`（功能裁剪开关）、`task_set.h`（栈/优先级 SSOT）、`rtt_manager.h`（打印开关区）、`dev_param.h`（`*_DFT` 默认值单点 + 热改上下限宏）。

### 上电启动顺序（main.c:238-277，顺序即依赖）

1. `Print_All_Clock_Freq` → `UsTimer_Bind(&hc_us_timer_ops)` 绑定时基
2. `Dev_Adc_Bind(&hc32_adc_ops)` 绑定 ADC 接口表（注册即 init，未启动采样）
3. `Dev_Param_Init()` 扫 Flash 加载参数（**必须先于** App_Model_Init 的 RodApply）
4. `Dev_RegisterAll()` → `Task_Set_Start()` 启动饿死金丝雀
5. `App_Model_Init()` **先建 sys_evt 事件组**（ISR 发事件依赖它）再填状态机表
6. `Dev_Start()`（B 模式 dev 线程）→ `Sys_Sm_Thread_Start()` → Led/Di/Rod_Task_Start
7. 主循环 1s：`monitor_sample_1s` + 饿死检查 + 栈哨兵水位监控

---

## 三、核心设计模式（-弹药库）

1. **接口表注入（ops 模式，仿 rt_pin_ops）**：`dev_adc_ops.h` 定义 `struct dev_adc_ops{init/start/stop/get_latest/get_raw/read_ring/get_mean}`，Dev 层只认接口，Adp 层提供 `hc32_adc_ops`；`us_timer_ops` 同理（`hc_us_timer_ops`）。→ 依赖倒置，换芯片只改 Adp。
2. **输出接缝**：仲裁内核不直接操作硬件，`Arb_BindOutputOps(fwd/rev/stop)` 由 `dev_pwm` 或 `dev_gpio_motor` 在各自 Init 里绑定 → 新旧板切换**上层零改动**。
3. **表驱动状态机**：`state_engine`（跳转表 cur+event→target + enter/exit 表）挂两个实例：系统 `System_t.sys_sm` + 每轴 `Axis_t.sm_act`（推杆状态机）。
4. **注册表 B/C 双模式**：`SysModule_t` 里 `task+period_ms`=B 模式协作周期任务（省线程省栈，monitor 用），`thread_entry+stack+prio`=C 模式独立线程（仲裁 act 用，实时性）。
5. **事件驱动 + 边沿语义**：事件只发**稳定态跳变沿**（极性跳变、限位故障沿），状态机线程 `rt_event_recv(SYS_SM_WAIT_EVENTS)` 阻塞等待，替代轮询。
6. **ISR/线程分工**：ISR 只做"快判定+发事件"（1ms 心跳读均值判阈值）；重活（ramp、校准、Flash、打印）全部下沉线程。pwm 命令接缝"ops 只写目标+信号量，ramp 由调速线程推进"。
7. **消抖三件套**：滑动窗口投票（极性 5×2ms、限位 3×10ms）+ 迟滞回差 + 连续超限确认窗（`volt_over_ms/under_ms`）+ 换向过流屏蔽（`cur_block_ms=30ms`，避开浪涌）。
8. **掉电安全链路**：欠压边沿→`Sys_State_Dispatch` 调 `Dev_Param_RodSaveRequest`（非阻塞幂等）→ Rod_Task `PollSave` 等 2 tick≈20ms（刹车滑行稳定）→ 写快块 B；270ms 实测余量覆盖 20+8ms。
9. **Flash 记录防御**：head/tail 双魔数 + sequence_id 单调递增 + erase_count + CRC32；上电扫描全区取**最大 seq 的有效块**，无有效块写默认值——任何一处损坏都能回滚。
10. **磨损均衡**：顺序追加、扇区写满逆序切下一扇区（B 块 60→51 共 10 扇区）；小记录（24B）换来 341 次/擦。
11. **可观测性**：`volatile Monitor_t g_monitor` 供 Watch；阈值字段带 `volatile VoltCfg_t*` 指针镜像（**Watch 里改 g_volt_cfg 立即生效**）；栈哨兵 '#' 水位（≥90% 告警）；金丝雀饿死检测（500ms 心跳/3s 超时）；故障瞬间电压/电流快照。
12. **故障恢复语义**：`fault_bits` 位图（bit0 过流/bit1 过压/bit2 欠压）全清才自动恢复；`prev_state` 决定恢复回跳（RUN→RUN，否则→IDLE）；极性跳变时**方向相关清除**过流故障。

---

## 四、-题 — 软件设计（12 题）

**Q1. 你的设备层怎么做到与芯片无关？**
考察：抽象/依赖倒置。答：三层手段——①ops 接口表注入：`dev_adc_ops.h` 定义芯片无关接口（含 `dev_adc_ch_cfg_t{channel,gain,offset,abs}` 通用换算），Dev 层经 `Dev_Adc_Bind(&hc32_adc_ops)` 拿实现；②`us_timer_ops` 同样模式；③引脚绑定宏收敛到单点（如 `MOTOR_GPIO_FWD_PIN PB8`、"换板只改这里"）。电路换算语义也分层：ADC 层只出 V，V→mA 换算下沉 `dev_cur_sensor`（换传感器只改一个文件）。

**Q2. 中断和线程怎么分工？为什么这样分？**
答：原则"ISR 快进快出，只做判定和通知"。1ms 心跳 ISR（TMR0_2）读 10ms 滑动均值做阈值判定，超限只 `rt_event_send`（中断安全）；缓启动 ramp、校准、Flash 写、打印全在线程。例证：`dev_pwm` 命令接缝"ops 只写目标方向+占空比+信号量，ramp 由 10ms 调速线程推进"；急停 `EStop` 例外——跳过 ramp 立即输出，ISR 可调。判据：ISR 耗时影响所有低优先级中断延迟，且很多 API（Flash/打印/阻塞）ISR 禁用。

**Q3. 事件机制和轮询怎么取舍？**
答：本工程把系统状态机从"轮询任务"改成"事件位分发"（`Sys_State_Dispatch` 逐位投递）。选择依据：事件源多为**跳变沿**（极性变化、限位故障、电压恢复），天然适合事件；而周期观测类（monitor 刷新、极性扫描）保留轮询（B 模式 task）。事件集支持一次等待多个位（`SYS_SM_WAIT_EVENTS` 掩码），比逐个信号量高效。

**Q4. 为什么要自己写电机仲裁内核（dev_act）？**
答：电机只有一台，命令源有十几个（电源极性正/反、限位、CAN、IO、过压/欠压/过流、急停、回转……`ArbDeviceId_t` 18 个 ID）。直接操作会互踩。仲裁内核：命令统一 `ArbCommandMsg_t{axis,device,priority,cmd,duty,timestamp}` 经 `rt_mq`（深度16）发给仲裁线程；每轴 4 条**优先级有序队列**（block_fwd/block_rev/allow_fwd/allow_rev），优先级 EMERGENCY(0)>LIMIT(1)>FAULT(2)>MANUAL(3)>CAN(4)>POWER(5)；支持 urgent 插队、block 语义（限位挂起而非清除）、同设备双向请求→`conflict_fault` 冲突停机。好处：输出唯一所有权、决策可观测（`g_arb_dbg_*`）、新增命令源只需发消息。

**Q5. 按键/传感器抖动你怎么处理？为什么用窗口不用计数器？**
答：滑窗状态投票：极性 5 点×2ms=10ms 稳定判定（`POLARITY_WIN_SIZE`），限位霍尔 3 点×10ms=30ms（`ROD_HALL_WIN_SIZE`，仿极性双窗口）。窗口法比计数器多了"序"信息：只有**稳定态发生跳变**才发事件，窗口未满保持上次态（UNKNOWN 不发事件），天然滤掉瞬态毛刺且无需定时器复位逻辑。故障判定另加确认窗+迟滞（避免阈值附近震荡）。

**Q6. 掉电保存为什么能保证写得进去？**
答：预算驱动设计。实测欠压掉电余量 270ms；链路：欠压边沿置保存请求（幂等，仅一次）→ Rod_Task 10ms 周期轮询发现请求→ 延时 2 tick≈20ms 等刹车滑行稳定（否则存进去的位置不准）→ 写 24B 记录（纯写 ~0.3ms；最坏当前扇区满含擦 ~8ms）。20+8 << 270，且热改限值 `VOL_OVER_MS_MAX=100` 注释明确"确认窗+急停+存行程须 << 270ms 掉电余量"——用编译期限值保护运行时预算。

**Q7. Flash 参数记录怎么防掉电写坏？**
答：24B/92B 定长记录，`#pragma pack(4)`：head_magic + sequence_id + erase_count + 数据 + CRC32 + tail_magic。上电顺序扫描全部记录位，**CRC 和双魔数都对才算有效**，取 seq 最大者；一个都没有→回调 `pSetDefaults` 写默认值。任何半条写入（掉电时刻）都会被 CRC 或魔数拦下，自动回滚到上一条。

**Q8. 磨损均衡怎么做的？**
答：追加写（append-only）：新记录写到当前地址+记录长；当前扇区写不下→切下一扇区（逆序：快块 60→51 共 10 扇区）；所有扇区满→擦除**最老**扇区继续。参数化多实例：`Param_Config_t{扇区范围,布局偏移,魔数}` + `Param_Runtime_t{当前位置}`，同一份引擎管慢块 A（92B/89 次/擦）和快块 B（24B/341 次/擦）。**记录越小每扇区可写次数越多**——所以行程高频保存独立成快块并与慢块扇区隔离。

**Q9. 参数在线修改怎么防误配？**
答：热改限值宏单点定义（如 `VOL_OVER_TH_MIN 22.0 / MAX 30.0`），`param_set` 越界拒收并打印 `PARAM_WARNNING`（仅出错时输出的告警宏），上电加载也做越界告警。明确取舍：只做单参数上下限，跨字段约束（欠压<过压-迟滞、校准窗≤行程）不在这一层校验——诚实说出边界比声称"全覆盖"加分。

**Q10. 状态机怎么分层的？故障怎么恢复？**
答：两层：`System_t.sys_sm`（INIT/IDLE/RUN/FAULT/EMERGENCY/STOP）+ 每轴 `Axis_t.sm_act`（UNKNOWN/STOPPED/EXTENDING/EXT_LIMIT/EXT_FAULT/RETRACTING/RET_LIMIT/RET_FAULT，事件由 10ms tick 合成：方向/限位/超时）。故障用 `fault_bits` 位图（过流/过压/欠压），**全清才自动恢复**；`prev_state` 记故障前状态，恢复时 RUN→RUN、其余→IDLE；极性跳变沿发 `EVT_SYS_POLARITY_CHG`，方向相关的过流清除决策放 dev_state（设备只报事实，决策收口在状态机）。

**Q11. 过流了，怎么区分"到限位顶住"和"真堵转卡死"？**
答：`dev_rod_calib` 三分类决策（从 sys_sm 过流分支抽出复用）：①仲裁未使能/方向无效→忽略（输出已停电流必回落）；②方向+位置在**校准窗口**内或未校准→软限位：写 `calib_req=MAX/MIN` 由 rod_task 执行位置重置（伸出到顶记为行程、缩回到底记为 0），**不算故障**；③窗口外（位置记忆在中间却过流）→真故障（异物卡住），走系统故障机制并记录方向供换向清除。这套"过流软限位"让无绝对编码器的系统建立了位置基准。

**Q12. 怎么让整个系统"看得见"？**
答：四件套：①`g_monitor` 全局观测结构（状态用枚举，Watch 显示名字；阈值放指针镜像，调试器里改 `g_volt_cfg` 实时生效）；②栈哨兵：线程栈预填 '#'，1s 扫描水位 ≥90% 告警（曾因 256B 栈溢出写坏 TCB，rod/di 提到 2048）；③饿死金丝雀：prio 2 线程 500ms 心跳，3s 没跑就告警，周期任务循环里调 `Task_Set_Beat` 点名；④故障现场快照（`GetFaultVolt/GetFaultMa` 故障触发瞬间的值，不是恢复后的值）。

---

## 五、-题 — 软件架构（10 题）

**Q1. 画一下你的架构，为什么这么分层？**
按上面第二节图。答：单向依赖 applications→Task→Dev→Adp；Dev 是业务语义层（"电流传感器"），Adp 是芯片语义层（"ADC1 通道 4"）；Utils 是纯引擎横切。判据：哪一层会变——换板只动 Adp（引脚/外设绑定宏），换业务逻辑动 Dev，换调度形态动 Task。唯一妥协：`dev_gpio_motor.h` 因引脚复用需要 include `hc32_ll.h` 拿端口宏——如实承认并解释为引脚宏单点（换板仍只改这一个文件）。

**Q2. 注册表 B/C 两种模式怎么选？**
答：B 模式=协作周期任务（registry 表里 `task+period_ms`，dev 线程 10ms tick 轮询分发）：省线程省栈，适合低频观测（monitor 100ms 刷 g_monitor）；C 模式=registry 为该模块建独立线程：适合事件阻塞型（act 仲裁线程阻塞在 `rt_mq_recv`）。成本差异直观：B 共享 512B dev 栈，C 独占 2048B。上限 `MAX_REG_MODULES=16`，注册集中 `Dev_RegisterAll()`，配合 `dev_config.h` 开关做编译期裁剪。

**Q3. 电机输出为什么做成"接缝"？带来什么好处？**
答：`ArbOutputOps_t{fwd,rev,stop}` 由设备 Init 时 `Arb_BindOutputOps` 绑定：旧板绑 `dev_pwm`（TMR4_3 缓启动 ramp 线程），新板绑 `dev_gpio_motor`（PB8/PB9 双脚、双高=刹车、无调速）。上层链（仲裁/rod 状态机/sys_sm/过流软限位）**零改动**。这是策略与机制分离：仲裁决定"该往哪转"，设备决定"怎么转"。副产品：ramp 等策略差异被内化到各自设备里，不外溢。

**Q4. 启动顺序为什么是现在这样？**
答：依赖倒推：①参数先加载（`Dev_Param_Init`）因为 `App_Model_Init` 末尾要 `Dev_Param_RodApply` 恢复行程；②事件组先建（`App_Model_Init` 注释明确"先建 sys_evt，检测 ISR 发事件依赖它"）——否则 1ms ISR 发事件时野指针；③绑定先于注册（`Dev_Adc_Bind` 在 `Dev_RegisterAll` 前，注册即 init 要走接口）；④监控金丝雀在业务线程起来前先站岗。任何顺序错误都写过诊断（INIT 卡死三件套：状态+事件组未消费位+sys_sm 线程是否存在一屏定位）。

**Q5. 优先级怎么排的？依据是什么？**
答：硬实时链路最高：ISR（EXTI=02、1ms 心跳=03、ADC=06，DDL 原生优先级域独立于线程）；线程里 act 仲裁(15) > pwm 调速(16) > dev/rod/di(18，周期控制链) > led/sys_sm(19) > tshell(20)；main(10) 负责 1s 监控但允许被抢；canary(2) 保证饿死时仍能告警。依据：**离执行器越近、截止期越硬优先级越高**；打印/外壳类最低——它们饿死不影响安全。另用统一入口 `Task_Set_Create` 做栈/优先级合法性检查（防坏线程导致低优先级线程饿死，头文件注释原话）。

**Q6. 线程栈大小怎么定的？**
答：SSOT 单点 `task_set.h`（`TASK_STACK_*` 全部线程，rtconfig.h 的 idle/workq/timer 也宏引用它）+ 三个运行期手段：上电 `Task_Stack_Dump` 打全部栈和堆余量；`Task_Stack_Monitor` 哨兵水位 ≥90% 告警（name/sp/stack_addr/used%）；踩坑教训写进注释：rod/di 曾 256B 栈溢出写坏 TCB（现象 INIT 卡死+线程饿死），加到 2048 并留档 `md_record/INIT卡死-线程饿死与栈溢出.md`。结论：**栈大小不是拍的，是"初值+水位监控+事故复盘"迭代出来的**。

**Q7. 线程饿死怎么检测？为什么会发生？**
答：发生：低优先级线程被高优先级周期任务长期抢占（或坏线程不阻塞）。检测：canary 线程（工程注释"仅高于 idle"）500ms 心跳；`Task_Set_StarvationCheck` 在 main 1s 循环里对每个登记任务核对心跳（`Task_Set_Beat` 点名，事件型任务 beat=0 不参与）；超时 3000ms/5000ms 告警。配套诊断：main 1s 遍历 `rt_object_get_information(RT_Object_Class_Thread)` 打全部线程 stat/prio——"谁没启动/谁在空转一目了然"。

**Q8. 多轴扩展预留了什么？**
答：`MAX_AXIS_NUM=2`；`Axis_t{id,dir,sm_act,evt_act,position,state}` 对象化；轴事件位独立组（`EVT_ACT_*`）经 `Act_Event_Send` 广播；仲裁 per-axis（`ArbData_t` 每轴一份含 4 队列）；`MOTOR_HALL_MAX_AXIS_NUM` 同步。当前只挂 axis0（`POLARITY_ARB_AXIS_ID=0`，axis1 预留）——预留的是结构不是半成品，asm 未挂表、enter 回调按 `dir==NONE` 跳过。

**Q9. 架构怎么支撑"新板/旧板切换"和裁剪？**
答：四层开关：①`dev_config.h` 功能开关（`DEV_ENABLE_HALL_MOTOR` 等，`Dev_RegisterAll` 据此裁剪）；②仲裁输出接缝绑定（PWM 版/GPIO 版设备二选一 Init）；③实现选择宏（`PWM_DRV_USE_TMR4` 1/0 切 TMR4/TMRA4 两套完整实现）；④引脚绑定宏单点。板级差异被压到"改一个宏/换一次绑定"，无 if(board==) 散落。

**Q10. 如果把 RT-Thread 拿掉，哪些设计还活着？**
答：直接存活：表驱动状态机引擎、ops 接口表、注册表 B 模式（本身就是主循环+定时器形态）、param_manager（临界区宏换 `__disable_irq()`）、消抖/迟滞/仲裁算法。需要替换：rt_event/rt_mq/线程（C 模式）/MSH/Watch 依赖的调试器变量仍在（它们只是普通全局变量）。这说明架构的价值在**决策的分层**而非具体 OS。

---

## 六、-题 — RT-Thread / RT-Thread Studio 组件（12 题）

**Q1. 你项目里实际用了 RT-Thread 哪些设施？**
答（rtconfig.h 为证）：线程 + 事件集（sys_evt/evt_act）+ 邮箱/消息队列（arb_mq）+ 互斥量（每轴仲裁数据）+ 信号量（pwm ramp 同步）+ 临界区 `rt_hw_interrupt_disable/enable`（param_manager RTOS 适配点）+ 小内存堆（SMALL_MEM_AS_HEAP）+ 设备框架（PIN/UART4 console/SPI）+ MSH FinSH（tshell prio 20/栈 1024/历史 5/符号表）+ 系统工作队列 + 组件自动初始化（本工程主要用手动 InitAll 保证顺序，讲清取舍）。

**Q2. 事件集、信号量、消息队列、互斥量怎么选？**
答：事件集=多位开关量同步（`SYS_SM_WAIT_EVENTS` 一次等 13 个位，支持 OR 语义）；消息队列=带数据的异步命令（`ArbCommandMsg_t` 6 字节域，天然解耦发送方）；信号量=单事件通知（pwm 目标更新）；互斥量=数据保护（每轴 ArbData_t，注意与二值信号量区别：所有权+递归+优先级继承）。ISR 侧只能用 `rt_event_send/rt_mq_send/release` 这类中断安全 API。

**Q3. ISR 里能调什么、不能调什么？**
答：能：`rt_event_send`、`rt_mq_send`、`rt_sem_release`、`rt_hw_interrupt_disable/enable`。不能：任何可能阻塞/睡眠的（`rt_thread_mdelay`、`rt_mutex_take`、`rt_mq_recv` 等待）、大计算、Flash 擦写（EFM BUS 期间取指停等）。本工程例证：欠压 ISR 只置保存请求标志，实际 `Dev_Param_RodPollSave` 在 Rod_Task 线程里延时 20ms 后写 Flash。

**Q4. rt_kprintf 的重定向和线程安全？**
答：本工程双通道：console 设备 "uart4"（rtconfig.h `RT_CONSOLE_DEVICE_NAME`，`rt_hw_console_output` 默认走它）；业务日志用 SEGGER RTT（`MAIN_D` 宏 → `rtt_log.h`），`RTT_PRINTF_EN` 可选把 rt_kprintf 也重定向到 RTT。线程安全：rt_kprintf 有锁但不保证原子行，跨线程打印会交织——所以工程用"pending 打印"模式（ISR 里只记 `Polarity_PrintPending`，线程上下文再打）。浮点：MAIN_D 不打印浮点，`cmd_power` 拆成整数+两位小数打印（省却 newlib 浮点格式化栈开销）。

**Q5. MSH 命令怎么导出？写过一个什么命令？**
答：`MSH_CMD_EXPORT_ALIAS(cmd_power, power, show bus voltage & current)`。本工程命令：`power`（电压电流+状态）、`sys_evt <hex>`（手动向系统事件集发位，调试状态机）、`param_set/param_show/param_erase`（参数热改/查看/擦除）、`dir_set`（霍尔/电机相序反置并保存）。要点：导出的是函数符号进 symtab，参数经 `argc/argv`。

**Q6. RT_USING_USER_MAIN 和 main 线程的关系？**
答：`RT_USING_USER_MAIN` 让 `main()` 变成内核线程（优先级 `RT_MAIN_THREAD_PRIORITY=10`、栈 `RT_MAIN_THREAD_STACK_SIZE=TASK_STACK_MAIN=4096`，都宏引用 task_set.h SSOT）；`RT_USING_COMPONENTS_INIT` 在 main 前跑自动初始化段。本工程 main 线程后来变成"1s 监控循环"——初始化完就低频值守，体现 main 线程可降级为普通任务。

**Q7. 栈溢出 RT-Thread 有什么机制？你额外做了什么？**
答：内核侧 `RT_USING_OVERFLOW_CHECK`+`RT_USING_STACK_CHECK`（调度时查栈 Magic 溢出告警）、`RT_USING_HOOK` 可挂栈统计。工程侧自建哨兵法：`Task_Stack_Monitor` 扫 '#' 水位 ≥90% 打 name/sp/used，比"溢出才报"早发现；另有 `Task_Stack_Dump` 上电全量报告。真实案例：256B 栈 + printf 峰值 → 溢出写坏 TCB → 表现为无关线程饿死（root cause 离现场很远）。

**Q8. 线程状态有哪些？怎么诊断"线程没跑起来"？**
答：stat：INIT(0)/SUSPEND(1)/READY(2)/RUNNING(3)/CLOSE(4)。诊断：遍历对象容器 `rt_object_get_information(RT_Object_Class_Thread)` 打全部线程 stat+prio（main.c 里就有，TH_DIAG 开关控制）；配合 `rt_thread_find("sys_sm")` 判存在性。工程里 INIT 卡死诊断三件套：状态机当前态 + 事件组未消费位 + 目标线程是否存在。

**Q9. RT-Thread Studio 工程结构说一下？**
答：applications/（main+业务入口，本工程把 Dev/Task/Adp/Utils 也以 SConscript 纳入构建）、board/（board_config + drv_config + config/uart_config 等外设配置 + linker_scripts link.icf/ld/sct 三套）、libraries/（hc32f460_ddl：cmsis+驱动库+组件 24cxx/w25qxx）、rtconfig.h（组件裁剪）、Kconfig、.project/.cproject（Studio 元数据）、调试用 flashloader（FlashHC32F460xC.flash/.FLM）。构建走 SCons 脚本，每个模块一个 SConscript——加目录=加 SConscript，不动全局。

**Q10. rt_pin API 和直接操作 LL 库怎么混用？**
答：设备框架 PIN：`GET_PIN(B,2)` + rt_pin_read/write 用于简单 IO（限位霍尔、LED）；性能/特殊功能路径直接 LL 库（H 桥 TMR4 复用、EXTI INT013、ADC/AOS 路由——这些 RT-Thread PIN 设备覆盖不了）。准则：简单读写走框架换板省事，时序敏感/复用功能走 LL。`Adp/hc32_drv_gpio` 还封了 `Output_GPIO_Init/Input_GPIO_Init + Hc32_Gpio_Set/Reset/Toggle/Read` 统一两种风格。

**Q11. 为什么用 SEGGER RTT 打日志而不用串口？**
答：①不占 UART 引脚（本板 UART 让给 shell）；②J-Link SWD 即可，量产板没接串口也能看；③实时性高（内存缓冲，微秒级），可在 ISR 里留痕（配合 pending 模式线程打印）；④`rtt_manager.h` 20+ 个模块级开关（`XXX_PRINT_EN`）统一管理，"开关区置 0 即关，不改业务代码"，全部落到 MAIN_D（英文整型、无浮点）。`RttManager_DumpSwitches` 上电报告开关状态。

**Q12. 组件自动初始化（INIT_APP_EXPORT 等）你怎么用？为什么这工程不全用它？**
答：机制：`INIT_BOARD_EXPORT/INIT_PREV.../INIT_APP_EXPORT` 把初始化函数放自定义段，`components_init` 按段序执行。本工程刻意**手动编排**（main 里按依赖顺序调用）：因为初始化有严格数据依赖（参数→模型→绑定→注册→线程），且要精确控制在"事件组先于 ISR 活跃"这类时序。自动初始化适合互相独立的驱动级 init；强依赖链路手动更可控。能讲出"两个都用、各得其所"比站队好。

---

## 七、高频开放题 & 项目故事（STAR 素材）

**① 最大的技术挑战** → INIT 卡死/线程饿死排查：现象无关线程不跑 → 全线程 stat 转储发现目标线程 CLOSE → 栈哨兵定位 rod/di 线程 256B 栈溢出**写坏相邻 TCB** → 栈加到 2048 复位正常 → 沉淀出三件套：栈哨兵水位监控 + 饿死金丝雀 + `Task_Set_Create` 统一入口合法性检查。教训：**嵌入式里"怪现象"先查内存破坏**。

**② 一次硬件-软件联调** → 电流读数偏大 10 倍：标称灵敏度 200mV/A（10mΩ×20 倍）对不上 → 不改硬件，软件加 `/10` 校准系数进 `CUR_SENSITIVITY_MA_PER_V` 并在注释里写明三种可能等效原因（增益 200 / 采样电阻 100mΩ）待硬件确认。体现：**用校准层隔离硬件不确定性，注释留证据链**。

**③ 换板适配** → 旧板 PWM 调速 → 新板双 GPIO 方向：靠仲裁输出接缝（`Arb_BindOutputOps`）上层零改动；同时保留旧实现（`PWM_DRV_USE_TMR4` 开关）可回退。

**④ 掉电数据完整性** → 270ms 余量的预算分配（确认窗上限 100ms 进编译期限值、20ms 刹车等待、8ms 最坏 Flash）。

**⑤ 为什么 PWM 停止态是 50/50 混合极性？** → 运行态低有效，若直接 0% 停，桥臂状态突变电流冲击大；改"两端电压差=0 时刻（U=V=50%）换极性"，电流连续无突变，再 ramp 10%/10ms 收尾；急停才允许跳过 ramp。

---

## 八、一页速记（背这 10 句）

1. 四层单向依赖：applications→Task→Dev→Adp，Utils 横切纯引擎。
2. 三个 ops 接口表：dev_adc_ops / us_timer_ops / ArbOutputOps —— 换芯片/换板只动 Adp。
3. 两个表驱动状态机：sys_sm（系统）+ sm_act（轴），引擎 state_engine 多实例。
4. 注册表 B（协作周期）/C（独立线程）双模式，dev_config.h 开关裁剪。
5. ISR 只判定+发事件；ramp/校准/Flash 全在线程；急停是唯一 ISR 直控例外。
6. 仲裁内核：mq→仲裁线程→每轴 4 条优先级队列→唯一输出所有权，conflict 停机。
7. 消抖=滑窗投票+迟滞+确认窗+换向屏蔽；事件只发稳定态跳变沿。
8. Flash：双魔数+seq+CRC32 追加写+逆序换扇区；A 慢块 92B/89 次/擦，B 快块 24B/341 次/擦。
9. 掉电链路 270ms 预算：请求幂等→等 20ms 刹车→纯写 0.3ms/含擦 8ms。
10. 可观测四件套：g_monitor Watch / 栈哨兵 90% / 饿死金丝雀 / 故障瞬间快照。
