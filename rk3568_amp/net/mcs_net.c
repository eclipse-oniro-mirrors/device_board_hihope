// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 *
 * mcs_net.c - RK3568 GMAC1 (eth1) handover driver for the MCS AMP slave core.
 *
 * Master-side (this driver) brings the port up and keeps the platform alive:
 *   1. enable + hold all GMAC1 clocks (never disabled; solves the unbind /
 *      runtime-pm clock gating problem), set clk_mac_speed for 100M (25MHz)
 *   2. release the CRU stmmaceth reset, apply the RGMII pinmux group
 *   3. program GRF RGMII delays (sequence from stmmac dwmac-rk.c
 *      rk3568_set_to_rgmii())
 *   4. own MDIO (MAC + 0x200, c22 bit fields per stmmac_mdio.c GMAC4 path),
 *      force the PHY to 100M full duplex, poll link state into the shared
 *      control block once per second
 *
 * Interrupt ownership: the GMAC1 macirq (GIC SPI 32 = INTID 64) is NOT
 * configured here. Per the per-core GICD ownership contract the slave
 * owns it end-to-end: LiteOS ArchHwiCreate (gic-v3 createIrq) sets group,
 * IROUTER affinity, trigger mode, priority and enable when NetStart()
 * runs; this driver only cross-checks the dts "oh_rt,irq-intid" value.
 *
 * The data path (DMA/MTL/MAC registers) is owned exclusively by the
 * slave-side LiteOS-M driver; this driver only touches the MDIO register
 * page (0x200/0x204) after probe.
 *
 * The eth1 device node is marked disabled in the DTS, so Linux never binds
 * the stmmac driver to it.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/stdarg.h>
#include <linux/workqueue.h>

#include "net_ctrl.h"

/* ---------------- GMAC MDIO (dwmac-4.20a), per stmmac_mdio.c GMAC4 ------- */

#define GMAC_MDIO_ADDR_REG		0x200
#define GMAC_MDIO_DATA_REG		0x204

#define MII_BUSY			BIT(0)
#define MII_GMAC4_GOC_SHIFT		2
#define MII_GMAC4_WRITE			(1 << MII_GMAC4_GOC_SHIFT)
#define MII_GMAC4_READ			(3 << MII_GMAC4_GOC_SHIFT)
/* GMAC4 MDIO address register field shifts (dwmac4_core.c) */
#define MII_ADDR_SHIFT			21	/* PHY address */
#define MII_REG_SHIFT			16	/* PHY register */
#define MII_CLK_CSR_SHIFT		8	/* MDC clock range */
/*
 * clk_csr = 5 for a 125MHz stmmaceth clock (SCLK_GMAC1 = 125MHz on this
 * board): MDC = 125MHz / 2^(5+1) ~= 1.95MHz, within the RTL8211F spec.
 */
#define MII_CLK_CSR_125MHZ		5

/* PHY registers (clause 22) */
#define MII_BMCR			0
#define MII_BMSR			1
#define MII_PHYSID1			2
#define MII_PHYSID2			3
#define MII_ADVERTISE			4
#define MII_LPA			5
#define MII_CTRL1000			9
#define MII_STAT1000			10
#define BMCR_ANEG_RESTART		0x1200	/* aneg enable + restart */
#define BMCR_ANEG_ENABLE		BIT(12)
#define BMCR_SPEED100			BIT(13)
#define BMCR_FULLDPLX			BIT(8)
#define BMCR_100M_FD			0x2100	/* speed MSB | duplex, aneg off */
#define BMSR_LSTATUS			BIT(2)
#define BMSR_ANEGCOMPLETE		BIT(5)
#define BMSR_ERCAP			BIT(15)	/* bits15:14 speed resolved: 11=1G */
#define CTRL1000_ADV_1000FD		BIT(9)

/* YT8531S extended registers (page protocol: c22 reg0x1E selects the
 * page, reg0x1F is the data window). Same layout as kernel
 * drivers/net/phy/motorcomm.c. */
#define PHY_ID_YT8531S			0x4f51e91a

#define YTPHY_PAGE_SELECT_REG		0x1E
#define YTPHY_PAGE_DATA_REG		0x1F

#define YT8521_CHIP_CONFIG_REG		0xA001
#define YT8521_CCR_SW_RST		BIT(15)	/* write-1 self-resets: keep 0 on write */
#define YT8521_CCR_RXC_DLY_EN		BIT(8)	/* 1.9ns rxc delay */

#define YT8521_RGMII_CONFIG1_REG	0xA003
#define YT8521_RC1R_RX_DELAY_MASK	GENMASK(13, 10)
#define YT8521_RC1R_GE_TX_DELAY_MASK	GENMASK(3, 0)

/* ---------------- GRF (sequence from dwmac-rk.c, rk3568) ---------------- */

#define RK3568_GRF_GMAC1_CON0		0x0388
#define RK3568_GRF_GMAC1_CON1		0x038c

#define HIWORD_UPDATE(val, mask, shift) \
	(((u32)(val) << (shift)) | ((u32)(mask) << ((shift) + 16)))
#define GRF_BIT(nr)			(BIT(nr) | BIT(nr + 16))
#define GRF_CLR_BIT(nr)		BIT(nr + 16)

/* CON1: RGMII mode + tx/rx clock delay enable */
#define RK3568_GMAC1_CON1_RGMII		\
	(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6) | GRF_BIT(0) | GRF_BIT(1))
/* CON0: delay values, 7-bit fields */
#define RK3568_GMAC1_CON0_DLY(tx, rx)	\
	(HIWORD_UPDATE((tx) & 0x7f, 0x7f, 0) | HIWORD_UPDATE((rx) & 0x7f, 0x7f, 8))

/* ---------------- interrupt ownership contract -------------------------- */

#define GMAC1_MACIRQ_INTID		64	/* GIC_SPI 32 + 32; owned by the
						 * slave core, never enabled
						 * by Linux (dts node disabled) */

/* probe steps 0..MCS_STEP_MAX-1: clocks, clk_mac_speed, reset, pinmux, grf,
 * mac-io, phy, ctrl, link poll (GICD step removed with the handover) */
#define MCS_STEP_MAX			9

/* ---------------- driver ------------------------------------------------ */

struct mcs_net_priv {
	struct device *dev;

	struct clk *clk_stmmaceth;
	struct clk *clk_aclk;
	struct clk *clk_pclk;
	struct clk *clk_mac_speed;
	struct clk *clk_xpcs;		/* optional */
	struct clk *clk_xpcs_eee;	/* optional */

	struct reset_control *rst;

	struct regmap *grf;
	u32 tx_delay, rx_delay;

	struct gpio_desc *phy_reset;

	void __iomem *mac_io;		/* only the MDIO page is used */

	void __iomem *ctrl;		/* shared NetCtrl block */
	struct delayed_work link_work;
	u32 speed_clk;			/* speed clk_mac_speed is set for */

	/* probe/runtime state for sysfs */
	u32 phy_id;
	u32 link_up;
	u32 link_changes;
	char steps[MCS_STEP_MAX][24];
	int step_fail;
};

/* ---------------- MDIO --------------------------------------------------- */

static int mcs_mdio_wait(void __iomem *io)
{
	u32 v;

	return readl_poll_timeout(io + GMAC_MDIO_ADDR_REG, v,
				  !(v & MII_BUSY), 100, 10000);
}

static int mcs_mdio_read(struct mcs_net_priv *p, int phy, int reg, u16 *val)
{
	u32 v = MII_BUSY | MII_GMAC4_READ;
	int ret;

	ret = mcs_mdio_wait(p->mac_io);
	if (ret)
		return ret;

	v |= (phy << MII_ADDR_SHIFT) | (reg << MII_REG_SHIFT) |
	     (MII_CLK_CSR_125MHZ << MII_CLK_CSR_SHIFT);
	writel(0, p->mac_io + GMAC_MDIO_DATA_REG);
	writel(v, p->mac_io + GMAC_MDIO_ADDR_REG);

	ret = mcs_mdio_wait(p->mac_io);
	if (ret)
		return ret;

	*val = readl(p->mac_io + GMAC_MDIO_DATA_REG) & 0xffff;
	return 0;
}

static int mcs_mdio_write(struct mcs_net_priv *p, int phy, int reg, u16 val)
{
	u32 v = MII_BUSY | MII_GMAC4_WRITE;
	int ret;

	ret = mcs_mdio_wait(p->mac_io);
	if (ret)
		return ret;

	v |= (phy << MII_ADDR_SHIFT) | (reg << MII_REG_SHIFT) |
	     (MII_CLK_CSR_125MHZ << MII_CLK_CSR_SHIFT);
	writel(val, p->mac_io + GMAC_MDIO_DATA_REG);
	writel(v, p->mac_io + GMAC_MDIO_ADDR_REG);

	return mcs_mdio_wait(p->mac_io);
}

/* ---------------- PHY ---------------------------------------------------- */

#define PHY_ADDR_DEFAULT	0	/* rgmii_phy1 = mdio1 phy@0 */

static int mcs_phy_ext_read(struct mcs_net_priv *p, int phy, u16 reg, u16 *val)
{
	int ret;

	ret = mcs_mdio_write(p, phy, YTPHY_PAGE_SELECT_REG, reg);
	if (ret)
		return ret;
	return mcs_mdio_read(p, phy, YTPHY_PAGE_DATA_REG, val);
}

static int mcs_phy_ext_write(struct mcs_net_priv *p, int phy, u16 reg, u16 val)
{
	int ret;

	ret = mcs_mdio_write(p, phy, YTPHY_PAGE_SELECT_REG, reg);
	if (ret)
		return ret;
	return mcs_mdio_write(p, phy, YTPHY_PAGE_DATA_REG, val);
}

/*
 * RGMII delay ownership: GRF only (Linux-proven, re-verified 2026-09-27).
 *
 * History: on 2026-09-23 clearing the PHY-internal delay bits was tried and
 * "TX never reached the wire" - that experiment predates the GRF CON1
 * RXCLK/TXCLK DLY_ENABLE writes, so clearing the PHY bits removed ALL delay
 * and killed both directions. It was concluded (wrongly) that Linux keeps
 * the strap defaults. Reading linux-6.6 motorcomm.c settles it: for plain
 * PHY_INTERFACE_MODE_RGMII (what this port's dts uses), ytphy_rgmii_clk_
 * delay_config() CLEARS every PHY-internal delay - CHIP_CONFIG rxc-dly
 * (0xA001 bit8, 1.9ns on RXC) and RGMII_CONFIG1 rx/ge-tx bits - and the
 * whole delay budget comes from the GRF (CON0 tx=0x37/rx=0x0f + CON1 both
 * DLY_ENABLEs, identical to dwmac-rk rk3568_set_to_rgmii()). RX worked on
 * this exact port under Linux with that configuration (2026-09-24).
 *
 * The PHY straps leave rxc-dly + ge-tx-dly ON, stacking ~1.9ns on top of
 * the GRF rx delay: RXC lags RXD by ~4ns, outside the 1000M RGMII eye, so
 * the MAC CRC-drops every received frame (board->PC TX stays inside its
 * margin). mcs_phy_rgmii_delay_grf_only() below mirrors motorcomm after
 * the probe soft reset has restored the straps.
 */

static void mcs_phy_autoneg_1000m(struct mcs_net_priv *p)
{
	/*
	 * Align with the proven Linux path (stmmac + phylib + motorcomm,
	 * verified working on this exact port 2026-09-23):
	 *  - soft reset, then advertise 10/100/1000 (CTRL1000 FD|HD), restart aneg
	 *  - the soft reset restores the PHY-internal RGMII delay straps;
	 *    mcs_phy_rgmii_delay_grf_only() (called right after this) clears
	 *    them so the GRF is the only delay source, mirroring motorcomm's
	 *    plain-PHY_INTERFACE_MODE_RGMII configuration.
	 */
	mcs_mdio_write(p, PHY_ADDR_DEFAULT, MII_BMCR, 0x8000); /* soft reset */
	msleep(50);
	/* ANAR first: soft reset leaves it at strap defaults (10M-only on this
	 * board) and without it the link negotiates 10M (observed 2026-09-23). */
	mcs_mdio_write(p, PHY_ADDR_DEFAULT, MII_ADVERTISE, 0x01e1);
	mcs_mdio_write(p, PHY_ADDR_DEFAULT, MII_CTRL1000, 0x0300);
	mcs_mdio_write(p, PHY_ADDR_DEFAULT, MII_BMCR, BMCR_ANEG_RESTART);
}

/*
 * Clear the PHY-internal RGMII delays so the GRF is the only delay source
 * (see the ownership comment above mcs_phy_ext_write). Values are read-
 * modify-written exactly like motorcomm ytphy_modify_ext().
 */
static void mcs_phy_rgmii_delay_grf_only(struct mcs_net_priv *p)
{
	u16 v;

	if (p->phy_id != PHY_ID_YT8531S)
		return;
	if (!mcs_phy_ext_read(p, PHY_ADDR_DEFAULT,
			      YT8521_CHIP_CONFIG_REG, &v))
		mcs_phy_ext_write(p, PHY_ADDR_DEFAULT,
				  YT8521_CHIP_CONFIG_REG,
				  v & ~YT8521_CCR_RXC_DLY_EN);
	if (!mcs_phy_ext_read(p, PHY_ADDR_DEFAULT,
			      YT8521_RGMII_CONFIG1_REG, &v))
		mcs_phy_ext_write(p, PHY_ADDR_DEFAULT,
				  YT8521_RGMII_CONFIG1_REG,
				  v & ~(YT8521_RC1R_RX_DELAY_MASK |
					YT8521_RC1R_GE_TX_DELAY_MASK));
}

static int mcs_phy_poll_link(struct mcs_net_priv *p)
{
	u16 bmsr = 0;

	/*
	 * BMSR link bit is latched-low: read twice so a fresh "up" sample
	 * is not masked by a stale low latch.
	 */
	mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_BMSR, &bmsr);
	mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_BMSR, &bmsr);
	return !!(bmsr & BMSR_LSTATUS);
}

static void mcs_ctrl_write_link(struct mcs_net_priv *p, u32 link)
{
	struct NetCtrl __iomem *c = p->ctrl;
	u32 prev = p->link_up;
	u16 bmcr = 0, gbsr = 0, lpa = 0;
	u32 speed = 0, duplex = 1;

	/* seqlock-style publish: valid==0 marks the snapshot unstable
	 * while it is being recomputed; the slave discards any read that
	 * observes a 0 or a mid-read flip (net_ctrl.h contract). */
	writel(0, &c->valid);

	/*
	 * Resolved speed/duplex (phylib genphy_read_status semantics):
	 *  - forced (BMCR bit12 = 0): speed = BMCR bit13, duplex = bit8;
	 *    the aneg result registers hold stale values in this mode.
	 *  - autoneg: MII_STAT1000 (reg10) bit11 = partner 1000FD
	 *    advertisement (LPA_1000FULL, linux mii.h) -> 1000M (always FD).
	 *    The previous bit11|bit9 test never matched (bit9 is not a GBSR
	 *    capability bit), so 1000M links were misreported as 100M.
	 *  - 10/100: ANLPAR (reg5) advertisement bits of the link partner.
	 */
	mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_BMCR, &bmcr);
	if (!(bmcr & BMCR_ANEG_ENABLE)) {
		speed = (bmcr & BMCR_SPEED100) ? 100 : 10;
		duplex = !!(bmcr & BMCR_FULLDPLX);
	} else {
		mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_STAT1000, &gbsr);
		mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_LPA, &lpa);
		if (gbsr & BIT(11)) {
			speed = 1000;
			duplex = 1;
		} else if (lpa & BIT(8)) {
			speed = 100; duplex = 1;      /* 100M FD */
		} else if (lpa & BIT(7)) {
			speed = 100; duplex = 0;      /* 100M HD */
		} else if (lpa & BIT(6)) {
			speed = 10; duplex = 1;       /* 10M FD */
		} else if (lpa & BIT(5)) {
			speed = 10; duplex = 0;       /* 10M HD */
		}
	}

	writel(link, &c->link_up);
	writel(speed, &c->speed_mbps);
	writel(duplex, &c->full_duplex);
	writeq(ktime_get_boottime_ns(), &c->sample_time_ns);
	if (prev != link) {
		p->link_changes++;
		writel(p->link_changes, &c->link_changes);
	}
	p->link_up = link;
	writel(1, &c->valid);		/* snapshot stable: publish last */

	/*
	 * clk_mac_speed must track the resolved speed (dwmac-rk
	 * rk3568_set_gmac_speed: 10 -> 2.5MHz, 100 -> 25MHz, 1000 -> 125MHz).
	 * The slave keeps its MAC CSR in GMII mode, so the RGMII interface
	 * rate comes entirely from this clock: a stale rate silently breaks
	 * the mismatched direction(s) while occasional frames still pass.
	 */
	if (speed != p->speed_clk) {
		unsigned long hz = speed == 1000 ? 125000000UL :
				   speed == 100  ?  25000000UL :
				   speed == 10   ?   2500000UL : 0;

		if (hz) {
			clk_set_rate(p->clk_mac_speed, hz);
			p->speed_clk = speed;
		}
	}
}

static void mcs_link_work(struct work_struct *w)
{
	struct mcs_net_priv *p =
		container_of(w, struct mcs_net_priv, link_work.work);

	mcs_ctrl_write_link(p, mcs_phy_poll_link(p));
	schedule_delayed_work(&p->link_work, msecs_to_jiffies(1000));
}

/* ---------------- sysfs --------------------------------------------------- */

static ssize_t state_show(struct device *dev, struct device_attribute *a,
			  char *buf)
{
	struct mcs_net_priv *p = platform_get_drvdata(to_platform_device(dev));
	struct NetCtrl __iomem *c = p->ctrl;
	int limit = p->step_fail ? p->step_fail : MCS_STEP_MAX;
	int n = 0, i;

	for (i = 0; i < limit; i++)
		n += scnprintf(buf + n, PAGE_SIZE - n, "%s\n", p->steps[i]);
	if (p->step_fail)
		return n;

	n += scnprintf(buf + n, PAGE_SIZE - n,
		       "phy id=0x%08x link=%u changes=%u\n",
		       p->phy_id, p->link_up, p->link_changes);
	n += scnprintf(buf + n, PAGE_SIZE - n,
		       "ctrl master=%u link=%u valid=%u slave=%u\n",
		       readl(&c->master_state), readl(&c->link_up),
		       readl(&c->valid), readl(&c->slave_state));
	return n;
}
static DEVICE_ATTR_RO(state);

static ssize_t mdio_show(struct device *dev, struct device_attribute *a,
			 char *buf)
{
	struct mcs_net_priv *p = platform_get_drvdata(to_platform_device(dev));
	int n = 0, r;
	u16 v;

	for (r = 0; r < 6; r++) {
		if (mcs_mdio_read(p, PHY_ADDR_DEFAULT, r, &v))
			v = 0xffff;
		n += scnprintf(buf + n, PAGE_SIZE - n, "reg%d = 0x%04x\n", r, v);
	}
	/* YT8531 extended regs for bring-up diagnostics (delay config) */
	if (p->phy_id == PHY_ID_YT8531S) {
		if (mcs_phy_ext_read(p, PHY_ADDR_DEFAULT,
				     YT8521_CHIP_CONFIG_REG, &v))
			v = 0xffff;
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "ext 0xA001 = 0x%04x (bit8 rxc-dly)\n", v);
		if (mcs_phy_ext_read(p, PHY_ADDR_DEFAULT,
				     YT8521_RGMII_CONFIG1_REG, &v))
			v = 0xffff;
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "ext 0xA003 = 0x%04x (rx/ge-tx dly)\n", v);
	}
	return n;
}
static DEVICE_ATTR_RO(mdio);

static struct attribute *mcs_net_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_mdio.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mcs_net);

/* ---------------- probe / remove ----------------------------------------- */

static void mcs_step(struct mcs_net_priv *p, int idx, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(p->steps[idx], sizeof(p->steps[idx]), fmt, ap);
	va_end(ap);
	/* probe progress trace: the last line printed is where it hangs/fails */
	dev_emerg(p->dev, "step%d: %s\n", idx, p->steps[idx]);
}

static int mcs_net_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mcs_net_priv *p;
	u64 mac_base;
	u32 irq_intid = GMAC1_MACIRQ_INTID;
	u16 id1 = 0, id2 = 0, bmcr = 0;
	struct pinctrl *pc;
	int ret;

	pr_emerg("[mcs_net] probe enter\n");
	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->dev = dev;
	platform_set_drvdata(pdev, p);

	/* 1. clocks: named get + enable, one by one (deliberately NOT
	 * clk_bulk_get_all: the bulk path crashed on this kernel with a
	 * corrupted array pointer - see Oops 2026-09-22). */
	p->clk_stmmaceth = devm_clk_get(dev, "stmmaceth");
	p->clk_aclk = devm_clk_get(dev, "aclk_mac");
	p->clk_pclk = devm_clk_get(dev, "pclk_mac");
	p->clk_mac_speed = devm_clk_get(dev, "clk_mac_speed");
	p->clk_xpcs = devm_clk_get_optional(dev, "pclk_xpcs");
	p->clk_xpcs_eee = devm_clk_get_optional(dev, "clk_xpcs_eee");
	if (IS_ERR(p->clk_stmmaceth) || IS_ERR(p->clk_aclk) ||
	    IS_ERR(p->clk_pclk) || IS_ERR(p->clk_mac_speed)) {
		mcs_step(p, 0, "clocks: get FAILED stmmaceth/aclk/pclk/speed: %ld/%ld/%ld/%ld",
			 IS_ERR(p->clk_stmmaceth) ? PTR_ERR(p->clk_stmmaceth) : 0,
			 IS_ERR(p->clk_aclk) ? PTR_ERR(p->clk_aclk) : 0,
			 IS_ERR(p->clk_pclk) ? PTR_ERR(p->clk_pclk) : 0,
			 IS_ERR(p->clk_mac_speed) ? PTR_ERR(p->clk_mac_speed) : 0);
		p->step_fail = 1;
		return -ENODEV;
	}

#define MCS_CLK_ON(c, name) do { \
	ret = clk_prepare_enable(c); \
	if (ret) { \
		mcs_step(p, 0, "clocks: enable %s FAILED (%d)", name, ret); \
		p->step_fail = 1; \
		return ret; \
	} \
	pr_emerg("[mcs_net] clk %s ON (%lu Hz)\n", name, clk_get_rate(c)); \
} while (0)

	MCS_CLK_ON(p->clk_stmmaceth, "stmmaceth");
	MCS_CLK_ON(p->clk_aclk, "aclk_mac");
	MCS_CLK_ON(p->clk_pclk, "pclk_mac");
	MCS_CLK_ON(p->clk_mac_speed, "clk_mac_speed");
	if (!IS_ERR_OR_NULL(p->clk_xpcs))
		MCS_CLK_ON(p->clk_xpcs, "pclk_xpcs");
	if (!IS_ERR_OR_NULL(p->clk_xpcs_eee))
		MCS_CLK_ON(p->clk_xpcs_eee, "clk_xpcs_eee");
#undef MCS_CLK_ON
	mcs_step(p, 0, "clocks: OK (named, held on)");

	/* 1000Mbps default: clk_mac_speed = 125MHz (dwmac-rk set_rgmii_speed),
	 * matching the advertised 1000M autoneg. The 1s link poll re-targets
	 * it to the RESOLVED speed (100 -> 25MHz, 10 -> 2.5MHz) - see
	 * mcs_ctrl_write_link(); a stale rate breaks the RGMII interface
	 * (TX and/or RX) while occasional frames still pass. */
	clk_set_rate(p->clk_mac_speed, 125000000);
	mcs_step(p, 1, "clk_mac_speed: 125MHz (poll re-targets per speed)");

	/* 2. CRU reset release */
	p->rst = devm_reset_control_get_exclusive(dev, "stmmaceth");
	if (IS_ERR(p->rst)) {
		mcs_step(p, 2, "reset: get FAILED");
		p->step_fail = 3;
		return PTR_ERR(p->rst);
	}
	reset_control_deassert(p->rst);
	mcs_step(p, 2, "reset: deasserted");

	/* 3. RGMII pinmux (dt core already applies "default"; reselect is harmless) */
	pc = devm_pinctrl_get(dev);
	if (IS_ERR(pc)) {
		mcs_step(p, 3, "pinmux: get FAILED");
		p->step_fail = 4;
		return PTR_ERR(pc);
	}
	ret = pinctrl_select_state(pc,
			pinctrl_lookup_state(pc, PINCTRL_STATE_DEFAULT));
	if (ret) {
		mcs_step(p, 3, "pinmux: select FAILED");
		p->step_fail = 4;
		return ret;
	}
	mcs_step(p, 3, "pinmux: RGMII group applied");

	/* 4. GRF RGMII delays (rk3568_set_to_rgmii sequence) */
	p->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(p->grf)) {
		mcs_step(p, 4, "grf: lookup FAILED");
		p->step_fail = 5;
		return PTR_ERR(p->grf);
	}
	of_property_read_u32(np, "tx_delay", &p->tx_delay);
	of_property_read_u32(np, "rx_delay", &p->rx_delay);
	regmap_write(p->grf, RK3568_GRF_GMAC1_CON1, RK3568_GMAC1_CON1_RGMII);
	regmap_write(p->grf, RK3568_GRF_GMAC1_CON0,
		     RK3568_GMAC1_CON0_DLY(p->tx_delay, p->rx_delay));
	mcs_step(p, 4, "grf: RGMII tx_delay=0x%02x rx_delay=0x%02x",
		 p->tx_delay, p->rx_delay);

	/* 5. map the MAC MDIO page (the only MAC region the master touches) */
	ret = of_property_read_reg(np, 0, &mac_base, NULL);
	if (ret) {
		mcs_step(p, 5, "mac-io: no reg");
		p->step_fail = 6;
		return ret;
	}
	p->mac_io = devm_ioremap(dev, mac_base, SZ_4K);
	if (!p->mac_io) {
		mcs_step(p, 5, "mac-io: ioremap FAILED");
		p->step_fail = 6;
		return -ENOMEM;
	}

	/* 6. PHY reset (active-low, 20ms assert / 100ms settle, rtl8211f) */
	p->phy_reset = devm_gpiod_get_optional(dev, "phy-reset", GPIOD_OUT_HIGH);
	if (p->phy_reset) {
		msleep(20);
		gpiod_set_value(p->phy_reset, 0);
		msleep(100);
	}
	mcs_step(p, 5, "mac-io+phy-reset: OK (gpio%s)",
		 p->phy_reset ? "" : " none");

	/* 6.5 bus probe: read MAC version reg BEFORE touching MDIO.
	 * If the GMAC1 clock domain is dead this read hangs the bus:
	 * "about to read MAC version" being the LAST printed line proves it.
	 */
	{
		u32 ver;

		pr_emerg("[mcs_net] about to read MAC version reg (bus probe, hang here = gmac1 clock off)\n");
		/* dwmac4 core version lives at 0x110 (hwif.h GMAC4_VERSION);
		 * 0x20 is not the version register on this IP (read 0). */
		ver = readl(p->mac_io + 0x110);
		pr_emerg("[mcs_net] MAC version reg = 0x%08x (0x00000051 expected)\n", ver);
	}

	/* 7. MDIO: probe PHY, force 100M FD */
	pr_emerg("[mcs_net] about to read MDIO (hang here = clock/pinmux wrong)\n");
	ret = mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_PHYSID1, &id1) ?:
	      mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_PHYSID2, &id2);
	if (ret) {
		mcs_step(p, 6, "mdio: PHY@%d not responding", PHY_ADDR_DEFAULT);
		p->step_fail = 7;
		return -ENODEV;
	}
	p->phy_id = (id1 << 16) | id2;
	mcs_phy_autoneg_1000m(p);
	mcs_phy_rgmii_delay_grf_only(p);
	mcs_mdio_read(p, PHY_ADDR_DEFAULT, MII_BMCR, &bmcr);
	mcs_step(p, 6, "phy: id=0x%08x bmcr=0x%04x (aneg 1000M restarted)",
		 p->phy_id, bmcr);

	/* 8. cross-check the dts interrupt contract (informational only: the
	 * slave core owns and configures INTID 64 itself via ArchHwiCreate) */
	of_property_read_u32(np, "oh_rt,irq-intid", &irq_intid);
	if (irq_intid != GMAC1_MACIRQ_INTID)
		dev_warn(dev, "unexpected irq-intid %u, the slave expects %u\n",
			 irq_intid, GMAC1_MACIRQ_INTID);

	/* 9. shared control block (device-mapped: visible to the cacheless
	 * slave without any cache maintenance on this side) */
	p->ctrl = devm_ioremap(dev, NET_CTRL_PHYS, NET_CTRL_SIZE);
	if (!p->ctrl) {
		mcs_step(p, 7, "ctrl: ioremap FAILED");
		p->step_fail = 8;
		return -ENOMEM;
	}
	{
		struct NetCtrl __iomem *c = p->ctrl;

		writel(0, &c->master_state);
		writel(0, &c->valid);
		writel(NET_CTRL_MAGIC, &c->magic);
		writel(NET_CTRL_VERSION, &c->version);
		writel(p->phy_id, &c->phy_id);

		/* first link sample, then hand the port over as READY */
		p->link_up = mcs_phy_poll_link(p);
		mcs_ctrl_write_link(p, p->link_up);
		writel(NET_STATE_READY, &c->master_state);
	}
	mcs_step(p, 7, "ctrl: READY at 0x%llx, link=%u",
		 (u64)NET_CTRL_PHYS, p->link_up);

	INIT_DELAYED_WORK(&p->link_work, mcs_link_work);
	schedule_delayed_work(&p->link_work, msecs_to_jiffies(1000));
	mcs_step(p, 8, "link poll: 1s work started");

	dev_info(dev, "GMAC1 handed over to slave core: ctrl 0x%llx READY\n",
		 (u64)NET_CTRL_PHYS);
	return 0;
}

static void mcs_net_remove(struct platform_device *pdev)
{
	struct mcs_net_priv *p = platform_get_drvdata(pdev);
	struct NetCtrl __iomem *c = p->ctrl;

	cancel_delayed_work_sync(&p->link_work);
	if (c)
		writel(NET_STATE_OFFLINE, &c->master_state);
	/*
	 * Clocks/reset/GPIO mappings are devm-managed and stay prepared for
	 * the slave; on rmmod the whole handover goes away with them. The
	 * macirq is owned by the slave core (per-core GICD ownership) and is
	 * not touched here.
	 */
}

static const struct of_device_id mcs_net_of_match[] = {
	{ .compatible = "oh_rt,mcs-net-gmac1" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcs_net_of_match);

static struct platform_driver mcs_net_driver = {
	.probe  = mcs_net_probe,
	.remove_new = mcs_net_remove,
	.driver = {
		.name = "mcs-net-gmac1",
		.of_match_table = mcs_net_of_match,
		.dev_groups = mcs_net_groups,
	},
};
static int __init mcs_net_init(void)
{
	int ret;

	pr_emerg("[mcs_net] module init: registering driver\n");
	ret = platform_driver_register(&mcs_net_driver);
	pr_emerg("[mcs_net] driver registered: %d (probe may still be running)\n", ret);
	return ret;
}

static void __exit mcs_net_exit(void)
{
	platform_driver_unregister(&mcs_net_driver);
}

module_init(mcs_net_init);
module_exit(mcs_net_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RK3568 GMAC1 handover driver for the MCS AMP slave core");
MODULE_AUTHOR("mcs port");
