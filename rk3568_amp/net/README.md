# mcs_net - RK3568 GMAC1 handover driver (master side)

Out-of-tree Linux module (Linux 6.6, OpenHarmony kernel tree) that prepares
RK3568 **GMAC1** (`0xFE010000`, RGMII, YT8531S PHY) for the MCS AMP slave
core and keeps the platform alive while **LiteOS-M on CPU3** owns the whole
data plane. The slave exposes a protocol-agnostic frame-level NIC API
(`Net*`, see the slave-side `device/board/rockchip/rk3568/liteos_new/board/
driver/net/net.h`); any link-layer protocol (EtherCAT, proprietary, raw
Ethernet) can run on top of it.

Master responsibilities (probe, then hands off):

| step | action |
|------|--------|
| 0 | enable + hold all named GMAC1 clocks (survives unbind/runtime-pm) |
| 1 | `clk_mac_speed` = 125 MHz (RGMII reference; retargeted 2.5/25/125 MHz by the 1s link poll as the negotiated speed changes) |
| 2 | release the CRU `stmmaceth` soft reset |
| 3 | apply the RGMII pinmux group |
| 4 | program GRF RGMII delays (`tx_delay`/`rx_delay` dts properties) |
| 5 | ioremap the MAC MDIO page, toggle the PHY reset GPIO |
| 6 | PHY bring-up via MDIO (YT8531S: soft reset, advertise 10/100/1000, restart autoneg; PHY-internal RGMII delays stay at strap - GRF owns the delays) |
| 7 | publish the shared control block (`net_ctrl.h`) at `0x740ff000`: magic/version/`master_state=READY`, then handshake with the slave |
| 8 | start the 1 s link-poll delayed work (BMSR double-read, speed/duplex resolution, ctrl block refresh) |

Not owned by the master (by design):

- **Interrupts**: GMAC1 macirq (GIC SPI 32 -> INTID 64) is configured
  end-to-end by the slave (`ArchHwiCreate`, per-core GICD ownership).
  The master only cross-checks the dts `oh_rt,irq-intid` value (warn).
- **Data path**: DMA/MTL/MAC registers are slave-exclusive after probe.

## Files

| file | purpose |
|------|---------|
| `mcs_net.c` | the module |
| `net_ctrl.h` | shared control block contract (mirror of the slave copy) |
| `Makefile` | out-of-tree `obj-m` build |
| `build_mcsnet.sh` | build + sign flow against the OpenHarmony kernel tree |
| `rk3568-mcs-net.dtsi` | dts fragment: `&gmac1 { status = "disabled"; }` + the `mcs-net-gmac1` node |
| `USER_GUIDE.md` | end-to-end usage guide (master ops + slave `Net*` API) |

## Quick start

```sh
# 1) integrate the dts fragment into the board dts and flash boot_linux
# 2) build the module against a built OpenHarmony tree
export CODE=/path/to/openharmony        # full source tree root
./build_mcsnet.sh                       # produces signed mcs_net.ko
# 3) on the board (Linux up, slave firmware loaded on CPU3)
hdc file send mcs_net.ko /data
insmod /data/mcs_net.ko                 # dmesg shows step0..step8 then READY
# 4) diagnostics
cat /sys/devices/platform/mcs-net-gmac1/state
cat /sys/devices/platform/mcs-net-gmac1/mdio
```

See `USER_GUIDE.md` for the full document.
