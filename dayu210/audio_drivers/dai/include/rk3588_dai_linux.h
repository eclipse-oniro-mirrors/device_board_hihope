/*
 * Copyright (C) 2022 HiHope Open Source Organization .
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */

#ifndef RK3588_DAI_LINUX_H
#define RK3588_DAI_LINUX_H

#include <sound/dmaengine_pcm.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */
/*
 * CKR
 * clock generation register
 */
#define I2S_CKR_TRCM_SHIFT	28
#define I2S_CKR_TRCM(x)	((x) << I2S_CKR_TRCM_SHIFT)
#define I2S_CKR_TRCM_TXRX	(0 << I2S_CKR_TRCM_SHIFT)
#define I2S_CKR_TRCM_TXONLY	(1 << I2S_CKR_TRCM_SHIFT)
#define I2S_CKR_TRCM_RXONLY	(2 << I2S_CKR_TRCM_SHIFT)
#define I2S_CKR_TRCM_MASK	(3 << I2S_CKR_TRCM_SHIFT)
#define I2S_CKR_RSD_SHIFT	8
#define I2S_CKR_RSD(x)		(((x) - 1) << I2S_CKR_RSD_SHIFT)
#define I2S_CKR_RSD_MASK	(0xff << I2S_CKR_RSD_SHIFT)
#define I2S_CKR_TSD_SHIFT	0
#define I2S_CKR_TSD(x)		(((x) - 1) << I2S_CKR_TSD_SHIFT)
#define I2S_CKR_TSD_MASK	(0xff << I2S_CKR_TSD_SHIFT)

#define I2S_TXCR_TFS_SHIFT	5
#define I2S_TXCR_TFS_I2S	(0 << I2S_TXCR_TFS_SHIFT)
#define I2S_TXCR_TFS_PCM	(1 << I2S_TXCR_TFS_SHIFT)
#define I2S_TXCR_TFS_TDM_PCM	(2 << I2S_TXCR_TFS_SHIFT)
#define I2S_TXCR_TFS_TDM_I2S	(3 << I2S_TXCR_TFS_SHIFT)
#define I2S_TXCR_TFS_MASK	(3 << I2S_TXCR_TFS_SHIFT)
#define I2S_TXCR_CSR_SHIFT	15
#define I2S_TXCR_CSR(x)		((x) << I2S_TXCR_CSR_SHIFT)
#define I2S_TXCR_CSR_MASK	(3 << I2S_TXCR_CSR_SHIFT)
#define I2S_TXCR_VDW_SHIFT	0
#define I2S_TXCR_VDW(x)		(((x) - 1) << I2S_TXCR_VDW_SHIFT)
#define I2S_TXCR_VDW_MASK	(0x1f << I2S_TXCR_VDW_SHIFT)

#define I2S_RXCR_CSR_SHIFT	15
#define I2S_RXCR_CSR(x)		((x) << I2S_RXCR_CSR_SHIFT)
#define I2S_RXCR_CSR_MASK	(3 << I2S_RXCR_CSR_SHIFT)
#define I2S_RXCR_VDW_SHIFT	0
#define I2S_RXCR_VDW(x)		(((x) - 1) << I2S_RXCR_VDW_SHIFT)
#define I2S_RXCR_VDW_MASK	(0x1f << I2S_RXCR_VDW_SHIFT)

/*
 * DMACR
 * DMA control register
 */
#define I2S_DMACR_RDE_SHIFT	24
#define I2S_DMACR_RDE_DISABLE	(0 << I2S_DMACR_RDE_SHIFT)
#define I2S_DMACR_RDE_ENABLE	(1 << I2S_DMACR_RDE_SHIFT)
#define I2S_DMACR_RDL_SHIFT	16
#define I2S_DMACR_RDL(x)	(((x) - 1) << I2S_DMACR_RDL_SHIFT)
#define I2S_DMACR_RDL_MASK	(0x1f << I2S_DMACR_RDL_SHIFT)
#define I2S_DMACR_TDE_SHIFT	8
#define I2S_DMACR_TDE_DISABLE	(0 << I2S_DMACR_TDE_SHIFT)
#define I2S_DMACR_TDE_ENABLE	(1 << I2S_DMACR_TDE_SHIFT)
#define I2S_DMACR_TDL_SHIFT	0
#define I2S_DMACR_TDL(x)	((x) << I2S_DMACR_TDL_SHIFT)
#define I2S_DMACR_TDL_MASK	(0x1f << I2S_DMACR_TDL_SHIFT)

/*
 * CLR
 * clear SCLK domain logic register
 */
#define I2S_CLR_RXC	BIT(1)
#define I2S_CLR_TXC	BIT(0)


/*
 * XFER
 * Transfer start register
 */
#define I2S_XFER_RXS_SHIFT	1
#define I2S_XFER_RXS_STOP	(0 << I2S_XFER_RXS_SHIFT)
#define I2S_XFER_RXS_START	(1 << I2S_XFER_RXS_SHIFT)
#define I2S_XFER_TXS_SHIFT	0
#define I2S_XFER_TXS_STOP	(0 << I2S_XFER_TXS_SHIFT)
#define I2S_XFER_TXS_START	(1 << I2S_XFER_TXS_SHIFT)

/*
 * CLKDIV
 * Mclk div register
 */
#define I2S_CLKDIV_TXM_SHIFT	0
#define I2S_CLKDIV_TXM(x)		(((x) - 1) << I2S_CLKDIV_TXM_SHIFT)
#define I2S_CLKDIV_TXM_MASK	(0xff << I2S_CLKDIV_TXM_SHIFT)
#define I2S_CLKDIV_RXM_SHIFT	8
#define I2S_CLKDIV_RXM(x)		(((x) - 1) << I2S_CLKDIV_RXM_SHIFT)
#define I2S_CLKDIV_RXM_MASK	(0xff << I2S_CLKDIV_RXM_SHIFT)



/* channel select */
#define I2S_CSR_SHIFT	15
#define I2S_CHN_2	(0 << I2S_CSR_SHIFT)
#define I2S_CHN_4	(1 << I2S_CSR_SHIFT)
#define I2S_CHN_6	(2 << I2S_CSR_SHIFT)
#define I2S_CHN_8	(3 << I2S_CSR_SHIFT)


/* I2S REGS */
#define I2S_TXCR	(0x0000)
#define I2S_RXCR	(0x0004)
#define I2S_CKR		(0x0008)
#define I2S_TXFIFOLR	(0x000c)
#define I2S_DMACR	(0x0010)
#define I2S_INTCR	(0x0014)
#define I2S_INTSR	(0x0018)
#define I2S_XFER	(0x001c)
#define I2S_CLR		(0x0020)
#define I2S_TXDR	(0x0024)
#define I2S_RXDR	(0x0028)
#define I2S_RXFIFOLR	(0x002c)
#define I2S_TDM_TXCR	(0x0030)
#define I2S_TDM_RXCR	(0x0034)
#define I2S_CLKDIV	(0x0038)

#define DEFAULT_MCLK_FS				256
#define CH_GRP_MAX				4  /* The max channel 8 / 2 */
#define MULTIPLEX_CH_MAX			10
#define CLK_PPM_MIN				(-1000)
#define CLK_PPM_MAX				(1000)
struct txrx_config {
	u32 addr;
	u32 reg;
	u32 txonly;
	u32 rxonly;
};

struct rk_i2s_soc_data {
	u32 softrst_offset;
	u32 grf_reg_offset;
	u32 grf_shift;
	int config_count;
	const struct txrx_config *configs;
	int (*init)(struct device *dev, u32 addr);
};

struct rk3588_i2s_tdm_dev {
	struct device *dev;
	struct clk *hclk;
	struct clk *mclk_tx;
	struct clk *mclk_rx;
	/* The mclk_tx_src is parent of mclk_tx */
	struct clk *mclk_tx_src;
	/* The mclk_rx_src is parent of mclk_rx */
	struct clk *mclk_rx_src;
	/*
	 * The mclk_root0 and mclk_root1 are root parent and supplies for
	 * the different FS.
	 *
	 * e.g:
	 * mclk_root0 is VPLL0, used for FS=48000Hz
	 * mclk_root0 is VPLL1, used for FS=44100Hz
	 */
	struct clk *mclk_root0;
	struct clk *mclk_root1;
	struct regmap *regmap;
	struct regmap *grf;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct reset_control *tx_reset;
	struct reset_control *rx_reset;
	const struct rk_i2s_soc_data *soc_data;
	void __iomem *cru_base;
	bool is_master_mode;
	bool io_multiplex;
	bool mclk_calibrate;
	bool tdm_mode;
	bool tdm_fsync_half_frame;
	unsigned int mclk_rx_freq;
	unsigned int mclk_tx_freq;
	unsigned int mclk_root0_freq;
	unsigned int mclk_root1_freq;
	unsigned int mclk_root0_initial_freq;
	unsigned int mclk_root1_initial_freq;
	unsigned int bclk_fs;
	unsigned int clk_trcm;
	unsigned int i2s_sdis[CH_GRP_MAX];
	unsigned int i2s_sdos[CH_GRP_MAX];
	int clk_ppm;
	int tx_reset_id;
	int rx_reset_id;
	atomic_t refcount;
	spinlock_t lock; /* xfer lock */
	bool txStart;
    bool rxStart;
};
#ifdef __cplusplus
#if __cplusplus
        }
#endif
#endif /* __cplusplus */

#endif /* RK3588_DAI_LINUX_H */
