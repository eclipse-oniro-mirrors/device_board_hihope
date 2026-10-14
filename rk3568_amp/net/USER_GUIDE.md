# RK3568 AMP 网卡让渡用户使用指南

> 对象：在 RK3568（toybrick/DAYU210）AMP 双系统上，把 GMAC1 让渡给 LiteOS-M
> 实时核（CPU3）作为**协议无关实时网口**使用的协议开发者/集成者。
> 主核侧驱动：本目录 `mcs_net.c`（Linux 6.6 out-of-tree 模块）。
> 从核侧驱动：`device/board/rockchip/rk3568/liteos_new/board/driver/net/`
> （`net.h` 公开 API + `net_dwmac.c` 实现）。
> 设计细节见 `mcs_net.c` 头部注释（本文只讲"怎么用"）。

---

## 1. 概述

### 1.1 这是什么

RK3568 有两个 GMAC。默认两个口都归 Linux；本方案把 **GMAC1**（`0xFE010000`，
RGMII，裕太 YT8531S PHY）整个数据面**让渡**给运行 LiteOS-M 的实时核 CPU3，
用于 EtherCAT 等对 determinism 敏感的链路层协议。驱动只提供**帧级收发接口**，
不绑定任何协议——协议层（如 SOEM 移植、私有协议栈、裸以太网实验）只链接
`net.h`，自行组织帧内容。

```
                RK3568 (4 x Cortex-A55)
 ┌─────────────────────────────────────────────────────┐
 │  CPU0-2: OpenHarmony Linux 6.6        CPU3: LiteOS-M │
 │                                                      │
 │  ┌────────────┐   硬件让渡(handover)     ┌────────────┐
 │  │ mcs_net.ko │ ──────────────────────► │ net 驱动   │
 │  │ 仅初始化+  │   ctrl block @0x740ff000 │ 数据面     │
 │  │ 链路轮询   │   (net_ctrl.h 契约)      │ Net* API   │
 │  └────────────┘                          └────────────┘
 │  Linux eth0(GMAC0) 正常使用              协议层(用户代码)
 └─────────────────────────────────────────────────────┘
          │GMAC0               │GMAC1
          ▼                    ▼
       普通网口            实时网口(RGMII + YT8531S)
```

### 1.2 职责划分（重要约定）

| 事项 | 主核 (Linux) | 从核 (LiteOS-M) |
|---|---|---|
| GMAC1 时钟（含 clk_mac_speed 跟速） | **配置并保持常开** | 不碰 |
| CRU 复位 / RGMII pinmux / GRF 延迟 | **配置** | 不碰 |
| PHY（MDIO 读写、自协商） | **probe 期配置后独占 MDIO**，之后仅 1s 轮询读 | 强制速率模式可写 PHY BMCR（见 4.2） |
| DMA / MTL / MAC 寄存器 | 不碰 | **独占** |
| GMAC1 中断（INTID 64） | **不配置 GICD**（只交叉检查 dts 值） | `ArchHwiCreate` 端到端自配（每核 GICD 所有权） |
| 共享控制块 0x740ff000 | 写 master_state/链路快照 | 写 slave_state |
| 报文收发 | **零参与** | 全权 |

---

## 2. 端到端流程总览

```
① 板级 dts 集成（一次性）      rk3568-mcs-net.dtsi 并入板级 dts，刷 boot_linux
② 主核构建+加载               build_mcsnet.sh → insmod mcs_net.ko → step0..8 READY
③ 从核固件运行                LiteOS-M 固件（含 Net 驱动+协议代码）在 CPU3 启动
④ 协议代码初始化网口           NetInit(cfg) → NetStart(dev)
⑤ 收发循环                    NetSend / NetRecv / NetGetLinkInfo / NetGetStats
⑥ 收尾                        NetStop
```

从核固件通过 MCS 通路加载（如 `rpmsg_demo -f /data/liteos-m.elf -c 3`），
混合部署环境搭建（CPU3/内存预留、内核重编、上板部署）见 MCS 子系统完整指南
`device/board/hihope/rk3568_amp/USER_GUIDE.md`（其 dts 补丁已含本驱动的
`mcs-net-gmac1` 节点，无需重复改 dts）。**时序要求：从核 `NetInit` 之前
主核 `mcs_net.ko` 必须**已加载完成（ctrl 块 READY）——`NetInit` 第一步就
校验 ctrl 块 magic/version/`master_state`，不通过直接返回 `-ENODEV`
（GMAC1 时钟未开时访问其寄存器会挂总线，该门禁先于一切寄存器访问）。

---

## 3. 主核侧操作

### 3.1 DTS 配置（一次性）

将本目录 `rk3568-mcs-net.dtsi` 并入板级 dts（如 `rk3568-toybrick-x0-linux.dts`
`#include`，或折叠进 kernel.patch 的 dts 注入链），其效果两件事：

1. `&gmac1 { status = "disabled"; }` —— Linux stmmac 不再绑定 eth1，Linux
   侧不会再出现 eth1 网口（这是让渡的前提）；
2. 声明 `mcs-net-gmac1` 节点，供 `mcs_net.ko` 匹配（`compatible =
   "oh_rt,mcs-net-gmac1"`）。

节点属性说明：

| 属性 | 必需性 | 说明 |
|---|---|---|
| `compatible` | 必需 | `"oh_rt,mcs-net-gmac1"`，驱动匹配串 |
| `reg` | 必需 | GMAC1 寄存器空间。驱动实际只 ioremap 并使用 **MDIO 页**（基址+0x200/0x204），其余空间归从核 |
| `rockchip,grf` | 必需 | GRF 句柄，用于写 RGMII 延迟 |
| `clocks` + `clock-names` | 必需 | 按名引用：`stmmaceth / mac_clk_rx / mac_clk_tx / clk_mac_refout / aclk_mac / pclk_mac / clk_mac_speed / ptp_ref / pclk_xpcs / clk_xpcs_eee`。驱动全部使能并**保持**（防 runtime-pm 门控） |
| `assigned-clocks` 等 | 建议 | RGMII 时钟树父级切换 + 125MHz 基准（对齐内核 dwmac-rk 的 set_rgmii） |
| `resets` + `reset-names` | 必需 | `SRST_A_GMAC1`，probe 释放复位 |
| `pinctrl-0` | 必需 | RGMII 引脚组（`gmac1m1_*` 五组） |
| `phy-reset-gpios` | 建议 | PHY 复位脚（低有效）。缺省则跳过 PHY 硬复位 |
| `tx_delay` / `rx_delay` | 必需 | GRF RGMII 延迟；**缺省按 0 处理**，必须给板级实测值（toybrick：0x37 / 0x0f） |
| `oh_rt,irq-intid` | 建议 | **仅交叉检查**用：与从核期望 INTID（64）不一致时主核打 warn，主核不配置中断 |
| `oh_rt,ctrl-phys` | 信息 | 共享控制块物理地址记录（0x740ff000）；实际地址编进两侧 `net_ctrl.h`，此属性不参与运行 |

### 3.2 构建模块

```sh
# 前置：OpenHarmony 全源码树且 kernel/linux-6.6 已编译过（out/kernel/src_tmp 存在）
export CODE=/path/to/openharmony    # 也可由脚本从脚本位置向上自动探测 prebuilts/clang
cd device/board/hihope/rk3568_amp/net
./build_mcsnet.sh
# 产物：当前目录 mcs_net.ko（已用内核签名密钥签名），日志在 ../out/logs/
```

### 3.3 加载与卸载

```sh
hdc file send mcs_net.ko /data
insmod /data/mcs_net.ko
```

成功的 dmesg（节选，`step0..step8` 是 probe 进度追踪——**最后一行打印在哪里，
卡住/失败就在哪一步**）：

```
[mcs_net] module init: registering driver
[mcs_net] probe enter
[mcs_net] clk stmmaceth ON (125000000 Hz) ...（6 个时钟全开）
mcs-net-gmac1 ... step0: clocks: OK (named, held)
mcs-net-gmac1 ... step1: clk_mac_speed: 125MHz
mcs-net-gmac1 ... step2: reset: deasserted
mcs-net-gmac1 ... step3: pinmux: RGMII group applied
mcs-net-gmac1 ... step4: grf: RGMII tx_delay=0x37 rx_delay=0x0f
mcs-net-gmac1 ... step5: mac-io+phy-reset: OK
[mcs_net] MAC version reg = 0x00000000 (0x00000051 expected)   ← 已知无害，见 8
[mcs_net] about to read MDIO (hang here = clock/pinmux wrong)
mcs-net-gmac1 ... step6: phy: id=0x4f51e91a ...
mcs-net-gmac1 ... step7: ctrl: READY at 0x740ff000
mcs-net-gmac1 ... step8: link poll: 1s work started
[mcs_net] driver registered: 0 (probe may still be running)
```

卸载：`rmmod mcs_net`。remove 会把 ctrl 块 `master_state` 置 `OFFLINE`，
时钟/复位/GPIO 随 devm 释放——**整个让渡随之消失**，正常使用不建议在线卸载。

### 3.4 主核侧诊断

```sh
# probe 步骤回放 + ctrl 块实时状态
cat /sys/devices/platform/mcs-net-gmac1/state
# 输出示例（成功时）：
#   step0: clocks: OK (named, held)
#   ...
#   step8: link poll: 1s work started
#   phy id=0x4f51e91a link=1 changes=2
#   ctrl master=1 link=1 valid=1 slave=2      ← master=1(READY) slave=2(RUNNING)

# PHY 寄存器直读（YT8531S 含扩展寄存器 0xA001/0xA003 延迟诊断）
cat /sys/devices/platform/mcs-net-gmac1/mdio
```

`state` 中 `master=1` 而 `slave=0`：主核就绪但从核还没 `NetStart`；
`slave=2`：从核运行中（RUNNING）。probe 失败时只回放到失败步骤为止。

---

## 4. 从核侧 API 参考（协议层视角）

从核侧使能：板级 Kconfig `MCS_NET`（依赖 `MCS`），vendor config 打开
`LOSCFG_MCS_NET=y`（当前该开关同时编入 `net_test.c` 用户 demo，见 5.3）。

> **注意**：`LOSCFG_MCS_NET` 已在 morpheus 默认开启
> （`vendor/rockchip/rk3568_amp/kernel_configs/*.config`）。90a4b6251
> 基线上曾默认注释（UART 台架验证期间），旧基线源码构建需手动取消
> 注释；从核 SDK 包内 `config.h` 一直默认打开。

协议代码只需要：

```c
#include "net.h"     // device/board/rockchip/rk3568/liteos_new/board/driver/net/
```

`NetDevice` 对协议层**不透明**（内部为板级驱动的设备实例），不要解引用。

### 4.1 初始化配置 NetConfig

```c
typedef struct {
    uint32_t port_id;       /* NET_PORT_*：使用哪个被让渡的网口 */
    uint32_t mode;          /* NET_MODE_IRQ / NET_MODE_POLL / NET_MODE_HYBRID */
    uint32_t autoneg;       /* 1=自协商(默认)，0=强制 */
    uint32_t speed_mbps;    /* 仅强制模式：10 或 100 */
    uint32_t full_duplex;   /* 仅强制模式：0/1 */
    uint32_t promisc;       /* 1=混杂(默认)，0=按 MAC 过滤 */
} NetConfig;
```

| 字段 | 取值与语义 |
|---|---|
| `port_id` | `NET_PORT_GMAC1_RK3568`（=1）：主核让渡的 GMAC1。多网口扩展见第 7 章 |
| `mode` | `NET_MODE_IRQ`（0，默认）：中断驱动，ISR 收帧入内部就绪队列，`NetRecv` 拷出；`NET_MODE_POLL`：**完全不注册中断**，`NetRecv` 直接从 DMA 环收割（`irq_count` 恒 0，可作断言）；`NET_MODE_HYBRID`：中断为主 + `NetRecv` 兜底收割。**运行模式在 `NetInit` 时配置，非编译期** |
| `autoneg` | 1：使用主核配置的自协商结果（10/100/1000），结果由 `NetGetLinkInfo` 发布。0：从核直接写 PHY BMCR 强制速率（主核 probe 后不再写 MDIO，无竞争）。**1000M 不能强制**（IEEE 802.3 规定 1000BASE-T 必须自协商），返回 `-EINVAL` |
| `speed_mbps` / `full_duplex` | 仅 `autoneg=0` 生效：10/100 + 全/半双工 |
| `promisc` | 1：收一切帧（EtherCAT/抓包语义，默认）。0：单播按站地址 MAC 过滤 + 广播/多播放行（站地址 = `GMAC1_DEFAULT_MAC`，见 6.2） |

**强制模式注意**：强制链路要求**对端同样强制**。对端若跑自协商，会 parallel-detect
出一个通常为**半双工**的链路——经典双工失配（TX 正常、RX 丢/错）。这是协议
特性不是驱动缺陷（内置 demo 即强制模式：对端不强制时 demo 能验证强制管道与
TX，不保证回环流量，见 5.2/5.3）。

### 4.2 API 一览

所有返回值为 0 成功、负数为错误（POSIX errno 语义，见 4.4）。
**未设计多任务并发安全**：按单一协议任务串行调用（参照 `net_test.c`）；
如需多生产者/消费者，上层自行加锁。

```c
int  NetInit(const NetConfig *cfg, NetDevice **dev);
int  NetStart(NetDevice *dev);
int  NetStop(NetDevice *dev, uint32_t timeout_us);
int  NetSend(NetDevice *dev, const uint8_t *frame, uint32_t len);
int  NetRecv(NetDevice *dev, uint8_t *frame, uint32_t capacity, uint32_t *len);
int  NetGetLinkInfo(NetDevice *dev, NetLinkInfo *info);
int  NetGetStats(NetDevice *dev, NetStats *stats);
```

#### NetInit — 配置并获取设备实例

- 校验 `cfg`（如强制 1000M → `-EINVAL`；`port_id` 无效 → `-ENODEV`），
  按板级 profile 注册的端口创建实例，应用 `mode`/链路策略/收包过滤。
- **要求主核已 READY**：第一步校验 ctrl 块（magic/version/`master_state`），
  不通过返回 `-ENODEV`，不碰任何 GMAC 寄存器（时钟未开时访问会挂核）；
  通过后一次性完成全部硬件初始化（PHY 链路策略、DMA/MTL 编程），
  `NetStart` 只负责引擎启动，保持启动路径短、抖动小。
- 允许在 READY 状态重复 `NetInit`（Stop→Init→Start 换配置重配是驱动
  支持的用法，开发期多相位自测验证过）。

#### NetStart — 启动数据面

- 前置：`NetInit` 已成功（设备处于 READY 状态）；主核 ctrl 块校验已在
  `NetInit` 第一步完成（ko 未加载/版本不一致在那时即返回 `-ENODEV`）。
- 动作：PHY 链路策略生效（强制模式写 BMCR）、DMA 描述符环/缓冲池编程、
  MAC 过滤与混杂设置、（IRQ/HYBRID 模式）`ArchHwiCreate` 注册 INTID 64
  （优先级 LOS 4；每核 GICD 所有权，从核自配路由到 CPU3），
  ctrl 块 `slave_state = RUNNING`。
- 返回 0 后才可 `NetSend/NetRecv`。

#### NetStop — 停止并回收

- 停 DMA/中断，有界超时（`DMA_DEBUG_STATUS`）等待在途发送写回；
  超时返回 `-ETIMEDOUT` 且 ctrl 块 `slave_state` 置 `FAULT`，
  成功则回到 `READY`（可再次 `NetStart`）。
- 停止后可重新 `NetInit`（换配置）或不再使用。

#### NetSend — 发送一帧

- `frame` 指向**完整以太网帧**（目的 MAC 在前，**不含 FCS**，MAC 硬件生成），
  `len` 合法范围 **14..1514**；不足 60B 由 MAC 自动填充。
- **拷贝语义**：帧内容拷入驱动自有的 TX 池缓冲，返回后调用者指针即可复用
  （异步完成，完成度看 `NetStats.tx_completed`）。
- `-EAGAIN`：TX 池满（对应 `tx_busy`），稍后重试；长度超限 `-EMSGSIZE`。

#### NetRecv — 接收一帧（非阻塞）

- `frame` 缓冲至少 `NET_FRAME_SIZE`（1536）字节才能收下任意合法帧；
  收到的一帧写入 `frame`，长度写入 `*len`（不含 FCS）。
- **拷贝语义**：拷出后驱动立即回收缓冲。无帧时返回 `-EAGAIN`——
  **不要忙等死循环不加延时**，轮询模式下参照 `net_test.c` 的周期节奏。
- POLL 模式：本调用直接从 DMA 环收割；IRQ 模式：从 ISR 投递的就绪队列取
  （队列满时 ISR 丢帧，计 `rx_overflow`）。

#### NetGetLinkInfo — 链路状态快照

```c
typedef struct {
    uint32_t link_up;        /* 0/1，来自 PHY BMSR（自协商与强制模式均有效） */
    uint32_t speed_mbps;     /* 自协商：协商结果；强制：配置值 */
    uint32_t full_duplex;    /* 0/1 */
    uint32_t valid;          /* 1=已采样（link_up==0 也是合法已采样状态） */
    uint64_t sample_time_ns; /* 采样时刻 */
} NetLinkInfo;
```

- 自协商模式下为**主核 1s 轮询**写入 ctrl 块的镜像：链路变化可见延迟
  最多约 1s（主核轮询周期），`link=1/0M` 表示协商尚未解析完，下一轮会更新。
- 协议启动建议：`NetStart` 后轮询本接口等 `link_up==1 && speed 正确` 再开跑。

#### NetGetStats — 驱动统计

```c
tx_accepted  Send() 受理数          tx_completed  DMA 写回完成数
tx_errors    发送错误汇总            tx_busy       Send() -EAGAIN 次数（池耗尽）
rx_delivered Recv() 拷出帧数         rx_dropped    坏帧/超长/队列溢出丢弃
rx_errors    接收错误汇总            rx_overflow   ISR 时就绪队列满
link_changes 链路变化计数（ctrl 镜像） dma_errors   FBE/CDE 异常事件
irq_count    中断到达次数（POLL 模式恒 0）
```

收发健康判定：`tx_accepted == tx_completed`（稳态）、`rx_delivered` 持续增长、
`tx_errors/rx_errors/rx_overflow/dma_errors == 0`。

### 4.3 帧契约

- 完整以太网帧：`[dst MAC 6B][src MAC 6B][ethertype 2B][payload 46..1500B]`，
  **不含 FCS**（收发两端硬件处理）；总长 14..1514，<60B 由 MAC 填充。
- 发送侧：`NetSend` 返回后调用方缓冲即可释放/复用；
  接收侧：`NetRecv` 返回后 `frame` 内容归调用方，缓冲可立即复用。
  两侧接口都**不持有调用方指针**（无零拷贝；确定性优先的简单契约）。

### 4.4 错误码

| 返回 | 含义 |
|---|---|
| 0 | 成功 |
| `-EAGAIN` (-11) | 无帧可收 / TX 池满 |
| `-EINVAL` (-22) | 配置非法（强制 1000M、长度 <14 等） |
| `-EMSGSIZE` (-90) | 帧长 >1514 |
| `-ENODEV` (-19) | `port_id` 无对应端口；或主核 ctrl 块无效（`mcs_net.ko` 未加载/已卸载，门禁拒绝） |
| `-EBUSY` (-16) | 状态冲突（如未 Stop 重复 Start） |
| `-EIO` (-5) | 硬件 IO 错误 |
| `-ETIMEDOUT` (-110) | `NetStop` 等待在途发送超时 |
| `-ENOTSUP` (-95) | 不支持的配置组合 |

---

## 5. 典型使用模式

### 5.1 中断 vs 轮询怎么选

| 模式 | 适用 | 依据 |
|---|---|---|
| `NET_MODE_IRQ` | 通用默认；事件驱动、CPU 占用低 | ISR 收帧入队，任务侧 `NetRecv` 消费。板上实测 RX 帧:中断 = 严格 1:1 |
| `NET_MODE_POLL` | 确定性周期调度（ECAT 周期帧）；不希望任何中断进入实时核 | `Recv` 直接收割 DMA 环，`irq_count` 恒 0 可作运行断言 |
| `NET_MODE_HYBRID` | 中断为主、周期兜底防丢 | ISR + 周期性 `Recv` 双保险 |

模式是 `NetInit` 的运行时参数，同一固件可按部署场景切换（开发期三相自测
验证过；当前内置 demo 固定用 POLL 模式）。

### 5.2 自协商 vs 强制速率

- **默认自协商**（推荐）：主核配置 PHY 通告 10/100/1000，协商结果经 ctrl 块
  发布；`clk_mac_speed` 由主核跟随协商速率（2.5/25/125MHz），协议层无需关心。
- **强制速率**（`autoneg=0`，仅 10/100）：用于对端是固定速率设备的场景。
  记住两条：1000M 不可强制；**对端必须同样强制**，否则大概率半双工失配。
- Windows 测试网卡若要配合强制模式：设备管理器 → 网卡 → 高级 →
  "速度和双工" 手动设为同样的值（改完 Windows 侧会重新枚举链路）。

### 5.3 内置用户 demo（net_test.c）解读

`LOSCFG_MCS_NET=y` 时固件自动启动一个**单一配置**的用户 demo，作为
`Net*` API 的参考模板（拷走 `NetDemoTask` 循环体替换成你的协议即可）：

- **配置**：强制 100M 全双工（`autoneg=0`）+ 纯轮询收包
  （`mode=NET_MODE_POLL`）+ 混杂接收；
- **流程**：`NetInit` → `NetStart` → 有界等待链路 up（最多 10s，期间轮询
  `NetGetLinkInfo`）→ 永续循环：每 100ms 发一帧 0x88B5 广播测试帧
  （payload `NET-DEMO-<n>`，Wireshark 可见）+ `NetRecv` 收干环上所有帧，
  每 1s 打一行统计；
- **前提**：主核 `mcs_net.ko` 先加载（否则 `NetInit` 直接拒绝返回 `-ENODEV` 并打印提示，门禁先于任何寄存器访问、无挂核风险）；
- **对端要求**：强制链路要求对端同样强制 100M FD（见 5.2），否则 demo 只能
  验证强制管道与 TX，RX 不回属于预期。

日志格式：`r<轮次> tx a<受理>/c<完成>/e<错>/b<busy> rx d<交付>/e<错>/drop<丢>
irq<中断> dma<异常> link=<0|1>/<速率>M/<FD|HD>`，正常输出形如：

```
[net] [demo] up (force 100M FD, poll mode)
[net] [demo] link up: 100M FD
[net] [demo] first rx frame: 60 bytes            ← 收到第一帧时打印一次
[net] [demo] r10 tx a10/c10/e0/b0 rx d0/e0/drop0 irq0 dma0 link=1/100M/FD
[net] [demo] r20 tx a20/c20/e0/b0 rx d8/e0/drop0 irq0 dma0 link=1/100M/FD
...
```

判读要点：

- `irq0` 恒零 = 纯轮询路径成立（无任何中断进入实时核）；
- `a` 与 `c` 稳态相等（异步写回，短暂尾随 1~2 帧正常）；
- `rx d` 是否增长取决于对端是否回流量（对端强制 100M FD + ping/发包即可）；
- `link=1/100M/FD`：`NetGetLinkInfo` 反馈强制配置生效；
- demo 不退出、不调用 `NetStop`（常驻观测，随时可从 PC 侧 ping/发包）；
  `NetStop` 用法见 4.2。

历史参考（开发期用例为三相自测：自协商-中断 → 强制100M-中断 → 自协商-轮询，
各 100 帧）：约 300 帧全部受理零错误、轮询相 `irq==0`；更早期 100M 链路上
实测 TX 200/200 零错误、RX 52 帧交付/52 中断严格 1:1（Wireshark 线上抓包实证）。
这些数据证明了中断与轮询两条路径及强制管道，当前 demo 是其单一配置子集。

---

## 6. 内存与缓冲

### 6.1 DMA 内存从哪来（从核侧配置）

收发缓冲**全部由从核侧静态划定**，主核不参与：

- 位置：`NET_DMA_BASE = 0x76000000`，2MB **Normal Non-Cacheable** 窗口
  （`kernel/liteos_new/arch/arm64/los_mmu.c` 里以独立 L2 block 映射，
  恒等映射 PA==VA，DMA 编程地址即物理地址）。
- 为什么不在固件 BSS：从核 D-cache 已使能，普通镜像内存是 Normal-WB
  **非 DMA 一致**（GMAC 无 IO-coherent），描述符/缓冲必须放 NC 窗口。
- 窗口内布局（`gmac1_profile.h`）：

```
0x76000000  RX 描述符环 (16 × 16B, enhanced desc)   +0x0000
0x76001000  TX 描述符环 (16 × 16B)                  +0x1000
0x76002000  RX 帧缓冲池 (16 × 1536B = 0x6000)       +0x2000
0x76008000  TX 帧缓冲池 (16 × 1536B)                +0x8000
...         (余量 ~1.9MB，多网口扩展可用)
```

- 调整：描述符数量 `NET_DESC_NUM`（默认 16）、帧缓冲大小即 `NET_FRAME_SIZE`
  （1536，公开契约，一般不动）——都在 `gmac1_profile.h`，属于**编译期板级参数**。
  若扩展多网口需要更大窗口，改 `los_mmu.c` 的 `NET_DMA_BASE/SIZE` 映射。

### 6.2 默认 MAC 地址

`GMAC1_DEFAULT_MAC = 02:11:22:33:44:55`（`gmac1_profile.h`，写 GMAC_ADDR0）。
混杂模式下仅信息性；`promisc=0` 时它是单播过滤目标，协议层需要特定 MAC
（如 ECAT 主站口 MAC）就改 profile 重编。

---

## 7. 多网口 / 换板扩展

接口层已按多口预留：`NetConfig.port_id`（`NET_PORT_*` 每口一个 id）、
每口独立 `NetDevice` 实例 + 独立共享控制块。当前版本实现单口
（`NET_PORT_GMAC1_RK3568`）。**让渡第二个网口**（如 GMAC0 或其他 SoC 口）的
适配清单：

1. **主核 dts**：新增 `mcs-net-gmacX` 节点（照抄 `rk3568-mcs-net.dtsi`，
   改基址/时钟/复位/引脚组/中断号/delay 实测值），对应 Linux 原节点
   `status = "disabled"`。
2. **主核驱动**：`mcs_net.ko` 目前单节点匹配（compatible 与 ctrl 地址各一）；
   第二口需要节点参数化（compatible 按 id 派生、`oh_rt,ctrl-phys` 变为
   必选真属性）——属于小步演进，probe 流程（时钟/复位/pinmux/GRF/PHY）
   每口各跑一遍。
3. **从核 profile**：新 `gmacX_profile.h`（基址、PHY 地址、INTID、DMA 池
   偏移——共用 0x76000000 窗口内错开，或扩窗口），并把端口注册进
   `net_dwmac.c` 的板级端口表 + `net.h` 增加 `NET_PORT_GMACX_*`。
4. **控制块**：每口一个实例（地址两侧 profile/契约对齐），布局不变则
   `NET_CTRL_VERSION` 不动。
5. **中断**：每口各自 INTID，从核分别 `ArchHwiCreate`（天然隔离，主核仍零
   配置）。

**换板（非 RK3568）**：从核 `net_dwmac.c` 面向 Synopsys DWMAC 4.20a IP 通用，
换同 IP 板只需新 profile（基址/PHY/中断/DMA 窗口）+ 主核侧新板 dts 节点与
时钟/GRF 适配；非 DWMAC IP 则需新驱动实现 `net.h` 契约（接口不变，实现可换）。

---

## 8. 常见问题排查

| 现象 | 原因 | 验证/处理 |
|---|---|---|
| insmod 后 dmesg 停在 step5 附近 | PHY 复位脚/MDIO 时钟/pinmux 问题 | `cat .../mdio` 全 0xffff → MDIO 不通；核对 `phy-reset-gpios`、时钟名 |
| `MAC version reg = 0x00000000` 打印 | **已知无害**：总线 probe 期该寄存器读 0，不影响从核后续使用（板上长期实证） | step6 PHY id 正常（0x4f51e91a）即忽略 |
| 从核 `NetInit` 返回 -19（refusing register access） | 主核 ko 未加载/已 rmmod（ctrl 无效，门禁拒绝，防时钟未开挂核）；两侧 `net_ctrl.h` 版本不一致同理 | 先 `insmod /data/mcs_net.ko`；主核 `cat .../state` 看 `master=1`；比对 magic/version |
| `NetInit` 返回 `-ENODEV` | `port_id` 写错或板级 profile 未注册 | 用 `NET_PORT_GMAC1_RK3568`；确认 `LOSCFG_MCS_NET=y` |
| RX 一直为 0，TX 正常 | 对端不通（IP/网段/防火墙）；或链路未 up | 先看 `link=1`；Windows 对接时确认两网卡不同网段（同网段路由坑）；Wireshark 抓 0x88B5 确认线上有帧 |
| 强制 100M 后对端不通 | 对端自协商 → parallel-detect 半双工失配 | 对端同样强制（见 5.2），这是协议特性 |
| 强制 1000M 返回 `-EINVAL` | IEEE 802.3 禁止 | 用自协商跑 1000M |
| `link=1/0M` | 主核轮询尚未解析出速率（1s 周期） | 等下一轮快照即更新 |
| `tx_busy` 增长 | 发送快过池回收 | 节流或按 `tx_completed` 对账后再发 |
| `rx_overflow` 增长 | 就绪队列满（IRQ 模式消费不及时） | 提高消费任务优先级/缩短周期，或改 POLL 模式 |
| `rx_dropped` 增长 | 坏帧/超长帧 | `NetStats` 定位 + 抓包看链路质量 |
| Windows 双网卡都接板子时 ping 不通 | 两网卡同网段时 Windows 路由/ARP 应答错口 | 每网卡独立网段（板上踩过并实证） |

---

## 9. 附：实测记录摘要

| 日期 | 链路 | 结论 |
|---|---|---|
| 2026-09-23 | 100M FD 自协商（对接 PC） | TX 200/200 帧零错误；RX 52 帧交付/52 中断严格 1:1；Wireshark 线上抓包实证 |
| 2026-09-24 | 同上 | 接口 v2 泛化（多口/强制/初始化期模式）三相自测通过；DMA 池迁 Normal-NC、运行空间 Normal Cache |
| 终版 | 1000M FD 自协商（Intel I210 直连） | 三相 300 帧全受理零错误；轮询相 `irq==0`；链路轮询 100M↔1000M 跟速（`clk_mac_speed` 2.5/25/125MHz）正确 |
| 用例 v3 | — | 内置用例由三相自测简化为单一用户 demo（强制 100M FD + POLL 轮询），作为 `Net*` API 参考模板；驱动零改动 |
| 2026-09-29 | 100M FD 强制（Intel I210 直连，对端同强制） | 修复从核 `GMAC_CONFIG` 速度/双工位（PS/FES/DM）未随链路速率编程的问题（此前固定按 1G 模式，100M 时帧不上线）；串口 tx 2000/2000 零错误，对端网卡收包 2001 帧实证帧已上线；1000M 自协商路径回归通过 |

主核侧 dmesg 三次 insmod 复测（含 1000M 对接）均 step0..8 全绿、ctrl READY，
从核三相全过。
