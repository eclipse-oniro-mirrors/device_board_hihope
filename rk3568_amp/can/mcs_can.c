// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 *
 * mcs_can.c - RK3568 CAN1/CAN2 handover driver for the MCS AMP slave core.
 *
 * One driver, one module, N ports: every mcs-canX dts node binds here and
 * the compatible's match data selects the port (CAN_PORT_CAN1/2). All
 * platform facts come from the node itself - the dts IS the port:
 *   reg                -> controller base (published to the ctrl block)
 *   clocks/resets      -> per-port CLK_CANx/PCLK_CANx, SRST_CANx pairs
 *   pinctrl-0          -> the pin group (can1m1 / can2m0 / ...)
 *   oh_rt,irq-intid    -> the slave-owned GIC INTID (cross-check value)
 *   oh_rt,ctrl-phys    -> the per-port shared control block page
 *
 * Master-side (this driver) brings the controller up and keeps the
 * platform alive:
 *   1. enable + hold the CAN clocks (baudclk + apb_pclk), retarget
 *      CLK_CANx to 100MHz and publish the READ-BACK rate (the bit-timing
 *      calculator on the slave needs the true frequency)
 *   2. pulse the CRU CAN resets (SRST_CANx + SRST_P_CANx)
 *   3. apply the pinmux group from pinctrl-0
 *   4. probe the register bus (a read of CAN_MODE with the clock domain
 *      dead would hang the AXI bus - the same lesson as GMAC1)
 *   5. read the silicon cpu version (V1/V2 silicon differ in the
 *      transmit-to-rx-loopback behaviour of extended frames; 0 here means
 *      unknown - CONFIG_ROCKCHIP_CPUINFO off)
 *   6. publish the shared control block (can_ctrl.h) and mark it READY
 *
 * Interrupt ownership: the CAN irqs (CAN1: GIC_SPI 2 = INTID 34, CAN2:
 * GIC_SPI 3 = INTID 35) are NOT configured here. Per the per-core GICD
 * ownership contract the slave owns them end-to-end: LiteOS
 * ArchHwiCreate (gic-v3 createIrq) sets group, IROUTER affinity, trigger
 * mode, priority and enable when CanStart() runs; this driver only
 * publishes the dts "oh_rt,irq-intid" value for the slave's cross-check.
 *
 * The whole CAN data path (mode/bittiming/filters/rx fifo/tx fifo
 * registers) is owned exclusively by the slave-side LiteOS-M driver
 * (can_rk3568.c); after step 4 this driver never touches a CAN register.
 *
 * The base can1/can2 device nodes stay disabled in the base dtsi
 * (rk356x-oh.dtsi), so Linux never binds rockchip_canfd to them; the
 * mcs-canX nodes are the only bindings.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/stdarg.h>
#include <linux/rockchip/cpu.h>

#include "can_ctrl.h"

/* ---------------- CAN register (bus probe read only) --------------------- */

#define CAN_MODE_REG		0x00	/* any readable reg works as probe */

/* ---------------- handover contract values ------------------------------- */

/* bit-timing clock target for every port (the solver on the slave works
 * against the READ-BACK rate published in the ctrl block) */
#define MCS_CAN_BAUDCLK_HZ	100000000UL

/* ctrl-page fallback when the dts omits oh_rt,ctrl-phys (the dts
 * property is the source of truth; this only backstops old dtss) */
static u64 mcs_can_ctrl_phys(u32 port)
{
	switch (port) {
	case CAN_PORT_CAN1:	return CAN1_CTRL_PHYS;
	case CAN_PORT_CAN2:	return CAN2_CTRL_PHYS;
	}
	return 0;
}

/* probe steps 0..MCS_STEP_MAX-1: clocks, resets, pinmux, bus probe,
 * cpu version, ctrl publish. */
#define MCS_STEP_MAX		6

/* ---------------- driver ------------------------------------------------ */

struct mcs_can_priv {
	struct device *dev;

	u32 port_index;			/* CAN_PORT_CANx (from match data) */
	u64 ctrl_phys;			/* shared ctrl page (from dts) */

	struct clk *clk_baud;
	struct clk *clk_apb;

	struct reset_control *rst_can;
	struct reset_control *rst_apb;

	void __iomem *can_io;		/* bus probe only, never used after */

	void __iomem *ctrl;		/* shared CanCtrl block */
	u32 baudclk_hz;			/* read-back CLK_CANx rate */

	/* probe state for sysfs */
	char steps[MCS_STEP_MAX][24];
	int step_fail;
};

/* ---------------- sysfs --------------------------------------------------- */

static ssize_t state_show(struct device *dev, struct device_attribute *a,
			  char *buf)
{
	struct mcs_can_priv *p = platform_get_drvdata(to_platform_device(dev));
	struct CanCtrl __iomem *c = p->ctrl;
	int limit = p->step_fail ? p->step_fail : MCS_STEP_MAX;
	int n = 0, i;

	for (i = 0; i < limit; i++)
		n += scnprintf(buf + n, PAGE_SIZE - n, "%s\n", p->steps[i]);
	if (p->step_fail)
		return n;

	n += scnprintf(buf + n, PAGE_SIZE - n,
		       "port=%u baudclk=%uHz pclk=%uHz cpu_version=%u\n",
		       readl(&c->port_index), readl(&c->baudclk_hz),
		       readl(&c->pclk_hz), readl(&c->cpu_version));
	n += scnprintf(buf + n, PAGE_SIZE - n,
		       "ctrl master=%u slave=%u\n",
		       readl(&c->master_state), readl(&c->slave_state));
	return n;
}
static DEVICE_ATTR_RO(state);

static struct attribute *mcs_can_attrs[] = {
	&dev_attr_state.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mcs_can);

/* ---------------- probe / remove ----------------------------------------- */

static void mcs_step(struct mcs_can_priv *p, int idx, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(p->steps[idx], sizeof(p->steps[idx]), fmt, ap);
	va_end(ap);
	/* probe progress trace: the last line printed is where it hangs/fails */
	dev_emerg(p->dev, "step%d: %s\n", idx, p->steps[idx]);
}

static int mcs_can_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mcs_can_priv *p;
	u64 can_base, ctrl_phys;
	u32 port_index, irq_intid;
	u32 mode_reg;
	struct pinctrl *pc;
	int ret;

	pr_emerg("[mcs_can] probe enter\n");
	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->dev = dev;
	platform_set_drvdata(pdev, p);

	/* 0. port identity: the compatible's match data selects the
	 * controller; everything else is read from this node below. */
	port_index = (u32)(uintptr_t)of_device_get_match_data(dev);
	if ((port_index != CAN_PORT_CAN1) && (port_index != CAN_PORT_CAN2)) {
		dev_err(dev, "bad port id in compatible data: %u\n", port_index);
		return -EINVAL;
	}
	p->port_index = port_index;
	dev_info(dev, "probing CAN%u (node %s)\n", port_index, np->name);

	/* 1. clocks: named get + enable, held on forever (the slave data
	 * path depends on them; same policy as mcs_net). */
	p->clk_baud = devm_clk_get(dev, "baudclk");
	p->clk_apb = devm_clk_get(dev, "apb_pclk");
	if (IS_ERR(p->clk_baud) || IS_ERR(p->clk_apb)) {
		mcs_step(p, 0, "clocks: get FAILED baud/apb: %ld/%ld",
			 IS_ERR(p->clk_baud) ? PTR_ERR(p->clk_baud) : 0,
			 IS_ERR(p->clk_apb) ? PTR_ERR(p->clk_apb) : 0);
		p->step_fail = 1;
		return -ENODEV;
	}
	ret = clk_prepare_enable(p->clk_baud);
	if (ret) {
		mcs_step(p, 0, "clocks: enable baudclk FAILED (%d)", ret);
		p->step_fail = 1;
		return ret;
	}
	ret = clk_prepare_enable(p->clk_apb);
	if (ret) {
		mcs_step(p, 0, "clocks: enable apb_pclk FAILED (%d)", ret);
		p->step_fail = 1;
		return ret;
	}
	pr_emerg("[mcs_can] CAN%u clk baudclk ON (%lu Hz), apb_pclk ON (%lu Hz)\n",
		 port_index, clk_get_rate(p->clk_baud),
		 clk_get_rate(p->clk_apb));

	/* Retarget the bit-timing clock to 100MHz and READ IT BACK: the
	 * slave bit-timing calculator consumes CanCtrl.baudclk_hz, and the
	 * clock framework is free to land on any parent/divider that
	 * produces the requested rate (both GPLL and CPLL divide evenly
	 * here). Publishing the request instead of the read-back would
	 * silently mis-clock every bitrate if the tree ever changes. */
	ret = clk_set_rate(p->clk_baud, MCS_CAN_BAUDCLK_HZ);
	if (ret) {
		mcs_step(p, 0, "clk_set_rate(%lu) FAILED (%d)",
			 MCS_CAN_BAUDCLK_HZ, ret);
		p->step_fail = 1;
		return ret;
	}
	p->baudclk_hz = clk_get_rate(p->clk_baud);
	mcs_step(p, 0, "clocks: OK, baudclk=%uHz apb=%luHz (held on)",
		 p->baudclk_hz, clk_get_rate(p->clk_apb));

	/* 2. CRU reset pulse (rockchip_canfd set_reset_mode sequence):
	 * assert both lines, wait, release. The slave enters the CAN
	 * soft-reset mode itself during CanInit; this is the power-on
	 * domain reset. */
	p->rst_can = devm_reset_control_get_exclusive(dev, "can");
	p->rst_apb = devm_reset_control_get_exclusive(dev, "can-apb");
	if (IS_ERR(p->rst_can) || IS_ERR(p->rst_apb)) {
		mcs_step(p, 1, "resets: get FAILED can/apb: %ld/%ld",
			 IS_ERR(p->rst_can) ? PTR_ERR(p->rst_can) : 0,
			 IS_ERR(p->rst_apb) ? PTR_ERR(p->rst_apb) : 0);
		p->step_fail = 2;
		return -ENODEV;
	}
	reset_control_assert(p->rst_can);
	reset_control_assert(p->rst_apb);
	udelay(2);
	reset_control_deassert(p->rst_can);
	reset_control_deassert(p->rst_apb);
	mcs_step(p, 1, "resets: pulsed (can + can-apb)");

	/* 3. pinmux (dt core already applies "default"; reselect is
	 * harmless and proves the group exists). A select failure is NOT
	 * fatal: the internal-loopback bring-up does not leave the die, so
	 * the handover continues with a loud warning - a real bus needs the
	 * pins, and the warning says exactly that. */
	pc = devm_pinctrl_get(dev);
	if (IS_ERR(pc)) {
		mcs_step(p, 2, "pinmux: get FAILED (continuing; loopback only)");
	} else {
		ret = pinctrl_select_state(pc,
				pinctrl_lookup_state(pc, PINCTRL_STATE_DEFAULT));
		if (ret)
			mcs_step(p, 2,
				 "pinmux: select FAILED (continuing; loopback only - a real bus needs the pins)");
		else
			mcs_step(p, 2, "pinmux: group applied");
	}

	/* 4. map the CAN page and probe the register bus. With the clock
	 * domain dead this read would hang the AXI bus on this SoC (the
	 * GMAC1 lesson), so it happens AFTER the clocks are held on and is
	 * deliberately the first CAN register access of the handover.
	 * The dts reg IS the source of truth for the base (published to
	 * the ctrl block below; the slave cross-checks it against its own
	 * compiled-in profile). */
	ret = of_property_read_reg(np, 0, &can_base, NULL);
	if (ret) {
		mcs_step(p, 3, "can-io: no reg");
		p->step_fail = 4;
		return ret;
	}
	p->can_io = devm_ioremap(dev, can_base, SZ_4K);
	if (!p->can_io) {
		mcs_step(p, 3, "can-io: ioremap FAILED");
		p->step_fail = 4;
		return -ENOMEM;
	}
	mode_reg = readl(p->can_io + CAN_MODE_REG);
	mcs_step(p, 3, "can-io: bus probe OK @0x%llx, MODE=0x%08x",
		 can_base, mode_reg);

	/* 5. silicon version: V1 silicon (version < 3) loops self-transmitted
	 * EXTENDED frames back into the rx fifo even outside loopback mode
	 * (the kernel rockchip_canfd driver carries a software workaround);
	 * V2 (version == 3) does not. Published for the slave's demo/docs;
	 * 0 = unknown (this kernel is built without CONFIG_ROCKCHIP_CPUINFO,
	 * rockchip_get_cpu_version() is a static inline returning 0 then). */
	mcs_step(p, 4, "cpu version: %lu (0 = unknown, cpuinfo off)",
		 rockchip_get_cpu_version());

	/* 6. dts handover facts: the irq intid and the ctrl page. The slave
	 * core owns and configures the INTID itself via ArchHwiCreate (the
	 * per-core GICD ownership contract); this value is the published
	 * cross-check fact, so it must be present. */
	if (of_property_read_u32(np, "oh_rt,irq-intid", &irq_intid)) {
		mcs_step(p, 5, "ctrl: dts has no oh_rt,irq-intid");
		p->step_fail = 6;
		return -EINVAL;
	}
	if (!of_property_read_u64(np, "oh_rt,ctrl-phys", &ctrl_phys) &&
	    ctrl_phys) {
		/* dts is the source of truth */
	} else {
		ctrl_phys = mcs_can_ctrl_phys(port_index);
		dev_info(dev, "dts has no oh_rt,ctrl-phys, using built-in 0x%llx\n",
			 ctrl_phys);
	}
	if (!ctrl_phys) {
		mcs_step(p, 5, "ctrl: no ctrl page for port %u", port_index);
		p->step_fail = 6;
		return -EINVAL;
	}
	p->ctrl_phys = ctrl_phys;

	/* 7. shared control block (device-mapped: visible to the cacheless
	 * slave without any cache maintenance on this side). One 0x100 page
	 * per port in the last 4KB of the 1MB MCS shared window: net at
	 * 0x740ff000, CAN1 at 0x740ff100, CAN2 at 0x740ff200 (can_ctrl.h). */
	p->ctrl = devm_ioremap(dev, ctrl_phys, CAN_CTRL_SIZE);
	if (!p->ctrl) {
		mcs_step(p, 5, "ctrl: ioremap FAILED");
		p->step_fail = 6;
		return -ENOMEM;
	}
	{
		struct CanCtrl __iomem *c = p->ctrl;

		writel(0, &c->master_state);
		writel(0, &c->slave_state);
		writel(CAN_CTRL_MAGIC, &c->magic);
		writel(CAN_CTRL_VERSION, &c->version);
		writel(port_index, &c->port_index);
		writel((u32)can_base, &c->base_phys);
		writel(irq_intid, &c->intid);
		writel(p->baudclk_hz, &c->baudclk_hz);
		writel(clk_get_rate(p->clk_apb), &c->pclk_hz);
		writel((u32)rockchip_get_cpu_version(), &c->cpu_version);
		writel(CAN_CTRL_STATE_READY, &c->master_state);
	}
	mcs_step(p, 5, "ctrl: READY at 0x%llx", ctrl_phys);

	dev_info(dev, "CAN%u handed over to slave core: ctrl 0x%llx READY, base 0x%llx intid %u baudclk %uHz\n",
		 port_index, ctrl_phys, can_base, irq_intid, p->baudclk_hz);
	return 0;
}

static void mcs_can_remove(struct platform_device *pdev)
{
	struct mcs_can_priv *p = platform_get_drvdata(pdev);
	volatile struct CanCtrl *c = p->ctrl;

	if (c)
		writel(CAN_CTRL_STATE_OFFLINE, &c->master_state);
	/*
	 * Clocks/reset/pinctrl mappings are devm-managed and stay prepared
	 * for the slave; on rmmod the whole handover goes away with them.
	 * The CAN irqs are owned by the slave core (per-core GICD ownership)
	 * and are not touched here.
	 */
}

static const struct of_device_id mcs_can_of_match[] = {
	{ .compatible = "oh_rt,mcs-can1", .data = (void *)(uintptr_t)CAN_PORT_CAN1 },
	{ .compatible = "oh_rt,mcs-can2", .data = (void *)(uintptr_t)CAN_PORT_CAN2 },
	{ }
};
MODULE_DEVICE_TABLE(of, mcs_can_of_match);

static struct platform_driver mcs_can_driver = {
	.probe  = mcs_can_probe,
	.remove_new = mcs_can_remove,
	.driver = {
		.name = "mcs-can",
		.of_match_table = mcs_can_of_match,
		.dev_groups = mcs_can_groups,
	},
};
static int __init mcs_can_init(void)
{
	int ret;

	pr_emerg("[mcs_can] module init: registering driver\n");
	ret = platform_driver_register(&mcs_can_driver);
	pr_emerg("[mcs_can] driver registered: %d (probe may still be running)\n", ret);
	return ret;
}

static void __exit mcs_can_exit(void)
{
	platform_driver_unregister(&mcs_can_driver);
}

module_init(mcs_can_init);
module_exit(mcs_can_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RK3568 CAN1/CAN2 handover driver for the MCS AMP slave core");
MODULE_AUTHOR("mcs port");
