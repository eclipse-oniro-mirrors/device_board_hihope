// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 *
 * mcs_uart.c - RK3568 UART3 handover driver for the MCS AMP slave core.
 *
 * One module, one shot: wait until the Linux 8250_dw driver has probed
 * the handed-over UART, then release the device from that driver - the
 * in-kernel equivalent of
 *
 *   echo fe670000.serial > /sys/bus/platform/drivers/dw-apb-uart/unbind
 *
 * Unlike the GMAC1/CAN handovers (../net, ../can) the master provides
 * no resource service for a UART: no PHY, no clock retarget, no shared
 * control block, no runtime state. The one thing only the master can
 * do is the pad bring-up the driver core performs while probing:
 *
 *   Root-caused on DAYU200 (issue 1014, 2026-10): with &uart3
 *   status="disabled" the slave configured the GRF IOMUX/PULL bits of
 *   the uart3m1 group with raw register writes - and the GPIO3_C0
 *   (RX) input path stayed dead while TX already looped back. The C0
 *   input path needs initialization beyond the GRF register values
 *   themselves, performed by the probe frameworks (pinctrl default
 *   state et al.) while dw-apb-uart binds fe670000.serial. Any UART
 *   controller reset issued afterwards (SRR or CRU soft reset) undoes
 *   it.
 *
 * Handover flow ("probe first, then evict"):
 *
 *   1. dts: &uart3 { status = "okay"; }   (../dts/rk3568-ohrt-mcs.dtsi)
 *   2. boot: dw-apb-uart probes fe670000.serial, the driver core
 *      applies the uart3m1_xfer pin group, the pads come alive
 *   3. this module: wait for that bind, then device_release_driver()
 *      - the same guard and detach as the sysfs unbind path; the pin
 *      default state outlives the driver detach (verified on HW)
 *   4. the slave (kernel/liteos_m targets/rk3568_m board/uart.c)
 *      brings up CRU clock gating, the xin24m parent clock, GRF
 *      pinmux and GIC INTID 151 on its own and must NOT reset the
 *      controller (no SRR, no CRU soft reset) - a reset after step 2
 *      re-breaks the pads
 *
 * The module holds nothing after the handover: rmmod is harmless for
 * the platform and a re-bind would need an explicit bus probe trigger
 * that nothing on this platform issues.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#define DRV_NAME		"mcs-uart"

/* The port handed to the slave: RK3568 UART3 (fe670000, uart3m1 group
 * on the 20-pin header). Platform device name = <reg base>.serial. */
#define MCS_UART_DEF_PORT	"fe670000.serial"

/* The Linux serial driver that must probe it first
 * (drivers/tty/serial/8250/8250_dw.c registers as "dw-apb-uart"). */
#define MCS_UART_LINUX_DRV	"dw-apb-uart"

/* poll periods in ms: platform device creation, driver bind */
#define DEV_POLL_MS		1000
#define BIND_POLL_MS		200

static char *mcs_uart_port = MCS_UART_DEF_PORT;
module_param_named(port, mcs_uart_port, charp, 0444);
MODULE_PARM_DESC(port, "platform device name of the UART to hand over (default " MCS_UART_DEF_PORT ")");

static unsigned int mcs_uart_timeout_sec = 30;
module_param_named(timeout_sec, mcs_uart_timeout_sec, uint, 0444);
MODULE_PARM_DESC(timeout_sec, "max wait in seconds per step (default 30)");

/* handover steps, mirrored in the step logs (see mcs_can for the
 * family convention): 0=dev 1=bind 2=release */
static const char *const mcs_uart_steps[] = { "dev", "bind", "release" };

/*
 * Find the platform device for the port (reference held for the
 * caller). bus_find_device_by_name() is a static inline over the same
 * pair.
 */
static struct device *mcs_uart_find_dev(void)
{
	return bus_find_device(&platform_bus_type, NULL, mcs_uart_port,
			       device_match_name);
}

/*
 * Evict the port from dw-apb-uart. Same guard and detach as the sysfs
 * unbind (drivers/base/bus.c unbind_store):
 *   - not bound yet            -> -EAGAIN (caller keeps polling)
 *   - bound to another driver  -> -EINVAL (refuse; something is off)
 *   - device_release_driver() takes dev->mutex, so a probe still in
 *     flight completes first, then ->remove runs and the driver
 *     detaches with the pin state left applied
 *   - still bound afterwards   -> -EIO (unexpected, reported)
 */
static int mcs_uart_evict(struct device *dev)
{
	struct device_driver *drv = READ_ONCE(dev->driver);

	if (!drv)
		return -EAGAIN;

	if (strcmp(drv->name, MCS_UART_LINUX_DRV)) {
		pr_err("%s: %s is bound to '%s', expected '%s' - refusing\n",
		       DRV_NAME, dev_name(dev), drv->name, MCS_UART_LINUX_DRV);
		return -EINVAL;
	}

	device_release_driver(dev);

	return READ_ONCE(dev->driver) ? -EIO : 0;
}

static int __init mcs_uart_init(void)
{
	unsigned long deadline;
	struct device *dev;
	int ret;

	pr_emerg("[%s] handover of '%s' from '%s'\n", DRV_NAME,
		 mcs_uart_port, MCS_UART_LINUX_DRV);

	/* step 0 'dev': the platform device must exist (dts okay) */
	deadline = jiffies + msecs_to_jiffies(mcs_uart_timeout_sec * 1000);
	dev = mcs_uart_find_dev();
	while (!dev) {
		if (time_after(jiffies, deadline)) {
			pr_err("[%s] step %s: TIMEOUT - no such device; check "
			       "&uart3 { status = \"okay\"; } in the dts\n",
			       DRV_NAME, mcs_uart_steps[0]);
			return -ENODEV;
		}
		msleep(DEV_POLL_MS);
		dev = mcs_uart_find_dev();
	}
	pr_emerg("[%s] step %s: OK (%s present)\n", DRV_NAME,
		 mcs_uart_steps[0], dev_name(dev));

	/* step 1 'bind': wait for the dw-apb-uart probe (the probe
	 * frameworks bring the pads up); step 2 'release': evict it */
	deadline = jiffies + msecs_to_jiffies(mcs_uart_timeout_sec * 1000);
	for (;;) {
		ret = mcs_uart_evict(dev);
		if (ret != -EAGAIN)
			break;
		if (time_after(jiffies, deadline)) {
			pr_err("[%s] step %s: TIMEOUT - '%s' never bound '%s'\n",
			       DRV_NAME, mcs_uart_steps[1], mcs_uart_port,
			       MCS_UART_LINUX_DRV);
			ret = -ENODEV;
			break;
		}
		msleep(BIND_POLL_MS);
	}
	put_device(dev);

	if (ret)
		return ret;

	pr_emerg("[%s] step %s: OK ('%s' probed, pads up)\n", DRV_NAME,
		 mcs_uart_steps[1], mcs_uart_port);
	pr_emerg("[%s] step %s: OK - '%s' free for the AMP slave "
		 "(pads stay configured)\n", DRV_NAME, mcs_uart_steps[2],
		 mcs_uart_port);
	return 0;
}

static void __exit mcs_uart_exit(void)
{
	/*
	 * Nothing to undo: no resource held, no ctrl block, no runtime
	 * state. The released device stays driver-less for the slave.
	 */
	pr_emerg("[%s] exited ('%s' remains free for the AMP slave)\n",
		 DRV_NAME, mcs_uart_port);
}

module_init(mcs_uart_init);
module_exit(mcs_uart_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RK3568 UART3 handover driver for the MCS AMP slave core");
MODULE_AUTHOR("mcs port");
