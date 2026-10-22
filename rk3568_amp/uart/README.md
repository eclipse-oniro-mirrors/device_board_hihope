# mcs_uart - RK3568 UART3 让渡驱动（主核侧）

主核（Linux）侧的 UART 让渡模块：把 RK3568 的 UART3（fe670000，DW 8250，
20pin J9903 引出 uart3m1 组：TX=GPIO3_B7 / RX=GPIO3_C0）让渡给从核
（CPU3 LiteOS-M）做实时串口。构建与部署方式与 `../can`、`../net`
完全一致（out-of-tree ko）。

与 GMAC1/CAN 的“资源服务型”让渡不同，UART 无需主核提供 PHY/时钟/共享
控制块等任何服务——主核唯一且不可替代的职责是**探活焊盘**：

> 根因（issue 1014，DAYU200 实测）：`status="disabled"` 下从核用裸寄存器
> 写 GRF（IOMUX+PULL 同值）配置 uart3m1 组，TX 回环正常而 GPIO3_C0（RX）
> 输入通路始终不通。C0 输入通路依赖 Linux 驱动模型 probe 框架在
> 8250_dw 绑定 fe670000.serial 时完成的初始化（pinctrl 等），从核无法用
> 寄存器写复现；其后任何 SRR / CRU 软复位都会撤销该初始化。

因此让渡流程是**先探活、后逐出**：

| step | 动作 | 执行者 |
|------|------|--------|
| dev | `&uart3 { status = "okay"; }`（`../dts/rk3568-ohrt-mcs.dtsi` 默认已开），platform 设备 fe670000.serial 出现 | dts |
| bind | boot 时 8250_dw 绑定该设备，probe 框架完成 uart3m1 焊盘初始化 | Linux |
| release | 等待绑定完成后释放驱动（等效 `echo fe670000.serial > /sys/bus/platform/drivers/dw-apb-uart/unbind`，同一代码路径 `device_release_driver()`），焊盘保持已初始化态 | **本模块** |

模块内部自带 dev/bind 等待（每步默认 30s 超时，`timeout_sec` 可调），
insmod 时机任意；加载失败（超时/绑定到意外驱动）返回错误码并给出
dmesg 诊断，不重试、不影响其它口。

从核侧（evolve 分支 `kernel/liteos_m/targets/rk3568_m/liteos_m/board/uart.c`）
自带全套 bring-up：CRU 时钟门控 + xin24m 父时钟 + GRF pinmux/pull + GIC
INTID 151 认领/路由；**严禁复位控制器**（SRR / CRU SOFTRST 均会把焊盘
打回未初始化态，从核驱动已按此约束实现）。让渡完成后本模块不持有任何
资源、无运行时状态：rmmod 无副作用，不会触发重绑定。

## 构建

```sh
export CODE=<OpenHarmony 完整源码树>
./build_mcsuart.sh          # 产物: mcs_uart.ko（已签名），日志 ../out/logs/
```

## 部署（板上）

```sh
hdc file send mcs_uart.ko /data/mcs_uart.ko
hdc shell insmod /data/mcs_uart.ko    # 时机：boot 完成后、mcs_km.ko 之前
dmesg | grep mcs_uart                 # step dev/bind/release 全 OK
hdc shell "ls /sys/bus/platform/drivers/dw-apb-uart/"   # 无 fe670000.serial
```

## 验证

从核侧 demo（`uart_test`，rk3568_m target）：片内 MCR 回环 + 片外跳线
回环（GPIO3_B7 ↔ GPIO3_C0 短接）。`uart_test 3 self` 与片外跳线双 PASS
（256/256，isr=480 基线）即通过。详见 liteos_m 仓 issue #1014。

## 换口 / 多口

当前默认让渡 UART3（`port=fe670000.serial`，模块参数可改）。让渡其它
UART：`insmod mcs_uart.ko port=<reg基址>.serial`，从核侧同步调整
BOARD_UARTx_PINMUX 配置；一次 insmod 让渡一个口，多口多次加载即可。
