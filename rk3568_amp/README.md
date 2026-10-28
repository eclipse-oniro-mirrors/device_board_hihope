# RK3568 混合部署（AMP）— 板级仓（device/board/hihope/rk3568_amp）

RK3568 混合部署方案拆分到两个仓：

| 仓 | 目录 | 职责 |
|---|---|---|
| device_board_hihope | `rk3568_amp/`（本目录） | **板级**：网卡让渡（`net/`，GMAC1）、CAN 让渡（`can/`，CAN1/CAN2）、UART3 让渡（`uart/`，先探活-后逐出）、板级 dts（`dts/`）与 dts 补丁（`patches/`）、示例（`example/`：rpmsg_demo / netproxy_demo，`slave_boot/`）、端到端用户指南 |
| device_soc_rockchip | `rk3568/mcs/` | **架构**：mcs_km 让渡框架、libmica 通信库、open-amp / libmetal 预编译二进制（源码待三方开源引入评审通过后进 third_party） |

快速开始：`USER_GUIDE.md`（端到端：dts 修改 → 内核重编 → 构建主/从核产物 → 上板部署 → 通信验证）；编译与使用速查见 `mcs-build-usage-guide.md`。
接口说明：`API.md`。网卡/CAN/UART 模块各自的详细指南在 `net/USER_GUIDE.md`、`can/USER_GUIDE.md`、`uart/README.md`；主从核通信应用开发见 `example/README.md`。
