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
 * slave_boot.c — 最小从核拉起工具（不通信）
 *
 * 只做三件事：mica_create（解析 ELF + 建 baremetal pedestal）
 *           → mica_start（加载 ELF 段到保留内存 + PSCI CPU_ON 启核）
 *           → 停留（不注册任何 rpmsg 服务，不收发包）
 *
 * 用途：内核调试场景，只关心从核跑起来，不关心主从核通信。
 *
 * 用法: slave_boot [-f firmware] [-c cpu] [-K]
 *   -f  从核固件路径（默认 /data/liteos-m.elf）
 *   -c  从核 CPU 编号（默认 3；baremetal pedestal 只接受单核）
 *   -K  拉起成功后直接退出，从核保持运行（之后停核需重启板子）
 *
 * 默认行为：拉起成功后阻塞等待，Ctrl+C → mica_stop + mica_remove
 * （通过 rsc_table 标志 + IPI 通知从核自陷 PSCI CPU_OFF，可反复拉起/停止）。
 *
 * 前提（同 rpmsg_demo）：
 *   1. mcs_km.ko 已 insmod（提供 /dev/mcs、IPI 转发）
 *   2. OH 设备树含 oh_rt 混合部署节点（rk3568_dts_ohrt.patch）
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <mica/mica.h>

#define DEFAULT_FIRMWARE	"/data/liteos-m.elf"
#define DEFAULT_CPU		"3"

static volatile sig_atomic_t stop_req;

static void on_sigint(int sig)
{
	(void)sig;
	stop_req = 1;
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-f firmware] [-c cpu] [-K]\n", prog);
}

int main(int argc, char **argv)
{
	const char *firmware = DEFAULT_FIRMWARE;
	const char *cpu_str = DEFAULT_CPU;
	struct mica_client *client;
	int keep_running = 0;
	int opt, ret;

	while ((opt = getopt(argc, argv, "f:c:Kh")) != -1) {
		switch (opt) {
		case 'f': firmware = optarg; break;
		case 'c': cpu_str = optarg; break;
		case 'K': keep_running = 1; break;
		default: usage(argv[0]); return 2;
		}
	}

	signal(SIGINT, on_sigint);
	signal(SIGTERM, on_sigint);
	setvbuf(stdout, NULL, _IOLBF, 0);

	client = calloc(1, sizeof(*client));
	if (!client) {
		fprintf(stderr, "[slave_boot] alloc client failed\n");
		return 1;
	}

	/* baremetal pedestal + 固件路径 + 目标 CPU */
	snprintf(client->path, sizeof(client->path), "%s", firmware);
	client->ped = BARE_METAL;
	snprintf(client->ped_setup.name, sizeof(client->ped_setup.name), "%s", "slave-boot");
	snprintf(client->ped_setup.cpu_str, sizeof(client->ped_setup.cpu_str), "%s", cpu_str);

	printf("[slave_boot] firmware=%s cpu=%s\n", firmware, cpu_str);

	ret = mica_create(client);
	if (ret) {
		fprintf(stderr, "[slave_boot] mica_create failed: %d\n", ret);
		goto out_free;
	}
	printf("[slave_boot] client created\n");

	ret = mica_start(client);
	if (ret) {
		fprintf(stderr, "[slave_boot] mica_start failed: %d\n", ret);
		mica_remove(client);
		goto out_free;
	}
	printf("[slave_boot] slave core is UP (status: %s)\n", mica_status(client));

	if (keep_running) {
		printf("[slave_boot] keep-running mode, exiting (slave keeps running)\n");
		goto out_free;
	}

	printf("[slave_boot] press Ctrl+C to stop the slave core...\n");
	while (!stop_req)
		pause();

	printf("[slave_boot] stopping slave core...\n");
	ret = mica_stop(client);
	if (ret)
		fprintf(stderr, "[slave_boot] mica_stop failed: %d\n", ret);
	mica_remove(client);
	printf("[slave_boot] slave core stopped and removed\n");
	ret = 0;

out_free:
	free(client);
	return ret;
}
