# RK3568 混合部署（mcs）编译与使用指导

> 适用对象：拿到代码后需要在 RK3568（dayu210）上编译、部署、验证 mcs 混合部署方案的同事。
> 主核基线：OH 6.1-LTS（分支 `OpenHarmony_oh2b_mxs_20260908`）。

## 1. 方案概览

```
┌──────────────────────────── RK3568 (4 x Cortex-A55) ────────────────────────────┐
│  CPU0-2：OpenHarmony Linux 6.6（主核）        CPU3：LiteOS-M 裸核（从核）      │
│  ┌──────────────────────────────┐            ┌──────────────────────────────┐  │
│  │ mcs_km.ko（内核模块）        │            │ LiteOS-M 内核                │  │
│  │  - 从核固件加载 / PSCI 启停  │  rpmsg     │  + compat/mcs 组件           │  │
│  │  - 共享内存 / vring 管理     │◄──virtio──►│    - openamp（从核裁剪版）   │  │
│  │ 用户态：libmica + open-amp   │  (共享内存  │    - libmica（从核版）       │  │
│  │  + libmetal                  │  + SGI7)   │    - tty/umt/echo 服务      │  │
│  │ 测试：rpmsg_demo / slave_boot│            │ 板级：rockchip/rk3568        │  │
│  └──────────────────────────────┘            └──────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────────┘
```

- 通信机制：rpmsg over virtio（共享内存 vring + SGI7 核间中断）
- 从核 tick：本地 PPI 30（CNTP，EL1 物理定时器），主核零配合（详见仓内 USER_GUIDE 第 5 节）
- 验证用例：`rpmsg-echo` 回环（主核发 → 从核回显 → 主核校验）

## 2. 代码组成与拉取

方案在两个仓单元中（与 openEuler 上游解耦、独立演进）：

| 仓 | 位置 | 内容 |
|---|---|---|
| **device_board_hihope**（板级） | `$CODE/device/board/hihope`，目录 `rk3568_amp/` | `net/`（网卡让渡 ko）、`can/`（CAN 让渡 ko）、`dts/`、`patches/`、`example/`（rpmsg_demo）、`slave_boot/`、`USER_GUIDE.md` / `API.md` 等文档 |
| **device_soc_rockchip**（架构） | `$CODE/device/soc/rockchip`，目录 `rk3568/mcs/` | `mcs_km/`（ko 源码）、`libmica/`（用户态框架源码）、`libmica-compat/`、`prebuilt/`（libmetal/open-amp 预编译二进制 + 头文件 + 适配补丁）、`scripts/` |
| **从核源码**（evolve 分支多仓） | OpenHarmony **evolve 分支**（`OpenHarmony_liteos_evolve_20260924`）repo 工作区 | `kernel/liteos_m`（LiteOS-M 内核 + `compat/mcs/` 从核组件：openamp 适配 + libmica 从核版 + tty/umt/echo 服务；`targets/rk3568_m/liteos_m/board` 板级与 can/net 实时驱动），配套 `device/board/hihope/rk3568_m`、`device/soc/rockchip/rk3568_m`、`vendor/hihope/rk3568_m`（Kconfig/部件注册） |

```bash
# 主核侧：两仓在 OH 树内（repo 工作区或单独 clone 后放入）
git clone -b <分支> <hihope 仓地址> $CODE/device/board/hihope
git clone -b <分支> <soc 仓地址>   $CODE/device/soc/rockchip

# 从核侧：evolve 分支 repo 工作区（从核多仓协同演进）
repo init -u <manifest-url> -b OpenHarmony_liteos_evolve_20260924
repo sync -c
```

> 从核侧另有**二进制 SDK 包**（`rk3568_amp_slave_sdk.tar.gz`）：内核/openamp/libmetal/
> libmica/musl 预编译库 + 板级与应用源码，适合不关心内核的应用开发者，见其包内 README。

## 3. 环境要求

| 项 | 要求 |
|---|---|
| OH 全仓 | 已完成 rk3568 产品编译（主核构建链依赖其产物） |
| musl sysroot | `$CODE/out/<产品名>/obj/third_party/musl/usr`（脚本自动探测，可 `MUSL=` 覆盖） |
| 已编内核 | `$CODE/out/kernel/{src_tmp,OBJ}/linux-6.6`（ko 外部构建 + 签名用） |
| 工具链 | 无需自装，全部取自 `$CODE/prebuilts/clang/ohos/linux-x86_64/llvm` |
| `CODE` 变量 | 脚本自动从自身位置向上探测 OH 树根（含 `prebuilts/` 的目录）；树外使用时 `export CODE=<OH 树根>` |
| 板子镜像 | **必须包含 oh_rt 预留资源**（CPU3 内存/共享内存/中断），见 6.1 |

## 4. 从核侧编译（evolve 分支源码树）

方式一（源码，产品级构建，产品 `rk3568_m` 已内置全部配置）：

```bash
cd <evolve 工作区>
./build.sh --product-name rk3568_m

# 产物（out/ 下另有带符号版本，便于 addr2line）：
cp out/rk3568_m/rk3568_m/OHOS_Image liteos-m.elf
```

方式二（SDK 包，只需 OH clang 15.0.4 工具链）：

```bash
tar xzf rk3568_amp_slave_sdk.tar.gz && cd rk3568_amp_slave_sdk
export OHOS_LLVM=<OH clang llvm 目录>
./build.sh                        # → out/liteos-m.elf（改 example/main.c 写业务）
```

## 5. 主核侧编译（两仓：board rk3568_amp + soc rk3568/mcs）

三方库以预编译动态库交付（`device/soc/rockchip/rk3568/mcs/prebuilt/`）；libmica 从 soc 仓源码编译。主核侧构建顺序：

```bash
cd $CODE/device/soc/rockchip/rk3568/mcs/scripts
./rebuild_ohrt.sh                 # 1. mcs_km.ko（树外构建+签名）→ ../mcs_km/
./build_libmica.sh                # 2. libmica.a/.so（源码编译）+ 上板 so 部署集 → ../out/

cd $CODE/device/board/hihope/rk3568_amp
./slave_boot/build_slave_boot.sh  # 3. slave_boot（仅拉核工具，内核调试用，可选）→ out/artifacts
./example/build_example.sh        # 4. rpmsg_demo（应用示例，动态链接）→ out/artifacts
./net/build_mcsnet.sh             # 5. mcs_net.ko（网卡让渡，可选）→ net/
./can/build_mcscan.sh             # 6. mcs_can.ko（CAN 让渡，可选）→ can/
```

产物清单：

| 产物 | 位置 | 用途 |
|---|---|---|
| `libmetal.so.1` `libopen_amp.so.1`（prebuilt 交付，`-l:` 语法链接）；`libmica.a/.so` `libmica_compat.a`（源码构建） | `device/soc/rockchip/rk3568/mcs/prebuilt/lib`、`out/staging/lib` | 主核用户态库（应用动态链 .so） |
| `rpmsg_demo` | `device/board/hihope/rk3568_amp/out/artifacts` | 主从核通信示例（拉起 + echo 回环 + interactive 模式） |
| `slave_boot` | `device/board/hihope/rk3568_amp/out/artifacts` | 只拉起从核不通信（内核适配调试用，可选） |
| `mcs_km.ko` | `device/soc/rockchip/rk3568/mcs/mcs_km/` | 内核模块（已用内核树证书签名） |
| so 部署集（5 个文件） | `device/soc/rockchip/rk3568/mcs/out/artifacts/lib`（build_libmica.sh 生成） | 上板 so 部署集：`libmica.so` + `libmetal.so.1` + `libopen_amp.so.1` + aarch64 musl `libc.so`、`ld-musl-aarch64.so.1` |

说明：
- 三方库（libmetal/open-amp）以预编译动态库交付在 soc 仓 `prebuilt/`；libmica 由 soc 仓源码经 `build_libmica.sh` 编译
- 应用全部动态链接（musl interpreter），上板需带 so 部署集
- 每步日志在 `out/logs/*.log`；`check_gpl.sh` 为 ko 导出符号 GPL 合规检查

### out/ 目录结构

`out/` 是构建脚本的输出，**纯生成物**：可整目录删除，重跑脚本即重建，不入库。

```
device/soc/rockchip/rk3568/mcs/out/          # rebuild_ohrt.sh / build_libmica.sh 输出
├── staging/lib/                             #   libmica.a、libmica.so、libmica_compat.a（源码编译）
├── artifacts/lib/                           # ★ 上板 so 部署集，整目录拷 /data/lib/：
│   ├── libmica.so / libmetal.so.1 / libopen_amp.so.1
│   ├── libc.so                              #   aarch64 musl libc
│   └── ld-musl-aarch64.so.1                 #   aarch64 musl 动态加载器（musl libc.so 兼任）
└── logs/                                    #   分步构建日志

device/board/hihope/rk3568_amp/out/          # demo 构建输出
└── artifacts/
    ├── rpmsg_demo                           #   应用示例（example/build_example.sh）
    └── slave_boot                           #   拉核工具（slave_boot/build_slave_boot.sh，可选）
```

**为什么 `artifacts/lib` 里要带 musl 的 libc 和 loader**：板上 OH 用户态是 32 位 ARM，
而主核侧应用与库按 64 位（aarch64）编译，板上没有现成的 aarch64 动态加载器。
musl 的 `libc.so` 兼任 ldso，随部署集放在 `/data/lib/`，应用在链接期通过
`-Wl,--dynamic-linker=/data/lib/ld-musl-aarch64.so.1` 固定引用它——
**loader 路径已写进 demo 的 ELF 头，板上部署路径 `/data/lib` 不能改**。

`mcs_km.ko` 不在 `out/` 下：树外构建直接落在源码目录 `mcs_km/`，与用户态产物分开。

## 6. 上板部署与验证

### 6.1 镜像前提（重要）

板上的 OH 镜像必须打上 `mcs/patches/rk3568_dts_ohrt.patch`（预留 CPU3 内存窗口
0x74000000、共享内存、中断等 `oh_rt` 节点资源）。**没打 patch 的镜像 insmod
会失败**。补丁打入方式见 `mcs/patches/`，或直接使用已按此构建的镜像。

### 6.2 板子准备（每次 reboot 后）

```bash
# 调试串口（COM5，115200）上执行：
dmesg -n 3                                        # 关刷屏日志
ifconfig eth0 192.168.100.10 netmask 255.255.255.0 up
```

### 6.3 部署与测试

```bash
# 主机侧传输（hdc，eth0 直连 192.168.100.10:8710）：
hdc tconn 192.168.100.10:8710
hdc file send $CODE/device/soc/rockchip/rk3568/mcs/mcs_km/mcs_km.ko /data/
hdc file send $CODE/device/board/hihope/rk3568_amp/out/artifacts/rpmsg_demo /data/
hdc file send $CODE/device/soc/rockchip/rk3568/mcs/out/artifacts/lib/* /data/lib/
hdc file send <evolve>/out/rk3568_m/rk3568_m/OHOS_Image /data/liteos-m.elf

# 板上（串口或 hdc shell）：
insmod /data/mcs_km.ko
export LD_LIBRARY_PATH=/data/lib
/data/rpmsg_demo -f /data/liteos-m.elf -c 3 -e rpmsg-echo -n 100
```

rpmsg_demo 参数：`-f` 固件路径、`-c` 从核号（默认 3）、`-e` 端点
（`rpmsg-echo` 回环 / `rpmsg-tty` 字节流）、`-n` 收发次数、`-i` 进入
interactive 模式（逐行收发）。

### 6.4 预期输出

从核侧（启动阶段在调试串口，运行期打印经 rpmsg 回主核）：

```
LiteOS-M for RK3568 (AMP core3) boot...
mica task enter.
[openamp] get rsctable 7xxxxxxx, size a64
[openamp]: get vring0: da 74060000
[openamp]: get vring1: da 740600e0
Creating receiver thread...
Creating UMT thread...
Creating ECHO thread...
```

主核侧：

```
[demo] firmware=/data/liteos-m.elf cpu=3 ept=rpmsg-echo mode=auto
[demo] step1 mica_create ok
[demo] step2 mica_start ok, waiting NS announce...
[demo] NS bind: name=rpmsg-echo peer_addr=1026
[demo] step3 endpoint bound (local addr 1024, peer 1026)
...
[demo] PASS: 100/100 round trips
[demo] slave core powered off
[demo] done
```

### 6.5 slave_boot（只拉起从核，无通信）

调内核/arch 适配、不关心通信时用：

```bash
/data/slave_boot -f /data/liteos-m.elf -c 3        # 拉起后 Ctrl-C 停核退出
/data/slave_boot -f /data/liteos-m.elf -c 3 -K     # 拉起后保持运行（keep-alive）
```

## 7. 常见问题（FAQ）

1. **脚本报 `export CODE=<OpenHarmony full source tree root>`？**
   脚本不在 OH 树内运行（自动探测失败）。把 mcs 目录放回 `$CODE/device/board/hihope/rk3568/` 下，或显式 `export CODE=<OH 树根>`。
2. **insmod 报 vermagic / 签名不匹配？**
   ko 必须用与板上镜像**同一个 OH 树**编出的内核构建（`out/kernel/.../linux-6.6`）。镜像重编过就要重跑 `rebuild_ohrt.sh`。
3. **dmesg 里有 `mcs: mcs_km: oh_rt,mcs_riscv_remoteproc node not found in DTS`？**
   这是 riscv 从核路径的探测日志。我们是同构（4 个 A55，3 跑 OH + 1 跑 LiteOS-M），不涉及 riscv，**该行可忽略**；只要 oh_rt（ARM 路径）节点在即可。
4. **insmod 报 oh_rt 节点不存在 / mcs_km 初始化失败？**
   板上镜像没打 dts 补丁（见 6.1），重新构建烧录镜像。
5. **build_userlibs.sh 报找不到 musl sysroot？**
   你的 OH 树还没构建过产品（`out/<产品名>/obj/third_party/musl/usr` 不存在）。先跑一次产品编译；或 `export MUSL=<sysroot>` 显式指定。
6. **第二次拉起从核失败？**
   正常路径 demo 会自动下电从核（`slave core powered off`）后退出。若上次异常退出，从核可能残留上电状态，重新 reboot 板子后重试。
7. **串口刷屏 / 网络不通？**
   reboot 后需重新执行 6.2 的两条命令（非持久配置）。

## 8. 提交规范（给要改代码的同事）

- 主核侧：hihope 仓，分支 `OpenHarmony_oh2b_mxs_20260908`；commit 需带 `Signed-off-by`（DCO），邮箱与账号绑定邮箱一致
- 从核侧：evolve 分支各仓（`kernel/liteos_m` 等），正常 git flow，提 PR 合入 `OpenHarmony_liteos_evolve_20260924`
- `mcs/out/` 构建产物已 gitignore，不上传
- Change-Id 由 repo 工具的 commit-msg hook 自动生成；单独 clone 的仓从 repo 工作区 `$CODE/repo/hooks/commit-msg` 拷到 `.git/hooks/`

## 9. 参考文档

- `$CODE/device/board/hihope/rk3568_amp/README.md` — 三仓结构与职责（板级/架构/示例）
- `$CODE/device/board/hihope/rk3568_amp/USER_GUIDE.md` — 完整用户指南（dts/烧录/上板/应用开发/从核 SDK）
- `$CODE/device/board/hihope/rk3568_amp/API.md` — libmica 应用接口
- `$CODE/device/soc/rockchip/rk3568/mcs/README.md` — 架构仓结构与 prebuilt 重编方法
- `$CODE/device/board/hihope/rk3568_amp/example/README.md` — 主从核通信应用开发指南（rpmsg_demo）
- `kernel/liteos_m/compat/mcs/README.md`（evolve 分支）— 从核组件与裁剪记录
- 从核 SDK 包内 `README.md` — SDK 使用说明
