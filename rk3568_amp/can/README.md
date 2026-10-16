# mcs_can - RK3568 CAN 让渡驱动（主核侧）

主核（Linux）侧的 CAN 让渡模块：把 RK3568 的 CAN 控制器（CAN1: fe580000 /
CAN2: fe590000，经典 CAN 2.0B）整体让渡给从核（CPU3 LiteOS-M）做实时
CAN 口，结构、构建与验证方式与 `../mcs_net`（GMAC1 让渡）完全一致。
单模块多口：一个 ko 按节点绑定 CAN1/CAN2，两口可并发让渡。

## 职责划分

- **主核 mcs_can.ko**（本目录）——对每个启用的 `mcs-canN` 节点：
  1. 使能并永久持有该口 CAN 时钟（`baudclk`=CLK_CANx + `apb_pclk`=PCLK_CANx），
     将 CLK_CANx 重设为 100MHz 并**读回实际频率**发布到共享控制块；
  2. CRU 复位脉冲（SRST_CANx + SRST_P_CANx）；
  3. 应用引脚复用（CAN1: can1m1（GPIO4_C2=RX / GPIO4_C3=TX，func3；can1m0
     组与 i2c3m0 同脚，被板上 rt5670 codec 占用，不可用）；CAN2: can2m0
     （GPIO4_B4/B5，func3），can2m1 备选）；
  4. 一次寄存器总线探活读（时钟未开时读 CAN 寄存器会挂死 AXI 总线）；
  5. 读硅片版本（V1/V2 的 EXT 帧自发回环行为不同；0 = 未知）；
  6. 发布共享控制块 `can_ctrl`（按 0x100 分页：CAN1: 0x740ff100 /
     CAN2: 0x740ff200，见 can_ctrl.h）并置 READY。
  - **不碰 GICD、不碰 CAN 数据面寄存器**（INTID 34/35 由从核经 ArchHwiCreate
    自配置，遵循各核独占自己 GICD 的契约）。

- **从核 can_rk3568.c**（evolve 分支
  `kernel/liteos_m/targets/rk3568_m/liteos_m/board/can/`）：
  全实例化多口驱动（每口一个 `CanDevice`，独立 FIFO/中断/ctrl 页）——
  工作模式（正常/回环/静默/静默回环）、位时序（bitrate 自动计算或 raw
  brp/sjw/ts1/ts2）、验收滤波、RX FIFO（6 深）、TX FIFO（单帧同步握手）
  以及各口 CAN 中断，对外提供协议无关的 `Can*` 接口（can.h），
  `cfg->port_id` 选口（`CAN_PORT_CAN1_RK3568` / `CAN_PORT_CAN2_RK3568`）。

## CAN2 启用（可选，默认 disabled）

```dts
&mcs_can2 { status = "okay"; };   /* 板级 dts 覆盖，重编 boot 烧录 */
```

未启用时从核 `CanInit(CAN2)` 返回 `-ENODEV`（demo 显示 skipped），CAN1
不受影响。详见 `USER_GUIDE.md` §3.1/§7。

## 构建

```sh
export CODE=<OpenHarmony 完整源码树>
./build_mcscan.sh          # 产物: mcs_can.ko（已签名），日志 out/logs/
```

## 部署（板上）

```sh
# dts 集成（二选一，勿同时用！）：
#   a) 板级 dts include rk3568-mcs-can.dtsi
#   b) mcs/dts/rk3568-ohrt-mcs.dtsi 已内嵌同节点（toybrick/DAYU210 默认）
hdc file send mcs_can.ko /data/mcs_can.ko
hdc shell insmod /data/mcs_can.ko
dmesg | grep mcs_can       # step0..5 全 OK + ctrl READY
cat /sys/devices/platform/mcs-can1/state
```

## 验证

从核侧 demo（`can_test.c`，`LOSCFG_MCS_CAN`）跑片内回环（零外部硬件）：
11 阶段 = CAN1 的 loopback-irq / loopback-poll / bitrate-sweep(125k/250k/
500k/1M) / raw-timing / std+ext+rtr / dlc-sweep + CAN2 的 lb2-irq / lb2-poll
（CAN2 未让渡时 skipped 属设计行为）。全阶段 `Stop: 0` 即通过。外部总线
验证需 CAN 收发器硬件（can1m1 引脚在 toybrick/DAYU210 未接收发器）。

注意：800kbps 在 100MHz 位时钟下**不可达**（100M/800k=125，而分频器
步进为 2，无偶数分频解），位时序扫描不含该档。
