# RK3568 AMP CAN 让渡用户使用指南

> 对象：在 RK3568（toybrick/DAYU210）AMP 双系统上，把 CAN1（默认）与
> CAN2（**接口预留扩展，尚未上板硬件验证**——可选，需在 dts 启用）让渡给 LiteOS-M 实时核（CPU3）作为**协议无关
> 实时 CAN 口**使用的协议开发者/集成者。两口可**并发**使用。
> 主核侧驱动：本目录 `mcs_can.c`（Linux 6.6 out-of-tree 模块，单模块多口：
> dts 节点的 compatible 选口）。
> 从核侧驱动：morpheus 仓 `device/board/rockchip/rk3568/liteos_new/board/driver/can/`
> （`can.h` 公开 API + `can_rk3568.c` 实现，每口一个 `CanDevice` 实例）。
> 设计细节见 `mcs_can.c` / `can_rk3568.c` 头部注释（本文只讲"怎么用"）。

---

## 1. 概述

### 1.1 这是什么

RK3568 内置两个 CAN 控制器（经典 CAN 2.0B 模式，非 CAN FD）。默认归 Linux；
本方案把 CAN 控制器的整个数据面**让渡**给运行 LiteOS-M 的实时核 CPU3，用于
J1939、CANopen 等对确定性敏感的协议。**CAN1**（`0xFE580000`，can1m1 引脚组）
默认让渡；**CAN2**（`0xFE590000`，can2m0 引脚组）为可选让渡（dts 中默认
disabled，按需启用），两口独立 fifo/中断/控制块，可并发运行。驱动只提供**帧
级收发接口**，不绑定任何上层协议——协议栈只链接 `can.h`，自行组织帧内容
（与网卡 `net.h` 同一设计哲学）。

```
                RK3568 (4 x Cortex-A55)
 ┌──────────────────────────────────────────────────────────┐
 │  CPU0-2: OpenHarmony Linux 6.6        CPU3: LiteOS-M    │
 │                                                          │
 │  ┌──────────────┐    硬件让渡(handover)    ┌──────────┐ │
 │  │ mcs_can.ko   │ ───────────────────────► │ can 驱动 │ │
 │  │ 仅初始化+    │  ctrl @0x740ff100 (CAN1) │ 数据面   │ │
 │  │ 时钟保持     │  ctrl @0x740ff200 (CAN2) │ 每口实例 │ │
 │  │ （单模块多口）│    (can_ctrl.h 契约)     │ Can* API │ │
 │  └──────────────┘                          └──────────┘ │
 │  Linux 侧 CAN 驱动不绑定 can1/can2          协议层(用户) │
 └──────────────────────────────────────────────────────────┘
        │CAN1 (fe580000, can1m1: GPIO4_C2/C3)   [默认让渡]
        │CAN2 (fe590000, can2m0: GPIO4_B4/B5)   [可选让渡]
        ▼
  外部 CAN 收发器（板载未焊，需自接）
```

内部回环（`CAN_WORK_LOOPBACK`）**不需要任何外部硬件**——收发全在片内完成，
是零硬件的上板验证/单元测试路径（内置 demo 即以此运行）。

### 1.2 职责划分（重要约定）

| 事项 | 主核 (Linux) | 从核 (LiteOS-M) |
|---|---|---|
| CAN 时钟（CLK_CANx 重设 100MHz + 读回发布、PCLK 常开） | **配置并保持常开** | 不碰 |
| CRU 复位脉冲（SRST_CANx/SRST_P_CANx） | **配置** | 不碰 |
| pinmux（can1m1 / can2m0，func3） | **配置**（失败仅告警：回环不受影响） | 不碰 |
| 寄存器总线探活 / 硅片版本 | **probe 期一次** | 不碰 |
| 数据面寄存器（模式/位时序/滤波/FIFO） | **不碰** | **独占** |
| CAN 中断（CAN1: INTID 34 = GIC_SPI 2；CAN2: INTID 35 = GIC_SPI 3） | **不配置 GICD**（只交叉核对 dts 值） | `ArchHwiCreate` 端到端自配（每核 GICD 所有权） |
| 共享控制块（CAN1: 0x740ff100 / CAN2: 0x740ff200） | 写 master_state/平台参数 | 写 slave_state |
| 帧收发 | **零参与** | 全权 |

时序要求：从核 `CanInit` 之前对应口的 mcs_can.ko probe 必须**已完成**（该口
ctrl 块 READY）——`CanInit` 第一步就校验该口 ctrl 块 magic/version/
`master_state`，不通过直接返回 `-ENODEV`（CAN 时钟未开时访问其寄存器会挂
AXI 总线，该门禁先于一切寄存器访问）。CAN2 节点未在 dts 启用时，从核对
CAN2 的 `CanInit` 同样返回 `-ENODEV`——内置 demo 将其显示为 `skipped`
（设计行为，非错误）。

---

## 2. 端到端流程总览

```
① 板级 dts 集成（一次性）      mcs-can1 节点（rk3568-ohrt-mcs.dtsi 已内嵌）
② 主核构建+加载               build_mcscan.sh → insmod mcs_can.ko → step0..5 READY
③ 从核固件运行                LiteOS-M 固件（含 can 驱动+协议代码）在 CPU3 启动
④ 协议代码初始化 CAN 口       CanInit(cfg) → CanStart(dev)
⑤ 收发循环                    CanSend / CanRecv / CanGetBusState / CanGetStats
⑥ 收尾                        CanStop
```

从核固件通过 MCS 通道加载（`rpmsg_demo -f /data/liteos-m.elf -c 3`），混合
部署环境搭建见 MCS 子系统指南 `../USER_GUIDE.md`。

---

## 3. 主核侧操作

### 3.1 DTS 配置（一次性）

两种方式**二选一，勿同时使用**（同 label 会编译报错）：

- **a)** 板级 dts `#include "rk3568-mcs-can.dtsi"`（本目录独立副本）；
- **b)** `mcs/dts/rk3568-ohrt-mcs.dtsi` 已内嵌同节点（toybrick/DAYU210 默认，
  含在 dts 三合一 patch 里），无需再改。

节点内容（已含于上述 dtsi）：

```dts
mcs_can1: mcs-can1 {
        compatible = "oh_rt,mcs-can1";
        reg = <0x0 0xfe580000 0x0 0x1000>;
        clocks = <&cru CLK_CAN1>, <&cru PCLK_CAN1>;
        clock-names = "baudclk", "apb_pclk";
        resets = <&cru SRST_CAN1>, <&cru SRST_P_CAN1>;
        reset-names = "can", "can-apb";
        pinctrl-names = "default";
        pinctrl-0 = <&can1m1_pins>;
        oh_rt,irq-intid = <34>;      /* 从核自配置，此处仅交叉核对 */
        oh_rt,ctrl-phys = <0x0 0x740ff100>;
};

/* 可选第二口：默认 disabled，板级 dts 将 status 改 "okay" 启用 */
mcs_can2: mcs-can2 {
        compatible = "oh_rt,mcs-can2";
        reg = <0x0 0xfe590000 0x0 0x1000>;
        clocks = <&cru CLK_CAN2>, <&cru PCLK_CAN2>;
        clock-names = "baudclk", "apb_pclk";
        resets = <&cru SRST_CAN2>, <&cru SRST_P_CAN2>;
        reset-names = "can", "can-apb";
        pinctrl-names = "default";
        pinctrl-0 = <&can2m0_pins>;
        oh_rt,irq-intid = <35>;
        oh_rt,ctrl-phys = <0x0 0x740ff200>;
        status = "disabled";
};
```

> 基础 `can@fe580000`/`can@fe590000` 节点在 `rk356x-oh.dtsi` 保持 disabled，
> Linux 的 rockchip_canfd 不会绑定，`mcs-can1`/`mcs-can2` 是唯一绑定。
> 引脚说明：can1m0 组（GPIO1_A0/A1）与 i2c3m0 同脚，被板上 rt5670 音频
> codec 占用，**本板不可用**；can1m1（GPIO4_C2=RX / GPIO4_C3=TX，func3）
> 是唯一可用组。若你的板子 CAN 收发器实际接在 GPIO1_A0/A1，需先释放
> i2c3 并把 `pinctrl-0` 指回 `&can1m0_pins`。
> CAN2 引脚：can2m0（GPIO4_B4=RX / GPIO4_B5=TX，func3）为默认组，
> can2m1（GPIO2_B1/B2，func4）为备选；回环验证不出管芯，引脚仅对真实
> 总线有意义（pinmux 失败非致命）。

**启用 CAN2 的操作**（一次性）：

1. 板级 dts 覆盖 `&mcs_can2 { status = "okay"; };`（或直接改 dtsi）；
2. 重编 boot（dts 变更）并烧录；
3. `insmod mcs_can.ko` 后 `dmesg` 应出现 `CAN2` probe 日志、
   `/sys/devices/platform/mcs-can2/state` 就绪；
4. 从核 `CanInit` 用 `port_id = CAN_PORT_CAN2_RK3568` 即得第二口（与
   CAN1 并发：两口各持一个 `CanDevice*`，互不干扰）。

### 3.2 构建模块

```sh
# 前置：OpenHarmony 全源码树且 kernel/linux-6.6 已编译过（out/kernel/src_tmp 存在）
export CODE=<OpenHarmony 完整源码树>
./build_mcscan.sh
# 产物：当前目录 mcs_can.ko（已用内核签名密钥签名），日志在 ../out/logs/
```

### 3.3 加载与卸载

```sh
hdc file send mcs_can.ko /data/mcs_can.ko
hdc shell insmod /data/mcs_can.ko
dmesg | grep mcs_can          # step0..5 全 OK 即就绪
hdc shell cat /sys/devices/platform/mcs-can1/state
# 卸载（从核应已 CanStop；rmmod 后 ctrl → OFFLINE，从核 CanInit 会拒绝）
hdc shell rmmod mcs_can
```

与其他模块无顺序依赖：`mcs_km.ko`（AMP 框架）先行，`mcs_net.ko`/`mcs_can.ko`
相互独立、可各自加载（混合固件两者都要，见 §5.3）。

### 3.4 主核侧诊断

```sh
cat /sys/devices/platform/mcs-can1/state     # CAN2 同理：mcs-can2/state
# 输出示例（成功时）：
#   port=1
#   step0: clocks: OK, baudclk=100000000Hz apb=...Hz (held on)
#   step1: resets: pulsed (can + can-apb)
#   step2: pinmux: can1m1 group applied
#   step3: can-io: bus probe OK, MODE=0x...
#   step4: cpu version: 0 (0 = unknown, cpuinfo off)
#   step5: ctrl: READY at 0x740ff100
#   baudclk=100000000Hz pclk=...Hz cpu_version=0
#   ctrl master=1 slave=2      ← master=1(READY) slave=2(RUNNING)
```

probe 卡在哪一步，`dmesg` 里 `stepN:` 的最后一行就是故障点（时钟/复位/
pinmux/总线探活/ctrl 发布）；`port=N` 行标明该节点绑定的控制器号（与从核
`port_id`、ctrl 页 `port_index` 三方一致）。

---

## 4. 从核侧 API 参考（协议层视角）

```c
#include "can.h"   // device/board/rockchip/rk3568/liteos_new/board/driver/can/
```

共 7 个接口，全部无动态内存、可重入于单任务上下文（Recv/GetStats 可从
任意任务调用；Send 与 Start/Stop 需同一任务串行）。

### 4.1 初始化配置 CanConfig

```c
typedef struct {
    uint32_t port_id;        /* CAN_PORT_CAN1_RK3568 / CAN_PORT_CAN2_RK3568
                                 （选口；两口可并发，每口一个 CanDevice） */
    uint32_t mode;           /* 收包模式（见 4.1.1） */
    uint32_t work_mode;      /* 总线工作模式（见 4.1.2） */
    uint32_t use_raw_timing; /* 0 = bitrate 自动求解（默认） */
    uint32_t bitrate;        /* bits/s，默认 500000 */
    CanBittiming timing;     /* raw brp/sjw/ts1/ts2（use_raw_timing=1 时生效） */
    uint32_t filter_en;      /* 0 = 全收（默认） */
    uint32_t filter_code;    /* 验收码（29-bit id 空间） */
    uint32_t filter_mask;    /* 验收屏蔽：掩码位不参与比较；0 = 精确匹配 code */
} CanConfig;
```

**4.1.1 收包模式 mode（运行期配置，非编译期）**

| 值 | 模式 | 行为 |
|---|---|---|
| `CAN_MODE_IRQ`（默认 0） | 中断接收 | ISR 收帧入 32 深就绪队列，`CanRecv` 只出队；`irq_count` 增长 |
| `CAN_MODE_POLL` | 纯轮询 | 完全不注册中断（GICD 零触碰），`CanRecv` 直接收割硬件 FIFO；`irq_count` 恒 0 |
| `CAN_MODE_HYBRID` | 中断+轮询兜底 | ISR 先收，`CanRecv` 再补收一遍（兜住丢中断）；两者都计数 |

**4.1.2 总线工作模式 work_mode**

| 值 | 模式 | 行为 |
|---|---|---|
| `CAN_WORK_NORMAL`（默认 0） | 正常收发 | 正常上总线，应答/重传/错误处理全硬件 |
| `CAN_WORK_LOOPBACK` | 片内回环 | 自发自收 + 自应答，引脚无输出：**零硬件验证模式**（语义见 §6.1） |
| `CAN_WORK_SILENT` | 只听 | 总线监视：只收不发、不应答 |
| `CAN_WORK_SILENT_LB` | 静默回环 | 回环 + 只听组合（不出引脚、不参与总线） |

**4.1.3 位时序**

- **bitrate 风格（默认）**：驱动对主核发布的**真实读回** baudclk（100MHz）
  求解分频，采样点自动取 ~87.5%（CiA 推荐）。支持 125k/250k/500k/1M 等。
  **800kbps 不可达**：100M/800k=125，分频器步进 2，无偶数解（`CanInit`
  返回 `-EINVAL`）。
- **raw 风格**：`use_raw_timing=1` 时 `brp/sjw/ts1/ts2` 直写 NBTP（偶数
  brp 2..512、各段 1..128，范围外返回 `-EINVAL`），供时序专家微调。

### 4.2 API 一览

| 接口 | 语义 |
|---|---|
| `CanInit(cfg, &dev)` | 按 `cfg->port_id` 选口 + 校验配置 + 主核门禁 + 软复位模式编程（滤波/FIFO/模式位/位时序），停在 READY。RUNNING 中需先 Stop；口未让渡返回 `-ENODEV` |
| `CanStart(dev)` | 注册该口 INTID（CAN1: 34 / CAN2: 35，IRQ/HYBRID）+ 开中断掩码（POLL 全掩）+ 退出软复位上总线 |
| `CanStop(dev, timeout_us)` | 掩全部中断 → 回软复位 → 注销 IRQ → 清就绪队列（无 DMA，即刻静止） |
| `CanSend(dev, frame, timeout_us)` | 同步发一帧：单帧深 TX FIFO 握手，有界等待完成（超时 `-ETIMEDOUT`） |
| `CanRecv(dev, frame)` | 非阻塞收一帧；无帧返回 `-EAGAIN`（POLL/HYBRID 先收割硬件 FIFO） |
| `CanGetBusState(dev, &info)` | 总线状态快照（error-active/warning/passive/bus-off + 错误计数） |
| `CanGetStats(dev, &stats)` | 收发统计（见 4.4） |

### 4.3 帧契约 CanFrame

```c
typedef struct {
    uint32_t id;        /* 11-bit 0x0..0x7ff；带 EFF 时 29-bit 0x0..0x1fffffff */
    uint8_t  flags;     /* CAN_FRAME_EFF（扩展帧）| CAN_FRAME_RTR（远程帧） */
    uint8_t  dlc;       /* 数据长度 0..8（>8 返回 -EMSGSIZE） */
    uint8_t  reserved;
    uint8_t  data[8];   /* RTR 帧不含数据（比较时跳过） */
} CanFrame;
```

经典 CAN 2.0B：标准/扩展 id、数据/远程帧、0..8 字节。**CAN FD 不支持**
（RK3568 CAN IP 以 2.0 模式使用；收到 FDF 位置位的帧计 `rx_errors`）。

验收滤波（单 code/mask 组，作用于 29-bit id 空间）：
`filter_en=1` 时收 `(id & ~mask) == (code & ~mask)` 的帧；`mask=0` 精确
匹配 `code`。5 个辅助滤波组保持全开（内核 start 对齐）。

### 4.4 错误码与统计

| 错误码 | 含义 |
|---|---|
| `0` | 成功 |
| `-EAGAIN`（-11） | 无帧可收（Recv 非阻塞语义） |
| `-EINVAL`（-22） | 配置非法（id 超界/时序无解或超范围/mode 值） |
| `-ENODEV`（-19） | 主核 ko 未就绪（ctrl 门禁）或 port_id 不匹配 |
| `-EBUSY`（-16） | RUNNING 中重复 Init（先 Stop） |
| `-EIO`（-5） | 未 Start 就 Send/Recv，或 Start 时 IRQ 注册失败 |
| `-ETIMEDOUT`（-110） | 发送有界等待超时（如 LOOPBACK 之外无应答的死总线） |
| `-EMSGSIZE`（-90） | dlc > 8 |

`CanStats` 关键字段：`tx_accepted/tx_completed/tx_timeouts`（发送三态）、
`rx_delivered/rx_dropped/rx_errors`（接收通路）、`rx_dup_suppressed`
（回环重复帧被驱动吞掉数，正常=回环模式下每帧恰 0，见 §6.1）、
`irq_count`（POLL 恒 0）、`bus_off_count`。

---

## 5. 典型使用模式

### 5.1 收包模式怎么选

- **默认 IRQ**：负载不密集、要省 CPU——事件驱动，任务阻塞在信号量/事件上。
- **POLL**：最高确定性（协议周期固定，如 1ms 轮询总线）——零中断抖动，
  GICD 零触碰，`CanRecv` 空转开销 ~微秒级。
- **HYBRID**：IRQ 为主 + 轮询兜底（防单次中断丢失导致 FIFO 溢出覆盖）。

### 5.2 工作模式怎么选

- 上板第一阶段用 **LOOPBACK** 验证驱动+协议栈（零硬件，内置 demo 即此）；
- 接入真实总线切 **NORMAL**（外部总线需 CAN 收发器：can1m1 引脚在
  toybrick/DAYU210 未焊接收发器，需自行外接）；
- 总线分析/被动监听用 **SILENT**（不应答、不发送，不影响总线负载）。

### 5.3 内置用户 demo（can_test.c）解读

`LOSCFG_MCS_CAN` 打开时 `main.c` 自动拉起，13 阶段全片内回环（零硬件）：

```
lb-irq-500k    20 帧，IRQ 接收 + 500kbps     （自协商求解时序）
lb-poll-500k   20 帧，纯轮询（无中断注册）
lb-hyb-500k    20 帧，HYBRID 接收（ISR 抢收 + Recv 兜底，irq 计数 >0）
lb-bd-125k     10 帧，125kbps
lb-bd-250k     10 帧，250kbps
lb-bd-1m       10 帧，1Mbps
lb-raw-500k    10 帧，raw brp/sjw/ts1/ts2（=500k 求解值）
lb-ext-data    10 帧，扩展 id（29-bit）数据帧
lb-std-rtr     10 帧，标准 id 远程帧
lb-dlc-sweep   18 帧，dlc 0..8 边界扫描
lb-flt-even    20 帧，验收滤波：code/mask 只放行偶数 id——奇数 id 必须在
               硬件滤波器中消失（~10ms 静默窗口内 Recv 不得返回帧，泄漏即
               失败），偶数 id 正常回显校验；预期 echoed 10 / filtered 10
lb2-irq-500k   20 帧，CAN2 口 IRQ 接收（可选：口未让渡时 skipped）
lb2-poll-500k  20 帧，CAN2 口纯轮询（可选：同上）
```

每帧 Send 后有限重试收回显并逐字段比对（id/flags/dlc/payload）。判定：
每阶段 `Stop: 0 (sent N, echoed N [+ filtered K], errors 0)` + stats 行
`rx dN = tx aN`、`dup0`、`to0`，结尾 `[can] all phases done` 无
`WITH FAILURES` 即全绿。demo 同时也是协议任务的参考模板——把
`DemoRunPhase` 换成你的协议循环即可。

与网卡 demo 共存：`LOSCFG_MCS_NET` + `LOSCFG_MCS_CAN` 同开即混合固件
（`[net]` 与 `[can]` 输出交错为正常）。

---

## 6. 硅片特性与已知问题（必读）

### 6.1 LOOPBACK 语义（实测钉死的硅事实）

- **回环组合**：`LBACK|SELF_TEST|SILENT`（主线 rkcanfd 语义——LBACK 提供
  自应答；无 LBACK 时死总线上 TX 永不完成，Send 返回 -110）。
- **RXSTX 常开**：`MODE_RXSTX` 是自发帧进 RX FIFO 的**唯一路径**，驱动在
  `CanInit` 一次性锁存常开（主线 rkcanfd 同策略）。注意 OH 6.6 内树
  rockchip_canfd 对标准帧清 RXSTX，其回环模式在本板**必失败**（RX 恒 0）
  ——这是从核侧驱动不复用该行为的原因。
- **NORMAL 模式的自收帧 = 回显语义**：CAN 协议要求发送器监视总线，
  RXSTX 常开意味着**正常模式下你也会收到自己发出的每一帧**（echo）。
  协议层应按回显处理（跳过或用于发送确认），这与 SocketCAN 的
  `CAN_RAW_RECV_OWN_MSGS` 行为一致。
- **重复帧抑制**：LBACK+RXSTX 下自发帧可能以两份 FIFO 元素出现（时间
  邻近）。驱动按 FIFO 时间戳窗口（25µs @100MHz ts）自动吞掉第二份
  （任何合法重复帧在支持的最高速率下不可能 25µs 内完成——dlc0@1M 也要
  ~47µs，无误吞风险），计入 `rx_dup_suppressed`。
- **erratum 5（RX FIFO 计数器滞后）**：pop 一帧后 RXFC 计数器不立即归零，
  立即重读会弹出幽灵元素。驱动在 drain 的每次 pop 后沉降 ~20µs 再读
  （`can_settle_delay`），实测彻底消除（最终验证 `dup0` 且无错帧）。

### 6.2 其他硅片行为

- **V1 硅片 EXT 帧自发回环**（cpu_version<3）：正常模式下自发**扩展帧**
  也会回环进 RX FIFO（内核驱动带软件规避）。本驱动不处理（仅影响外接
  总线 + 扩展 id 场景，回环 demo 无此问题）。cpu_version 经 ctrl 块发布
  （0 = 未知：本内核未开 CONFIG_ROCKCHIP_CPUINFO）。
- **V1 空帧**：RX 通路可能交付 dlc==0 && id==0 的空帧，驱动按内核同款
  逻辑跳过。
- **800kbps 不可达**（100MHz 位时钟数学约束，见 4.1.3）。
- **TX FIFO 单帧深**：CanSend 同步握手，不排队——连续发送即逐帧等待，
  500kbps 一帧最坏 ~260µs（dlc8），调用方按此预算超时。

---

## 7. 多 CAN 口 / 换板扩展

**多口并发机制已实现**（与网卡 `net.h` 同一机制，软件层两口可同时使用；**CAN2 硬件通路尚未上板验证，当前请使用 CAN1**）：

1. **port_id**：`CAN_PORT_CAN1_RK3568 = 1`、`CAN_PORT_CAN2_RK3568 = 2`；
   `CanInit` 按口返回独立的 `CanDevice*`，两口各持一个、并发使用互不干扰
   （独立硬件 FIFO、独立中断 34/35、独立 ctrl 页与就绪队列，从核驱动主体
   `can_rk3568.c` 全实例化，无共享可变状态）；
2. **板级 profile**：每控制器一个 `canN_profile.h`（基址/INTID/ctrl 页/
   FIFO 深度/默认波特率——`can1_profile.h`/`can2_profile.h`），驱动主体复用；
3. **主核 dts 节点**：每控制器一个 `mcs-canN` 节点；`mcs_can.ko` 单模块多口
   ——compatible 的 match data 选口，节点属性（reg/irq-intid/ctrl-phys）即
   事实源；ctrl 块按 0x100 步进分页（CAN1: 0x740ff100，CAN2: 0x740ff200）；
4. **ctrl 契约**：`can_ctrl.h` 的 `port_index` 与从核 `port_id` 交叉核对，
   错配返回 `-ENODEV`；
5. **CAN2 为 opt-in**：dts 中 `mcs-can2` 默认 `disabled`，启用步骤见 §3.1；
   未启用时从核 `CanInit(CAN2)` 返回 `-ENODEV`（demo 显示 skipped）。

**换其他 CAN 控制器/换板**：新控制器只需①主核 dts 加一个 `mcs-canN` 节点
（时钟/复位/pinmux/ctrl-phys 按控制器填，ctrl 页按 0x100 步进分配新页）+
②从核加一个 `canN_profile.h` 并在 `g_ports[]` 表添一行，`Can*` API 与协议
层零改动。

---

## 8. 常见问题排查

| 现象 | 原因与处理 |
|---|---|
| 从核 `[can] master mcs_can.ko not up ... CAN2 clocks off` + demo `skipped` | **CAN2 节点未启用**（dts 默认 disabled）：设计行为。需要第二口时按 §3.1 启用 |
| 从核 `[can] master mcs_can.ko not up`（CAN1） | 主核 ko 未加载/已 rmmod。`insmod` 后看 dmesg step 全 OK 再重启从核 demo |
| `CanInit` 返回 -EINVAL 且打印 "no bit timing" | 多半是 800kbps（数学不可达）或 bitrate=0 且未给 raw；换档位或用 raw |
| Send 返回 -110 超时 | LOOPBACK 之外的死总线（无应答者）；确认 work_mode 或接好总线 |
| RX 恒 0 但 TX 正常 | 对端未发帧/滤波配置过严（`filter_en` 关掉试试）/SILENT 模式忘记切回 |
| 回环模式下偶发 mismatch | 不应发生；若复现记录 `dup-miss` 哨兵打印并反馈（去重窗口需调整） |
| demo 后期 rx 不再增长 | 检查 `[can] ... rx dN` 是否与 tx 对齐；`rx_overflow` 非零=任务消费不及时（提优先级或改 POLL） |

---

## 9. 附：实测记录摘要（RK3568 DAYU200，主核 Linux 6.6 / 从核 CPU3 AArch64 EL1）

- **CAN-only 固件**（2026-10-07）：9 阶段全绿——`sent=echoed`（20/20/10/
  10/10/10/10/10/18）、errors 0、`dup0`（erratum 5 沉降修复后幽灵元素
  消除）、`to0`；IRQ/POLL 双收包模式、125k~1M 四档波特率、raw 时序、
  扩展帧、RTR、dlc 0..8 扫描全通过。
- **混合固件（NET+CAN 同开）**（2026-10-07）：CAN 9 阶段同样全绿；
  NET demo（强制 100M FD + POLL）并行运行 link=1/100M/FD、tx 千帧级
  无错、rx 正常收包；两驱动共存无干扰，rpmsg 通道正常（rtt ~108µs）。
- **多口并发重构**（2026-10-08）：从核 `can_rk3568.c` 全实例化（每口独立
  `CanDevice`）+ 主核 `mcs_can.ko` 参数化（单模块多口，dts 事实源）。
  混合固件回归 11 阶段：CAN1 9 阶段全绿（与重构前逐项一致：sent=echoed、
  dup0、to0、errors 0）；CAN2 2 阶段在未启用 dts 节点的板上正确显示
  `skipped (no master handover for port 2)`；`all phases done` 无失败；
  NET demo 并行正常（rpmsg rtt ~59µs）。CAN2 双口实跑需板级 dts 启用
  can2 节点后重编 boot（步骤见 §3.1）。
- **HYBRID 收包 + 验收滤波补测**（2026-10-08）：demo 扩至 13 阶段。
  `lb-hyb-500k` 20/20 全收且 `irq40>0`（ISR 抢收与 Recv 兜底双路径均
  工作）；`lb-flt-even` sent 20 / echoed 10 / filtered 10 / errors 0——
  code=0x100、mask=0x7fe 下奇数 id 在硬件滤波器中**零泄漏**消失，且
  证明 LOOPBACK+RXSTX 回显路径同样过硬件滤波器。同轮修复驱动滤波分支
  `mask=0` 语义（此前 fallthrough 全开，现为精确匹配 code，对齐 §4.1
  契约）；其余 9 个 CAN1 阶段与上轮逐项一致，`all phases done` 无失败。
- 回归环境：`mcs_km.ko + mcs_can.ko`（混合另加 `mcs_net.ko`），
  `rpmsg_demo -f liteos-*.elf -c 3`，串口 1500000。
