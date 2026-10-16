/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.

 *
 * rpmsg_demo.c — 混合部署框架（mcs/mica）主核侧应用开发示例
 *
 * 面向应用开发者演示通过 rpmsg 与从核通信的最小完整流程：
 *
 *   步骤 1  mica_create(client)          创建 mica 客户端（填固件路径/核号/pedestal）
 *   步骤 2  mica_start(client)           加载从核 ELF + PSCI 启核 + 建立 rpmsg vdev
 *   步骤 3  mica_register_service()      注册服务（NS 匹配/绑定回调），
 *                                        从核通告端点时自动回调 → rpmsg_create_ept
 *   步骤 4  rpmsg_sendto() / rx 回调     收发数据（本例对端为从核 tty 回显服务）
 *   步骤 5  mica_stop / mica_remove      停核、回收资源
 *
 * 两种运行模式：
 *   自动模式（默认）：发 N 条 "ping-i"，校验从核回显，打印往返时延
 *   交互模式（-i）  ：键盘输入一行 → 发给从核 → 打印从核回复；输入 quit 退出
 *
 * 用法:
 *   rpmsg_demo [-f firmware] [-c cpu_str] [-e ept_name] [-n count] [-i]
 *     -f  从核固件路径（默认 /data/liteos-m.elf）
 *     -c  从核 CPU 编号（默认 3）
 *     -e  绑定的端点名（默认 rpmsg-tty，须与从核服务端点名一致）
 *     -n  自动模式收发条数（默认 5）
 *     -i  交互模式
 *
 * 依赖：内核 mcs_km.ko 已 insmod；从核固件为 mcs 混合部署镜像（含 tty 回显服务）。
 * 编译：scripts/build_example.sh（动态链接 libmica/open-amp/libmetal；
 *       上板需带 out/artifacts/lib 部署集并设 LD_LIBRARY_PATH）。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include <openamp/rpmsg.h>
#include <mica/mica.h>

#define DEFAULT_FIRMWARE	"/data/liteos-m.elf"
#define DEFAULT_CPU		"3"
#define DEFAULT_EPT_NAME	"rpmsg-tty"
#define DEFAULT_COUNT		5
#define EPT_WAIT_SEC		30
#define RX_WAIT_SEC		10
#define MAX_PAYLOAD		400	/* rpmsg 单包负载上限（buffer 512B，留余量） */

static struct rpmsg_endpoint demo_ept;
static sem_t ept_ready;
static sem_t rx_done;
static volatile sig_atomic_t ept_ok;
static volatile sig_atomic_t stop_req;

static char g_ept_name[RPMSG_NAME_SIZE];
static char g_rx_buf[512];
static size_t g_rx_len;
static uint32_t g_peer_addr;	/* 从核端点地址（NS 绑定时获得） */

static int demo_rx_cb(struct rpmsg_endpoint *ept, void *data,
		      size_t len, uint32_t src, void *priv);

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-f firmware] [-c cpu_str] [-e ept_name] [-n count] [-i]\n",
		prog);
}

static void on_sigint(int sig)
{
	(void)sig;
	stop_req = 1;
	sem_post(&rx_done);
	sem_post(&ept_ready);
}

static uint64_t now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000;
}

/* ---------- 步骤 3a：NS 匹配回调 --------------------------------------
 * 从核创建端点后会广播 name service 通告（端点名 + 地址）。
 * 框架对每个通告调用本回调，返回 true 表示"这个端点是我的服务要绑的"。
 */
static bool demo_match(struct rpmsg_device *rdev, const char *name,
		       uint32_t addr, uint32_t dest, void *priv)
{
	(void)rdev; (void)addr; (void)dest; (void)priv;

	return name && strncmp(name, g_ept_name, RPMSG_NAME_SIZE) == 0;
}

/* ---------- 步骤 3b：NS 绑定回调 --------------------------------------
 * 匹配成功后框架调用本回调，参数给出从核端点的地址信息。
 * 在这里创建主核侧端点（对端地址 = 从核通告地址），并唤醒主线程。
 */
static void demo_bind(struct rpmsg_device *rdev, const char *name,
		      uint32_t remote_addr, uint32_t remote_dest, void *priv)
{
	int ret;

	(void)name; (void)priv;

	/* libmica ns 回调传 (对端addr, RPMSG_ADDR_ANY)，取非 ANY 的作 peer */
	g_peer_addr = (remote_addr != RPMSG_ADDR_ANY) ? remote_addr : remote_dest;
	printf("[demo] NS bind: name=%s peer_addr=%u\n", g_ept_name, g_peer_addr);

	ret = rpmsg_create_ept(&demo_ept, rdev, g_ept_name, remote_dest,
			       remote_addr, demo_rx_cb, NULL);
	if (ret) {
		fprintf(stderr, "[demo] rpmsg_create_ept failed: %d\n", ret);
		return;
	}
	ept_ok = 1;
	sem_post(&ept_ready);
}

/* ---------- 步骤 4a：接收回调（rpmsg 接收线程上下文） ------------------
 * 只做最小动作：拷贝 + 信号量唤醒。耗时处理放主线程，不要阻塞回调。
 */
static int demo_rx_cb(struct rpmsg_endpoint *ept, void *data,
		      size_t len, uint32_t src, void *priv)
{
	(void)ept; (void)src; (void)priv;

	if (len > sizeof(g_rx_buf))
		len = sizeof(g_rx_buf);
	memcpy(g_rx_buf, data, len);
	g_rx_buf[len] = '\0';
	g_rx_len = len;
	sem_post(&rx_done);
	return 0;
}

/* 发一条消息并等回复。返回 0=收到回复，<0=失败/超时 */
static int round_trip(const char *tx, int tx_len)
{
	struct timespec ts;
	uint64_t t0;
	int ret;

	/* openamp 2022.10：发送成功返回字节数，负数才是错误 */
	ret = rpmsg_sendto(&demo_ept, tx, tx_len, g_peer_addr);
	if (ret < 0) {
		fprintf(stderr, "[demo] send failed: %d\n", ret);
		return ret;
	}

	t0 = now_us();
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += RX_WAIT_SEC;
	if (sem_timedwait(&rx_done, &ts) != 0)
		return -ETIMEDOUT;
	g_rx_buf[g_rx_len] = '\0';
	printf("[demo] rtt %llu us, reply(%zu): %s\n",
	       (unsigned long long)(now_us() - t0), g_rx_len, g_rx_buf);
	return 0;
}

int main(int argc, char **argv)
{
	const char *firmware = DEFAULT_FIRMWARE;
	const char *cpu_str = DEFAULT_CPU;
	const char *ept_name = DEFAULT_EPT_NAME;
	int count = DEFAULT_COUNT;
	int interactive = 0;
	struct mica_client *client;
	struct mica_service svc;
	struct timespec ts;
	char line[512];
	int opt, ret, i;

	while ((opt = getopt(argc, argv, "f:c:e:n:ih")) != -1) {
		switch (opt) {
		case 'f': firmware = optarg; break;
		case 'c': cpu_str = optarg; break;
		case 'e': ept_name = optarg; break;
		case 'n': count = atoi(optarg); break;
		case 'i': interactive = 1; break;
		default: usage(argv[0]); return 2;
		}
	}
	if (count <= 0)
		count = DEFAULT_COUNT;
	snprintf(g_ept_name, sizeof(g_ept_name), "%s", ept_name);

	sem_init(&ept_ready, 0, 0);
	sem_init(&rx_done, 0, 0);
	signal(SIGINT, on_sigint);
	setvbuf(stdout, NULL, _IOLBF, 0);

	printf("[demo] firmware=%s cpu=%s ept=%s mode=%s\n", firmware, cpu_str,
	       g_ept_name, interactive ? "interactive" : "auto");

	/* ---------- 步骤 1：创建客户端 ----------
	 * 应用只需填三样：固件路径、pedestal 类型（裸核 = BARE_METAL）、
	 * ped_setup 里的 name/cpu_str。其余字段 calloc 清零即可。
	 */
	client = calloc(1, sizeof(*client));
	if (!client) {
		fprintf(stderr, "[demo] alloc client failed\n");
		return 1;
	}
	snprintf(client->path, sizeof(client->path), "%s", firmware);
	client->ped = BARE_METAL;
	snprintf(client->ped_setup.name, sizeof(client->ped_setup.name), "%s",
		 "rpmsg-demo");
	snprintf(client->ped_setup.cpu_str, sizeof(client->ped_setup.cpu_str),
		 "%s", cpu_str);

	ret = mica_create(client);
	if (ret) {
		fprintf(stderr, "[demo] mica_create failed: %d\n", ret);
		goto out_free;
	}
	printf("[demo] step1 mica_create ok\n");

	/* ---------- 步骤 2：拉起从核 ----------
	 * mica_start 完成：ELF 段加载 → PSCI cpu_on 启核 → 等 vdev ready。
	 * 从核起来后会通过 NS 通告它的服务端点（异步）。
	 */
	ret = mica_start(client);
	if (ret) {
		fprintf(stderr, "[demo] mica_start failed: %d\n", ret);
		/* 从核未启动成功：rsc_table 尚未建立，mica_stop 会在
		 * remoteproc_shutdown 里解引用 NULL，只能走 remove。 */
		goto out_remove;
	}
	printf("[demo] step2 mica_start ok, waiting NS announce...\n");

	/* ---------- 步骤 3：注册服务（注意：必须在 mica_start 之后） ----------
	 * 若 NS 通告已先到，register 内部会立即补一次匹配绑定。
	 */
	memset(&svc, 0, sizeof(svc));
	snprintf(svc.name, sizeof(svc.name), "%s", "rpmsg-demo-svc");
	svc.rpmsg_ns_match = demo_match;
	svc.rpmsg_ns_bind_cb = demo_bind;
	ret = mica_register_service(client, &svc);
	if (ret) {
		fprintf(stderr, "[demo] mica_register_service failed: %d\n", ret);
		goto out_stop;
	}

	if (!ept_ok) {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += EPT_WAIT_SEC;
		if (sem_timedwait(&ept_ready, &ts) != 0 || !ept_ok) {
			fprintf(stderr, "[demo] timeout waiting '%s' announce\n",
				g_ept_name);
			ret = -ETIMEDOUT;
			goto out_stop;
		}
	}
	printf("[demo] step3 endpoint bound (local addr %u, peer %u)\n",
	       demo_ept.addr, g_peer_addr);

	/* ---------- 步骤 4：收发 ---------- */
	if (interactive) {
		printf("[demo] interactive mode: type a line, 'quit' to exit\n");
		while (!stop_req && fgets(line, sizeof(line), stdin)) {
			int n = strlen(line);

			while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
				line[--n] = '\0';
			if (n == 0)
				continue;
			if (strcmp(line, "quit") == 0)
				break;
			if (n > MAX_PAYLOAD)
				n = MAX_PAYLOAD;
			/* tty 服务按行解析（'\n' 触发回显），strip 后需补回行
			 * 结束符，否则从核永远等不到行结束导致回显超时 */
			line[n++] = '\n';
			ret = round_trip(line, n);
			if (ret < 0) {
				fprintf(stderr, "[demo] round trip failed: %d\n", ret);
				break;
			}
		}
	} else {
		int ok = 0;

		for (i = 0; i < count && !stop_req; i++) {
			char tx[64];
			int n = snprintf(tx, sizeof(tx), "ping-%03d", i + 1);

			ret = round_trip(tx, n);
			if (ret < 0)
				break;
			/* 校验：回显应以发送内容结尾（从核回显带前缀） */
			if (g_rx_len >= (size_t)n &&
			    strncmp(g_rx_buf + g_rx_len - n, tx, n) == 0)
				ok++;
			else
				fprintf(stderr, "[demo] #%d payload mismatch\n", i + 1);
		}
		if (ok == count) {
			printf("[demo] PASS: %d/%d round trips\n", ok, count);
			ret = 0;
		} else {
			fprintf(stderr, "[demo] FAIL: %d/%d\n", ok, count);
			ret = 1;
		}
	}

	/* ---------- 步骤 5：停核回收 ----------
	 * mica_stop 链路：写 rsc_table->reserved[0]=CPU_OFF_FUNCID 并 kick 从核，
	 * 从核 side 的 system_poweroff 回调须执行 PSCI CPU_OFF；主核轮询
	 * AFFINITY_INFO 确认 OFF（3s 超时）。失败时 CPU 仍在运行，再次
	 * mica_start 会因 PSCI ALREADY_ON 而失败。
	 */
out_stop:
	{
		int sret = mica_stop(client);

		if (sret)
			fprintf(stderr,
				"[demo] mica_stop: %d (slave still ON; next start will fail until it powers off)\n",
				sret);
		else
			printf("[demo] slave core powered off\n");
	}
out_remove:
	mica_remove(client);
	free(client);
	printf("[demo] done\n");
	return ret;
out_free:
	free(client);
	return ret;
}
