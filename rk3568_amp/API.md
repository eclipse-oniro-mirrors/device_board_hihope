# 混合部署框架（mcs/mica）主从核接口说明

> 本文面向在 RK3568 上使用 mcs 混合部署框架的开发者，系统说明**从核拉起**与
> **核间通信**两侧（主核 OpenHarmony Linux CPU0-2 / 从核 LiteOS-M CPU3）
> 的编程接口，并配套可运行 demo。
>
> 姊妹文档：`README.md`（构建与上板）、`INTEGRATION.md`（内核/dts 联调）、
> `example/README.md`（应用上手指南）。

## 1. 总览

```
拉起链路                                     通信链路（rpmsg over 共享内存）
────────────                                ──────────────────────────────
应用（主核）                                  应用（主核）
  mica_create()                               mica_register_service(match/bind)
  mica_start() ──► mcs_km.ko                   │ NS 通告匹配
     │ 载 ELF 到保留内存                        rpmsg_create_ept()
     │ PSCI CPU_ON(core3)                       rpmsg_sendto() / rx 回调
     ▼                                            │ vring（共享内存 0x74000000）
从核 CPU3 @0x3F000000（LiteOS-M）                 ▼
  mica_init() ──► vdev ready ◄──────────► mcs_km.ko（/dev/mcs）
  mica_create_all_services()                  从核 CPU3（LiteOS-M）
     │ rpmsg_create_ept() 自动 NS 通告            rpmsg_create_ept() / rpmsg_send()
     ▼                                          mica_tty_send()（TTY 服务）
  业务服务就绪
```

主从核的**契约**只有两个：

1. **端点名（service name）**：从核创建端点时命名，主核按名绑定——主核不需要
   预先知道从核地址。
2. **协议常量**（共享内存基址/大小、vring 参数、通知中断），两侧源码对齐：
   - 主核：`dts/rk3568-ohrt-mcs.dtsi` + `mcs_km/` 内核模块源码
   - 从核：`kernel/liteos_m/components/mcs/mica/mica_cfg.h`

## 2. 接口分层

| 层 | 主核（Linux，CPU0-2） | 从核（LiteOS-M，CPU3） | 应用是否直接调用 |
|---|---|---|---|
| 生命周期 | libmica：`mica_create/start/stop/remove` | `mica_init` + `mica_create_all_services` | 是（两侧各一次） |
| 服务注册 | libmica：`mica_register_service`（match/bind 回调） | `rpmsg_create_ept`（创建即自动 NS 通告） | 是 |
| 数据收发 | open-amp：`rpmsg_create_ept / rpmsg_sendto / rx 回调` | open-amp：`rpmsg_send`（回请求方）；`mica_tty_send/printf`（TTY 通道） | 是 |
| 传输底座 | mcs_km.ko（vring/共享内存/核间中断） | components/mcs（openamp+libmetal 移植） | 否（insmod/随镜像） |

头文件位置：
- 主核：`mica/mica.h`（libmica）、`openamp/rpmsg.h`（open-amp），编译时 `-I<staging>/include`
- 从核：`mica/mica.h`、`mica/service.h`（components/mcs/mica/libmica/src/include）、
  `openamp/rpmsg.h`、`mica_service_internal.h`（`mica_get_rpdev` 声明，位于
  `components/mcs/mica/libmica/src/services/`）

## 3. 拉起接口（主核侧）

### 3.0 前置条件（应用不直接调用，但必须就位）

| 项 | 说明 |
|---|---|
| 内核 `mcs_km.ko` 已 insmod | 提供 `/dev/mcs`、vring 与共享内存管理、PSCI 启核 |
| 镜像 dts 含 `oh_rt,mcs_remoteproc` 节点 | 共享内存 0x74000000/1MB + 从核保留内存 0x74100000/61MB |
| cmdline `maxcpus=3` | CPU3 不被 Linux 调度 |
| 从核固件（liteos-rk3568.elf）已放到板上 | 内含 mcs 从核库与业务服务 |

### 3.1 `mica_create(client)` — 创建从核客户端

```c
struct mica_client {
    char path[MAX_FIRMWARE_PATH_LEN];  /* 从核 ELF 固件路径 */
    enum pedestal_type ped;            /* 填 BARE_METAL */
    struct pedestal_setup ped_setup;   /* name、cpu_str 两个字段 */
    /* 其余字段全部清零（calloc） */
};
int mica_create(struct mica_client *client);
```

应用只需填 3 样：`client->path`（固件路径）、`client->ped = BARE_METAL`、
`client->ped_setup.cpu_str`（从核 CPU 编号字符串，RK3568 为 `"3"`）。
返回 0 成功，负数为 errno。

### 3.2 `mica_start(client)` — 加载固件并拉起从核

```c
int mica_start(struct mica_client *client);
```

内部完成：ELF 段加载到保留内存 → PSCI `CPU_ON` 启核 → 等待从核 vdev ready。
**从核起来后 NS 通告是异步到达的**——`mica_start` 返回不代表服务可发，
要等服务绑定回调（见 4.2）。

### 3.3 `mica_stop / mica_remove` — 停核回收

```c
int mica_stop(struct mica_client *client);   /* 停核、销毁 vdev */
int mica_remove(struct mica_client *client); /* 释放客户端资源 */
```

顺序必须 stop → remove → free。**注意**：当前版本停核后 CPU3 不会自动复位，
再次拉起同一从核需 reboot 板子（已知限制）。

### 3.4 辅助接口

```c
int  mica_set(struct mica_client *client, char *key, char *value); /* 运行时参数 */
int  mica_register_service(struct mica_client *client, struct mica_service *svc);
```

`mica_register_service` 见 4.2；**必须在 `mica_start` 之后调用**（内部要求
RPROC_RUNNING；若 NS 通告已先到，内部会立即补一次匹配绑定）。

## 4. 核间通信接口

### 4.1 传输底座（协议常量，两侧必须一致）

| 常量 | 值 | 定义处（主核/从核） |
|---|---|---|
| 共享内存基址 `VDEV_START_ADDR` | 0x74000000 | dtsi `reg` / `mica_cfg.h` |
| 共享内存大小 `SHM_SIZE` | 0x100000（1MB） | 同上 |
| vring 数量 / 大小 | 2 个 / 16 buffer | dtsi / `VRING_SIZE` |
| vring ID | TX=0，RX=1（Linux 兼容，固定） | `VRING0_ID/VRING1_ID` |
| 通知中断 | 从核 `cfg.ipc_irq_num = 7`（SGI7） | `los_board.c` / mcs_km 适配 |
| 单包负载上限 | ~400 字节（buffer 512B 留头部） | 应用层自行分段 |

### 4.2 NS 服务发现 — "从核通告、主核绑定"

从核调 `rpmsg_create_ept(..., "服务名", ...)` 时，openamp 自动向主核广播
NS（name service）通告（端点名 + 地址）。主核框架收到后依次调用应用注册的
两个回调：

```c
struct mica_service {
    char name[...];
    bool (*rpmsg_ns_match)(struct rpmsg_device *rdev, const char *name,
                           uint32_t addr, uint32_t dest, void *priv);
    void (*rpmsg_ns_bind_cb)(struct rpmsg_device *rdev, const char *name,
                             uint32_t remote_addr, uint32_t remote_dest, void *priv);
};
```

- `rpmsg_ns_match`：逐个通告调用，返回 true 表示"这是我要绑的服务"
  （比对端点名）。
- `rpmsg_ns_bind_cb`：匹配成功调用。参数给出**从核端点地址**；在这里
  `rpmsg_create_ept` 建主核侧端点（对端地址 = 通告地址），并通知应用线程
  "可以收发了"。

### 4.3 主核收发

```c
int  rpmsg_create_ept(struct rpmsg_endpoint *ept, struct rpmsg_device *rdev,
                      const char *name, uint32_t src, uint32_t dest,
                      rpmsg_ept_cb cb, void *priv);
int  rpmsg_sendto(struct rpmsg_endpoint *ept, const void *data, int len,
                  uint32_t dst);      /* 返回值：成功=字节数，负数=错误 */
int  rpmsg_destroy_ept(struct rpmsg_endpoint *ept);
```

接收回调签名（运行在框架接收线程上下文——**只做拷贝+唤醒，勿阻塞**）：

```c
int rx_cb(struct rpmsg_endpoint *ept, void *data, size_t len,
          uint32_t src, void *priv);
```

### 4.4 从核侧

**启动**（板级 `vendor/hihope/dayu210/los_board.c` 的 `MicaTaskEntry`，业务
一般不用改）：

```c
struct mica_config cfg = {0};
cfg.shm_base_addr = VDEV_START_ADDR;   /* 0x74000000，与 dts 对齐 */
cfg.shm_size      = SHM_SIZE;          /* 0x100000 */
cfg.ipc_irq_num   = 7;                 /* SGI7 */
cfg.sys_ops.shell_cmd_handler = TtyShellHandler;  /* TTY 服务的字节处理器 */
mica_init(&cfg);
mica_create_all_services();            /* 起 RPC + TTY + UMT 三个默认服务 */
```

**发送到主核**（两条路）：

```c
int mica_tty_send(unsigned char *data, size_t len);  /* 走 rpmsg-tty 端点 */
int mica_tty_printf(const char *format, ...);        /* 同上，printf 风格 */
```

**自定义服务**（推荐业务接入方式）：

```c
extern struct rpmsg_device *mica_get_rpdev(void);  /* mica_service_internal.h */

static struct rpmsg_endpoint my_ept;
/* 在自己的任务里等 mica 起来后创建端点：创建即自动向主核发 NS 通告 */
rpmsg_create_ept(&my_ept, mica_get_rpdev(), "my-service",
                 RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, my_rx_cb, NULL);
```

接收回调里 `rpmsg_send(ept, resp, len)` 即可回到请求方（`src`）。

## 5. Demo 举例

### 5.1 现成 demo：`example/rpmsg_demo.c`（主核侧完整参考）

5 步全流程（可整文件拷走改）：

```bash
# 构建（构建机）：见 INTEGRATION.md 第 1 节
# 上板运行（自动模式：5 条 ping 往返校验，期望 PASS: 5/5）
/data/rpmsg_demo -f /data/liteos-m.elf -c 3
# 交互模式：输入一行回车即发送
/data/rpmsg_demo -f /data/liteos-m.elf -i
```

关键代码骨架（与 `rpmsg_demo.c` 步骤注释一一对应）：

```c
/* 步骤1：创建客户端 */
client = calloc(1, sizeof(*client));
snprintf(client->path, sizeof(client->path), "%s", "/data/liteos-m.elf");
client->ped = BARE_METAL;
snprintf(client->ped_setup.cpu_str, ..., "3");
mica_create(client);

/* 步骤2：拉起从核（NS 通告异步到达） */
mica_start(client);

/* 步骤3：注册服务（match: 比对端点名; bind: 建本地 ept + 唤醒） */
struct mica_service svc = {
    .rpmsg_ns_match    = demo_match,   /* strncmp(name, g_ept_name)==0 */
    .rpmsg_ns_bind_cb  = demo_bind,    /* rpmsg_create_ept + 记 peer_addr */
};
mica_register_service(client, &svc);
sem_timedwait(&ept_ready, ...);        /* 等 bind 回调（超时 30s） */

/* 步骤4：收发（发送成功返回字节数，负数才是错误） */
rpmsg_sendto(&demo_ept, "ping-001", 8, g_peer_addr);
sem_timedwait(&rx_done, ...);          /* rx_cb 里 memcpy + sem_post */

/* 步骤5：停核回收（顺序不能乱） */
mica_stop(client); mica_remove(client); free(client);
```

### 5.2 自定义服务 demo：从核"算术服务"端到端

场景：主核发 `"12+34"`，从核回 `"sum=46"`。演示自定义端点 + 二进制/
文本协议两种风格中最简单的文本协议。

**从核侧**：新增 `math_service.c`（放 `kernel/liteos_m/vendor/hihope/dayu210/`，
并加入该目录 Makefile 源文件列表；不依赖 libc，纯手写解析）：

```c
/* math_service.c — 从核自定义 rpmsg 服务示例
 * 端点名 "math"（主核按此名绑定）；协议：收到 "a+b" 回 "sum=N"
 */
#include <stdint.h>
#include <stddef.h>
#include "los_task.h"
#include "los_sem.h"
#include <openamp/rpmsg.h>
#include "mica/mica.h"
#include "securec.h"

#define MATH_EPT_NAME   "math"
#define MATH_STACK_SZ   0x2000
#define MATH_PRIO       25

extern struct rpmsg_device *mica_get_rpdev(void);  /* services/mica_service_internal.h */

static struct rpmsg_endpoint g_math_ept;
static UINT32 g_mathTaskID;

static long MathAtoi(const char *p, size_t len, size_t *used)
{
    long v = 0; size_t i = 0;
    while (i < len && (p[i] < '0' || p[i] > '9')) i++;      /* 跳过非数字 */
    for (; i < len && p[i] >= '0' && p[i] <= '9'; i++)
        v = v * 10 + (p[i] - '0');
    *used = i;
    return v;
}

/* rx 回调：解析 "a+b" 并直接回复（数据量小，回调内处理即可；
 * 大数据量请仿 tty_service：拷贝 + 信号量，业务线程处理） */
static int MathRxCb(struct rpmsg_endpoint *ept, void *data,
                    size_t len, uint32_t src, void *priv)
{
    char resp[40];
    size_t used = 0;
    long a = MathAtoi(data, len, &used);
    long b = MathAtoi((const char *)data + used, len - used, &used);
    int n;

    if (used == 0) {
        n = snprintf_s(resp, sizeof(resp), sizeof(resp) - 1,
                       "err: expect 'a+b'");
    } else {
        n = snprintf_s(resp, sizeof(resp), sizeof(resp) - 1,
                       "sum=%ld", a + b);
    }
    return (n > 0) ? rpmsg_send(ept, resp, n) : -1;   /* 回到请求方 */
}

static VOID MathServiceTask(VOID)
{
    struct rpmsg_device *rpdev = NULL;
    int wait;

    /* 等 mica_init 完成（MicaTaskEntry 优先级更高，先起） */
    for (wait = 0; wait < 500; wait++) {      /* 最多 5s */
        rpdev = mica_get_rpdev();
        if (rpdev != NULL) break;
        LOS_TaskDelay(10);
    }
    if (rpdev == NULL) {
        PRINTK("[math] rpdev not ready, give up\n");
        return;
    }
    if (rpmsg_create_ept(&g_math_ept, rpdev, MATH_EPT_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                         MathRxCb, NULL) == 0) {
        PRINTK("[math] service '%s' up\n", MATH_EPT_NAME);  /* 创建即 NS 通告 */
    }
}

/* 在 los_board.c 的 main 流程（MicaTaskEntry 创建之后）调用 */
VOID MathServiceInit(VOID)
{
    TSK_INIT_PARAM_S taskParam;
    (VOID)memset_s(&taskParam, sizeof(taskParam), 0, sizeof(taskParam));
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)MathServiceTask;
    taskParam.pcName       = "math";
    taskParam.usTaskPrio   = MATH_PRIO;
    taskParam.uwStackSize  = MATH_STACK_SZ;
    if (LOS_TaskCreate(&g_mathTaskID, &taskParam) != LOS_OK)
        PRINT_ERR("create math task failed\n");
}
```

**主核侧**：零新代码——`rpmsg_demo` 的 `-e` 参数就是端点名：

```bash
export LD_LIBRARY_PATH=/data/lib
/data/rpmsg_demo -f /data/liteos-m.elf -e math -i
# 输入 12+34 回车 → 期望回复 sum=46
```

程序化调用（替换 demo 的 round_trip 校验逻辑即可）：

```c
char req[32] = "12+34", resp[512];
rpmsg_sendto(&ept, req, strlen(req), peer_addr);
/* rx 回调收到后主线程校验 strncmp(resp, "sum=46", 6) == 0 */
```

### 5.3 已有的从核内置服务（可直接用）

| 端点名 | 服务 | 用途 |
|---|---|---|
| `rpmsg-tty` | tty_service.c | 字节流回显/命令通道（handler 可注入业务） |
| UMT | umt_service.c | 大块/流式数据（回调式，见 `mica/service.h` UMT API） |

## 6. 约束与限制（必读）

1. 单包负载 ~400B 上限，更大数据应用层分段（或走 UMT 服务）。
2. `mica_register_service` 必须在 `mica_start` 之后调用。
3. rx 回调在框架接收线程上下文：只做拷贝+唤醒，勿阻塞/耗时处理。
4. `rpmsg_sendto` 成功返回字节数（open-amp 2022.10 语义），负数才是错误。
5. 停核后 CPU3 不自动复位，二次拉起需 reboot（当前限制）。
6. 一个 `mica_client` 对应一个从核实例；多服务 = 多个 `mica_service` 注册。
7. 主从核端点名即契约：改名必须两侧同步（从核镜像重编 + 主核 `-e` 参数）。

## 7. 参考文件索引

| 内容 | 位置 |
|---|---|
| 主核 demo（本文 5.1） | `example/rpmsg_demo.c` |
| 主核生命周期/服务注册 API | `libmica/include/mica/mica.h`、`libmica/mica/mica.c` |
| 从核内置服务实现（模板） | `kernel/liteos_m/components/mcs/mica/libmica/src/services/tty_service.c` |
| 从核 API 头 | `kernel/liteos_m/components/mcs/mica/libmica/src/include/mica/{mica,service}.h` |
| 协议常量（从核） | `kernel/liteos_m/components/mcs/mica/mica_cfg.h` |
| 从核板级启动 | `kernel/liteos_m/vendor/hihope/dayu210/los_board.c`（MicaTaskEntry） |
| 内核态（mcs_km） | `mcs_km/` + `dts/rk3568-ohrt-mcs.dtsi` |
