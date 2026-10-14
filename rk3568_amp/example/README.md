# 混合部署框架用户指南 — 主从核 rpmsg 通信

> 面向应用开发者：怎么用 mcs 混合部署框架跟从核通信。
> 配套示例 `rpmsg_demo.c`：拉起从核 + 主从核互相收发消息的完整可运行参考实现。

## 0. 框架分层（30 秒版）

```
你的应用（主核 OpenHarmony Linux，CPU0-2）
  └─ libmica                    从核生命周期 + 服务注册      ← 应用直接调用
       └─ openamp / libmetal    rpmsg 端点收发               ← 应用直接调用（发数据）
            └─ /dev/mcs         内核模块 mcs_km.ko           ← 应用不碰，insmod 一次即可

从核（CPU3）跑 LiteOS-M + mica 从核库，经共享内存 vring 与主核互通
```

动态库依赖：`libmica.so`、`libopen_amp.so.1`、`libmetal.so.1`
（上板放 `/data/lib`，运行前 `export LD_LIBRARY_PATH=/data/lib`）。

## 1. 快速上手：跑示例

```bash
# 构建机编译（先在 soc 仓编译 libmica，再编 demo；产物 out/artifacts/rpmsg_demo）
$CODE/device/soc/rockchip/rk3568/mcs/scripts/build_libmica.sh
cd $CODE/device/board/hihope/rk3568_amp
./example/build_example.sh

# 上板（前提：镜像含 oh_rt dts 节点、mcs_km.ko 已 insmod）
SOC=$CODE/device/soc/rockchip/rk3568/mcs     # 架构仓（so 部署集由 build_libmica.sh 生成）
hdc shell mkdir -p /data/lib
hdc file send out/artifacts/rpmsg_demo                /data/
hdc file send $SOC/out/artifacts/lib/libmica.so       /data/lib/
hdc file send $SOC/out/artifacts/lib/libmetal.so.1    /data/lib/
hdc file send $SOC/out/artifacts/lib/libopen_amp.so.1 /data/lib/
hdc file send <liteos-rk3568.elf>  /data/liteos-m.elf
hdc shell chmod +x /data/rpmsg_demo

# 板上（串口 / hdc 交互 shell）：
insmod /data/mcs_km.ko                       # 若尚未加载
export LD_LIBRARY_PATH=/data/lib
/data/rpmsg_demo -f /data/liteos-m.elf -c 3  # 自动模式：5 条 ping 往返 + 校验
/data/rpmsg_demo -f /data/liteos-m.elf -i    # 交互模式：输入一行回车即发送，quit 退出
```

预期输出（自动模式，对端为从核 rpmsg-tty 回显服务）：

```
[demo] firmware=/data/liteos-m.elf cpu=3 ept=rpmsg-tty mode=auto
[demo] step1 mica_create ok
[demo] step2 mica_start ok, waiting NS announce...
[demo] NS bind: name=rpmsg-tty peer_addr=1025
[demo] step3 endpoint bound (local addr 1024, peer 1025)
[demo] rtt 170 us, reply(...): ...ping-001
...
[demo] PASS: 5/5 round trips
[demo] done
```

## 2. 主核应用编程模型（5 步）

```
mica_create → mica_start → mica_register_service → rpmsg_sendto / rx 回调 → mica_stop / mica_remove
(填固件/核号)  (载ELF+启核)   (NS 匹配/绑定回调)      (收发数据)             (停核回收)
```

| 步骤 | API | 要点 |
|---|---|---|
| 1 | `mica_create(client)` | 只填 3 样：`client->path`（从核 ELF 路径）、`client->ped = BARE_METAL`、`client->ped_setup.cpu_str`（核号字符串，如 "3"）；结构体 `calloc` 清零即可 |
| 2 | `mica_start(client)` | 加载 ELF → PSCI 启核 → 建 rpmsg vdev。从核起来后**异步**广播 NS 端点通告 |
| 3 | `mica_register_service(client, &svc)` | 注册两个回调：`rpmsg_ns_match`（要不要绑这个端点）、`rpmsg_ns_bind_cb`（绑定动作：`rpmsg_create_ept` 建本地端点、记下对端地址）。**必须在 mica_start 之后调** |
| 4 | `rpmsg_sendto(&ept, buf, len, peer_addr)` | 发送；接收走 `rpmsg_create_ept` 注册的 rx 回调 |
| 5 | `mica_stop` → `mica_remove` → `free` | 停核回收，顺序别乱 |

完整代码骨架直接看 `rpmsg_demo.c`（按步骤注释分块，可整文件拷走改）。

### 关键机制：服务发现 = "从核通告、主核绑定"

- 从核 `rpmsg_create_ept(..., "rpmsg-tty", ...)` 创建端点时，openamp 自动向主核发
  **NS（name service）通告**（端点名 + 地址）。
- 主核框架收到通告后依次调用你的 `match`（比对端点名）→ `bind`（创建本地 ept）。
  **主核不需要预先知道从核地址——端点名字就是两边的契约**。
- 主核也能自建端点主动通告（`rpmsg_create_ept` src=ANY），但从核服务框架按
  "从核通告"模式实现，建议沿用。

### 应用层注意事项

1. `mica_register_service` 必须在 `mica_start` 之后（内部要求 RPROC_RUNNING；
   若 NS 通告已先到，register 内部会立即补一次匹配绑定）。
2. `rpmsg_sendto` 成功**返回发送字节数**，负数才是错误（openamp 2022.10 语义）。
3. 单包负载上限 ~400 字节（rpmsg buffer 512B 留头部余量），更大数据应用层分段。
4. rx 回调运行在框架接收线程上下文：只做 `memcpy` + 唤醒，**勿阻塞、勿耗时处理**。
5. 一个从核实例对应一个 `mica_client`；要多个服务（不同端点名）就注册多个
   `mica_service`。
6. demo 结束 `mica_stop` 后若要再次拉起同一从核，需先 reboot 板子（CPU3 不会
   自动复位，这是当前限制）。

## 3. 从核侧：写自己的服务

从核（LiteOS-M）侧一个"服务" = 一个端点 + 一个接收处理函数。当前镜像自带
`rpmsg-tty` 回显服务（`components/mcs/mica/libmica/src/services/tty_service.c`），
收到的每个字节喂给 `mica_config.sys_ops.shell_cmd_handler`（板级
`vendor/hihope/dayu210/los_board.c` 的 `TtyShellHandler` 即回显逻辑）。

**方式一（推荐，最小改动）**：换掉 handler，不动框架：

```c
/* vendor/hihope/dayu210/los_board.c：MicaTaskEntry 的 cfg 注入自己的处理函数 */
STATIC VOID MyCmdHandler(CHAR c)
{
    /* 按字符积累命令行，回车触发处理；结果用 mica_tty_send 发回主核 */
    ...
    (VOID)mica_tty_send((unsigned char *)result, strlen(result));
}

cfg.sys_ops.shell_cmd_handler = MyCmdHandler;   /* 原来是 TtyShellHandler */
```

**方式二（独立端点，自定义协议不走 tty）**：仿 `tty_service.c` 在从核建自己的
端点（服务名即端点名，主核 demo 换 `-e` 参数对上）：

```c
/* 从核自定义服务骨架（LiteOS-M，线程入口） */
static struct rpmsg_endpoint my_ept;

static int my_rx_cb(struct rpmsg_endpoint *ept, void *data,
                    size_t len, uint32_t src, void *priv)
{
    /* 处理请求并直接回复（也可置标志交业务线程处理） */
    return rpmsg_send(ept, response, resp_len);   /* 回到请求方 src */
}

void *my_service_thread(void *arg)
{
    struct rpmsg_device *rpdev = mica_get_rpdev();
    rpmsg_create_ept(&my_ept, rpdev, "my-service",
                     RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, my_rx_cb, NULL);
    while (1) { /* 业务循环 / 事件等待 */ }
}
```

## 4. 自己的应用怎么编译

仿 `example/build_example.sh`：同一套 clang + musl sysroot + 动态链接参数，
把源文件换成你的。要点：

```bash
# 工具链（构建机上）
CLANG=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
MUSL=$CODE/out/wukong100/obj/third_party/musl/usr     # 产品名不同时相应调整
SOC=$CODE/device/soc/rockchip/rk3568/mcs               # 头文件 + 三方库；libmica.so 由 build_libmica.sh 产出

$CLANG --target=aarch64-linux-ohos --sysroot=$MUSL \
  -I$MUSL/include/aarch64-linux-ohos -B$MUSL/lib/aarch64-linux-ohos \
  -I$SOC/libmica/include -I$SOC/prebuilt/include -O2 -Wall \
  your_app.c -L$SOC/out/staging/lib -L$SOC/prebuilt/lib -lmica -l:libmetal.so.1 -l:libopen_amp.so.1 -lpthread \
  -o your_app

# 上板：your_app + $SOC/out/artifacts/lib 的 so 部署集 + 固件 elf，运行时 LD_LIBRARY_PATH=/data/lib
```

最省事的做法：直接改 `build_example.sh` 里的源文件路径复制一份当你的构建脚本。

## 5. API 速查

**主核侧**（头文件 `mica/mica.h` + `openamp/rpmsg.h`）：

| API | 作用 |
|---|---|
| `mica_create / mica_start / mica_stop / mica_remove` | 从核生命周期 |
| `mica_register_service(client, &svc)` | 注册 NS 匹配/绑定回调 |
| `mica_set / mica_status` | 运行时参数 / 状态查询 |
| `rpmsg_create_ept / rpmsg_destroy_ept` | 端点创建（bind 回调里调）/ 销毁 |
| `rpmsg_send / rpmsg_sendto` | 发数据（to 指定对端地址） |

**从核侧**（`mica/mica.h` + `mica/service.h`，见 `components/mcs/mica/libmica/src/services/`）：

| API | 作用 |
|---|---|
| `mica_get_rpdev()` | 取 rpmsg device（建端点用） |
| `mica_tty_send / mica_tty_printf` | 经 rpmsg-tty 服务发往主核 |
| `rpmsg_create_ept` | 建自定义服务端点（创建即自动 NS 通告） |

## 6. 常见问题

| 现象 | 原因/处理 |
|---|---|
| 运行报 cannot load / no such file | so 没传全或 `LD_LIBRARY_PATH` 未设置（/data/lib 4 个 so + export） |
| timeout waiting NS announce | 从核固件没有该端点名的服务，或 `-e` 名字与从核不一致 |
| round trip 超时偶发 | 从核首启时序竞态，reboot 后重试 |
| 二次运行 mica_start 失败 | CPU3 拉起过未复位，reboot 板子后再来 |
| 单包发送失败/被拒 | 负载 > ~400B，应用层分段 |
