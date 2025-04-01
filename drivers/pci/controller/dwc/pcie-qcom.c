// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm PCIe root complex driver
 *
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 * Copyright 2015 Linaro Limited.
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Author: Stanimir Varbanov <svarbanov@mm-sol.com>
 */

#include <linux/clk.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/nvmem-consumer.h>

#include "../../pci.h"
#include "pcie-designware.h"

/* PARF registers */
#define PARF_SYS_CTRL				0x00
#define PARF_DB_CTRL				0x10
#define PARF_PM_CTRL				0x20
#define PARF_MHI_CLOCK_RESET_CTRL		0x174
#define PARF_MHI_BASE_ADDR_LOWER		0x178
#define PARF_MHI_BASE_ADDR_UPPER		0x17c
#define PARF_DEBUG_INT_EN			0x190
#define PARF_AXI_MSTR_RD_HALT_NO_WRITES		0x1a4
#define PARF_AXI_MSTR_WR_ADDR_HALT		0x1a8
#define PARF_Q2A_FLUSH				0x1ac
#define PARF_LTSSM				0x1b0
#define PARF_CFG_BITS				0x210
#define PARF_INT_ALL_STATUS			0x224
#define PARF_INT_ALL_CLEAR			0x228
#define PARF_INT_ALL_MASK			0x22c
#define PARF_SLV_ADDR_MSB_CTRL			0x2c0
#define PARF_DBI_BASE_ADDR			0x350
#define PARF_DBI_BASE_ADDR_HI			0x354
#define PARF_SLV_ADDR_SPACE_SIZE		0x358
#define PARF_SLV_ADDR_SPACE_SIZE_HI		0x35c
#define PARF_ATU_BASE_ADDR			0x634
#define PARF_ATU_BASE_ADDR_HI			0x638
#define PARF_SRIS_MODE				0x644
#define PARF_DEBUG_CNT_PM_LINKST_IN_L2		0xc04
#define PARF_DEBUG_CNT_PM_LINKST_IN_L1		0xc0c
#define PARF_DEBUG_CNT_PM_LINKST_IN_L0S		0xc10
#define PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L1	0xc84
#define PARF_DEBUG_CNT_AUX_CLK_IN_L1SUB_L2	0xc88
#define PARF_DEVICE_TYPE			0x1000
#define PARF_BDF_TO_SID_CFG			0x2c00

#define PCIE20_PARF_SYS_CTRL			0x00
#define MST_WAKEUP_EN				BIT(13)
#define SLV_WAKEUP_EN				BIT(12)
#define MSTR_ACLK_CGC_DIS			BIT(10)
#define SLV_ACLK_CGC_DIS			BIT(9)
#define CORE_CLK_CGC_DIS			BIT(6)
#define AUX_PWR_DET				BIT(4)
#define L23_CLK_RMV_DIS				BIT(2)
#define L1_CLK_RMV_DIS				BIT(1)

#define PCIE20_PARF_PM_CTRL			0x20
#define REQ_NOT_ENTR_L1				BIT(5)

#define PCIE20_PARF_PHY_CTRL			0x40
#define PHY_CTRL_PHY_TX0_TERM_OFFSET_MASK	GENMASK(20, 16)
#define PHY_CTRL_PHY_TX0_TERM_OFFSET(x)		((x) << 16)

#define PCIE20_PARF_PHY_REFCLK			0x4C
#define PHY_REFCLK_SSP_EN			BIT(16)
#define PHY_REFCLK_USE_PAD			BIT(12)

#define PCIE20_PARF_DBI_BASE_ADDR		0x168
#define PCIE20_PARF_SLV_ADDR_SPACE_SIZE		0x16C
#define PCIE20_PARF_MHI_CLOCK_RESET_CTRL	0x174
#define AHB_CLK_EN				BIT(0)
#define MSTR_AXI_CLK_EN				BIT(1)
#define BYPASS					BIT(4)

#define PARF_INT_ALL_STATUS			0x224
#define PARF_INT_ALL_CLEAR			0x228
#define PARF_INT_ALL_MASK			0x22c
#define PARF_STATUS				0x230

/* PARF_STATUS fields */
#define FLUSH_COMPLETED				BIT(9)
#define SW_CLEAR_FLUSH_MODE			BIT(10)
#define FLUSH_MODE				BIT(11)

/* PARF_INT_ALL_{STATUS/CLEAR/MASK} register fields */
#define PARF_INT_ALL_LINK_DOWN		BIT(1)
#define PARF_INT_ALL_LINK_UP		BIT(13)
#define PARF_INT_MSI_DEV_0_7		GENMASK(30, 23)

/* Virtual Channel registers and fields */
#define PCIE_TYPE0_VC_CAPABILITIES_REG_1 	0x14c
#define PCIE_TYPE0_RESOURCE_CON_REG_VC0 	0x15c
#define PCIE_TYPE0_RESOURCE_CON_REG_VC1 	0x168
#define PCIE_TYPE0_RESOURCE_STATUS_REG_VC1 	0x16c

#define EP_REG_BASE 				0x1E1DFFF
#define WINDOW_RANGE_MASK 			0x7FFFE

#define VC_EXT_VC_CNT_MASK 			GENMASK(2, 0)
#define VC_TC_MAP_VC_BIT1_MASK 			GENMASK(7, 1)
#define VC_ID_VC1_MASK 				GENMASK(26, 24)
#define VC_ENABLE_VC1_MASK 			BIT(31)
#define VC_NEGO_PENDING_VC1_MASK 		BIT(17)

#define VC_EXT_VC_CNT_OFFSET(x) 		((x) << 0)
#define VC_TC_MAP_VC_BIT1_OFFSET(x) 		((x) << 1)
#define VC_ID_VC1_OFFSET(x) 			((x) << 24)
#define VC_ENABLE_VC1_OFFSET(x) 		((x) << 31)
#define VC_NEGO_PENDING_VC1_OFFSET(x) 		((x) << 17)

#define PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT	0x178
#define PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2	0x1A8
#define PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2_MASK 0x1F
#define PCIE20_PARF_LTSSM			0x1B0
#define PCIE20_PARF_SID_OFFSET			0x234
#define PCIE20_PARF_BDF_TRANSLATE_CFG		0x24C
#define PCIE20_PARF_DEVICE_TYPE			0x1000
#define PCIE20_PARF_BDF_TO_SID_TABLE_N		0x2000

#define PCIE20_ELBI_SYS_CTRL			0x04
#define PCIE20_ELBI_SYS_CTRL_LT_ENABLE		BIT(0)

#define PCIE20_AXI_MSTR_RESP_COMP_CTRL0		0x818
#define CFG_REMOTE_RD_REQ_BRIDGE_SIZE_2K	0x4
#define CFG_REMOTE_RD_REQ_BRIDGE_SIZE_4K	0x5
#define PCIE20_AXI_MSTR_RESP_COMP_CTRL1		0x81c
#define CFG_BRIDGE_SB_INIT			BIT(0)

#define PCIE_CAP_SLOT_POWER_LIMIT_VAL		FIELD_PREP(PCI_EXP_SLTCAP_SPLV, \
						250)
#define PCIE_CAP_SLOT_POWER_LIMIT_SCALE		FIELD_PREP(PCI_EXP_SLTCAP_SPLS, \
						1)
#define PCIE_CAP_SLOT_VAL			(PCI_EXP_SLTCAP_ABP | \
						PCI_EXP_SLTCAP_PCP | \
						PCI_EXP_SLTCAP_MRLSP | \
						PCI_EXP_SLTCAP_AIP | \
						PCI_EXP_SLTCAP_PIP | \
						PCI_EXP_SLTCAP_HPS | \
						PCI_EXP_SLTCAP_HPC | \
						PCI_EXP_SLTCAP_EIP | \
						PCIE_CAP_SLOT_POWER_LIMIT_VAL | \
						PCIE_CAP_SLOT_POWER_LIMIT_SCALE)

#define PCIE20_PARF_Q2A_FLUSH			0x1AC

#define PCIE20_MISC_CONTROL_1_REG		0x8BC
#define DBI_RO_WR_EN				1

#define PERST_DELAY_US				1000
#define FLUSH_TIMEOUT_US			1000

/* PARF registers */
#define PCIE20_PARF_PCS_DEEMPH			0x34
#define PCS_DEEMPH_TX_DEEMPH_GEN1(x)		((x) << 16)
#define PCS_DEEMPH_TX_DEEMPH_GEN2_3_5DB(x)	((x) << 8)
#define PCS_DEEMPH_TX_DEEMPH_GEN2_6DB(x)	((x) << 0)

#define PCIE20_PARF_PCS_SWING			0x38
#define PCS_SWING_TX_SWING_FULL(x)		((x) << 8)
#define PCS_SWING_TX_SWING_LOW(x)		((x) << 0)

#define PCIE20_PARF_CONFIG_BITS		0x50
#define PHY_RX0_EQ(x)				((x) << 24)

#define PCIE20_v3_PARF_SLV_ADDR_SPACE_SIZE	0x358
#define SLV_ADDR_SPACE_SZ			0x10000000

/* DBI registers */
#define PCIE20_PORT_LINK_CTRL_OFF		0x710
#define PCIE20_PORT_LINK_CTRL_OFF_MASK		0x3F0000
#define LINK_CAPABLE_OFFSET(x)			((x) << 16)
#define PCIE20_LANE_SKEW_OFF			0x714
#define PCIE20_LANE_SKEW_OFF_MASK		0xFF000000
#define PCIE20_MULTI_LANE_CONTROL_OFF		0x8C0
#define PCIE20_LINK_CONTROL_LINK_STATUS_REG	0x80
#define PCIE20_PARF_LTSSM_MASK			0x3F

/* RATEADAPT_VAL = 256 / ((NOC frequency / PCIe AXI frequency) - 1) */
/* RATEADAPT_VAL = 256 / ((342M / 240M) - 1) */
#define AGGR_NOC_PCIE_1LANE_RATEADAPT_VAL	0x200

#define SYSTEM_NOC_PCIE_RATEADAPT_BYPASS	0x1

#define PCIE20_LNK_CONTROL2_LINK_STATUS2	0xa0

#define DEVICE_TYPE_RC				0x4

#define QCOM_PCIE_2_1_0_MAX_SUPPLY	3
#define QCOM_PCIE_2_1_0_MAX_CLOCKS	5

#define QCOM_PCIE_CRC8_POLYNOMIAL (BIT(2) | BIT(1) | BIT(0))

#define PCIE_CAP_LINK_SPEED_MASK		GENMASK(19, 16)
#define PCIE_CAP_NEGO_LINK_WIDTH_MASK		GENMASK(25, 20)
#define PCIE_CAP_RETRAIN_LINK_MASK		GENMASK(5, 5)
#define PCIE_CAP_LINK_SPEED_SHIFT(x)		((x) >> 16)
#define PCIE_CAP_NEGO_LINK_WIDTH_SHIFT(x)	((x) >> 20)
#define PCIE_CAP_RETRAIN_LINK_OFFSET(x)		((x) << 5)
#define PCIE_CAP_TARGET_LINK_SPD_MASK		GENMASK(3, 0)
#define QCOM_IPQ9574_DEVICE_ID			0x1108
#define QCOM_IPQ5332_DEVICE_ID			0x1005

struct qcom_pcie_resources_2_1_0 {
	struct clk_bulk_data clks[QCOM_PCIE_2_1_0_MAX_CLOCKS];
	struct reset_control *pci_reset;
	struct reset_control *axi_reset;
	struct reset_control *ahb_reset;
	struct reset_control *por_reset;
	struct reset_control *phy_reset;
	struct reset_control *ext_reset;
	struct regulator_bulk_data supplies[QCOM_PCIE_2_1_0_MAX_SUPPLY];
};

struct qcom_pcie_resources_1_0_0 {
	struct clk *iface;
	struct clk *aux;
	struct clk *master_bus;
	struct clk *slave_bus;
	struct reset_control *core;
	struct regulator *vdda;
};

#define QCOM_PCIE_2_3_2_MAX_SUPPLY	2
struct qcom_pcie_resources_2_3_2 {
	struct clk *aux_clk;
	struct clk *master_clk;
	struct clk *slave_clk;
	struct clk *cfg_clk;
	struct regulator_bulk_data supplies[QCOM_PCIE_2_3_2_MAX_SUPPLY];
};

#define QCOM_PCIE_2_4_0_MAX_CLOCKS	4
struct qcom_pcie_resources_2_4_0 {
	struct clk_bulk_data clks[QCOM_PCIE_2_4_0_MAX_CLOCKS];
	int num_clks;
	struct reset_control *axi_m_reset;
	struct reset_control *axi_s_reset;
	struct reset_control *pipe_reset;
	struct reset_control *axi_m_vmid_reset;
	struct reset_control *axi_s_xpu_reset;
	struct reset_control *parf_reset;
	struct reset_control *phy_reset;
	struct reset_control *axi_m_sticky_reset;
	struct reset_control *pipe_sticky_reset;
	struct reset_control *pwr_reset;
	struct reset_control *ahb_reset;
	struct reset_control *phy_ahb_reset;
};

struct qcom_pcie_resources_2_3_3 {
	struct clk *iface;
	struct clk *axi_m_clk;
	struct clk *axi_s_clk;
	struct clk *ahb_clk;
	struct clk *aux_clk;
	struct reset_control *rst[7];
};

/* 6 clocks typically, 7 for sm8250 */
struct qcom_pcie_resources_2_7_0 {
	struct clk_bulk_data clks[12];
	int num_clks;
	struct regulator_bulk_data supplies[2];
	struct reset_control *pci_reset;
};

struct qcom_pcie_resources_2_9_0 {
	struct clk_bulk_data clks[5];
	struct reset_control *rst;
};

struct qcom_pcie_resources_1_27_0 {
	struct clk_bulk_data *clks;
	struct reset_control *rst;
	int num_clks;
};

union qcom_pcie_resources {
	struct qcom_pcie_resources_1_0_0 v1_0_0;
	struct qcom_pcie_resources_2_1_0 v2_1_0;
	struct qcom_pcie_resources_2_3_2 v2_3_2;
	struct qcom_pcie_resources_2_3_3 v2_3_3;
	struct qcom_pcie_resources_2_4_0 v2_4_0;
	struct qcom_pcie_resources_2_7_0 v2_7_0;
	struct qcom_pcie_resources_2_9_0 v2_9_0;
	struct qcom_pcie_resources_1_27_0 v1_27_0;
};

struct qcom_pcie;
static struct pci_ops qcom_pcie_bridge_ops;

struct qcom_pcie_ops {
	int (*get_resources)(struct qcom_pcie *pcie);
	int (*init)(struct qcom_pcie *pcie);
	int (*post_init)(struct qcom_pcie *pcie);
	void (*deinit)(struct qcom_pcie *pcie);
	void (*ltssm_enable)(struct qcom_pcie *pcie);
	int (*config_sid)(struct qcom_pcie *pcie);
};

struct qcom_pcie_cfg {
	const struct qcom_pcie_ops *ops;
};

struct qcom_pcie {
	struct dw_pcie *pci;
	void __iomem *parf;			/* DT parf */
	void __iomem *elbi;			/* DT elbi */
	resource_size_t parf_size;
	void __iomem *aggr_noc;
	void __iomem *system_noc;
	union qcom_pcie_resources res;
	struct phy *phy;
	struct gpio_desc *reset;
	const struct qcom_pcie_cfg *cfg;
	uint32_t axi_wr_addr_halt;
	uint32_t num_lanes;
	uint32_t domain;
	int global_irq;
	bool enable_vc;
};

#define to_qcom_pcie(x)		dev_get_drvdata((x)->dev)

static void qcom_ep_reset_assert(struct qcom_pcie *pcie)
{
	gpiod_set_value_cansleep(pcie->reset, 1);
	usleep_range(PERST_DELAY_US, PERST_DELAY_US + 500);
}

static void qcom_ep_reset_deassert(struct qcom_pcie *pcie)
{
	/* Ensure that PERST has been asserted for at least 100 ms */
	msleep(100);
	gpiod_set_value_cansleep(pcie->reset, 0);
	usleep_range(PERST_DELAY_US, PERST_DELAY_US + 500);
}

static int qcom_pcie_start_link(struct dw_pcie *pci)
{
	struct qcom_pcie *pcie = to_qcom_pcie(pci);

	/* Enable Link Training state machine */
	if (pcie->cfg->ops->ltssm_enable)
		pcie->cfg->ops->ltssm_enable(pcie);

	return 0;
}

static irqreturn_t qcom_pcie_global_irq_thread_fn(int irq, void *data)
{
	struct qcom_pcie *pcie = data;
	struct dw_pcie_rp *pp = &pcie->pci->pp;
	struct device *dev = pcie->pci->dev;
	u32 status = readl_relaxed(pcie->parf + PARF_INT_ALL_STATUS);

	writel_relaxed(status, pcie->parf + PARF_INT_ALL_CLEAR);

	if (FIELD_GET(PARF_INT_ALL_LINK_UP, status)) {
		dev_info(dev, "Received Link up event. Starting enumeration!\n");

		/* Rescan the bus to enumerate endpoint devices */
		pci_lock_rescan_remove();
		pci_rescan_bus(pp->bridge->bus);
		pci_unlock_rescan_remove();

	} else if (FIELD_GET(PARF_INT_ALL_LINK_DOWN, status)) {
		dev_info(dev, "Received Link down event\n");
		pci_host_bridge_handle_link_down(pp->bridge);

	} else
		dev_WARN_ONCE(dev, 1, "Received unknown event. INT_STATUS: 0x%08x\n",
			      status);

	return IRQ_HANDLED;
}

static void qcom_pcie_2_1_0_ltssm_enable(struct qcom_pcie *pcie)
{
	u32 val;

	/* enable link training */
	val = readl(pcie->elbi + PCIE20_ELBI_SYS_CTRL);
	val |= PCIE20_ELBI_SYS_CTRL_LT_ENABLE;
	writel(val, pcie->elbi + PCIE20_ELBI_SYS_CTRL);
}

static int qcom_pcie_get_resources_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	res->supplies[0].supply = "vdda";
	res->supplies[1].supply = "vdda_phy";
	res->supplies[2].supply = "vdda_refclk";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(res->supplies),
				      res->supplies);
	if (ret)
		return ret;

	res->clks[0].id = "iface";
	res->clks[1].id = "core";
	res->clks[2].id = "phy";
	res->clks[3].id = "aux";
	res->clks[4].id = "ref";

	/* iface, core, phy are required */
	ret = devm_clk_bulk_get(dev, 3, res->clks);
	if (ret < 0)
		return ret;

	/* aux, ref are optional */
	ret = devm_clk_bulk_get_optional(dev, 2, res->clks + 3);
	if (ret < 0)
		return ret;

	res->pci_reset = devm_reset_control_get_exclusive(dev, "pci");
	if (IS_ERR(res->pci_reset))
		return PTR_ERR(res->pci_reset);

	res->axi_reset = devm_reset_control_get_exclusive(dev, "axi");
	if (IS_ERR(res->axi_reset))
		return PTR_ERR(res->axi_reset);

	res->ahb_reset = devm_reset_control_get_exclusive(dev, "ahb");
	if (IS_ERR(res->ahb_reset))
		return PTR_ERR(res->ahb_reset);

	res->por_reset = devm_reset_control_get_exclusive(dev, "por");
	if (IS_ERR(res->por_reset))
		return PTR_ERR(res->por_reset);

	res->ext_reset = devm_reset_control_get_optional_exclusive(dev, "ext");
	if (IS_ERR(res->ext_reset))
		return PTR_ERR(res->ext_reset);

	res->phy_reset = devm_reset_control_get_exclusive(dev, "phy");
	return PTR_ERR_OR_ZERO(res->phy_reset);
}

static void qcom_pcie_deinit_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;

	clk_bulk_disable_unprepare(ARRAY_SIZE(res->clks), res->clks);
	reset_control_assert(res->pci_reset);
	reset_control_assert(res->axi_reset);
	reset_control_assert(res->ahb_reset);
	reset_control_assert(res->por_reset);
	reset_control_assert(res->ext_reset);
	reset_control_assert(res->phy_reset);

	writel(1, pcie->parf + PCIE20_PARF_PHY_CTRL);

	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
}

static int qcom_pcie_init_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	/* reset the PCIe interface as uboot can leave it undefined state */
	reset_control_assert(res->pci_reset);
	reset_control_assert(res->axi_reset);
	reset_control_assert(res->ahb_reset);
	reset_control_assert(res->por_reset);
	reset_control_assert(res->ext_reset);
	reset_control_assert(res->phy_reset);

	ret = regulator_bulk_enable(ARRAY_SIZE(res->supplies), res->supplies);
	if (ret < 0) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	ret = reset_control_deassert(res->ahb_reset);
	if (ret) {
		dev_err(dev, "cannot deassert ahb reset\n");
		goto err_deassert_ahb;
	}

	ret = reset_control_deassert(res->ext_reset);
	if (ret) {
		dev_err(dev, "cannot deassert ext reset\n");
		goto err_deassert_ext;
	}

	ret = reset_control_deassert(res->phy_reset);
	if (ret) {
		dev_err(dev, "cannot deassert phy reset\n");
		goto err_deassert_phy;
	}

	ret = reset_control_deassert(res->pci_reset);
	if (ret) {
		dev_err(dev, "cannot deassert pci reset\n");
		goto err_deassert_pci;
	}

	ret = reset_control_deassert(res->por_reset);
	if (ret) {
		dev_err(dev, "cannot deassert por reset\n");
		goto err_deassert_por;
	}

	ret = reset_control_deassert(res->axi_reset);
	if (ret) {
		dev_err(dev, "cannot deassert axi reset\n");
		goto err_deassert_axi;
	}

	return 0;

err_deassert_axi:
	reset_control_assert(res->por_reset);
err_deassert_por:
	reset_control_assert(res->pci_reset);
err_deassert_pci:
	reset_control_assert(res->phy_reset);
err_deassert_phy:
	reset_control_assert(res->ext_reset);
err_deassert_ext:
	reset_control_assert(res->ahb_reset);
err_deassert_ahb:
	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);

	return ret;
}

static int qcom_pcie_post_init_2_1_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_1_0 *res = &pcie->res.v2_1_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	struct device_node *node = dev->of_node;
	u32 val;
	int ret;

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(res->clks), res->clks);
	if (ret)
		return ret;

	if (of_device_is_compatible(node, "qcom,pcie-ipq8064") ||
	    of_device_is_compatible(node, "qcom,pcie-ipq8064-v2")) {
		writel(PCS_DEEMPH_TX_DEEMPH_GEN1(24) |
			       PCS_DEEMPH_TX_DEEMPH_GEN2_3_5DB(24) |
			       PCS_DEEMPH_TX_DEEMPH_GEN2_6DB(34),
		       pcie->parf + PCIE20_PARF_PCS_DEEMPH);
		writel(PCS_SWING_TX_SWING_FULL(120) |
			       PCS_SWING_TX_SWING_LOW(120),
		       pcie->parf + PCIE20_PARF_PCS_SWING);
		writel(PHY_RX0_EQ(4), pcie->parf + PCIE20_PARF_CONFIG_BITS);
	}

	if (of_device_is_compatible(node, "qcom,pcie-ipq8064")) {
		/* set TX termination offset */
		val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
		val &= ~PHY_CTRL_PHY_TX0_TERM_OFFSET_MASK;
		val |= PHY_CTRL_PHY_TX0_TERM_OFFSET(7);
		writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);
	}

	/* enable external reference clock */
	val = readl(pcie->parf + PCIE20_PARF_PHY_REFCLK);
	/* USE_PAD is required only for ipq806x */
	if (!of_device_is_compatible(node, "qcom,pcie-apq8064"))
		val &= ~PHY_REFCLK_USE_PAD;
	val |= PHY_REFCLK_SSP_EN;
	writel(val, pcie->parf + PCIE20_PARF_PHY_REFCLK);

	/* wait for clock acquisition */
	usleep_range(1000, 1500);

	/* Set the Max TLP size to 2K, instead of using default of 4K */
	writel(CFG_REMOTE_RD_REQ_BRIDGE_SIZE_2K,
	       pci->dbi_base + PCIE20_AXI_MSTR_RESP_COMP_CTRL0);
	writel(CFG_BRIDGE_SB_INIT,
	       pci->dbi_base + PCIE20_AXI_MSTR_RESP_COMP_CTRL1);

	return 0;
}

static int qcom_pcie_get_resources_1_0_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_0_0 *res = &pcie->res.v1_0_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;

	res->vdda = devm_regulator_get(dev, "vdda");
	if (IS_ERR(res->vdda))
		return PTR_ERR(res->vdda);

	res->iface = devm_clk_get(dev, "iface");
	if (IS_ERR(res->iface))
		return PTR_ERR(res->iface);

	res->aux = devm_clk_get(dev, "aux");
	if (IS_ERR(res->aux))
		return PTR_ERR(res->aux);

	res->master_bus = devm_clk_get(dev, "master_bus");
	if (IS_ERR(res->master_bus))
		return PTR_ERR(res->master_bus);

	res->slave_bus = devm_clk_get(dev, "slave_bus");
	if (IS_ERR(res->slave_bus))
		return PTR_ERR(res->slave_bus);

	res->core = devm_reset_control_get_exclusive(dev, "core");
	return PTR_ERR_OR_ZERO(res->core);
}

static void qcom_pcie_deinit_1_0_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_0_0 *res = &pcie->res.v1_0_0;

	reset_control_assert(res->core);
	clk_disable_unprepare(res->slave_bus);
	clk_disable_unprepare(res->master_bus);
	clk_disable_unprepare(res->iface);
	clk_disable_unprepare(res->aux);
	regulator_disable(res->vdda);
}

static int qcom_pcie_init_1_0_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_0_0 *res = &pcie->res.v1_0_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = reset_control_deassert(res->core);
	if (ret) {
		dev_err(dev, "cannot deassert core reset\n");
		return ret;
	}

	ret = clk_prepare_enable(res->aux);
	if (ret) {
		dev_err(dev, "cannot prepare/enable aux clock\n");
		goto err_res;
	}

	ret = clk_prepare_enable(res->iface);
	if (ret) {
		dev_err(dev, "cannot prepare/enable iface clock\n");
		goto err_aux;
	}

	ret = clk_prepare_enable(res->master_bus);
	if (ret) {
		dev_err(dev, "cannot prepare/enable master_bus clock\n");
		goto err_iface;
	}

	ret = clk_prepare_enable(res->slave_bus);
	if (ret) {
		dev_err(dev, "cannot prepare/enable slave_bus clock\n");
		goto err_master;
	}

	ret = regulator_enable(res->vdda);
	if (ret) {
		dev_err(dev, "cannot enable vdda regulator\n");
		goto err_slave;
	}

	return 0;
err_slave:
	clk_disable_unprepare(res->slave_bus);
err_master:
	clk_disable_unprepare(res->master_bus);
err_iface:
	clk_disable_unprepare(res->iface);
err_aux:
	clk_disable_unprepare(res->aux);
err_res:
	reset_control_assert(res->core);

	return ret;
}

static int qcom_pcie_post_init_1_0_0(struct qcom_pcie *pcie)
{
	/* change DBI base address */
	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		u32 val = readl(pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT);

		val |= BIT(31);
		writel(val, pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT);
	}

	return 0;
}

static void qcom_pcie_2_3_2_ltssm_enable(struct qcom_pcie *pcie)
{
	u32 val;

	/* enable link training */
	val = readl(pcie->parf + PCIE20_PARF_LTSSM);
	val |= BIT(8);
	writel(val, pcie->parf + PCIE20_PARF_LTSSM);
}

static int qcom_pcie_get_resources_2_3_2(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_2 *res = &pcie->res.v2_3_2;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	res->supplies[0].supply = "vdda";
	res->supplies[1].supply = "vddpe-3v3";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(res->supplies),
				      res->supplies);
	if (ret)
		return ret;

	res->aux_clk = devm_clk_get(dev, "aux");
	if (IS_ERR(res->aux_clk))
		return PTR_ERR(res->aux_clk);

	res->cfg_clk = devm_clk_get(dev, "cfg");
	if (IS_ERR(res->cfg_clk))
		return PTR_ERR(res->cfg_clk);

	res->master_clk = devm_clk_get(dev, "bus_master");
	if (IS_ERR(res->master_clk))
		return PTR_ERR(res->master_clk);

	res->slave_clk = devm_clk_get(dev, "bus_slave");
	if (IS_ERR(res->slave_clk))
		return PTR_ERR(res->slave_clk);

	return 0;
}

static void qcom_pcie_deinit_2_3_2(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_2 *res = &pcie->res.v2_3_2;

	clk_disable_unprepare(res->slave_clk);
	clk_disable_unprepare(res->master_clk);
	clk_disable_unprepare(res->cfg_clk);
	clk_disable_unprepare(res->aux_clk);

	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
}

static int qcom_pcie_init_2_3_2(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_2 *res = &pcie->res.v2_3_2;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(res->supplies), res->supplies);
	if (ret < 0) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	ret = clk_prepare_enable(res->aux_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable aux clock\n");
		goto err_aux_clk;
	}

	ret = clk_prepare_enable(res->cfg_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable cfg clock\n");
		goto err_cfg_clk;
	}

	ret = clk_prepare_enable(res->master_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable master clock\n");
		goto err_master_clk;
	}

	ret = clk_prepare_enable(res->slave_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable slave clock\n");
		goto err_slave_clk;
	}

	return 0;

err_slave_clk:
	clk_disable_unprepare(res->master_clk);
err_master_clk:
	clk_disable_unprepare(res->cfg_clk);
err_cfg_clk:
	clk_disable_unprepare(res->aux_clk);

err_aux_clk:
	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);

	return ret;
}

static int qcom_pcie_post_init_2_3_2(struct qcom_pcie *pcie)
{
	u32 val;

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	/* change DBI base address */
	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	/* MAC PHY_POWERDOWN MUX DISABLE  */
	val = readl(pcie->parf + PCIE20_PARF_SYS_CTRL);
	val &= ~BIT(29);
	writel(val, pcie->parf + PCIE20_PARF_SYS_CTRL);

	val = readl(pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);
	val |= BIT(4);
	writel(val, pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);

	val = readl(pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);
	val |= BIT(31);
	writel(val, pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);

	return 0;
}

static int qcom_pcie_get_resources_2_4_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_4_0 *res = &pcie->res.v2_4_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	bool is_ipq = of_device_is_compatible(dev->of_node, "qcom,pcie-ipq4019");
	int ret;

	res->clks[0].id = "aux";
	res->clks[1].id = "master_bus";
	res->clks[2].id = "slave_bus";
	res->clks[3].id = "iface";

	/* qcom,pcie-ipq4019 is defined without "iface" */
	res->num_clks = is_ipq ? 3 : 4;

	ret = devm_clk_bulk_get(dev, res->num_clks, res->clks);
	if (ret < 0)
		return ret;

	res->axi_m_reset = devm_reset_control_get_exclusive(dev, "axi_m");
	if (IS_ERR(res->axi_m_reset))
		return PTR_ERR(res->axi_m_reset);

	res->axi_s_reset = devm_reset_control_get_exclusive(dev, "axi_s");
	if (IS_ERR(res->axi_s_reset))
		return PTR_ERR(res->axi_s_reset);

	if (is_ipq) {
		/*
		 * These resources relates to the PHY or are secure clocks, but
		 * are controlled here for IPQ4019
		 */
		res->pipe_reset = devm_reset_control_get_exclusive(dev, "pipe");
		if (IS_ERR(res->pipe_reset))
			return PTR_ERR(res->pipe_reset);

		res->axi_m_vmid_reset = devm_reset_control_get_exclusive(dev,
									 "axi_m_vmid");
		if (IS_ERR(res->axi_m_vmid_reset))
			return PTR_ERR(res->axi_m_vmid_reset);

		res->axi_s_xpu_reset = devm_reset_control_get_exclusive(dev,
									"axi_s_xpu");
		if (IS_ERR(res->axi_s_xpu_reset))
			return PTR_ERR(res->axi_s_xpu_reset);

		res->parf_reset = devm_reset_control_get_exclusive(dev, "parf");
		if (IS_ERR(res->parf_reset))
			return PTR_ERR(res->parf_reset);

		res->phy_reset = devm_reset_control_get_exclusive(dev, "phy");
		if (IS_ERR(res->phy_reset))
			return PTR_ERR(res->phy_reset);
	}

	res->axi_m_sticky_reset = devm_reset_control_get_exclusive(dev,
								   "axi_m_sticky");
	if (IS_ERR(res->axi_m_sticky_reset))
		return PTR_ERR(res->axi_m_sticky_reset);

	res->pipe_sticky_reset = devm_reset_control_get_exclusive(dev,
								  "pipe_sticky");
	if (IS_ERR(res->pipe_sticky_reset))
		return PTR_ERR(res->pipe_sticky_reset);

	res->pwr_reset = devm_reset_control_get_exclusive(dev, "pwr");
	if (IS_ERR(res->pwr_reset))
		return PTR_ERR(res->pwr_reset);

	res->ahb_reset = devm_reset_control_get_exclusive(dev, "ahb");
	if (IS_ERR(res->ahb_reset))
		return PTR_ERR(res->ahb_reset);

	if (is_ipq) {
		res->phy_ahb_reset = devm_reset_control_get_exclusive(dev, "phy_ahb");
		if (IS_ERR(res->phy_ahb_reset))
			return PTR_ERR(res->phy_ahb_reset);
	}

	return 0;
}

static void qcom_pcie_deinit_2_4_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_4_0 *res = &pcie->res.v2_4_0;

	reset_control_assert(res->axi_m_reset);
	reset_control_assert(res->axi_s_reset);
	reset_control_assert(res->pipe_reset);
	reset_control_assert(res->pipe_sticky_reset);
	reset_control_assert(res->phy_reset);
	reset_control_assert(res->phy_ahb_reset);
	reset_control_assert(res->axi_m_sticky_reset);
	reset_control_assert(res->pwr_reset);
	reset_control_assert(res->ahb_reset);
	clk_bulk_disable_unprepare(res->num_clks, res->clks);
}

static int qcom_pcie_init_2_4_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_4_0 *res = &pcie->res.v2_4_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = reset_control_assert(res->axi_m_reset);
	if (ret) {
		dev_err(dev, "cannot assert axi master reset\n");
		return ret;
	}

	ret = reset_control_assert(res->axi_s_reset);
	if (ret) {
		dev_err(dev, "cannot assert axi slave reset\n");
		return ret;
	}

	usleep_range(10000, 12000);

	ret = reset_control_assert(res->pipe_reset);
	if (ret) {
		dev_err(dev, "cannot assert pipe reset\n");
		return ret;
	}

	ret = reset_control_assert(res->pipe_sticky_reset);
	if (ret) {
		dev_err(dev, "cannot assert pipe sticky reset\n");
		return ret;
	}

	ret = reset_control_assert(res->phy_reset);
	if (ret) {
		dev_err(dev, "cannot assert phy reset\n");
		return ret;
	}

	ret = reset_control_assert(res->phy_ahb_reset);
	if (ret) {
		dev_err(dev, "cannot assert phy ahb reset\n");
		return ret;
	}

	usleep_range(10000, 12000);

	ret = reset_control_assert(res->axi_m_sticky_reset);
	if (ret) {
		dev_err(dev, "cannot assert axi master sticky reset\n");
		return ret;
	}

	ret = reset_control_assert(res->pwr_reset);
	if (ret) {
		dev_err(dev, "cannot assert power reset\n");
		return ret;
	}

	ret = reset_control_assert(res->ahb_reset);
	if (ret) {
		dev_err(dev, "cannot assert ahb reset\n");
		return ret;
	}

	usleep_range(10000, 12000);

	ret = reset_control_deassert(res->phy_ahb_reset);
	if (ret) {
		dev_err(dev, "cannot deassert phy ahb reset\n");
		return ret;
	}

	ret = reset_control_deassert(res->phy_reset);
	if (ret) {
		dev_err(dev, "cannot deassert phy reset\n");
		goto err_rst_phy;
	}

	ret = reset_control_deassert(res->pipe_reset);
	if (ret) {
		dev_err(dev, "cannot deassert pipe reset\n");
		goto err_rst_pipe;
	}

	ret = reset_control_deassert(res->pipe_sticky_reset);
	if (ret) {
		dev_err(dev, "cannot deassert pipe sticky reset\n");
		goto err_rst_pipe_sticky;
	}

	usleep_range(10000, 12000);

	ret = reset_control_deassert(res->axi_m_reset);
	if (ret) {
		dev_err(dev, "cannot deassert axi master reset\n");
		goto err_rst_axi_m;
	}

	ret = reset_control_deassert(res->axi_m_sticky_reset);
	if (ret) {
		dev_err(dev, "cannot deassert axi master sticky reset\n");
		goto err_rst_axi_m_sticky;
	}

	ret = reset_control_deassert(res->axi_s_reset);
	if (ret) {
		dev_err(dev, "cannot deassert axi slave reset\n");
		goto err_rst_axi_s;
	}

	ret = reset_control_deassert(res->pwr_reset);
	if (ret) {
		dev_err(dev, "cannot deassert power reset\n");
		goto err_rst_pwr;
	}

	ret = reset_control_deassert(res->ahb_reset);
	if (ret) {
		dev_err(dev, "cannot deassert ahb reset\n");
		goto err_rst_ahb;
	}

	usleep_range(10000, 12000);

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret)
		goto err_clks;

	return 0;

err_clks:
	reset_control_assert(res->ahb_reset);
err_rst_ahb:
	reset_control_assert(res->pwr_reset);
err_rst_pwr:
	reset_control_assert(res->axi_s_reset);
err_rst_axi_s:
	reset_control_assert(res->axi_m_sticky_reset);
err_rst_axi_m_sticky:
	reset_control_assert(res->axi_m_reset);
err_rst_axi_m:
	reset_control_assert(res->pipe_sticky_reset);
err_rst_pipe_sticky:
	reset_control_assert(res->pipe_reset);
err_rst_pipe:
	reset_control_assert(res->phy_reset);
err_rst_phy:
	reset_control_assert(res->phy_ahb_reset);
	return ret;
}

static int qcom_pcie_post_init_2_4_0(struct qcom_pcie *pcie)
{
	u32 val;

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	/* change DBI base address */
	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	/* MAC PHY_POWERDOWN MUX DISABLE  */
	val = readl(pcie->parf + PCIE20_PARF_SYS_CTRL);
	val &= ~BIT(29);
	writel(val, pcie->parf + PCIE20_PARF_SYS_CTRL);

	val = readl(pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);
	val |= BIT(4);
	writel(val, pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);

	val = readl(pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);
	val |= BIT(31);
	writel(val, pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);

	return 0;
}

static int qcom_pcie_get_resources_2_3_3(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_3 *res = &pcie->res.v2_3_3;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int i;
	const char *rst_names[] = { "axi_m", "axi_s", "pipe",
				    "axi_m_sticky", "sticky",
				    "ahb", "sleep", };

	res->iface = devm_clk_get(dev, "iface");
	if (IS_ERR(res->iface))
		return PTR_ERR(res->iface);

	res->axi_m_clk = devm_clk_get(dev, "axi_m");
	if (IS_ERR(res->axi_m_clk))
		return PTR_ERR(res->axi_m_clk);

	res->axi_s_clk = devm_clk_get(dev, "axi_s");
	if (IS_ERR(res->axi_s_clk))
		return PTR_ERR(res->axi_s_clk);

	res->ahb_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(res->ahb_clk))
		return PTR_ERR(res->ahb_clk);

	res->aux_clk = devm_clk_get(dev, "aux");
	if (IS_ERR(res->aux_clk))
		return PTR_ERR(res->aux_clk);

	for (i = 0; i < ARRAY_SIZE(rst_names); i++) {
		res->rst[i] = devm_reset_control_get(dev, rst_names[i]);
		if (IS_ERR(res->rst[i]))
			return PTR_ERR(res->rst[i]);
	}

	return 0;
}

static void qcom_pcie_deinit_2_3_3(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_3 *res = &pcie->res.v2_3_3;

	clk_disable_unprepare(res->iface);
	clk_disable_unprepare(res->axi_m_clk);
	clk_disable_unprepare(res->axi_s_clk);
	clk_disable_unprepare(res->ahb_clk);
	clk_disable_unprepare(res->aux_clk);
}

static int qcom_pcie_init_2_3_3(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_3_3 *res = &pcie->res.v2_3_3;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(res->rst); i++) {
		ret = reset_control_assert(res->rst[i]);
		if (ret) {
			dev_err(dev, "reset #%d assert failed (%d)\n", i, ret);
			return ret;
		}
	}

	usleep_range(2000, 2500);

	for (i = 0; i < ARRAY_SIZE(res->rst); i++) {
		ret = reset_control_deassert(res->rst[i]);
		if (ret) {
			dev_err(dev, "reset #%d deassert failed (%d)\n", i,
				ret);
			return ret;
		}
	}

	/*
	 * Don't have a way to see if the reset has completed.
	 * Wait for some time.
	 */
	usleep_range(2000, 2500);

	ret = clk_prepare_enable(res->iface);
	if (ret) {
		dev_err(dev, "cannot prepare/enable core clock\n");
		goto err_clk_iface;
	}

	ret = clk_prepare_enable(res->axi_m_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable core clock\n");
		goto err_clk_axi_m;
	}

	ret = clk_prepare_enable(res->axi_s_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable axi slave clock\n");
		goto err_clk_axi_s;
	}

	ret = clk_prepare_enable(res->ahb_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable ahb clock\n");
		goto err_clk_ahb;
	}

	ret = clk_prepare_enable(res->aux_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable aux clock\n");
		goto err_clk_aux;
	}

	return 0;

err_clk_aux:
	clk_disable_unprepare(res->ahb_clk);
err_clk_ahb:
	clk_disable_unprepare(res->axi_s_clk);
err_clk_axi_s:
	clk_disable_unprepare(res->axi_m_clk);
err_clk_axi_m:
	clk_disable_unprepare(res->iface);
err_clk_iface:
	/*
	 * Not checking for failure, will anyway return
	 * the original failure in 'ret'.
	 */
	for (i = 0; i < ARRAY_SIZE(res->rst); i++)
		reset_control_assert(res->rst[i]);

	return ret;
}

static int qcom_pcie_post_init_2_3_3(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;

	writel(SLV_ADDR_SPACE_SZ,
		pcie->parf + PCIE20_v3_PARF_SLV_ADDR_SPACE_SIZE);

	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	writel(MST_WAKEUP_EN | SLV_WAKEUP_EN | MSTR_ACLK_CGC_DIS
		| SLV_ACLK_CGC_DIS | CORE_CLK_CGC_DIS |
		AUX_PWR_DET | L23_CLK_RMV_DIS | L1_CLK_RMV_DIS,
		pcie->parf + PCIE20_PARF_SYS_CTRL);
	writel(0, pcie->parf + PCIE20_PARF_Q2A_FLUSH);

	writel(PCI_COMMAND_MASTER, pci->dbi_base + PCI_COMMAND);
	writel(DBI_RO_WR_EN, pci->dbi_base + PCIE20_MISC_CONTROL_1_REG);
	writel(PCIE_CAP_SLOT_VAL, pci->dbi_base + offset + PCI_EXP_SLTCAP);

	val = readl(pci->dbi_base + offset + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_ASPMS;
	writel(val, pci->dbi_base + offset + PCI_EXP_LNKCAP);

	writel(PCI_EXP_DEVCTL2_COMP_TMOUT_DIS, pci->dbi_base + offset +
		PCI_EXP_DEVCTL2);

	return 0;
}

static int qcom_pcie_get_resources_2_7_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_7_0 *res = &pcie->res.v2_7_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	unsigned int num_clks, num_opt_clks;
	unsigned int idx;
	int ret;

	res->pci_reset = devm_reset_control_get_exclusive(dev, "pci");
	if (IS_ERR(res->pci_reset))
		return PTR_ERR(res->pci_reset);

	res->supplies[0].supply = "vdda";
	res->supplies[1].supply = "vddpe-3v3";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(res->supplies),
				      res->supplies);
	if (ret)
		return ret;

	idx = 0;
	res->clks[idx++].id = "aux";
	res->clks[idx++].id = "cfg";
	res->clks[idx++].id = "bus_master";
	res->clks[idx++].id = "bus_slave";
	res->clks[idx++].id = "slave_q2a";

	num_clks = idx;

	ret = devm_clk_bulk_get(dev, num_clks, res->clks);
	if (ret < 0)
		return ret;

	res->clks[idx++].id = "tbu";
	res->clks[idx++].id = "ddrss_sf_tbu";
	res->clks[idx++].id = "aggre0";
	res->clks[idx++].id = "aggre1";
	res->clks[idx++].id = "noc_aggr_4";
	res->clks[idx++].id = "noc_aggr_south_sf";
	res->clks[idx++].id = "cnoc_qx";

	num_opt_clks = idx - num_clks;
	res->num_clks = idx;

	ret = devm_clk_bulk_get_optional(dev, num_opt_clks, res->clks + num_clks);
	if (ret < 0)
		return ret;

	return 0;
}

static int qcom_pcie_init_2_7_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_7_0 *res = &pcie->res.v2_7_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	u32 val;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(res->supplies), res->supplies);
	if (ret < 0) {
		dev_err(dev, "cannot enable regulators\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret < 0)
		goto err_disable_regulators;

	ret = reset_control_assert(res->pci_reset);
	if (ret < 0) {
		dev_err(dev, "cannot deassert pci reset\n");
		goto err_disable_clocks;
	}

	usleep_range(1000, 1500);

	ret = reset_control_deassert(res->pci_reset);
	if (ret < 0) {
		dev_err(dev, "cannot deassert pci reset\n");
		goto err_disable_clocks;
	}

	/* Wait for reset to complete, required on SM8450 */
	usleep_range(1000, 1500);

	/* configure PCIe to RC mode */
	writel(DEVICE_TYPE_RC, pcie->parf + PCIE20_PARF_DEVICE_TYPE);

	/* enable PCIe clocks and resets */
	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	/* change DBI base address */
	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	/* MAC PHY_POWERDOWN MUX DISABLE  */
	val = readl(pcie->parf + PCIE20_PARF_SYS_CTRL);
	val &= ~BIT(29);
	writel(val, pcie->parf + PCIE20_PARF_SYS_CTRL);

	val = readl(pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);
	val |= BIT(4);
	writel(val, pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);

	/* Enable L1 and L1SS */
	val = readl(pcie->parf + PCIE20_PARF_PM_CTRL);
	val &= ~REQ_NOT_ENTR_L1;
	writel(val, pcie->parf + PCIE20_PARF_PM_CTRL);

	val = readl(pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);
	val |= BIT(31);
	writel(val, pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);

	return 0;
err_disable_clocks:
	clk_bulk_disable_unprepare(res->num_clks, res->clks);
err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);

	return ret;
}

static void qcom_pcie_deinit_2_7_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_7_0 *res = &pcie->res.v2_7_0;

	clk_bulk_disable_unprepare(res->num_clks, res->clks);

	regulator_bulk_disable(ARRAY_SIZE(res->supplies), res->supplies);
}

static int qcom_pcie_get_resources_2_9_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_9_0 *res = &pcie->res.v2_9_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	res->clks[0].id = "iface";
	res->clks[1].id = "axi_m";
	res->clks[2].id = "axi_s";
	res->clks[3].id = "axi_bridge";
	res->clks[4].id = "rchng";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(res->clks), res->clks);
	if (ret < 0)
		return ret;

	res->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(res->rst))
		return PTR_ERR(res->rst);

	return 0;
}

static void qcom_pcie_deinit_2_9_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_9_0 *res = &pcie->res.v2_9_0;

	clk_bulk_disable_unprepare(ARRAY_SIZE(res->clks), res->clks);
}

static int qcom_pcie_init_2_9_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_2_9_0 *res = &pcie->res.v2_9_0;
	struct device *dev = pcie->pci->dev;
	int ret;

	ret = reset_control_assert(res->rst);
	if (ret) {
		dev_err(dev, "reset assert failed (%d)\n", ret);
		return ret;
	}

	/*
	 * Delay periods before and after reset deassert are working values
	 * from downstream Codeaurora kernel
	 */
	usleep_range(2000, 2500);

	ret = reset_control_deassert(res->rst);
	if (ret) {
		dev_err(dev, "reset deassert failed (%d)\n", ret);
		return ret;
	}

	usleep_range(2000, 2500);

	return clk_bulk_prepare_enable(ARRAY_SIZE(res->clks), res->clks);
}

static int qcom_pcie_post_init_2_9_0(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;
	int i;

	writel(SLV_ADDR_SPACE_SZ,
		pcie->parf + PCIE20_v3_PARF_SLV_ADDR_SPACE_SIZE);

	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	writel(DEVICE_TYPE_RC, pcie->parf + PCIE20_PARF_DEVICE_TYPE);
	writel(BYPASS | MSTR_AXI_CLK_EN | AHB_CLK_EN,
		pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);
	writel(GEN3_RELATED_OFF_RXEQ_RGRDLESS_RXTS |
		GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL,
		pci->dbi_base + GEN3_RELATED_OFF);

	writel(MST_WAKEUP_EN | SLV_WAKEUP_EN | MSTR_ACLK_CGC_DIS |
		SLV_ACLK_CGC_DIS | CORE_CLK_CGC_DIS |
		AUX_PWR_DET | L23_CLK_RMV_DIS | L1_CLK_RMV_DIS,
		pcie->parf + PCIE20_PARF_SYS_CTRL);

	writel(0, pcie->parf + PCIE20_PARF_Q2A_FLUSH);

	dw_pcie_dbi_ro_wr_en(pci);
	writel(PCIE_CAP_SLOT_VAL, pci->dbi_base + offset + PCI_EXP_SLTCAP);

	val = readl(pci->dbi_base + offset + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_ASPMS;
	writel(val, pci->dbi_base + offset + PCI_EXP_LNKCAP);

	writel(PCI_EXP_DEVCTL2_COMP_TMOUT_DIS, pci->dbi_base + offset +
			PCI_EXP_DEVCTL2);

	for (i = 0; i < 256; i++)
		writel(0, pcie->parf + PCIE20_PARF_BDF_TO_SID_TABLE_N + (4 * i));

	return 0;
}

static int qti_pcie_get_resources_1_27_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_27_0 *res = &pcie->res.v1_27_0;
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->clks < 0)
		return res->num_clks;

	res->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(res->rst))
		return PTR_ERR(res->rst);

	return 0;
}

static void qti_pcie_deinit_1_27_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_27_0 *res = &pcie->res.v1_27_0;

	clk_bulk_disable_unprepare(res->num_clks, res->clks);
}

static int qti_pcie_init_1_27_0(struct qcom_pcie *pcie)
{
	struct qcom_pcie_resources_1_27_0 *res = &pcie->res.v1_27_0;
	struct device *dev = pcie->pci->dev;
	int ret;

	ret = reset_control_assert(res->rst);
	if (ret) {
		dev_err(dev, "reset assert failed (%d)\n", ret);
		return ret;
	}

	/*
	 * Delay periods before and after reset deassert are working values
	 * from downstream Codeaurora kernel
	 */
	usleep_range(2000, 2500);

	ret = reset_control_deassert(res->rst);
	if (ret) {
		dev_err(dev, "reset deassert failed (%d)\n", ret);
		return ret;
	}

	usleep_range(2000, 2500);

	return clk_bulk_prepare_enable(res->num_clks, res->clks);
}

static int qti_pcie_post_init_1_27_0(struct qcom_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;
	int i;

	val = readl(pci->dbi_base + offset + PCI_EXP_DEVCTL);
	val &= ~PCI_EXP_DEVCTL_PAYLOAD;
	val |= PCI_EXP_DEVCTL_PAYLOAD_256B;
	writel(val, pci->dbi_base + offset + PCI_EXP_DEVCTL);

	writel(0x8000000, pcie->parf + PCIE20_v3_PARF_SLV_ADDR_SPACE_SIZE);

	val = readl(pcie->parf + PCIE20_PARF_PHY_CTRL);
	val &= ~BIT(0);
	writel(val, pcie->parf + PCIE20_PARF_PHY_CTRL);

	writel(0, pcie->parf + PCIE20_PARF_DBI_BASE_ADDR);

	writel(DEVICE_TYPE_RC, pcie->parf + PCIE20_PARF_DEVICE_TYPE);
	writel(BYPASS | MSTR_AXI_CLK_EN | AHB_CLK_EN,
	       pcie->parf + PCIE20_PARF_MHI_CLOCK_RESET_CTRL);
	writel(GEN3_RELATED_OFF_RXEQ_RGRDLESS_RXTS |
	       GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL,
	       pci->dbi_base + GEN3_RELATED_OFF);

	writel(MST_WAKEUP_EN | SLV_WAKEUP_EN | MSTR_ACLK_CGC_DIS |
	       SLV_ACLK_CGC_DIS | CORE_CLK_CGC_DIS |
	       AUX_PWR_DET | L23_CLK_RMV_DIS | L1_CLK_RMV_DIS,
	       pcie->parf + PCIE20_PARF_SYS_CTRL);

	writel(0, pcie->parf + PCIE20_PARF_Q2A_FLUSH);

	if (pcie->axi_wr_addr_halt) {
		val = readl(pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);
		val &= ~PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2_MASK;
		writel(val | pcie->axi_wr_addr_halt,
			pcie->parf + PCIE20_PARF_AXI_MSTR_WR_ADDR_HALT_V2);
	}

	if (pcie->aggr_noc != NULL && !IS_ERR(pcie->aggr_noc))
		writel(AGGR_NOC_PCIE_1LANE_RATEADAPT_VAL, pcie->aggr_noc);

	if (pcie->system_noc != NULL && !IS_ERR(pcie->system_noc)) {
		if (pcie->num_lanes == 2)
			writel(SYSTEM_NOC_PCIE_RATEADAPT_BYPASS, pcie->system_noc);
	}

	dw_pcie_dbi_ro_wr_en(pci);
	writel(PCIE_CAP_SLOT_VAL, pci->dbi_base + offset + PCI_EXP_SLTCAP);

	val = readl(pci->dbi_base + offset + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_ASPMS;
	writel(val, pci->dbi_base + offset + PCI_EXP_LNKCAP);

	writel(PCI_EXP_DEVCTL2_COMP_TMOUT_DIS, pci->dbi_base + offset +
	       PCI_EXP_DEVCTL2);

	for (i = 0; i < 256; i++)
		writel(0, pcie->parf + PCIE20_PARF_BDF_TO_SID_TABLE_N + (4 * i));

	return 0;
}

static int qcom_pcie_link_up(struct dw_pcie *pci)
{
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u16 val = readw(pci->dbi_base + offset + PCI_EXP_LNKSTA);

	return !!(val & PCI_EXP_LNKSTA_DLLLA);
}

static int qcom_pcie_config_sid_sm8250(struct qcom_pcie *pcie)
{
	/* iommu map structure */
	struct {
		u32 bdf;
		u32 phandle;
		u32 smmu_sid;
		u32 smmu_sid_len;
	} *map;
	void __iomem *bdf_to_sid_base = pcie->parf + PCIE20_PARF_BDF_TO_SID_TABLE_N;
	struct device *dev = pcie->pci->dev;
	u8 qcom_pcie_crc8_table[CRC8_TABLE_SIZE];
	int i, nr_map, size = 0;
	u32 smmu_sid_base;

	of_get_property(dev->of_node, "iommu-map", &size);
	if (!size)
		return 0;

	map = kzalloc(size, GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	of_property_read_u32_array(dev->of_node,
		"iommu-map", (u32 *)map, size / sizeof(u32));

	nr_map = size / (sizeof(*map));

	crc8_populate_msb(qcom_pcie_crc8_table, QCOM_PCIE_CRC8_POLYNOMIAL);

	/* Registers need to be zero out first */
	memset_io(bdf_to_sid_base, 0, CRC8_TABLE_SIZE * sizeof(u32));

	/* Extract the SMMU SID base from the first entry of iommu-map */
	smmu_sid_base = map[0].smmu_sid;

	/* Look for an available entry to hold the mapping */
	for (i = 0; i < nr_map; i++) {
		__be16 bdf_be = cpu_to_be16(map[i].bdf);
		u32 val;
		u8 hash;

		hash = crc8(qcom_pcie_crc8_table, (u8 *)&bdf_be, sizeof(bdf_be),
			0);

		val = readl(bdf_to_sid_base + hash * sizeof(u32));

		/* If the register is already populated, look for next available entry */
		while (val) {
			u8 current_hash = hash++;
			u8 next_mask = 0xff;

			/* If NEXT field is NULL then update it with next hash */
			if (!(val & next_mask)) {
				val |= (u32)hash;
				writel(val, bdf_to_sid_base + current_hash * sizeof(u32));
			}

			val = readl(bdf_to_sid_base + hash * sizeof(u32));
		}

		/* BDF [31:16] | SID [15:8] | NEXT [7:0] */
		val = map[i].bdf << 16 | (map[i].smmu_sid - smmu_sid_base) << 8 | 0;
		writel(val, bdf_to_sid_base + hash * sizeof(u32));
	}

	kfree(map);

	return 0;
}

static int qcom_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct qcom_pcie *pcie = to_qcom_pcie(pci);
	int ret;

	qcom_ep_reset_assert(pcie);

	ret = pcie->cfg->ops->init(pcie);
	if (ret)
		return ret;

	ret = phy_power_on(pcie->phy);
	if (ret)
		goto err_deinit;

	if (pcie->cfg->ops->post_init) {
		ret = pcie->cfg->ops->post_init(pcie);
		if (ret)
			goto err_disable_phy;
	}

	qcom_ep_reset_deassert(pcie);

	if (pcie->cfg->ops->config_sid) {
		ret = pcie->cfg->ops->config_sid(pcie);
		if (ret)
			goto err_assert_reset;
	}

	pp->bridge->ops = &qcom_pcie_bridge_ops;

	return 0;

err_assert_reset:
	qcom_ep_reset_assert(pcie);
err_disable_phy:
	phy_power_off(pcie->phy);
err_deinit:
	pcie->cfg->ops->deinit(pcie);

	return ret;
}

int pcie_set_link_speed(struct pci_dev *dev, u16 target_link_speed)
{
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	u32 val;

	if (dev->device != QCOM_IPQ9574_DEVICE_ID && dev->device != QCOM_IPQ5332_DEVICE_ID)
		return -EINVAL;

	if (target_link_speed < 1 || target_link_speed > 3)
		return -EINVAL;

	pp = dev->bus->sysdata;
	pci = to_dw_pcie_from_pp(pp);

	val = readl(pci->dbi_base + PCIE20_LNK_CONTROL2_LINK_STATUS2);
	val &= ~PCIE_CAP_TARGET_LINK_SPD_MASK;
	val |= target_link_speed;
	writel(val, pci->dbi_base + PCIE20_LNK_CONTROL2_LINK_STATUS2);

	val = readl(pci->dbi_base + PCIE20_LINK_CONTROL_LINK_STATUS_REG);
	val &= ~PCIE_CAP_RETRAIN_LINK_MASK;
	val |= PCIE_CAP_RETRAIN_LINK_OFFSET(0x1);
	writel(val, pci->dbi_base + PCIE20_LINK_CONTROL_LINK_STATUS_REG);

	msleep(1);

	val = readl(pci->dbi_base + PCIE20_LINK_CONTROL_LINK_STATUS_REG);
	val &= PCIE_CAP_LINK_SPEED_MASK;
	if (PCIE_CAP_LINK_SPEED_SHIFT(val) != target_link_speed) {
		dev_err(pci->dev, "Speed change failed. Current speed 0x%x\n",
			PCIE_CAP_LINK_SPEED_SHIFT(val));
		return -EAGAIN;
	}

	return 0;
}
EXPORT_SYMBOL(pcie_set_link_speed);

int pcie_set_link_width(struct pci_dev *dev, u16 target_link_width)
{
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	struct qcom_pcie *pcie;
	u32 val;

	if (dev->device != QCOM_IPQ9574_DEVICE_ID && dev->device != QCOM_IPQ5332_DEVICE_ID)
		return -EINVAL;

	if (target_link_width < 1 || target_link_width > 2)
		return -EINVAL;

	pp = dev->bus->sysdata;
	pci = to_dw_pcie_from_pp(pp);
	pcie = to_qcom_pcie(pci);

	dw_pcie_read(pci->dbi_base + PCIE20_PORT_LINK_CTRL_OFF, 4, &val);
	val &= ~PCIE20_PORT_LINK_CTRL_OFF_MASK;
	val |= LINK_CAPABLE_OFFSET(target_link_width);
	dw_pcie_write(pci->dbi_base + PCIE20_PORT_LINK_CTRL_OFF, 4, val);

	dw_pcie_read(pci->dbi_base + PCIE20_LANE_SKEW_OFF, 4, &val);
	val = (val & PCIE20_LANE_SKEW_OFF_MASK) | 0x20;
	dw_pcie_write(pci->dbi_base + PCIE20_LANE_SKEW_OFF, 4, val);

	val = 0xc0 | target_link_width;
	dw_pcie_write(pci->dbi_base + PCIE20_MULTI_LANE_CONTROL_OFF, 4, val);

	msleep(50);

	dw_pcie_read(pci->dbi_base + PCIE20_MULTI_LANE_CONTROL_OFF, 4, &val);
	if((val & 0x40) != 0) {
		dev_err(pci->dev, "LANE_CONTROL_OFF failed: val 0x%x\n", val);
		return -EAGAIN;
	}

	dw_pcie_read(pcie->parf + PCIE20_PARF_LTSSM, 4, &val);
	if ((val & PCIE20_PARF_LTSSM_MASK) != 0x11) {
		dev_err(pci->dev, "After lane switch, link is not in L0: val 0x%x\n", val);
		return -EAGAIN;
	}

	val = readl(pci->dbi_base + PCIE20_LINK_CONTROL_LINK_STATUS_REG);
	val &= PCIE_CAP_NEGO_LINK_WIDTH_MASK;
	if (PCIE_CAP_NEGO_LINK_WIDTH_SHIFT(val) != target_link_width) {
		dev_err(pci->dev, "Lane switch failed: 0x%x\n", val);
		return -EAGAIN;
	}

	return 0;
}
EXPORT_SYMBOL(pcie_set_link_width);

static void qcom_pcie_host_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct qcom_pcie *pcie = to_qcom_pcie(pci);

	qcom_ep_reset_assert(pcie);
	phy_power_off(pcie->phy);
	pcie->cfg->ops->deinit(pcie);
}

int pcie_parf_read(struct pci_dev *dev, u32 offset, u32 *val)
{
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	struct qcom_pcie *pcie;

	if(!dev)
		goto err;

	pp = dev->bus->sysdata;
	pci = to_dw_pcie_from_pp(pp);
	pcie = to_qcom_pcie(pci);

	if (offset > pcie->parf_size ||
			!IS_ALIGNED((uintptr_t)pcie->parf + offset, 4))
		goto err;

	*val = readl(pcie->parf + offset);

	return 0;

err:
	*val = 0;
	return -EINVAL;
}
EXPORT_SYMBOL(pcie_parf_read);

static const struct dw_pcie_host_ops qcom_pcie_dw_ops = {
	.host_init	= qcom_pcie_host_init,
	.host_deinit	= qcom_pcie_host_deinit,
};

static int qcom_pcie_retrain_link(struct pci_bus *bus)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct qcom_pcie *pcie = to_qcom_pcie(pci);
	struct device *dev = pcie->pci->dev;
	u32 val;
	int ret;

	/* Wait for the pending transactions to be completed */
	ret = readl_relaxed_poll_timeout(pcie->parf + PARF_STATUS, val,
					 val & FLUSH_COMPLETED, 10,
					 FLUSH_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Flush completion failed: %d, value: %u\n", ret, val);
		goto err_host_deinit;
	}

	/* Clear the FLUSH_MODE to allow the core to be reset */
	val = readl(pcie->parf + PARF_LTSSM);
	val |= SW_CLEAR_FLUSH_MODE;
	writel(val, pcie->parf + PARF_LTSSM);

	/* Wait for the FLUSH_MODE to clear */
	ret = readl_relaxed_poll_timeout(pcie->parf + PARF_LTSSM, val,
					 !(val & FLUSH_MODE), 10,
					 FLUSH_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Flush mode clear failed: %d, value: %u\n", ret, val);
		goto err_host_deinit;
	}

	qcom_pcie_host_deinit(pp);

	ret = qcom_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Host init failed\n");
		return ret;
	}

	ret = dw_pcie_setup_rc(pp);
	if (ret)
		goto err_host_deinit;

	/*
	 * Re-enable global IRQ events as the PARF_INT_ALL_MASK register is
	 * non-sticky.
	 */
	if (pcie->global_irq)
		writel_relaxed(PARF_INT_ALL_LINK_UP | PARF_INT_ALL_LINK_DOWN |
			       PARF_INT_MSI_DEV_0_7, pcie->parf + PARF_INT_ALL_MASK);

	qcom_pcie_start_link(pci);

	return 0;

err_host_deinit:
	qcom_pcie_host_deinit(pp);

	return ret;
}

static struct pci_ops qcom_pcie_bridge_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
	.retrain_link = qcom_pcie_retrain_link,
};

/* Qcom IP rev.: 2.1.0	Synopsys IP rev.: 4.01a */
static const struct qcom_pcie_ops ops_2_1_0 = {
	.get_resources = qcom_pcie_get_resources_2_1_0,
	.init = qcom_pcie_init_2_1_0,
	.post_init = qcom_pcie_post_init_2_1_0,
	.deinit = qcom_pcie_deinit_2_1_0,
	.ltssm_enable = qcom_pcie_2_1_0_ltssm_enable,
};

/* Qcom IP rev.: 1.0.0	Synopsys IP rev.: 4.11a */
static const struct qcom_pcie_ops ops_1_0_0 = {
	.get_resources = qcom_pcie_get_resources_1_0_0,
	.init = qcom_pcie_init_1_0_0,
	.post_init = qcom_pcie_post_init_1_0_0,
	.deinit = qcom_pcie_deinit_1_0_0,
	.ltssm_enable = qcom_pcie_2_1_0_ltssm_enable,
};

/* Qcom IP rev.: 2.3.2	Synopsys IP rev.: 4.21a */
static const struct qcom_pcie_ops ops_2_3_2 = {
	.get_resources = qcom_pcie_get_resources_2_3_2,
	.init = qcom_pcie_init_2_3_2,
	.post_init = qcom_pcie_post_init_2_3_2,
	.deinit = qcom_pcie_deinit_2_3_2,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 2.4.0	Synopsys IP rev.: 4.20a */
static const struct qcom_pcie_ops ops_2_4_0 = {
	.get_resources = qcom_pcie_get_resources_2_4_0,
	.init = qcom_pcie_init_2_4_0,
	.post_init = qcom_pcie_post_init_2_4_0,
	.deinit = qcom_pcie_deinit_2_4_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 2.3.3	Synopsys IP rev.: 4.30a */
static const struct qcom_pcie_ops ops_2_3_3 = {
	.get_resources = qcom_pcie_get_resources_2_3_3,
	.init = qcom_pcie_init_2_3_3,
	.post_init = qcom_pcie_post_init_2_3_3,
	.deinit = qcom_pcie_deinit_2_3_3,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 2.7.0	Synopsys IP rev.: 4.30a */
static const struct qcom_pcie_ops ops_2_7_0 = {
	.get_resources = qcom_pcie_get_resources_2_7_0,
	.init = qcom_pcie_init_2_7_0,
	.deinit = qcom_pcie_deinit_2_7_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 1.9.0 */
static const struct qcom_pcie_ops ops_1_9_0 = {
	.get_resources = qcom_pcie_get_resources_2_7_0,
	.init = qcom_pcie_init_2_7_0,
	.deinit = qcom_pcie_deinit_2_7_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
	.config_sid = qcom_pcie_config_sid_sm8250,
};

/* Qcom IP rev.: 2.9.0  Synopsys IP rev.: 5.00a */
static const struct qcom_pcie_ops ops_2_9_0 = {
	.get_resources = qcom_pcie_get_resources_2_9_0,
	.init = qcom_pcie_init_2_9_0,
	.post_init = qcom_pcie_post_init_2_9_0,
	.deinit = qcom_pcie_deinit_2_9_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

/* Qcom IP rev.: 1.27.0 Synopsys IP rev.: 5.80a */
static const struct qcom_pcie_ops ops_1_27_0 = {
	.get_resources = qti_pcie_get_resources_1_27_0,
	.init = qti_pcie_init_1_27_0,
	.post_init = qti_pcie_post_init_1_27_0,
	.deinit = qti_pcie_deinit_1_27_0,
	.ltssm_enable = qcom_pcie_2_3_2_ltssm_enable,
};

static const struct qcom_pcie_cfg cfg_1_0_0 = {
	.ops = &ops_1_0_0,
};

static const struct qcom_pcie_cfg cfg_1_9_0 = {
	.ops = &ops_1_9_0,
};

static const struct qcom_pcie_cfg cfg_2_1_0 = {
	.ops = &ops_2_1_0,
};

static const struct qcom_pcie_cfg cfg_2_3_2 = {
	.ops = &ops_2_3_2,
};

static const struct qcom_pcie_cfg cfg_2_3_3 = {
	.ops = &ops_2_3_3,
};

static const struct qcom_pcie_cfg cfg_2_4_0 = {
	.ops = &ops_2_4_0,
};

static const struct qcom_pcie_cfg cfg_2_7_0 = {
	.ops = &ops_2_7_0,
};

static const struct qcom_pcie_cfg cfg_2_9_0 = {
	.ops = &ops_2_9_0,
};

static const struct qcom_pcie_cfg cfg_1_27_0 = {
	.ops = &ops_1_27_0,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = qcom_pcie_link_up,
	.start_link = qcom_pcie_start_link,
};

static int qcom_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	struct qcom_pcie *pcie;
	const struct qcom_pcie_cfg *pcie_cfg;
	int ret;
	struct resource *res;
	uint32_t num_lanes = 0;
	struct nvmem_cell *pcie_nvmem;
	u8 *disable_status;
	size_t len;

	/* If nvmem-cells present on PCIe node in DTSI, then check the QFPROM
	 * fuses for PCIe is disabled */
	pcie_nvmem = of_nvmem_cell_get(pdev->dev.of_node, NULL);
	if (IS_ERR(pcie_nvmem)) {
		if (PTR_ERR(pcie_nvmem) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		disable_status = nvmem_cell_read(pcie_nvmem, &len);
		nvmem_cell_put(pcie_nvmem);
		if ( !IS_ERR(disable_status) && ((unsigned int)(*disable_status) == 1) ) {
			dev_info(dev,"Disabled in qfprom efuse\n");
			kfree(disable_status);
			return -ENODEV;
		}
		kfree(disable_status);
	}

	pcie_cfg = of_device_get_match_data(dev);
	if (!pcie_cfg || !pcie_cfg->ops) {
		dev_err(dev, "Invalid platform data\n");
		return -EINVAL;
	}

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_pm_runtime_put;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pp = &pci->pp;

	pcie->pci = pci;

	pcie->cfg = pcie_cfg;

	pcie->reset = devm_gpiod_get_optional(dev, "perst", GPIOD_OUT_HIGH);
	if (IS_ERR(pcie->reset)) {
		ret = PTR_ERR(pcie->reset);
		goto err_pm_runtime_put;
	}

	pcie->parf = devm_platform_ioremap_resource_byname(pdev, "parf");
	if (IS_ERR(pcie->parf)) {
		ret = PTR_ERR(pcie->parf);
		goto err_pm_runtime_put;
	}

	/* get the parf size which is needed for pcie_parf_read() */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "parf");
	if (res)
		pcie->parf_size = resource_size(res);

	pcie->elbi = devm_platform_ioremap_resource_byname(pdev, "elbi");
	if (IS_ERR(pcie->elbi)) {
		ret = PTR_ERR(pcie->elbi);
		goto err_pm_runtime_put;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aggr_noc");
	if (res != NULL) {
		pcie->aggr_noc = devm_ioremap_resource(dev, res);
		if (IS_ERR(pcie->aggr_noc)) {
			ret = PTR_ERR(pcie->aggr_noc);
			goto err_pm_runtime_put;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "system_noc");
	if (res != NULL) {
		pcie->system_noc = devm_ioremap_resource(dev, res);
		if (IS_ERR(pcie->system_noc)) {
			ret = PTR_ERR(pcie->system_noc);
			goto err_pm_runtime_put;
		}
	}

	of_property_read_u32(pdev->dev.of_node, "axi-halt-val",
				&pcie->axi_wr_addr_halt);

	of_property_read_u32(pdev->dev.of_node, "num-lanes",
				&num_lanes);
	pcie->num_lanes = num_lanes;

	of_property_read_u32(pdev->dev.of_node, "linux,pci-domain",&pcie->domain);

	pcie->enable_vc = of_property_read_bool(pdev->dev.of_node,
					"enable-virtual-channel");

	pcie->phy = devm_phy_optional_get(dev, "pciephy");
	if (IS_ERR(pcie->phy)) {
		ret = PTR_ERR(pcie->phy);
		goto err_pm_runtime_put;
	}

	ret = pcie->cfg->ops->get_resources(pcie);
	if (ret)
		goto err_pm_runtime_put;

	pp->ops = &qcom_pcie_dw_ops;

	ret = phy_init(pcie->phy);
	if (ret)
		goto err_pm_runtime_put;

	platform_set_drvdata(pdev, pcie);

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "cannot initialize host\n");
		goto err_phy_exit;
	}

	pcie->global_irq = platform_get_irq_byname(pdev, "global_irq");
	if (pcie->global_irq >= 0) {
		ret = devm_request_threaded_irq(&pdev->dev, pcie->global_irq,
					NULL,
					qcom_pcie_global_irq_thread_fn,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"pcie-global", pcie);
		if (ret) {
			dev_err(&pdev->dev, "Unable to request global irq\n");
			pm_runtime_disable(&pdev->dev);
			goto err_host_deinit;
		}

		writel_relaxed(PARF_INT_ALL_LINK_UP | PARF_INT_ALL_LINK_DOWN |
			PARF_INT_MSI_DEV_0_7, pcie->parf + PARF_INT_ALL_MASK);
	}

	return 0;

err_host_deinit:
	dw_pcie_host_deinit(pp);
err_phy_exit:
	phy_exit(pcie->phy);
err_pm_runtime_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static const struct of_device_id qcom_pcie_match[] = {
	{ .compatible = "qcom,pcie-apq8064", .data = &cfg_2_1_0 },
	{ .compatible = "qcom,pcie-apq8084", .data = &cfg_1_0_0 },
	{ .compatible = "qcom,pcie-ipq4019", .data = &cfg_2_4_0 },
	{ .compatible = "qti,pcie-ipq5332", .data = &cfg_1_27_0 },
	{ .compatible = "qcom,pcie-devsoc", .data = &cfg_1_27_0 },
	{ .compatible = "qcom,pcie-ipq6018", .data = &cfg_2_9_0 },
	{ .compatible = "qcom,pcie-ipq8064", .data = &cfg_2_1_0 },
	{ .compatible = "qcom,pcie-ipq8064-v2", .data = &cfg_2_1_0 },
	{ .compatible = "qcom,pcie-ipq8074", .data = &cfg_2_3_3 },
	{ .compatible = "qti,pcie-ipq9574", .data = &cfg_1_27_0 },
	{ .compatible = "qcom,pcie-msm8996", .data = &cfg_2_3_2 },
	{ .compatible = "qcom,pcie-qcs404", .data = &cfg_2_4_0 },
	{ .compatible = "qcom,pcie-sa8540p", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sc7280", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sc8180x", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sc8280xp", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sdm845", .data = &cfg_2_7_0 },
	{ .compatible = "qcom,pcie-sm8150", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8250", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8450-pcie0", .data = &cfg_1_9_0 },
	{ .compatible = "qcom,pcie-sm8450-pcie1", .data = &cfg_1_9_0 },
	{ }
};

static void qcom_fixup_class(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI_NORMAL;
}

static void qcom_ipq_tc_vc_mapping(struct pci_dev *dev)
{
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	struct qcom_pcie *pcie;
	int timeout = 50;
	u32 val;

	pp = dev->bus->sysdata;
	pci = to_dw_pcie_from_pp(pp);
	pcie = to_qcom_pcie(pci);

	if (!pcie->enable_vc)
		return;

	dev_dbg(&dev->dev, "Enabling Virtual channel for 0x%x:0x%x\n",dev->vendor, dev->device);

	/* Read device VC capabilities */
	pci_read_config_dword(dev, PCIE_TYPE0_VC_CAPABILITIES_REG_1, &val);
	if((val & VC_EXT_VC_CNT_MASK) != 0x1) {
		dev_err(&dev->dev,"device 0x%x does not support Virtual Channel\n", dev->device);
		return;
	}

	/* Program Q6 VC */
	pci_read_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC0, &val);
	val &= ~VC_TC_MAP_VC_BIT1_MASK;
	val |= VC_TC_MAP_VC_BIT1_OFFSET(0x0);
	pci_write_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC0, val);

	pci_read_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC1, &val);
	val &= ~VC_TC_MAP_VC_BIT1_MASK;
	val |= VC_TC_MAP_VC_BIT1_OFFSET(0x7F);
	pci_write_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC1, val);

	pci_read_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC1, &val);
	val &= ~VC_ID_VC1_MASK;
	val |= VC_ID_VC1_OFFSET(0x1);
	pci_write_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC1, val);

	pci_read_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC1, &val);
	val &= ~VC_ENABLE_VC1_MASK;
	val |= VC_ENABLE_VC1_OFFSET(0x1);
	pci_write_config_dword(dev, PCIE_TYPE0_RESOURCE_CON_REG_VC1, val);

	/* Program Host VC */
	val = readl(pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC0);
	val &= ~VC_TC_MAP_VC_BIT1_MASK;
	val |= VC_TC_MAP_VC_BIT1_OFFSET(0x0);
	writel(val, pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC0);

	val = readl(pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC1);
	val &= ~VC_TC_MAP_VC_BIT1_MASK;
	val |= VC_TC_MAP_VC_BIT1_OFFSET(0x7F);
	writel(val, pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC1);

	val = readl(pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC1);
	val &= ~VC_ID_VC1_MASK;
	val |= VC_ID_VC1_OFFSET(0x1);
	writel(val, pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC1);

	val = readl(pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC1);
	val &= ~VC_ENABLE_VC1_MASK;
	val |= VC_ENABLE_VC1_OFFSET(0x1);
	writel(val, pci->dbi_base + PCIE_TYPE0_RESOURCE_CON_REG_VC1);

	do {
		/* Poll for negotiation */
		val = readl(pci->dbi_base + PCIE_TYPE0_RESOURCE_STATUS_REG_VC1);
		if(!(val & VC_NEGO_PENDING_VC1_MASK)) {
			dev_info(&dev->dev,"Virtual channel is enabled for 0x%x:0x%x\n",
					dev->vendor, dev->device);
			break;
		}
		timeout--;
	}while(timeout);
}

static void qcom_ipq_switch_lane(struct pci_dev *dev)
{
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	struct qcom_pcie *pcie;
	struct device *devp;
	struct device_node *np;
	u32 val;
	int size = 4;

	pp = dev->bus->sysdata;
	pci = to_dw_pcie_from_pp(pp);
	pcie = to_qcom_pcie(pci);
	devp = pci->dev;
	np = devp->of_node;

	/* Switching PCIE Nodes 2/3 to single lane if force_to_single_lane property is defined in dts */
	if ((of_property_read_bool(np, "force_to_single_lane")) && (pcie->domain == 2 || pcie->domain == 3)) {

		dev_info(devp,"Forcing PCIE to single lane\n");

		/* check if Link is in L0 state */
		dw_pcie_read(pcie->parf + PCIE20_PARF_LTSSM, size, &val);
		if ((val & PCIE20_PARF_LTSSM_MASK) != 0x11)
			dev_info(devp,"Before lane switch: Link is not in L0 state: %u\n",val);

		/* set link width */
		dw_pcie_read(pci->dbi_base + PCIE20_PORT_LINK_CTRL_OFF, size, &val);
		val &= ~(PCIE20_PORT_LINK_CTRL_OFF_MASK);
		val |= 0x10000;
		dw_pcie_write(pci->dbi_base + PCIE20_PORT_LINK_CTRL_OFF, size, val);

		/* config lane skew */
		dw_pcie_read(pci->dbi_base + PCIE20_LANE_SKEW_OFF, size, &val);
		val = (val & PCIE20_LANE_SKEW_OFF_MASK)|0x20;
		dw_pcie_write(pci->dbi_base + PCIE20_LANE_SKEW_OFF, size, val);

		/* set target lane width & direct link width change */
		dw_pcie_read(pci->dbi_base + PCIE20_MULTI_LANE_CONTROL_OFF, size, &val);
		val |= 0xc1;
		dw_pcie_write(pci->dbi_base + PCIE20_MULTI_LANE_CONTROL_OFF, size, val);

		/* wait until the link width change is complete */
		dw_pcie_read(pci->dbi_base + PCIE20_MULTI_LANE_CONTROL_OFF, size, &val);
		mdelay(50);
		dw_pcie_read(pci->dbi_base + PCIE20_MULTI_LANE_CONTROL_OFF, size, &val);

		/* check if Link is in L0 state */
		dw_pcie_read(pcie->parf + PCIE20_PARF_LTSSM, size, &val);
		if ((val & PCIE20_PARF_LTSSM_MASK) != 0x11)
			dev_info(devp,"After lane switch: Link is not in L0 state: %u\n",val);

		dw_pcie_read(pci->dbi_base + PCIE20_LINK_CONTROL_LINK_STATUS_REG, size, &val);

		dev_info(devp,"Link width is: %u\n",(val&0x3F00000)>>20);
	}

}

DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0101, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0104, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0106, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0107, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x0302, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x1000, qcom_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_QCOM, 0x1001, qcom_fixup_class);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_QCOM, 0x1109, qcom_ipq_tc_vc_mapping);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_QCOM, 0x1108, qcom_ipq_switch_lane);

static struct platform_driver qcom_pcie_driver = {
	.probe = qcom_pcie_probe,
	.driver = {
		.name = "qcom-pcie",
		.suppress_bind_attrs = true,
		.of_match_table = qcom_pcie_match,
	},
};
builtin_platform_driver(qcom_pcie_driver);
