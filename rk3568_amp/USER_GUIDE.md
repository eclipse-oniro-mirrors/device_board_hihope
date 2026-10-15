# RK3568 混合部署（mcs）完整用户指南

> 面向第一次接触本方案的工程师，从一块标准 OpenHarmony RK3568 板子出发，
> 端到端完成：**预留 CPU3 → 修改 dts 预留内存并重编内核 → 构建主从核两侧产物 →
> 上板部署 → 使用混合部署框架通信**。
>
> 姊妹文档（按需深入）：`API.md`（两侧接口完整说明）、`example/README.md`
> （应用开发上手）、`mcs-build-usage-guide.md`（编译到上板快速上手）、
> `README.md`（构建速览）。

## 1. 方案概述

```
┌──────────────────────────── RK3568（4 × Cortex-A55）───────────────────────────┐
│                                                                                │
│  CPU0-2：OpenHarmony Linux 6.6           CPU3：LiteOS-M（裸金属 AMP）           │
│  ┌─────────────────────────────┐         ┌──────────────────────────────┐      │
│  │ 应用（rpmsg_demo / 你的应用）│         │ mica 从核服务框架             │      │
│  │   libmica.so（从核生命周期） │  rpmsg  │   ├─ rpmsg-echo 回显服务      │      │
│  │   libopen_amp/libmetal      │◄───────►│   ├─ rpmsg-tty shell 通道     │      │
│  │ mcs_km.ko（/dev/mcs）       │ vring   │   └─ rpmsg-umt                │      │
│  └─────────────┬───────────────┘          └──────────────┬───────────────┘      │
│                │            共享内存 0x74000000（1MB）     │                     │
│                └────────────────────┬─────────────────────┘                     │
│  Linux 侧内存管理不触及：0x74000000-0x77ffffff（64MB no-map 保留）              │
└────────────────────────────────────────────────────────────────────────────────┘
```

**内存布局**（由 dts `reserved-memory` 定义，Linux 页分配器完全让出）：

| 区域 | 范围 | 大小 | 用途 |
|---|---|---|---|
| 通信共享内存 | `0x74000000-0x740fffff` | 1MB | openamp vring + rpmsg buffer 池（shared-dma-pool） |
| 从核保留内存 | `0x74100000-0x77ffffff` | 63MB | 从核 ELF 加载（链接基址 0x74100000）+ 从核堆 |

**两侧软件栈各三层**：

- 主核：你的应用 → libmica（生命周期/服务注册）→ openamp+libmetal（rpmsg 端点收发）
  → mcs_km.ko 内核模块（vring/共享内存映射/核间中断 SGI）
- 从核（morpheus 仓）：mica 服务框架（`kernel/liteos_new/compat/mcs/mica`）→
  openamp+libmetal 从核移植（`kernel/liteos_new/compat/mcs` 下）→ LiteOS-M
  内核 + arm64 端口

主从核的契约只有两个：**端点名（service name）**和**协议常量**（共享内存基址、
vring 参数、通知中断号，分别定义在 dts/mcs_km 和从核 `mica_cfg.h`，两侧对齐）。

**网卡让渡（可选特性）**：除 rpmsg 通信外，主核还可把 **GMAC1**（eth1，
`0xFE010000`）整体让渡给从核作为**协议无关实时网口**——主核侧 `mcs_net.ko`
完成上电初始化（时钟/复位/pinmux/GRF/PHY）后移交数据面并退到 1s 链路轮询；
从核侧 `Net*` API（`net.h`）提供帧级收发，EtherCAT 等实时协议在其上实现。
完整使用文档见 `mcs_net/USER_GUIDE.md`；本指南第 4/6/7/9 节列出相关步骤与
API 速查。

## 2. 前置条件

- 硬件：RK3568 板（dayu210/Toybrick X0）、USB 串口线（1500000 波特率）、网线
- 构建机：OH 全仓已完整编译过 rk3568 产品（需要其 musl sysroot 与内核构建产物）
  ——**仅主核侧需要**；从核镜像在 morpheus 仓独立构建（第 5 节），只依赖 python3，
  工具链仓内自带
- `$CODE` = OH 全仓根目录；本方案分布在两个仓的目录：
  `$CODE/device/board/hihope/rk3568_amp`（板级：让渡驱动/dts/示例/文档）、
  `$CODE/device/soc/rockchip/rk3568/mcs`（架构：mcs_km/libmica/prebuilt 三方库）

## 3. 第一步：预留 CPU3 与内存（dts 修改 + 内核重编 + 烧录）

> **为什么必须重编内核镜像**：CPU3 的让出（bootargs）和内存的让出
> （reserved-memory）都写在设备树里，而 dts 编译进 `boot_linux.img`。
> 标准镜像不带这些节点，所以第一步是重编内核镜像并重新烧录。

### 3.1 dts 改了什么（三处，对应 `patches/rk3568_dts_ohrt.patch`）

| 改动 | 文件 | 作用 |
|---|---|---|
| bootargs 追加 `maxcpus=3` | `rk3568-linux.dtsi` 的 chosen 节点 | Linux 启动只 bring-up CPU0-2，**CPU3 从未进入 Linux 调度**，留给从核 |
| 新增 `rk3568-ohrt-mcs.dtsi` | 新文件 | `reserved-memory` 两个节点（1MB 通信 + 63MB 从核，均 `no-map`）+ `oh_rt,mcs_remoteproc` 节点（memory-region 引用上述两块）+ **网卡让渡**（`&gmac1` 置 disabled 摘出 Linux + `mcs-net-gmac1` 节点供 mcs_net.ko 匹配，见第 7 节）+ **CAN 让渡**（`mcs-can1` 节点默认启用 + `mcs-can2` 节点默认 disabled，供 mcs_can.ko 匹配——单模块多口，CAN2 为接口预留扩展（尚未上板验证），需要时板级 dts 改 okay，见第 7 节与 `mcs_can/USER_GUIDE.md`） |
| include 该 dtsi | `rk3568-toybrick-x0-linux.dts` | 接入板级 dts |

`rk3568-ohrt-mcs.dtsi` 核心 content（完整版见 `dts/rk3568-ohrt-mcs.dtsi`）：

```dts
/ {
	reserved-memory {
		#address-cells = <0x02>;
		#size-cells = <0x02>;
		ranges;

		oh_rt_mcs_dma: oh_rt-mcs-dma@74000000 {     /* 通信共享内存 */
			compatible = "shared-dma-pool";
			reg = <0x00 0x74000000 0x00 0x100000>;
			no-map;
		};
		oh_rt_mcs_mem: oh_rt-mcs-mem@74100000 {     /* 从核保留内存 */
			reg = <0x00 0x74100000 0x00 0x3F00000>;
			no-map;
		};
	};

	oh_rt_mcs_remoteproc: mcs-remoteproc {          /* mcs_km.ko 识别的节点 */
		compatible = "oh_rt,mcs_remoteproc";
		memory-region = <&oh_rt_mcs_dma>, <&oh_rt_mcs_mem>;
	};
};
```

> 地址选择依据（改地址前必读）：0x74000000-0x77ffffff 是按真机 `/proc/iomem`
> 核验的完全空闲段；若调整，须同时改三处保持一致：dts、从核
> `kernel/liteos_new/compat/mcs/mica/mica_cfg.h` 的 `VDEV_START_ADDR`、
> 从核链接脚本 `device/board/rockchip/rk3568/liteos_new/board/board.ld` 基址。

### 3.2 打 dts patch（两种方式任选）

**方式 A：临时直改 src_tmp（联调迭代最快，但可能被静默冲掉）**

```bash
cd $CODE/out/kernel/src_tmp/linux-6.6
patch -p1 --dry-run < $CODE/device/board/hihope/rk3568_amp/patches/rk3568_dts_ohrt.patch  # 先验
patch -p1 < $CODE/device/board/hihope/rk3568_amp/patches/rk3568_dts_ohrt.patch             # 真打
```

警告：OH 构建系统检测到 kernel/linux 等依赖仓变化时会**整个重建 src_tmp**，
手工 patch 静默丢失（症状：重编镜像后 insmod 报 oh_rt 节点不存在）。只用于快速迭代。

**方式 B：追加进 kernel.patch（可复现，正式通道，推荐）**

```bash
KP=$CODE/kernel/linux/patches/linux-6.6/rk3568_patch/kernel.patch
cp $KP $KP.bak-$(date +%m%d)     # 先备份！这是 50MB+ 的 RK 全量补丁
cat $CODE/device/board/hihope/rk3568_amp/patches/rk3568_dts_ohrt.patch >> $KP

# 强制 src_tmp 重建（否则 checker 认为无变化跳过重拷，patch 不生效）：
rm -rf $CODE/out/kernel/src_tmp $CODE/out/kernel/OBJ $CODE/out/kernel/checkpoint
```

### 3.3 重编内核

```bash
cd $CODE/out/rk3568
$CODE/device/board/hihope/rk3568/kernel/build_kernel.sh \
  ../../kernel/linux/linux-6.6 \
  $CODE/out/rk3568/packages/phone/images \
  $CODE/device/board/hihope/rk3568 \
  vendor/hihope/rk3568 \
  $CODE \
  rockchip rk3568 hihope linux-6.6 root default disable_lto_O0
```

产物（在 `$CODE/out/rk3568/packages/phone/images/`）：
- `resource.img`（脚本自动拷贝到位）
- `boot_linux.img` —— ⚠️ **disable_ramdisk 模式下生成在 src_tmp 而非 OBJ**，
  构建脚本拷贝步骤必失败提前退出，需手动补拷：
  ```bash
  cp $CODE/out/kernel/src_tmp/linux-6.6/boot_linux.img \
     $CODE/out/rk3568/packages/phone/images/
  ```

### 3.4 烧录

用既有烧录工具烧写 `boot_linux.img` + `resource.img`（只动这两个分区，
system/vendor 等不动）。烧录 config 无需重新配置，与标准流程相同。

### 3.5 板上验证（重编是否生效）

```bash
cat /proc/cmdline | grep -o maxcpus=3     # 命中 → CPU3 已预留
cat /proc/cpuinfo | grep -c processor     # = 3
cat /proc/iomem | grep 74000000           # 74000000-77ffffff reserved（两段 no-map）
```

> `dmesg` 里 `mcs: Failed to find dts node: oh_rt,mcs_riscv_remoteproc` 是
> riscv 路径探测日志，同构（4xA55）场景正常，忽略。

### 3.6 重编内核后必须重编 ko

内核重建 = vermagic/签名 key 变化，旧 `mcs_km.ko` 不可再用（见 4.5 节）。

### 3.7 增量更新：从"无 CAN"旧镜像升级（打 patch → 重编 boot → 烧录）

> `rk3568_dts_ohrt.patch` 自 CAN 版起已包含 CAN 让渡节点（`mcs-can1`
> 默认启用 / `mcs-can2` 默认 disabled）。**已按旧版 patch 烧过镜像**（或
> src_tmp 里是旧 dtsi）的用户按本节升级——只动 boot 镜像，system/vendor
> 分区不动。

**① 重打 dts patch（含 CAN 节点的最新版）**

src_tmp 走方式 A（手工 patch）的用户——先撤销旧版再打新版：

```bash
cd $CODE/out/kernel/src_tmp/linux-6.6
# 撤销旧版（方式 A 打的）：
patch -p1 -R < $CODE/device/board/hihope/rk3568_amp/patches/rk3568_dts_ohrt.patch.bak-旧版 \
  || true     # 旧版文件不在了就跳过，直接看下一步
# 直接删旧 dtsi 后重打（推荐，最干净）：
rm -f arch/arm64/boot/dts/rockchip/rk3568-ohrt-mcs.dtsi
git checkout -- arch/arm64/boot/dts/rockchip/rk3568-linux.dtsi \
                 arch/arm64/boot/dts/rockchip/rk3568-toybrick-x0-linux.dts 2>/dev/null || true
# 打最新 patch：
patch -p1 --dry-run < $CODE/device/board/hihope/rk3568_amp/patches/rk3568_dts_ohrt.patch   # 先验
patch -p1 < $CODE/device/board/hihope/rk3568_amp/patches/rk3568_dts_ohrt.patch              # 真打
# 验证 CAN 节点进树：
grep -c mcs-can arch/arm64/boot/dts/rockchip/rk3568-ohrt-mcs.dtsi   # ≥4
```

> 注意：`git checkout` 只对 src_tmp 有 .git 的树有效；无 .git 时手工把
> `rk3568-linux.dtsi` chosen 里追加的 `maxcpus=3` 去掉、toybrick dts 的
> `#include "rk3568-ohrt-mcs.dtsi"` 行去掉，效果等同。

走方式 B（kernel.patch 追加）的用户——**必须先取出旧段再追加新版**，否则
同 label 重复编译报错：

```bash
KP=$CODE/kernel/linux/patches/linux-6.6/rk3568_patch/kernel.patch
# 从备份恢复（打旧段前的 .bak）；没有备份则手工删掉 kernel.patch 尾部
# 旧版 rk3568_dts_ohrt 段（搜索 "rk3568-ohrt-mcs.dtsi" 定位起止）：
cp $KP.bak-<日期> $KP
cat $CODE/device/board/hihope/rk3568_amp/patches/rk3568_dts_ohrt.patch >> $KP
# 强制 src_tmp 重建：
rm -rf $CODE/out/kernel/src_tmp $CODE/out/kernel/OBJ $CODE/out/kernel/checkpoint
```

**② 重编内核（boot_linux.img）**——命令同 3.3。

**③ 手动补拷 boot_linux.img**——同 3.3 尾注（disable_ramdisk 模式产物
在 src_tmp，需手动拷进 images/）。

**④ 烧录**——用既有烧录工具烧写 `boot_linux.img` + `resource.img`，
同 3.4。

**⑤ 重编全部 ko**——内核重建后 vermagic 变化，`mcs_km/mcs_net/mcs_can`
三个 ko 都要重编（第 4 节命令）。

**⑥ 板上验证 CAN 节点生效**：

```bash
ls /proc/device-tree/mcs-can1/     # 节点存在（含 compatible/ctrl-phys 等）
ls /proc/device-tree/mcs-can2/     # 同上（disabled 状态节点也在树里）
```

## 4. 第二步：构建主核侧产物（六条命令）

```bash
# 前置：export CODE=<OpenHarmony 源码树根>（repo sync 的完整树，含
# prebuilts/clang 工具链；musl sysroot 需先完整构建一次任意产品）
cd $CODE/device/soc/rockchip/rk3568/mcs/scripts
./rebuild_ohrt.sh                 # 1. mcs_km.ko（树外构建+签名）→ ../mcs_km/
./build_libmica.sh                # 2. libmica.a/.so（源码编译）+ 上板 so 部署集 → ../out/

cd $CODE/device/board/hihope/rk3568_amp
./slave_boot/build_slave_boot.sh  # 3. slave_boot（仅拉核工具，内核调试用，可选）→ out/artifacts
./example/build_example.sh        # 4. rpmsg_demo（应用示例）→ out/artifacts
./net/build_mcsnet.sh             # 5. mcs_net.ko（网卡让渡模块，可选）→ net/
./can/build_mcscan.sh             # 6. mcs_can.ko（CAN 让渡模块，可选）→ can/
```

- 三方库（libmetal/open-amp）以预编译**动态库**交付在
  `device/soc/rockchip/rk3568/mcs/prebuilt/`（源码待三方开源引入评审通过后
  进 third_party；适配补丁见 `prebuilt/patches/`，重编方法见其 README）；
  libmica 不放二进制，由 soc 仓源码经 `scripts/build_libmica.sh` 编译。
- musl sysroot 自动从 `$CODE/out/*/obj/third_party/musl/usr` 探测（取第一个
  含 `crt1.o` 的完整构建），多产品树中指定目标可用 `MUSL=` 覆盖。
- 应用全部**动态链接**（musl interpreter），上板需带 `device/soc/rockchip/rk3568/mcs/out/artifacts/lib` 部署集（build_libmica.sh 生成），
  最小 3 个 so：`libmica.so`、`libmetal.so.1`、`libopen_amp.so.1`
  （libmetal 已裁剪 sysfs 设备层，无 sysfsutils 依赖）。
- `rebuild_ohrt.sh` 输出确认：`vermagic: 6.6.101 SMP mod_unload aarch64`、
  `OHRT_COUNT=3`（dts 命中数）、`MISSING=0`（内核符号解析）。
- 第 6 条 `build_mcsnet.sh` 仅实时网口场景需要（产出 `mcs_net.ko`，把 GMAC1
  让渡给从核）；其 dts 节点已包含在 3.1 的 dts 补丁里，无需额外改 dts。
- 第 7 条 `build_mcscan.sh` 仅实时 CAN 场景需要（产出 `mcs_can.ko`，单模块
  多口：CAN1 默认 / CAN2 dts 启用后并发）；其 dts 节点同样已包含在 3.1 的
  dts 补丁里（CAN2 启用方法见 `mcs_can/USER_GUIDE.md` §3.1）。

## 5. 第三步：构建从核 LiteOS-M 镜像

从核固件 = LiteOS-M 内核 + 混合部署组件（openamp/libmetal 从核移植 + mica
服务框架）。两种获取方式：**方式一**从 morpheus 源码仓构建（与主核侧 mcs
联调包独立演进、独立构建）；**方式二**使用从核 SDK 二进制包（内核与基础库
预编译，只改板级与应用源码，适合纯应用开发者）。

### 5.1 方式一：源码构建（morpheus 仓）

- 仓库：`https://gitcode.com/pb28/morpheus`，分支 `main`
- 内核与组件：`kernel/liteos_new`（混合部署组件在 `kernel/liteos_new/compat/mcs/`）
- 板级适配：`device/board/rockchip/rk3568/liteos_new`

```bash
git clone -b main https://gitcode.com/pb28/morpheus.git
cd morpheus

# hb 构建（产品 rk3568_amp 已内置全部配置，含工具链/组件开关/链接脚本）：
PYTHONPATH=$PWD/build python3 build/hb/main.py build --product-name rk3568_amp

# 产物（unstripped 版带符号，便于 addr2line 调试）：
out/rk3568_amp/rk3568_amp/obj/kernel/liteos_new/unstripped/bin/liteos
# 拷为主核 demo 约定的名字：
cp out/rk3568_amp/rk3568_amp/obj/kernel/liteos_new/unstripped/bin/liteos liteos-m.elf
```

### 5.2 方式二：从核 SDK 包（二进制库 + 应用源码）

SDK 包 `rk3568_amp_slave_sdk.tar.gz`（第 3 版，基线 morpheus main
`90a4b6251`，含从核 GMAC1 网卡驱动、CAN 多口驱动（CAN1/CAN2 并发）与
UART 驱动，变化详见包内 `release.md`）：内核（libliteos）、openamp、
libmetal、mcs/libmica、musl libc、safeC（libsec）均已预编译为静态库，板级代码
（`board/`）与应用入口（`example/main.c`）以源码交付：

```bash
tar xzf rk3568_amp_slave_sdk.tar.gz && cd rk3568_amp_slave_sdk
export OHOS_LLVM=/path/to/ohos/llvm    # OHOS clang 15.0.4（库为 LTO，需兼容 LLVM）
./build.sh                             # → out/liteos-m.elf
```

- 写业务：改 `example/main.c` 的 `app_task()`；新增 `.c` 加进 `build.sh`
  的 `SRCS`；
- 库说明：SDK 内 7 个库全部来自 morpheus 仓源码（liteos_new 内核 +
  `compat/mcs` 组件 + `third_party/{musl,bounds_checking_function}` 裁剪集）；
  链接所需的 `libclang_rt.builtins.a`（LLVM compiler-rt）不随包分发，由
  `build.sh` 从 `$OHOS_LLVM` 工具链自动定位；
- SDK 产物已与源码构建产物做符号等价校验与上板回归（echo 100/100、
  二次拉起、tick 正常、网卡 demo 强制 100M 线缆级收发 2000/2000、
  uart_test 自测），两种方式产出可互换使用。

> **从核 tick 机制**：从核使用**本地 PPI 30**（ARM 通用 EL1 物理定时器
> CNTPCT/CNTP_CVAL/CNTP_CTL）自驱 tick，**完全本地、主核零配合**。唯一的
> 坑是 GIC 给 PPI 30 的默认优先级 0xf0 与从核 ICC_PMR_EL1（PRIbits=5 投影
> 后）相等，中断会一直 pending 永不投递——从核初始化（PTimerTickStart）
> 把 INTID 30 的优先级改写为 0x00（非安全侧对 IPRIORITYR 的写是生效的，
> 与 rpmsg SGI7 kick 的优先级修复同款）。已在板上用 maxcpus=3 标准镜像
> 验证：CPU3 未经 Linux 二级启动链、直接 PSCI CPU_ON 拉起，PPI 30 精确
> 以 100Hz 投递。（早期"组寄存器 NS 不可写导致 PPI 不可投递"的判断已被
> 对照实验推翻；旧版主核 SGI9 hrtimer kick 方案已从 mcs_km.ko 移除，
> 保留在 git 历史中。）从核 app_task 每秒经 rpmsg-tty 发一条
> `[app] tick alive N`（N 为 tick 计数，100Hz 递增），可作从核 tick/调度
> 的健康证据（interactive 模式的回显窗口可见）。

## 6. 第四步：上板部署

```bash
# 板子 reboot 后 IP 不持久，先在串口执行：
dmesg -n 3                                              # 关刷屏日志
ifconfig eth0 192.168.100.10 netmask 255.255.255.0 up   # 配网

# 构建机/WSL 传文件（hdc，也可用 scp/网口方式）：
SOC=$CODE/device/soc/rockchip/rk3568/mcs      # 架构仓（mcs_km.ko、so 部署集）
BOARD=$CODE/device/board/hihope/rk3568_amp     # 板级仓（驱动 ko、demo）
hdc tconn 192.168.100.10:8710
hdc file send $SOC/mcs_km/mcs_km.ko             /data/
hdc file send $BOARD/out/artifacts/rpmsg_demo   /data/
hdc file send $BOARD/out/artifacts/slave_boot   /data/     # 可选
hdc file send $BOARD/net/mcs_net.ko             /data/     # 网卡让渡（可选，见第 7 节）
hdc file send $BOARD/can/mcs_can.ko             /data/     # CAN 让渡（可选，见第 7 节）
hdc file send <morpheus>/liteos-m.elf                       /data/liteos-m.elf

# 动态库部署集（先建目录；整目录拷，含 libmica.so + libmetal.so.1 +
# libopen_amp.so.1 + aarch64 musl libc.so / ld-musl-aarch64.so.1——板上用户态
# 是 32 位，demo 是 aarch64 动态链接，loader 路径 /data/lib 已写进 ELF 头，
# 不能改；该目录由 build_libmica.sh 生成）：
hdc shell mkdir -p /data/lib
hdc file send $SOC/out/artifacts/lib/* /data/lib/
# （out/ 三个子目录的完整说明见 mcs-build-usage-guide.md「out/ 目录结构」）
```

## 7. 第五步：运行与通信验证

```bash
# 板上（串口或 hdc shell）：
insmod /data/mcs_km.ko                        # 一次性；dmesg 应有 init done、SGI 注册
export LD_LIBRARY_PATH=/data/lib

# echo 回显往返测试（推荐，实测 100/100，RTT ~90us）：
/data/rpmsg_demo -f /data/liteos-m.elf -c 3 -e rpmsg-echo -n 100

# 交互模式（走 rpmsg-tty shell 通道，输入一行回车发送，quit 退出）：
/data/rpmsg_demo -f /data/liteos-m.elf -i
```

自动模式预期输出：

```
[demo] firmware=/data/liteos-m.elf cpu=3 ept=rpmsg-echo mode=auto
[demo] step1 mica_create ok
[demo] step2 mica_start ok, waiting NS announce...
[demo] NS bind: name=rpmsg-echo peer_addr=1026
[demo] step3 endpoint bound (local addr 1024, peer 1026)
[demo] rtt 92 us, reply(8): ping-001
...
[demo] PASS: 100/100 round trips
[demo] done
```

**验证通过 = 全链路闭环**：拉核（PSCI CPU_ON）→ NS announce → 主核 send →
vring 写共享内存 → SGI 核间中断 → 从核 GIC 收中断 → rpmsg rx → echo 回发 →
kick 回主核 → 应用收到。

**tick 健康检查**（从核调度是否活着）：从核 tick 完全本地（PPI 30），主核
侧**没有**对应的 tick 日志（旧版 SGI9 kick 的 `tick kick on/off` 已随 kick
移除而消失，属正常）。

从核 app_task 每秒经 rpmsg-tty 发一条 `[app] tick alive N`（N 按 100Hz
递增）；interactive 模式下可在回显窗口看到，是 tick/调度/rpmsg 三者的
实时证据。串口侧同步可看到从核启动日志（`LiteOS-M for RK3568 (AMP core3)
boot...` → `mica task enter` → `Creating ECHO thread...` 等）。

**网卡让渡验证（可选，需 mcs_net.ko + 从核固件 `LOSCFG_MCS_NET=y`）**：

```bash
# 时序：先加载两个 ko（网卡 ko 必须在拉起从核之前），再拉从核：
insmod /data/mcs_km.ko
insmod /data/mcs_net.ko        # dmesg 依次打出 step0..step8，最后 ctrl: READY
cat /sys/devices/platform/mcs-net-gmac1/state   # master=1(READY)、link、slave 状态

/data/rpmsg_demo -f /data/liteos-m.elf -c 3 -e rpmsg-echo -n 100   # 拉起从核
```

从核起来后串口可见网卡 demo 日志（`[net] [demo] up (force 100M FD, poll
mode)` → `link up: 100M FD` → 每 1s 一行收发统计）；**对端网卡需同样强制
100M 全双工**，否则 demo 只发不收（详见 `mcs_net/USER_GUIDE.md`）。

**CAN 让渡验证（可选，需 mcs_can.ko + 从核固件 `LOSCFG_MCS_CAN=y`；boot
镜像需含 CAN 节点，老镜像先按 3.7 增量升级）**：

```bash
# 时序同网卡：ko 先于从核拉起：
insmod /data/mcs_km.ko
insmod /data/mcs_can.ko        # dmesg: [mcs_can] CAN1 clk baudclk ON ... step0..5 全 OK
cat /sys/devices/platform/mcs-can1/state     # port=1 ... ctrl: READY at 0x740ff100

/data/rpmsg_demo -f /data/liteos-m.elf -c 3 -e rpmsg-echo -n 100   # 拉起从核
```

从核起来后串口可见 CAN demo **13 阶段片内回环自测**（零外部硬件，不出
引脚）：`[can] [lb-irq-500k] up` → 各阶段 → `[can] all phases done`，
每阶段 `Stop: 0 (sent N, echoed N [+ filtered K], errors 0)` 即通过。
覆盖：IRQ/POLL/HYBRID 三种收包模式 × 125k/250k/500k/1M 波特率、raw 位
时序、扩展帧、远程帧、dlc 0..8、验收滤波拒收；CAN2 未在 dts 启用时
lb2 两阶段 `skipped (no master handover for port 2)` 属设计行为。
收发 API / 位时序 / CAN2 启用 / 多口扩展的完整说明见
`mcs_can/USER_GUIDE.md`。

## 8. 应用开发：怎么用框架通信

### 8.1 编程模型（主核侧，5 步）

```
mica_create → mica_start → mica_register_service → rpmsg_sendto / rx 回调 → mica_stop / mica_remove
(填固件/核号)  (载ELF+启核)   (NS 匹配/绑定回调)      (收发数据)              (停核回收)
```

最小代码骨架（完整可运行版本见 `example/rpmsg_demo.c`，按步骤注释分块）：

```c
#include <mica/mica.h>
#include <openamp/rpmsg.h>

/* ① 创建：只填 3 样——固件路径、BARE_METAL、核号 */
struct mica_client *client = calloc(1, sizeof(*client));
client->path = "/data/liteos-m.elf";
client->ped = BARE_METAL;
client->ped_setup.cpu_str = "3";
mica_create(client);

/* ② 启动：载 ELF → PSCI 启核 → 建 rpmsg vdev；从核异步广播 NS 通告 */
mica_start(client);

/* ③ 注册服务：两个回调（必须在 mica_start 之后调用） */
struct mica_service svc = {
    .rpmsg_ns_match    = my_match,   /* 端点名比对：要不要绑 */
    .rpmsg_ns_bind_cb  = my_bind,    /* 绑定动作：rpmsg_create_ept + 记对端地址 */
};
mica_register_service(client, &svc);

/* ④ 收发：rpmsg_sendto(&ept, buf, len, peer_addr)；接收走 ept 的 rx 回调 */

/* ⑤ 回收（顺序别乱） */
mica_stop(client);      /* 停核 */
mica_remove(client);    /* 释放框架资源 */
```

match/bind 回调写法（服务发现核心）：

```c
static bool my_match(struct rpmsg_device *rdev, const char *name,
                     uint32_t addr, uint32_t dest, void *priv)
{
    return name && strncmp(name, "rpmsg-echo", RPMSG_NAME_SIZE) == 0; /* 按名契约 */
}

static void my_bind(struct rpmsg_device *rdev, const char *name,
                    uint32_t remote_addr, uint32_t remote_dest, void *priv)
{
    g_peer_addr = (remote_addr != RPMSG_ADDR_ANY) ? remote_addr : remote_dest;
    rpmsg_create_ept(&g_ept, rdev, name, remote_dest, remote_addr, my_rx_cb, NULL);
}

static int my_rx_cb(struct rpmsg_endpoint *ept, void *data,
                    size_t len, uint32_t src, void *priv)
{
    /* 接收线程上下文：只做 memcpy + 唤醒业务线程，勿阻塞 */
    memcpy(g_buf, data, len);
    sem_post(&rx_done);
    return 0;
}
```

### 8.2 服务发现机制（为什么"从核通告、主核绑定"）

- 从核 `rpmsg_create_ept(..., "rpmsg-echo", ...)` 创建端点时，openamp 自动向
  主核广播 **NS（name service）通告**（端点名 + 地址）。
- 主核框架收到通告后调用你的 `match`（比对名字）→ `bind`（建本地 ept）。
  **主核不需要预先知道从核地址——端点名就是两侧契约**。
- 主核也可自建端点主动通告，但从核服务框架按"从核通告"模式实现，建议沿用。

### 8.3 从核侧：写自己的服务

方式一（最小改动）：换掉 `rpmsg-tty` 的命令 handler（板级
`device/board/rockchip/rk3568/liteos_new/board/main.c` 的 MicaTaskEntry 中
`cfg.sys_ops.shell_cmd_handler = MyCmdHandler;`，用 `mica_tty_send` 回结果）。

方式二（独立端点，推荐）：仿 `echo_service.c` 建自己的端点：

```c
/* kernel/liteos_new/compat/mcs/mica/libmica/src/services/my_service.c（LiteOS-M 侧） */
static struct rpmsg_endpoint my_ept;

static int my_rx_cb(struct rpmsg_endpoint *ept, void *data,
                    size_t len, uint32_t src, void *priv)
{
    return rpmsg_send(ept, response, resp_len);   /* 直接回复请求方 */
}

void *my_service_thread(void *arg)
{
    struct rpmsg_device *rpdev = mica_get_rpdev();
    rpmsg_create_ept(&my_ept, rpdev, "my-service",
                     RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, my_rx_cb, NULL);
    while (1) { /* 业务循环 */ }
}
```

在 `mica_service.c` 的 `mica_create_all_services` 挂线程，Makefile 的
`MICA_SRCS` 加源文件。主核侧 demo 用 `-e my-service` 即可对上。

### 8.4 你的应用怎么编译

```bash
CLANG=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
MUSL=$(ls -d $CODE/out/*/obj/third_party/musl/usr | head -1)   # 构建过的产品
SOC=$CODE/device/soc/rockchip/rk3568/mcs   # 头文件 + 三方库；libmica.so 由 build_libmica.sh 产出

$CLANG --target=aarch64-linux-ohos --sysroot=$MUSL \
  -I$MUSL/include/aarch64-linux-ohos -B$MUSL/lib/aarch64-linux-ohos \
  -I$SOC/libmica/include -I$SOC/prebuilt/include -O2 -Wall \
  your_app.c -L$SOC/out/staging/lib -L$SOC/prebuilt/lib -lmica -l:libmetal.so.1 -l:libopen_amp.so.1 -lpthread \
  -o your_app
```

最省事：复制 `build_example.sh` 改源文件路径。上板 = your_app + 4 个 so +
固件 elf，运行时 `LD_LIBRARY_PATH=/data/lib`。

### 8.5 应用层注意事项

1. `mica_register_service` 必须在 `mica_start` 之后（要求 RPROC_RUNNING；
   若 NS 通告先到，register 内部会立即补一次匹配绑定）。
2. `rpmsg_sendto` 成功**返回发送字节数**，负数才是错误。
3. 单包负载上限 ~400 字节（rpmsg buffer 512B 留头部），更大数据应用层分段。
4. rx 回调运行在框架接收线程：只做 memcpy + 唤醒，勿阻塞。
5. 一个从核实例对应一个 `mica_client`；多服务就注册多个 `mica_service`。
6. `mica_stop` 会通知从核走 PSCI CPU_OFF 下电，之后可直接再次 `mica_start`
   拉起（板上已验证连续两次 echo 100/100）；若从核异常挂死没走下电流程，
   下一次 `mica_start` 会因 PSCI ALREADY_ON 失败，此时 reboot 板子。

## 9. API 速查

**主核侧**（`mica/mica.h` + `openamp/rpmsg.h`，完整说明见 `API.md`）：

| API | 作用 |
|---|---|
| `mica_create / mica_start / mica_stop / mica_remove` | 从核生命周期 |
| `mica_register_service(client, &svc)` | 注册 NS 匹配/绑定回调 |
| `mica_set / mica_status` | 运行时参数 / 状态查询 |
| `rpmsg_create_ept / rpmsg_destroy_ept` | 端点创建（bind 回调里调）/ 销毁 |
| `rpmsg_send / rpmsg_sendto` | 发数据（to 指定对端地址） |

**从核侧**（`mica/mica.h` + `mica/service.h`）：

| API | 作用 |
|---|---|
| `mica_get_rpdev()` | 取 rpmsg device（建端点用） |
| `mica_tty_send / mica_tty_printf` | 经 rpmsg-tty 服务发往主核 |
| `rpmsg_create_ept` | 建自定义服务端点（创建即自动 NS 通告） |

**网卡让渡**（GMAC1 → 从核实时网口，协议无关；完整说明见
`mcs_net/USER_GUIDE.md`）：

主核侧（`mcs_net.ko`，**无用户态 API**——insmod 即完成让渡，之后主核零参与）：

| 接口 | 作用 |
|---|---|
| `insmod mcs_net.ko` | 时钟/复位/pinmux/GRF/PHY 初始化后移交数据面，共享控制块置 READY（0x740ff000） |
| `cat .../mcs-net-gmac1/state` | 诊断：probe 步骤回放 + 控制块实时状态（master/slave/link） |
| `cat .../mcs-net-gmac1/mdio` | 诊断：PHY 寄存器直读（含 YT8531 扩展延迟寄存器） |

从核侧（`net.h`，位于 `device/board/rockchip/rk3568/liteos_new/board/driver/net/`；
参考用例 `net_test.c`——强制 100M FD + 轮询收包 demo）：

| API | 作用 |
|---|---|
| `NetInit(cfg, &dev)` | 配置并获取网口实例：链路策略（自协商/强制 10/100M）、收包模式（中断/轮询/混合）、混杂——均为**初始化期运行时配置**，非编译期 |
| `NetStart / NetStop` | 启动数据面（前置：主核 ko 已加载、ctrl 块 READY）/ 有界超时停止回收 |
| `NetSend(dev, frame, len)` | 发一帧完整以太网帧（14..1514B、不含 FCS、拷贝语义；池满返回 `-EAGAIN`） |
| `NetRecv(dev, buf, cap, &len)` | 非阻塞收一帧（轮询模式直接收割 DMA 环），无帧返回 `-EAGAIN` |
| `NetGetLinkInfo / NetGetStats` | 链路快照（up/速率/双工）/ 收发统计（受理/完成/错误/丢包/中断计数） |

**CAN 让渡**（经典 CAN 2.0B → 从核实时 CAN 口，单模块多口（CAN2 为接口预留，
尚未上板验证，当前请使用 CAN1）；完整说明见 `mcs_can/USER_GUIDE.md`）：

主核侧（`mcs_can.ko`，**无用户态 API**——insmod 即完成让渡，之后主核零参与）：

| 接口 | 作用 |
|---|---|
| `insmod mcs_can.ko` | 时钟（baudclk 重设 100MHz 并读回发布）/CRU 复位/pinmux/总线探活后移交数据面，控制块置 READY（按 0x100 分页：CAN1 0x740ff100 / CAN2 0x740ff200） |
| `cat .../mcs-canN/state` | 诊断：probe 步骤回放 + `port=N` + 控制块实时状态（master/slave） |

从核侧（`can.h`，位于 `device/board/rockchip/rk3568/liteos_new/board/driver/can/`；
参考用例 `can_test.c`——13 阶段片内回环 demo，零外部硬件）：

| API | 作用 |
|---|---|
| `CanInit(cfg, &dev)` | 选口（`CAN_PORT_CAN1/CAN2_RK3568`，两口可并发）+ 收包模式（中断/轮询/混合）+ 总线模式（正常/回环/只听/静默回环）+ 位时序（bitrate 自动求解 ~87.5% 采样点或 raw 直写）+ 验收滤波（code/mask）——均为**初始化期运行时配置**，非编译期 |
| `CanStart / CanStop` | 启动数据面（注册该口 INTID，CAN1=34/CAN2=35）/ 有界超时停止回收 |
| `CanSend / CanRecv` | 协议无关帧级收发（标准/扩展 id、数据/远程帧、dlc 0..8；Send 单帧 FIFO 同步握手，Recv 非阻塞返回 `-EAGAIN`） |
| `CanGetBusState / CanGetStats` | 总线状态（error-active/warning/passive/bus-off + 错误计数）/ 收发统计（受理/完成/超时/丢弃/去重/中断计数） |

## 10. 常见问题

| 现象 | 原因/处理 |
|---|---|
| 重编镜像后 insmod 报 oh_rt 节点不存在 | src_tmp 被重建、手工 dts patch 丢失（3.2 方式 A 警告），改走方式 B |
| insmod 报 vermagic/签名不匹配 | 内核重编过、ko 没重编（4.5 节 rebuild_ohrt.sh） |
| patch 报 malformed | patch 文件带 CRLF（Windows 往返过），`dos2unix` 后再用 |
| `/proc/cpuinfo` 是 4、`/proc/iomem` 无 74000000 | 烧的还是旧镜像；确认 boot_linux.img 已重编并烧写（3.3/3.4） |
| hdc 连不上 | reboot 后 IP 不持久，先串口配网（第 6 节） |
| 运行报 cannot load / no such file | so 没传全或 `LD_LIBRARY_PATH` 未设置（/data/lib 4 个 so + export） |
| timeout waiting NS announce | 从核固件没有该端点名的服务，或 `-e` 名字与从核不一致 |
| round trip 超时偶发 | 从核首启时序竞态，reboot 后重试 |
| 二次运行 mica_start 失败 | 上一次从核异常挂死未走 CPU_OFF（正常 mica_stop 可直接再次拉起）；挂死后只能 reboot 板子 |
| 单包发送失败/被拒 | 负载 > ~400B，应用层分段 |
| 报rpmsg_demo没有权限 | 可以先对rpmsg_demo本身进行chmod +x，如果还是报权限问题，则是 /data/lib/ld-musl-aarch64.so.1缺乏权限，对它进行chmod +x即可|
| 从核 `NetStart` 失败 | 主核 `mcs_net.ko` 没有先加载（ctrl 块未 READY）或两侧 `net_ctrl.h` 版本不一致；`cat .../mcs-net-gmac1/state` 看 master=1 |
| 网卡 demo 只发不收（rx=0） | 强制 100M 链路对端跑自协商 → 半双工失配；对端网卡同样强制 100M FD（Windows：设备管理器→网卡→高级→速度和双工） |
| 网卡 `link=0` 起不来 | 对端没强制/线序/PHY 复位脚问题；主核 `cat .../mdio` 看 PHY 寄存器是否全 0xffff（MDIO 不通） |
| `insmod mcs_can.ko` 后无 CAN 日志 / state 报 clocks off | boot 镜像还是无 CAN 节点的旧版，按 3.7 增量升级（重打 patch + 重编烧录 boot_linux.img） |
| 从核 `[can] master mcs_can.ko not up` / demo 报 skipped | `mcs_can.ko` 未加载（insmod 看 dmesg step0..5）；CAN2 场景则是 dts 节点未启用（板级 dts `&mcs_can2 { status = "okay"; };` + 重编烧录，见 `mcs_can/USER_GUIDE.md` §3.1） |
| `CanInit` 返回 -EINVAL 且打印 "no bit timing" | 800kbps 在 100MHz 位时钟下数学不可达（100M/800k=125，分频步进 2 无偶数解），换档位或用 raw 时序 |

## 11. CAN 驱动完整指南

> 本章为 `mcs_can/USER_GUIDE.md` 的完整副本（正本在模块目录，便于单独分发与阅读；两处内容一致，更新以正本为准）。**CAN1 已上板全量验证**（13 阶段回环自测全绿）；**CAN2 为接口预留扩展**，硬件通路尚未上板验证，当前请使用 CAN1。

> 对象：在 RK3568（toybrick/DAYU210）AMP 双系统上，把 CAN1（默认）与
> CAN2（**接口预留扩展，尚未上板硬件验证**——可选，需在 dts 启用）让渡给 LiteOS-M 实时核（CPU3）作为**协议无关
> 实时 CAN 口**使用的协议开发者/集成者。两口可**并发**使用。
> 主核侧驱动：本目录 `mcs_can.c`（Linux 6.6 out-of-tree 模块，单模块多口：
> dts 节点的 compatible 选口）。
> 从核侧驱动：morpheus 仓 `device/board/rockchip/rk3568/liteos_new/board/driver/can/`
> （`can.h` 公开 API + `can_rk3568.c` 实现，每口一个 `CanDevice` 实例）。
> 设计细节见 `mcs_can.c` / `can_rk3568.c` 头部注释（本文只讲"怎么用"）。

---

### 11.1 概述

#### 11.1.1 这是什么

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

#### 11.1.2 职责划分（重要约定）

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

### 11.2 端到端流程总览

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

### 11.3 主核侧操作

#### 11.3.1 DTS 配置（一次性）

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

#### 11.3.2 构建模块

```sh
# 前置：OpenHarmony 全源码树且 kernel/linux-6.6 已编译过（out/kernel/src_tmp 存在）
export CODE=<OpenHarmony 完整源码树>
./build_mcscan.sh
# 产物：当前目录 mcs_can.ko（已用内核签名密钥签名），日志在 ../out/logs/
```

#### 11.3.3 加载与卸载

```sh
hdc file send mcs_can.ko /data/mcs_can.ko
hdc shell insmod /data/mcs_can.ko
dmesg | grep mcs_can          # step0..5 全 OK 即就绪
hdc shell cat /sys/devices/platform/mcs-can1/state
# 卸载（从核应已 CanStop；rmmod 后 ctrl → OFFLINE，从核 CanInit 会拒绝）
hdc shell rmmod mcs_can
```

与其他模块无顺序依赖：`mcs_km.ko`（AMP 框架）先行，`mcs_net.ko`/`mcs_can.ko`
相互独立、可各自加载（混合固件两者都要，见 §11.11.5.3）。

#### 11.3.4 主核侧诊断

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

### 11.4 从核侧 API 参考（协议层视角）

```c
#include "can.h"   // device/board/rockchip/rk3568/liteos_new/board/driver/can/
```

共 7 个接口，全部无动态内存、可重入于单任务上下文（Recv/GetStats 可从
任意任务调用；Send 与 Start/Stop 需同一任务串行）。

#### 11.4.1 初始化配置 CanConfig

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
| `CAN_WORK_LOOPBACK` | 片内回环 | 自发自收 + 自应答，引脚无输出：**零硬件验证模式**（语义见 §11.11.6.1） |
| `CAN_WORK_SILENT` | 只听 | 总线监视：只收不发、不应答 |
| `CAN_WORK_SILENT_LB` | 静默回环 | 回环 + 只听组合（不出引脚、不参与总线） |

**4.1.3 位时序**

- **bitrate 风格（默认）**：驱动对主核发布的**真实读回** baudclk（100MHz）
  求解分频，采样点自动取 ~87.5%（CiA 推荐）。支持 125k/250k/500k/1M 等。
  **800kbps 不可达**：100M/800k=125，分频器步进 2，无偶数解（`CanInit`
  返回 `-EINVAL`）。
- **raw 风格**：`use_raw_timing=1` 时 `brp/sjw/ts1/ts2` 直写 NBTP（偶数
  brp 2..512、各段 1..128，范围外返回 `-EINVAL`），供时序专家微调。

#### 11.4.2 API 一览

| 接口 | 语义 |
|---|---|
| `CanInit(cfg, &dev)` | 按 `cfg->port_id` 选口 + 校验配置 + 主核门禁 + 软复位模式编程（滤波/FIFO/模式位/位时序），停在 READY。RUNNING 中需先 Stop；口未让渡返回 `-ENODEV` |
| `CanStart(dev)` | 注册该口 INTID（CAN1: 34 / CAN2: 35，IRQ/HYBRID）+ 开中断掩码（POLL 全掩）+ 退出软复位上总线 |
| `CanStop(dev, timeout_us)` | 掩全部中断 → 回软复位 → 注销 IRQ → 清就绪队列（无 DMA，即刻静止） |
| `CanSend(dev, frame, timeout_us)` | 同步发一帧：单帧深 TX FIFO 握手，有界等待完成（超时 `-ETIMEDOUT`） |
| `CanRecv(dev, frame)` | 非阻塞收一帧；无帧返回 `-EAGAIN`（POLL/HYBRID 先收割硬件 FIFO） |
| `CanGetBusState(dev, &info)` | 总线状态快照（error-active/warning/passive/bus-off + 错误计数） |
| `CanGetStats(dev, &stats)` | 收发统计（见 4.4） |

#### 11.4.3 帧契约 CanFrame

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

#### 11.4.4 错误码与统计

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
（回环重复帧被驱动吞掉数，正常=回环模式下每帧恰 0，见 §11.11.6.1）、
`irq_count`（POLL 恒 0）、`bus_off_count`。

---

### 11.5 典型使用模式

#### 11.5.1 收包模式怎么选

- **默认 IRQ**：负载不密集、要省 CPU——事件驱动，任务阻塞在信号量/事件上。
- **POLL**：最高确定性（协议周期固定，如 1ms 轮询总线）——零中断抖动，
  GICD 零触碰，`CanRecv` 空转开销 ~微秒级。
- **HYBRID**：IRQ 为主 + 轮询兜底（防单次中断丢失导致 FIFO 溢出覆盖）。

#### 11.5.2 工作模式怎么选

- 上板第一阶段用 **LOOPBACK** 验证驱动+协议栈（零硬件，内置 demo 即此）；
- 接入真实总线切 **NORMAL**（外部总线需 CAN 收发器：can1m1 引脚在
  toybrick/DAYU210 未焊接收发器，需自行外接）；
- 总线分析/被动监听用 **SILENT**（不应答、不发送，不影响总线负载）。

#### 11.5.3 内置用户 demo（can_test.c）解读

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

### 11.6 硅片特性与已知问题（必读）

#### 11.6.1 LOOPBACK 语义（实测钉死的硅事实）

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

#### 11.6.2 其他硅片行为

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

### 11.7 多 CAN 口 / 换板扩展

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
5. **CAN2 为 opt-in**：dts 中 `mcs-can2` 默认 `disabled`，启用步骤见 §11.11.3.1；
   未启用时从核 `CanInit(CAN2)` 返回 `-ENODEV`（demo 显示 skipped）。

**换其他 CAN 控制器/换板**：新控制器只需①主核 dts 加一个 `mcs-canN` 节点
（时钟/复位/pinmux/ctrl-phys 按控制器填，ctrl 页按 0x100 步进分配新页）+
②从核加一个 `canN_profile.h` 并在 `g_ports[]` 表添一行，`Can*` API 与协议
层零改动。

---

### 11.8 常见问题排查

| 现象 | 原因与处理 |
|---|---|
| 从核 `[can] master mcs_can.ko not up ... CAN2 clocks off` + demo `skipped` | **CAN2 节点未启用**（dts 默认 disabled）：设计行为。需要第二口时按 §11.11.3.1 启用 |
| 从核 `[can] master mcs_can.ko not up`（CAN1） | 主核 ko 未加载/已 rmmod。`insmod` 后看 dmesg step 全 OK 再重启从核 demo |
| `CanInit` 返回 -EINVAL 且打印 "no bit timing" | 多半是 800kbps（数学不可达）或 bitrate=0 且未给 raw；换档位或用 raw |
| Send 返回 -110 超时 | LOOPBACK 之外的死总线（无应答者）；确认 work_mode 或接好总线 |
| RX 恒 0 但 TX 正常 | 对端未发帧/滤波配置过严（`filter_en` 关掉试试）/SILENT 模式忘记切回 |
| 回环模式下偶发 mismatch | 不应发生；若复现记录 `dup-miss` 哨兵打印并反馈（去重窗口需调整） |
| demo 后期 rx 不再增长 | 检查 `[can] ... rx dN` 是否与 tx 对齐；`rx_overflow` 非零=任务消费不及时（提优先级或改 POLL） |

---

### 11.9 附：实测记录摘要（RK3568 DAYU200，主核 Linux 6.6 / 从核 CPU3 AArch64 EL1）

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
  can2 节点后重编 boot（步骤见 §11.11.3.1）。
- **HYBRID 收包 + 验收滤波补测**（2026-10-08）：demo 扩至 13 阶段。
  `lb-hyb-500k` 20/20 全收且 `irq40>0`（ISR 抢收与 Recv 兜底双路径均
  工作）；`lb-flt-even` sent 20 / echoed 10 / filtered 10 / errors 0——
  code=0x100、mask=0x7fe 下奇数 id 在硬件滤波器中**零泄漏**消失，且
  证明 LOOPBACK+RXSTX 回显路径同样过硬件滤波器。同轮修复驱动滤波分支
  `mask=0` 语义（此前 fallthrough 全开，现为精确匹配 code，对齐 §11.11.4.1
  契约）；其余 9 个 CAN1 阶段与上轮逐项一致，`all phases done` 无失败。
- 回归环境：`mcs_km.ko + mcs_can.ko`（混合另加 `mcs_net.ko`），
  `rpmsg_demo -f liteos-*.elf -c 3`，串口 1500000。
