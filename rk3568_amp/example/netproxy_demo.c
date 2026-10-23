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
 * netproxy_demo.c — 主核侧 RPC 服务化验证 + socket 代理 worker
 *
 * 在 rpmsg_demo 生命周期骨架上绑定 "rpmsg-rpc" 端点，双向角色：
 *   client  启动后向从核发 N 个 RPC_PING（验证从核侧 mica 服务化框架：
 *           从核 netproxy_test.c 注册的 echo handler），打印往返时延；
 *   server  常驻接收从核的 socket 代理请求（np_* API，见从核
 *           netproxy_test.c），在 worker 线程执行真实 BSD socket 调用
 *           并回响应（socket/connect/send/recv/close，recv 带 poll 超时）。
 *
 * 用法:
 *   netproxy_demo [-f firmware] [-c cpu_str] [-n ping_count]
 *     -f  从核固件路径（默认 /data/liteos_mv.elf）
 *     -c  从核 CPU 编号（默认 3）
 *     -n  服务化验证 PING 次数（默认 3，0 跳过）
 *
 * Ctrl-C 退出（mica_stop 停核回收）。
 * 从核侧验证命令（经 rpmsg tty shell）：
 *   rpctest [len]   从核 client → 本程序 PING echo
 *   nptest <ip> [port]  UDP/TCP 探测（经本程序 socket 代理到 Linux 栈）
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
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <termios.h>

#include <openamp/rpmsg.h>
#include <mica/mica.h>
#include <user_msg/user_msg.h>

#include "rpc_proto.h"

#define DEFAULT_FIRMWARE	"/data/liteos_mv.elf"
#define DEFAULT_CPU		"3"
#define DEFAULT_PINGS		3
#define EPT_WAIT_SEC		30
#define RESP_WAIT_SEC		10
#define RPC_EPT_NAME		"rpmsg-rpc"
#define TTY_EPT_NAME		"rpmsg-tty"
#define UMT_EPT_NAME		"rpmsg-umt"

static struct rpmsg_endpoint rpc_ept;
static struct rpmsg_endpoint g_tty_ept;
static struct rpmsg_endpoint g_umt_ept;
static sem_t ept_ready;
static sem_t resp_ready;
static sem_t tty_ready;
static volatile sig_atomic_t ept_ok;
static volatile sig_atomic_t tty_ok;
static volatile sig_atomic_t stop_req;
static uint32_t g_peer_addr;
static uint32_t g_tty_peer_addr;
/* tty bridge: pty master. Everything the slave prints over rpmsg-tty is
 * written here, and bytes typed into the pty slave (/dev/pts/N) are
 * forwarded to the slave shell. Any terminal can "screen /dev/pts/N" to
 * talk to the slave while the rpc worker / liveness / echo servers keep
 * running - independent endpoints, independent threads. */
static int g_ptym = -1;
static char g_pty_name[64];
/* set when the slave's LOG line says its autotest finished; the tty
 * injector uses it to know when the slave shell has its commands */
static volatile sig_atomic_t g_slave_autotest_done;

/* tx serialization: open-amp has no internal lock; the worker thread
 * (responses) and the liveness/ping threads (requests) must not hit
 * rpmsg_sendto concurrently or the virtqueue gets corrupted (observed:
 * SIGSEGV in the master when a LOG response raced the first liveness ping) */
static pthread_mutex_t g_tx_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- client state (one in-flight ping) ---------- */
static uint32_t g_ping_req_id;
static volatile uint32_t g_wait_func; /* RPC_FUNC_* the waiter expects (ids
				       * alone are ambiguous: both sides run
				       * their own req_id counters) */
static struct {
	int32_t ret;
	int32_t err;
	uint32_t len;
	uint8_t buf[RPC_MAX_PAYLOAD];
	volatile bool done;
} g_ping_resp;

/* ---------- server request queue (rx cb -> worker) ----------
 * Depth matters under concurrent slave callers: every np_* call is one
 * request, a bigmsg round is ~15 of them, and the liveness pinger
 * interleaves its own - depth 8 overflowed (dropped requests -> slave
 * timeouts) the moment two slave tasks hammered the proxy at once. */
#define REQQ_DEPTH 64
/* connect() cap: a blocking connect to a firewalled peer stalls the single
 * worker thread (and every queued request behind it) for the whole TCP SYN
 * retry window (~14s). Non-blocking connect + poll keeps the worst case
 * bounded so slow targets cannot starve other proxy users. */
#define NP_CONNECT_TIMEOUT_MS 10000
struct reqq_slot {
	uint32_t len;
	uint8_t msg[RPC_MAX_PAYLOAD + sizeof(struct rpc_hdr)];
	bool used;
};
static struct reqq_slot g_reqq[REQQ_DEPTH];
static pthread_mutex_t g_reqq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_reqq_cond = PTHREAD_COND_INITIALIZER;
static uint32_t g_reqq_dropped;

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-f firmware] [-c cpu_str] [-n ping_count]\n",
		prog);
}

static void on_sigint(int sig)
{
	(void)sig;
	stop_req = 1;
	sem_post(&ept_ready);
	sem_post(&resp_ready);
}

static uint64_t now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000;
}

/* ---------- rx callback (fwd decl, defined below) ---------- */
static int rpc_rx_cb(struct rpmsg_endpoint *ept, void *data,
		      size_t len, uint32_t src, void *priv);

/* ---------- rx callback (fwd decl, defined below) ---------- */
static int rpc_rx_cb(struct rpmsg_endpoint *ept, void *data,
		      size_t len, uint32_t src, void *priv);

/* ---------- NS match/bind (rpc + tty services of the slave) ---------- */
static bool rpc_match(struct rpmsg_device *rdev, const char *name,
		      uint32_t addr, uint32_t dest, void *priv)
{
	(void)rdev; (void)addr; (void)dest; (void)priv;

	if (!name)
		return false;
	return strncmp(name, RPC_EPT_NAME, RPMSG_NAME_SIZE) == 0 ||
	       strncmp(name, TTY_EPT_NAME, RPMSG_NAME_SIZE) == 0 ||
	       strncmp(name, UMT_EPT_NAME, RPMSG_NAME_SIZE) == 0;
}

/* umt rx: the slave umt service may push notifications; this demo does not
 * consume them - the endpoint exists so the slave umt ept gets bound (its
 * is_rpmsg_ept_ready()/dest_addr updates) and mica_create_all_services on
 * the slave can complete. */
static int umt_rx_cb(struct rpmsg_endpoint *ept, void *data,
		     size_t len, uint32_t src, void *priv)
{
	(void)ept; (void)data; (void)len; (void)src; (void)priv;
	return 0;
}

/* tty rx: slave shell output + the app task's periodic tick-alive lines */
static int tty_rx_cb(struct rpmsg_endpoint *ept, void *data,
		     size_t len, uint32_t src, void *priv)
{
	char line[512];
	size_t n = len < sizeof(line) - 1 ? len : sizeof(line) - 1;

	(void)ept; (void)src; (void)priv;
	memcpy(line, data, n);
	line[n] = '\0';
	printf("[tty] %s\n", line);
	/* also push to the interactive pty; mica_tty_printf sends a trailing
	 * NUL - strip it so it does not show up as ^@ on the terminal */
	if (g_ptym >= 0) {
		if (n > 0 && line[n - 1] == '\0')
			n--;
		if (n > 0)
			(void)write(g_ptym, line, n);
	}
	return 0;
}

static void rpc_bind(struct rpmsg_device *rdev, const char *name,
		     uint32_t remote_addr, uint32_t remote_dest, void *priv)
{
	int ret;

	(void)priv;

	if (strncmp(name, TTY_EPT_NAME, RPMSG_NAME_SIZE) == 0) {
		g_tty_peer_addr = (remote_addr != RPMSG_ADDR_ANY) ?
			remote_addr : remote_dest;
		printf("[np] NS bind: %s peer_addr=%u\n",
		       TTY_EPT_NAME, g_tty_peer_addr);
		ret = rpmsg_create_ept(&g_tty_ept, rdev, TTY_EPT_NAME,
				       remote_dest, remote_addr,
				       tty_rx_cb, NULL);
		if (ret) {
			fprintf(stderr, "[np] tty rpmsg_create_ept failed: %d\n",
				ret);
			return;
		}
		tty_ok = 1;
		sem_post(&tty_ready);
		return;
	}

	if (strncmp(name, UMT_EPT_NAME, RPMSG_NAME_SIZE) == 0) {
		printf("[np] NS bind: %s peer_addr=%u\n",
		       UMT_EPT_NAME, remote_addr);
		ret = rpmsg_create_ept(&g_umt_ept, rdev, UMT_EPT_NAME,
				       remote_dest, remote_addr,
				       umt_rx_cb, NULL);
		if (ret) {
			fprintf(stderr, "[np] umt rpmsg_create_ept failed: %d\n",
				ret);
			return;
		}
		/*
		 * First umt frame initializes the slave's UMT send buffer:
		 * the slave parses it as umt_data_t{phy_addr, data_len} and
		 * computes send_buffer_addr = phy_addr + 1MB. phy_addr must
		 * be the copy_mem base obtained from mcs_km (2MB at the tail
		 * of the 16MB openamp shm: master writes the low MB, the
		 * slave writes the high MB). data_len = 0 keeps the receive
		 * path harmless. Mirrors micad's init via libmica
		 * init_core_shared_memory().
		 */
		{
			static struct core_msg_mem_info umtMem;
			static int umtMemTried;
			struct { unsigned long phyAddr; unsigned int dataLen; } umtInit;

			if (!umtMemTried) {
				umtMemTried = 1;
				umtMem.instance_id = 0;
				if (init_core_shared_memory(&umtMem,
							    MCS_KM_PED_BAREMETAL) == 0)
					printf("[np] umt copy_mem 0x%lx size 0x%zx\n",
					       umtMem.phy_addr, umtMem.size);
				else
					fprintf(stderr, "[np] umt copy_mem init failed\n");
				/* Fallback: on this board mcs_km copy_mem only maps
				 * baremetal_mem[0] (the 1MB vring window), so
				 * GET_COPY_MSG_MEM fails. The whole 0x74000000..
				 * 0x77FFFFFF window is reserved from Linux
				 * (mcs_baremetal_mem in /proc/iomem) and the slave
				 * image + 4MB heap end near 0x74550000, so a static
				 * in-window address well past the heap is safe for
				 * the demo (master never reads the UMT region). */
				if (umtMem.phy_addr == 0)
					umtMem.phy_addr = 0x75000000UL;
			}
			if (umtMem.phy_addr != 0) {
				umtInit.phyAddr = umtMem.phy_addr;
				umtInit.dataLen = 0;
				if (rpmsg_send(&g_umt_ept, &umtInit,
					       sizeof(umtInit)) == 0)
					printf("[np] umt init sent (slave send buf 0x%lx)\n",
					       umtMem.phy_addr + 0x100000UL);
			}
		}
		return;
	}

	g_peer_addr = (remote_addr != RPMSG_ADDR_ANY) ? remote_addr : remote_dest;
	printf("[np] NS bind: %s peer_addr=%u\n", RPC_EPT_NAME, g_peer_addr);

	ret = rpmsg_create_ept(&rpc_ept, rdev, RPC_EPT_NAME, remote_dest,
			       remote_addr, rpc_rx_cb, NULL);
	if (ret) {
		fprintf(stderr, "[np] rpmsg_create_ept failed: %d\n", ret);
		return;
	}
	ept_ok = 1;
	sem_post(&ept_ready);
}

/* ---------- rx callback: RESP -> client waiter, REQ -> queue ---------- */
static int rpc_rx_cb(struct rpmsg_endpoint *ept, void *data,
		     size_t len, uint32_t src, void *priv)
{
	const struct rpc_hdr *hdr = data;

	(void)ept; (void)src; (void)priv;
	if (len < sizeof(*hdr))
		return 0;

	if (hdr->flags & RPC_F_RESP) {
		uint32_t plen = (len > sizeof(*hdr)) ? len - sizeof(*hdr) : 0;

		if (hdr->req_id == g_ping_req_id && !g_ping_resp.done &&
		    hdr->func_id == g_wait_func) {
			g_ping_resp.ret = hdr->ret;
			g_ping_resp.err = hdr->err;
			if (plen > RPC_MAX_PAYLOAD)
				plen = RPC_MAX_PAYLOAD;
			memcpy(g_ping_resp.buf, hdr + 1, plen);
			g_ping_resp.len = plen;
			g_ping_resp.done = true;
			sem_post(&resp_ready);
		} else if (hdr->req_id != g_ping_req_id ||
			   hdr->func_id != g_wait_func) {
			fprintf(stderr,
				"[np] drop resp f=%u id=%u want f=%u id=%u plen=%u flags=%04x\n",
				hdr->func_id, hdr->req_id, g_wait_func,
				g_ping_req_id, plen, hdr->flags);
		}
		return 0;
	}

	if (hdr->flags & RPC_F_REQ) {
		/* copy into a free queue slot for the worker thread */
		pthread_mutex_lock(&g_reqq_lock);
		for (int i = 0; i < REQQ_DEPTH; i++) {
			if (!g_reqq[i].used) {
				size_t n = len;

				if (n > sizeof(g_reqq[i].msg))
					n = sizeof(g_reqq[i].msg);
				memcpy(g_reqq[i].msg, data, n);
				g_reqq[i].len = (uint32_t)n;
				g_reqq[i].used = true;
				pthread_cond_signal(&g_reqq_cond);
				pthread_mutex_unlock(&g_reqq_lock);
				return 0;
			}
		}
		g_reqq_dropped++;
		pthread_mutex_unlock(&g_reqq_lock);
		fprintf(stderr, "[np] req queue full, dropped (%u total)\n",
			g_reqq_dropped);
	}
	return 0;
}

/* ---------- helpers: send responses (fragmented) ---------- */
static int send_resp_frag(uint32_t func_id, uint32_t req_id,
			  int32_t ret, int32_t err,
			  const uint8_t *payload, uint32_t total)
{
	uint8_t msg[RPC_MAX_PAYLOAD + sizeof(struct rpc_hdr)];
	struct rpc_hdr *h = (struct rpc_hdr *)msg;
	uint32_t off = 0;

	do {
		uint32_t chunk = total - off;

		if (chunk > RPC_MAX_PAYLOAD)
			chunk = RPC_MAX_PAYLOAD;
		h->func_id = func_id;
		h->req_id = req_id;
		h->flags = RPC_F_RESP | ((off + chunk < total) ? RPC_F_MORE : 0);
		h->payload_len = (uint16_t)chunk;
		h->ret = (off == 0) ? ret : 0;
		h->err = (off == 0) ? err : 0;
		if (chunk)
			memcpy(h + 1, payload + off, chunk);
		pthread_mutex_lock(&g_tx_lock);
		if (rpmsg_sendto(&rpc_ept, msg, sizeof(*h) + chunk,
				 g_peer_addr) < 0) {
			pthread_mutex_unlock(&g_tx_lock);
			return -EIO;
		}
		pthread_mutex_unlock(&g_tx_lock);
		off += chunk;
	} while (off < total);
	return 0;
}

/* ---------- socket proxy worker ---------- */
static void handle_req(struct reqq_slot *slot)
{
	struct rpc_hdr *hdr = (struct rpc_hdr *)slot->msg;
	uint8_t *payload = slot->msg + sizeof(*hdr);
	uint32_t plen = slot->len - sizeof(*hdr);
	uint8_t resp[2048]; /* > one rpmsg buffer: large RECV results exercise
			     * the RPC_F_MORE response fragmentation path */
	int32_t ret = 0, err = 0;
	uint32_t resp_len = 0;

	printf("[np] req func=%u id=%u plen=%u\n", hdr->func_id, hdr->req_id, plen);

	switch (hdr->func_id) {
	case RPC_FUNC_PING: { /* slave-side rpctest: echo back */
		resp_len = (plen < sizeof(resp)) ? plen : sizeof(resp);
		memcpy(resp, payload, resp_len);
		break;
	}
	case RPC_FUNC_GET_TICK: { /* slave-side: 64-bit master tick */
		struct timespec ts;
		uint64_t t;

		(void)clock_gettime(CLOCK_MONOTONIC, &ts);
		t = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
		memcpy(resp, &t, sizeof(t));
		resp_len = sizeof(t);
		break;
	}
	case RPC_FUNC_LOG: { /* slave-side diagnostics: print the line */
		if (plen > 0 && plen <= RPC_MAX_PAYLOAD) {
			char line[RPC_MAX_PAYLOAD + 1];

			memcpy(line, payload, plen);
			line[plen] = '\0';
			printf("[np][slave] %s\n", line);
			/* the slave registers its shell commands right after
			 * the autotest finishes - the tty injector waits for
			 * exactly this line before feeding the shell */
			if (strstr(line, "autotest finished"))
				g_slave_autotest_done = 1;
		}
		break;
	}
	case RPC_FUNC_SOCKET: {
		struct rpc_socket_args *a = (struct rpc_socket_args *)payload;
		struct rpc_socket_resp *r = (struct rpc_socket_resp *)resp;

		if (plen < sizeof(*a)) {
			ret = -1; err = -EINVAL;
			break;
		}
		ret = socket(a->domain, a->type, a->protocol);
		if (ret < 0) {
			err = -errno;
		} else {
			r->fd = ret;
			resp_len = sizeof(*r);
			ret = 0;
		}
		break;
	}
	case RPC_FUNC_CONNECT: {
		struct rpc_sockaddr_args *a = (struct rpc_sockaddr_args *)payload;
		struct pollfd pfd;
		socklen_t slen;
		int fl, soerr;

		if (plen < sizeof(*a) || plen < sizeof(*a) + a->addrlen) {
			ret = -1; err = -EINVAL;
			break;
		}
		/* see NP_CONNECT_TIMEOUT_MS: non-blocking connect + poll */
		fl = fcntl(a->fd, F_GETFL, 0);
		if (fl >= 0)
			(void)fcntl(a->fd, F_SETFL, fl | O_NONBLOCK);
		ret = connect(a->fd, (const struct sockaddr *)(payload + sizeof(*a)),
			      a->addrlen);
		if (ret < 0 && errno == EINPROGRESS) {
			pfd.fd = a->fd;
			pfd.events = POLLOUT;
			pfd.revents = 0;
			ret = poll(&pfd, 1, NP_CONNECT_TIMEOUT_MS);
			if (ret == 0) {          /* still connecting: give up */
				ret = -1;
				err = -ETIMEDOUT;
			} else if (ret > 0) {
				soerr = 0;
				slen = sizeof(soerr);
				(void)getsockopt(a->fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
				if (soerr == 0) {
					ret = 0;
				} else {
					ret = -1;
					err = -soerr; /* e.g. ECONNREFUSED */
				}
			} else {
				ret = -1;
				err = -errno;
			}
		} else if (ret < 0) {
			err = -errno;
		}
		if (fl >= 0)
			(void)fcntl(a->fd, F_SETFL, fl); /* restore blocking mode */
		break;
	}
	case RPC_FUNC_SEND: {
		struct rpc_send_args *a = (struct rpc_send_args *)payload;

		if (plen < sizeof(*a)) {
			ret = -1; err = -EINVAL;
			break;
		}
		ret = send(a->fd, payload + sizeof(*a), plen - sizeof(*a),
			   a->flags & 0xffff);
		if (ret < 0)
			err = -errno;
		break;
	}
	case RPC_FUNC_SENDTO: {
		struct rpc_sendto_args *a = (struct rpc_sendto_args *)payload;
		uint32_t hdrsz = sizeof(*a) + a->addrlen;

		if (plen < hdrsz) {
			ret = -1; err = -EINVAL;
			break;
		}
		ret = sendto(a->fd, payload + hdrsz, plen - hdrsz,
			     a->flags & 0xffff,
			     (const struct sockaddr *)(payload + sizeof(*a)),
			     a->addrlen);
		if (ret < 0)
			err = -errno;
		break;
	}
	case RPC_FUNC_RECV: {
		struct rpc_recv_args *a = (struct rpc_recv_args *)payload;
		struct pollfd pfd;
		int pret;

		if (plen < sizeof(*a)) {
			ret = -1; err = -EINVAL;
			break;
		}
		pfd.fd = a->fd;
		pfd.events = POLLIN;
		pret = poll(&pfd, 1, a->timeout_ms);
		if (pret == 0) {
			ret = -1; err = -EAGAIN;
			break;
		}
		if (pret < 0) {
			ret = -1; err = -errno;
			break;
		}
		ret = recv(a->fd, resp, sizeof(resp), a->flags & 0xffff);
		if (ret < 0) {
			ret = -1; err = -errno;
		} else {
			resp_len = (uint32_t)ret;
			printf("[np] recv fd=%d: %u bytes\n", a->fd, resp_len);
		}
		break;
	}
	case RPC_FUNC_RECVFROM: {
		struct rpc_recvfrom_args *a = (struct rpc_recvfrom_args *)payload;
		struct sockaddr_storage ss;
		socklen_t slen = sizeof(ss);
		struct pollfd pfd;
		uint8_t data[RPC_MAX_PAYLOAD - 16];
		int pret;
		ssize_t n;

		if (plen < sizeof(*a)) {
			ret = -1; err = -EINVAL;
			break;
		}
		pfd.fd = a->fd;
		pfd.events = POLLIN;
		pret = poll(&pfd, 1, a->timeout_ms);
		if (pret == 0) {
			ret = -1; err = -EAGAIN;
			break;
		}
		if (pret < 0) {
			ret = -1; err = -errno;
			break;
		}
		n = recvfrom(a->fd, data, sizeof(data), a->flags & 0xffff,
			     (struct sockaddr *)&ss, &slen);
		if (n < 0) {
			ret = -1; err = -errno;
			break;
		}
		/* resp payload: [addrlen u32][sockaddr][data] */
		{
			uint32_t alen = (slen > 128) ? 128 : slen;

			*(uint32_t *)resp = alen;
			memcpy(resp + 4, &ss, alen);
			memcpy(resp + 4 + alen, data, n);
			resp_len = 4 + alen + (uint32_t)n;
			ret = (int32_t)n;
			printf("[np] recvfrom fd=%d: %zd bytes\n", a->fd, n);
		}
		break;
	}
	case RPC_FUNC_CLOSE: {
		struct rpc_close_args *a = (struct rpc_close_args *)payload;

		if (plen < sizeof(*a)) {
			ret = -1; err = -EINVAL;
			break;
		}
		ret = close(a->fd);
		if (ret < 0)
			err = -errno;
		else
			printf("[np] close fd=%d\n", a->fd);
		break;
	}
	default:
		ret = -1;
		err = -EOPNOTSUPP;
		break;
	}

	(void)send_resp_frag(hdr->func_id, hdr->req_id, ret, err,
			     resp, resp_len);
}

static void *worker_thread(void *arg)
{
	(void)arg;
	while (!stop_req) {
		struct reqq_slot slot;
		bool found = false;

		pthread_mutex_lock(&g_reqq_lock);
		while (!stop_req) {
			for (int i = 0; i < REQQ_DEPTH; i++) {
				if (g_reqq[i].used) {
					memcpy(&slot, &g_reqq[i], sizeof(slot));
					g_reqq[i].used = false;
					found = true;
					break;
				}
			}
			if (found)
				break;
			{
				struct timespec ts;

				clock_gettime(CLOCK_REALTIME, &ts);
				ts.tv_sec += 1;
				pthread_cond_timedwait(&g_reqq_cond, &g_reqq_lock, &ts);
			}
		}
		pthread_mutex_unlock(&g_reqq_lock);
		if (found)
			handle_req(&slot);
	}
	return NULL;
}

/* ---------- local echo servers (slave nptest target, loopback over eth0) */
#define ECHO_UDP_PORT	7000
#define ECHO_TCP_PORT	7001
#define ECHO_BUF_MAX	2048

static void *udp_echo_thread(void *arg)
{
	struct sockaddr_in addr;
	uint8_t buf[ECHO_BUF_MAX];
	int fd;

	(void)arg;
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return NULL;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ECHO_UDP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "[np] udp echo bind: %s\n", strerror(errno));
		close(fd);
		return NULL;
	}
	printf("[np] udp echo :%d up\n", ECHO_UDP_PORT);
	while (!stop_req) {
		struct sockaddr_in from;
		socklen_t flen = sizeof(from);
		ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
				     (struct sockaddr *)&from, &flen);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (sendto(fd, buf, n, 0, (struct sockaddr *)&from, flen) < 0)
			fprintf(stderr, "[np] udp echo sendto: %s\n",
				strerror(errno));
	}
	close(fd);
	return NULL;
}

static void *tcp_echo_thread(void *arg)
{
	struct sockaddr_in addr;
	int fd;

	(void)arg;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return NULL;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ECHO_TCP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(fd, 4) < 0) {
		fprintf(stderr, "[np] tcp echo bind/listen: %s\n",
			strerror(errno));
		close(fd);
		return NULL;
	}
	printf("[np] tcp echo :%d up\n", ECHO_TCP_PORT);
	while (!stop_req) {
		struct sockaddr_in from;
		socklen_t flen = sizeof(from);
		int cfd = accept(fd, (struct sockaddr *)&from, &flen);

		if (cfd < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		/* Echo until the peer closes: a proxied sender may write
		 * several RPC-framed chunks into one TCP connection (e.g. the
		 * bigmsg test sends 1024 bytes as 3 fragments); closing after
		 * the first read would hang up on the remaining fragments. */
		while (!stop_req) {
			uint8_t buf[ECHO_BUF_MAX];
			ssize_t n = recv(cfd, buf, sizeof(buf), 0);

			if (n <= 0)
				break; /* EOF (peer close) or error */
			if (send(cfd, buf, n, 0) < 0)
				break;
		}
		close(cfd);
	}
	close(fd);
	return NULL;
}

/* ---------- client: ping the slave service framework ----------
 * wait_sec: bounded resp wait; quiet: no success print (liveness pinger) */
static int rpc_ping_wait(int len, int wait_sec, int quiet)
{
	uint8_t msg[RPC_MAX_PAYLOAD + sizeof(struct rpc_hdr)];
	struct rpc_hdr *h = (struct rpc_hdr *)msg;
	struct timespec ts;
	uint64_t t0;
	uint64_t dt = 0;
	int i;

	for (i = 0; i < len; i++)
		msg[sizeof(*h) + i] = (uint8_t)(i ^ 0x5a);

	g_ping_req_id++;
	g_wait_func = RPC_FUNC_PING;
	g_ping_resp.done = false;
	h->func_id = RPC_FUNC_PING;
	h->req_id = g_ping_req_id;
	h->flags = RPC_F_REQ;
	h->payload_len = (uint16_t)len;
	h->ret = 0;
	h->err = 0;

	t0 = now_us();
	pthread_mutex_lock(&g_tx_lock);
	if (rpmsg_sendto(&rpc_ept, msg, sizeof(*h) + len, g_peer_addr) < 0) {
		pthread_mutex_unlock(&g_tx_lock);
		return -EIO;
	}
	pthread_mutex_unlock(&g_tx_lock);

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += wait_sec;
	while (sem_timedwait(&resp_ready, &ts) == 0) {
		if (g_ping_resp.done)
			break;
	}
	if (!g_ping_resp.done)
		return -ETIMEDOUT;

	if (g_ping_resp.ret < 0)
		return g_ping_resp.err;
	if (g_ping_resp.len != (uint32_t)len ||
	    memcmp(g_ping_resp.buf, msg + sizeof(*h), len) != 0)
		return -EBADMSG;
	dt = now_us() - t0;
	if (!quiet)
		printf("[np] ping %d bytes: %llu us\n", len,
		       (unsigned long long)dt);
	return 0;
}

static int rpc_ping_slave(int len)
{
	return rpc_ping_wait(len, RESP_WAIT_SEC, 0);
}

/* ---------- client: fetch the slave rpc stat snapshot ---------- */
static int rpc_stat_slave(struct rpc_stat_resp *out)
{
	uint8_t msg[sizeof(struct rpc_hdr)];
	struct rpc_hdr *h = (struct rpc_hdr *)msg;
	struct timespec ts;

	g_ping_req_id++;
	g_wait_func = RPC_FUNC_STAT;
	g_ping_resp.done = false;
	h->func_id = RPC_FUNC_STAT;
	h->req_id = g_ping_req_id;
	h->flags = RPC_F_REQ;
	h->payload_len = 0;
	h->ret = 0;
	h->err = 0;

	pthread_mutex_lock(&g_tx_lock);
	if (rpmsg_sendto(&rpc_ept, msg, sizeof(*h), g_peer_addr) < 0) {
		pthread_mutex_unlock(&g_tx_lock);
		return -EIO;
	}
	pthread_mutex_unlock(&g_tx_lock);

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 2;
	while (sem_timedwait(&resp_ready, &ts) == 0) {
		if (g_ping_resp.done)
			break;
	}
	if (!g_ping_resp.done)
		return -ETIMEDOUT;
	if (g_ping_resp.ret < 0)
		return g_ping_resp.err;
	if (g_ping_resp.len < sizeof(*out) ||
	    ((struct rpc_stat_resp *)g_ping_resp.buf)->magic != 0x53544154u)
		return -EBADMSG;
	memcpy(out, g_ping_resp.buf, sizeof(*out));
	return 0;
}

/* ---------- liveness pinger: watch the slave rpc service over time -----
 * every 2 s: ping 16 + ping 64 + stat. Interleaves with the slave's own
 * RPC_FUNC_LOG lines in this console, so channel wedges / handler deaths
 * localize precisely in the timeline. */
static void *liveness_thread(void *arg)
{
	struct rpc_stat_resp st;

	(void)arg;
	while (!stop_req) {
		int p16 = rpc_ping_wait(16, 2, 1);
		int p64 = rpc_ping_wait(64, 2, 1);
		int sret = rpc_stat_slave(&st);

		if (sret == 0) {
			printf("[np] live: p16=%s p64=%s stat: seen=%u tick=%u"
			       " rx=%u req=%u tx=%u txfail=%u short=%u drop=%u"
			       " to=%u last=(f=%u fl=%04x id=%u)\n",
			       p16 == 0 ? "OK" : "FAIL",
			       p64 == 0 ? "OK" : "FAIL",
			       st.master_seen, st.tick, st.rx_cnt,
			       st.rx_req_cnt, st.tx_cnt, st.tx_fail,
			       st.rx_short_cnt, st.drop_cnt, st.timeout_cnt,
			       st.last_rx_func, st.last_rx_flags,
			       st.last_rx_id);
		} else {
			printf("[np] live: p16=%d p64=%d stat=%d\n",
			       p16, p64, sret);
		}
		sleep(2);
	}
	return NULL;
}

/* ---------- tty bridge: interactive slave console ----------
 * The slave shell reads its input from the rpmsg-tty service (one char per
 * byte delivered to shell_cmd_handler) and demo commands print back via
 * mica_tty_printf. Bridge that stream to a pty: open a terminal on
 *     screen /dev/pts/N      (or: echo "rpctest 64" > /dev/pts/N)
 * to talk to the slave shell while the rpc worker keeps serving.
 *
 * The slave creates its tty endpoint with dest=ANY; open-amp only
 * backfills ept->dest from the source of a received message. Until
 * the master sends something first, every slave mica_tty_printf()
 * (app tick-alive lines) fails to route. One carriage return
 * backfills the route - the tick-alive lines then prove the return
 * path immediately. */
static void *tty_bridge_thread(void *arg)
{
	struct timespec ts;
	const char *nudge = "\r";

	(void)arg;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += EPT_WAIT_SEC;
	while (!tty_ok && !stop_req) {
		if (sem_timedwait(&tty_ready, &ts) != 0)
			return NULL; /* slave has no tty service: fine */
	}

	/* O_NONBLOCK: when no terminal is attached to the slave, the master
	 * side write (slave output) would fill the kernel buffer and BLOCK -
	 * that would stall this rpmsg callback thread and with it the whole
	 * rpc channel. Non-blocking turns a full buffer into EAGAIN, i.e.
	 * output is dropped until someone opens the terminal. */
	g_ptym = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (g_ptym >= 0) {
		char *nm = NULL;

		if (grantpt(g_ptym) == 0 && unlockpt(g_ptym) == 0)
			nm = ptsname(g_ptym);
		if (nm) {
			snprintf(g_pty_name, sizeof(g_pty_name), "%s", nm);
			printf("[np] tty bridge: %s (open it in a terminal to use"
			       " the slave shell)\n", g_pty_name);
		} else {
			fprintf(stderr, "[np] tty bridge: pty setup failed (%s)"
				" - console only\n", strerror(errno));
			close(g_ptym);
			g_ptym = -1;
		}
	}

	/* Hold the slave side and turn ECHO OFF. The line discipline echoes
	 * everything the master WRITES (slave-core output) back onto the
	 * master READ side; the bridge would forward that to the slave core
	 * again and the slave would run its own output as shell commands -
	 * an infinite echo storm (observed on first bring-up). With ECHO off
	 * the bridge does a local echo itself after reading user input, so
	 * typing in the terminal still shows what you typed. */
	if (g_ptym >= 0) {
		int sfd = open(g_pty_name, O_RDWR | O_NOCTTY);

		if (sfd >= 0) {
			struct termios tio;

			if (tcgetattr(sfd, &tio) == 0) {
				tio.c_lflag &= ~(tcflag_t)(ECHO | ECHOE | ECHOK | ECHONL);
				(void)tcsetattr(sfd, TCSANOW, &tio);
			}
			/* keep sfd open (never read): slave-core output stays
			 * buffered for whoever opens the terminal later */
		}
	}

	pthread_mutex_lock(&g_tx_lock);
	if (rpmsg_sendto(&g_tty_ept, nudge, strlen(nudge), g_tty_peer_addr) == 0)
		printf("[np] tty nudge sent (slave tty route established)\n");
	pthread_mutex_unlock(&g_tx_lock);

	/* input loop: bytes typed into the pty slave -> slave shell */
	while (!stop_req && g_ptym >= 0) {
		struct pollfd pfd;
		char buf[128];
		int r;

		pfd.fd = g_ptym;
		pfd.events = POLLIN;
		pfd.revents = 0;
		r = poll(&pfd, 1, 1000);
		if (r <= 0)
			continue;
		r = read(g_ptym, buf, sizeof(buf));
		if (r <= 0)
			continue;
		(void)write(g_ptym, buf, r); /* local echo (slave ECHO is off) */
		pthread_mutex_lock(&g_tx_lock);
		if (rpmsg_sendto(&g_tty_ept, buf, r, g_tty_peer_addr) < 0)
			fprintf(stderr, "[np] tty bridge: send failed\n");
		pthread_mutex_unlock(&g_tx_lock);
	}
	return NULL;
}

/* ---------- one-shot automated tty input self-check ----------
 * Once the slave autotest has finished (its shell commands are registered
 * after that), feed "rpctest 64" into the slave shell: the command runs a
 * slave->master RPC ping, so success is visible both here (a new
 * RPC_FUNC_PING plen=64 request arrives) and as the [tty] exec echo. */
static void *tty_inject_thread(void *arg)
{
	const char *cmd = "rpctest 64\r";

	(void)arg;
	while (!g_slave_autotest_done && !stop_req)
		sleep(1);
	if (stop_req)
		return NULL;
	sleep(3); /* let NpShellCmdRegister() finish on the slave */
	pthread_mutex_lock(&g_tx_lock);
	if (rpmsg_sendto(&g_tty_ept, cmd, strlen(cmd), g_tty_peer_addr) < 0)
		fprintf(stderr, "[np] tty inject send failed\n");
	else
		printf("[np] tty injected 'rpctest 64' into the slave shell\n");
	pthread_mutex_unlock(&g_tx_lock);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *firmware = DEFAULT_FIRMWARE;
	const char *cpu_str = DEFAULT_CPU;
	int pings = DEFAULT_PINGS;
	struct mica_client *client;
	struct mica_service svc;
	struct timespec ts;
	pthread_t worker;
	int opt, ret, i;

	while ((opt = getopt(argc, argv, "f:c:n:h")) != -1) {
		switch (opt) {
		case 'f': firmware = optarg; break;
		case 'c': cpu_str = optarg; break;
		case 'n': pings = atoi(optarg); break;
		default: usage(argv[0]); return 2;
		}
	}

	sem_init(&ept_ready, 0, 0);
	sem_init(&resp_ready, 0, 0);
	sem_init(&tty_ready, 0, 0);
	signal(SIGINT, on_sigint);
	/* A socket proxy MUST ignore SIGPIPE: the proxied peer (local echo
	 * server here, any remote app in production) can close its side at any
	 * time; without this the first send() to a half-closed socket kills the
	 * whole proxy process (observed: master died silently right after the
	 * 3rd fragmented SEND of the bigmsg test, no kernel crash log). */
	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IOLBF, 0);

	printf("[np] netproxy_demo: firmware=%s cpu=%s pings=%d\n",
	       firmware, cpu_str, pings);

	client = calloc(1, sizeof(*client));
	if (!client)
		return 1;
	snprintf(client->path, sizeof(client->path), "%s", firmware);
	client->ped = BARE_METAL;
	snprintf(client->ped_setup.name, sizeof(client->ped_setup.name), "%s",
		 "netproxy-demo");
	snprintf(client->ped_setup.cpu_str, sizeof(client->ped_setup.cpu_str),
		 "%s", cpu_str);

	ret = mica_create(client);
	if (ret) {
		fprintf(stderr, "[np] mica_create failed: %d\n", ret);
		goto out_free;
	}
	printf("[np] step1 mica_create ok\n");

	ret = mica_start(client);
	if (ret) {
		fprintf(stderr, "[np] mica_start failed: %d\n", ret);
		goto out_remove;
	}
	printf("[np] step2 mica_start ok, waiting NS announce...\n");

	memset(&svc, 0, sizeof(svc));
	snprintf(svc.name, sizeof(svc.name), "%s", "netproxy-svc");
	svc.rpmsg_ns_match = rpc_match;
	svc.rpmsg_ns_bind_cb = rpc_bind;
	ret = mica_register_service(client, &svc);
	if (ret) {
		fprintf(stderr, "[np] mica_register_service failed: %d\n", ret);
		goto out_stop;
	}

	if (!ept_ok) {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += EPT_WAIT_SEC;
		if (sem_timedwait(&ept_ready, &ts) != 0 || !ept_ok) {
			fprintf(stderr, "[np] timeout waiting '%s' announce\n",
				RPC_EPT_NAME);
			ret = -ETIMEDOUT;
			goto out_stop;
		}
	}
	printf("[np] step3 rpc endpoint bound (peer %u)\n", g_peer_addr);

	/* wait a moment for the slave demo task to register its handler */
	sleep(2);

	/* service probe: PING the slave handler (client role). The slave app
	 * may still be running its own bring-up demos (CAN/net phases) when
	 * we get here - its rpc handler registers only after they finish, so
	 * retry on -EOPNOTSUPP/-EAGAIN for up to ~60 s. */
	for (i = 0; i < pings && !stop_req; i++) {
		int len = 16 << i; /* 16, 32, 64 ... */
		int tries;

		if (len > 256)
			len = 256;
		for (tries = 0; tries < 30 && !stop_req; tries++) {
			ret = rpc_ping_slave(len);
			if (ret != -EOPNOTSUPP && ret != -EAGAIN)
				break;
			if (tries == 0)
				printf("[np] slave handler not ready, retrying...\n");
			sleep(2);
		}
		if (ret < 0) {
			fprintf(stderr, "[np] ping %d failed: %d\n", i, ret);
			break;
		}
	}

	/* worker: serve slave socket-proxy requests until Ctrl-C */
	if (pthread_create(&worker, NULL, worker_thread, NULL) == 0) {
		pthread_t uecho, techo, live;

		printf("[np] service probe done; socket proxy worker running"
		       " (Ctrl-C to stop)\n");
		pthread_create(&uecho, NULL, udp_echo_thread, NULL);
		pthread_create(&techo, NULL, tcp_echo_thread, NULL);
		pthread_create(&live, NULL, liveness_thread, NULL);
		pthread_detach(live);
		{
			pthread_t tbrg, tinj;

			pthread_create(&tbrg, NULL, tty_bridge_thread, NULL);
			pthread_detach(tbrg);
			pthread_create(&tinj, NULL, tty_inject_thread, NULL);
			pthread_detach(tinj);
		}
		while (!stop_req)
			pause();
		pthread_join(worker, NULL);
	}

out_stop:
	{
		int sret = mica_stop(client);

		if (sret)
			fprintf(stderr, "[np] mica_stop: %d\n", sret);
		else
			printf("[np] slave core powered off\n");
	}
out_remove:
	mica_remove(client);
	free(client);
	printf("[np] done\n");
	return ret;
out_free:
	free(client);
	return ret;
}
