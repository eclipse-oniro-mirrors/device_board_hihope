/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 *
 * net_ctrl.h - shared control block between the Linux master (mcs_net.ko)
 * and the LiteOS-M slave (net_dwmac.c) for the RK3568 GMAC1 real-time port.
 *
 * Layout is a contract: any change bumps NET_CTRL_VERSION and must be
 * mirrored into the slave-side copy of this header.
 *
 * Location: last 4KB page of the 1MB MCS shared window
 *   0x74000000..0x740fefff  openamp vring / rpmsg buffers (owned by mcs_km)
 *   0x740ff000..0x740fffff  this control block
 */

#ifndef _NET_CTRL_H_
#define _NET_CTRL_H_

#define NET_CTRL_PHYS	0x740FF000UL
#define NET_CTRL_SIZE	0x1000

#define NET_CTRL_MAGIC	0x45434E31	/* "ECN1" */
#define NET_CTRL_VERSION	1

/* master_state / slave_state */
#define NET_STATE_OFFLINE	0
#define NET_STATE_READY	1
#define NET_STATE_RUNNING	2
#define NET_STATE_FAULT	3

struct NetCtrl {
	u32 magic;		/* NET_CTRL_MAGIC when master is up */
	u32 version;		/* NET_CTRL_VERSION */
	u32 master_state;	/* written by master (mcs_net.ko) */
	u32 phy_id;		/* PHYSID1<<16 | PHYSID2 */

	/* link snapshot, refreshed by the master 1s delayed work */
	u32 link_up;		/* 0/1 */
	u32 speed_mbps;		/* 100 */
	u32 full_duplex;	/* 0/1 */
	u32 valid;		/* 0 while the master publishes an update, 1 = stable */
	u64 sample_time_ns;	/* master ktime_get_boottime_ns() */
	u32 link_changes;	/* count of link transitions observed */

	u32 slave_state;	/* written by slave (Net runtime state) */

	/* keep the struct a multiple of 64B (cache line) for future fields */
	u8  _reserved[64 - 52];
};

#endif /* _NET_CTRL_H_ */
