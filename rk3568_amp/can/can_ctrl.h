/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 *
 * can_ctrl.h - shared control block between the Linux master (mcs_can.ko)
 * and the LiteOS-M slave (can_rk3568.c) for the RK3568 real-time CAN
 * ports. Master-side copy; the slave copy in the SDK board driver
 * (driver/can/can_ctrl.h) is the twin.
 *
 * Layout is a contract: any change bumps CAN_CTRL_VERSION and must be
 * mirrored on both sides.
 *
 * Location: per-port blocks in the last 4KB page of the 1MB MCS shared
 * window, one 0x100 page per handed-over controller:
 *   0x740ff000..0x740ff0ff  net control block (net_ctrl.h)
 *   0x740ff100..0x740ff1ff  CAN1 control block
 *   0x740ff200..0x740ff2ff  CAN2 control block
 *
 * The master publishes the platform facts ONCE at probe and never touches
 * CAN registers after the bus probe; the slave owns the whole CAN data
 * path and writes its own state here.
 */

#ifndef _CAN_CTRL_H_
#define _CAN_CTRL_H_

#define CAN_CTRL_SIZE		0x100

/* ctrl-page fallback values (the dts oh_rt,ctrl-phys property is the
 * source of truth; mcs_can.c falls back to these for old dtss) */
#define CAN1_CTRL_PHYS		0x740FF100UL
#define CAN2_CTRL_PHYS		0x740FF200UL

#define CAN_CTRL_MAGIC		0x43414E31	/* "CAN1" (protocol id, not
						 * a port number - the port
						 * lives in port_index) */
#define CAN_CTRL_VERSION	1

/* master_state / slave_state */
#define CAN_CTRL_STATE_OFFLINE	0
#define CAN_CTRL_STATE_READY	1
#define CAN_CTRL_STATE_RUNNING	2
#define CAN_CTRL_STATE_FAULT	3

/* port_index */
#define CAN_PORT_CAN1		1
#define CAN_PORT_CAN2		2

struct CanCtrl {
	u32 magic;		/* CAN_CTRL_MAGIC when master is up */
	u32 version;		/* CAN_CTRL_VERSION */
	u32 master_state;	/* written by master (mcs_can.ko) */
	u32 slave_state;	/* written by slave (Can runtime state) */

	u32 port_index;		/* CAN_PORT_CAN1 / CAN_PORT_CAN2 */
	u32 base_phys;		/* 0xfe580000 / 0xfe590000 */
	u32 intid;		/* 34 / 35 = GIC_SPI 2/3 + 32 (slave-owned) */
	u32 baudclk_hz;		/* READ-BACK CLK_CANx rate (bit timing) */

	u32 pclk_hz;		/* PCLK_CANx rate (informational) */
	u32 cpu_version;	/* rockchip_get_cpu_version(): 3 = V2
				 * silicon (no txtorx EXT loopback),
				 * <3 = V1, 0 = unknown (cpuinfo off) */

	/* keep the struct a multiple of 64B (cache line) for future fields */
	u32 _reserved[5];
};

#endif /* _CAN_CTRL_H_ */
