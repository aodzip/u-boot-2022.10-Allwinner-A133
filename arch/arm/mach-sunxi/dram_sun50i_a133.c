// SPDX-License-Identifier: GPL-2.0+
/*
 * sun50i H616 platform dram controller driver
 *
 * While controller is very similar to that in H6, PHY is completely
 * unknown. That's why this driver has plenty of magic numbers. Some
 * meaning was nevertheless deduced from strings found in boot0 and
 * known meaning of some dram parameters.
 * This driver only supports DDR3 memory and omits logic for all
 * other supported types supported by hardware.
 *
 * (C) Copyright 2020 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 */
#include <common.h>
#include <init.h>
#include <log.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/dram.h>
#include <asm/arch/cpu.h>
#include <asm/arch/prcm.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kconfig.h>

#define debug printf

enum {
	MBUS_QOS_LOWEST = 0,
	MBUS_QOS_LOW,
	MBUS_QOS_HIGH,
	MBUS_QOS_HIGHEST
};

inline void mbus_configure_port(u8 port,
				bool bwlimit,
				bool priority,
				u8 qos,
				u8 waittime,
				u8 acs,
				u16 bwl0,
				u16 bwl1,
				u16 bwl2)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	const u32 cfg0 = ( (bwlimit ? (1 << 0) : 0)
			   | (priority ? (1 << 1) : 0)
			   | ((qos & 0x3) << 2)
			   | ((waittime & 0xf) << 4)
			   | ((acs & 0xff) << 8)
			   | (bwl0 << 16) );
	const u32 cfg1 = ((u32)bwl2 << 16) | (bwl1 & 0xffff);

	debug("MBUS port %d cfg0 %08x cfg1 %08x\n", port, cfg0, cfg1);
	writel_relaxed(cfg0, &mctl_com->master[port].cfg0);
	writel_relaxed(cfg1, &mctl_com->master[port].cfg1);
}

#define MBUS_CONF(port, bwlimit, qos, acs, bwl0, bwl1, bwl2)	\
	mbus_configure_port(port, bwlimit, false, \
			    MBUS_QOS_ ## qos, 0, acs, bwl0, bwl1, bwl2)

static void mctl_set_master_priority(void)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	/* enable bandwidth limit windows and set windows size 1us */
	writel(399, &mctl_com->tmr);
	writel(BIT(16), &mctl_com->bwcr);

	MBUS_CONF( 0, true, HIGHEST, 0,  256,  128,  100);
	MBUS_CONF( 1, true,    HIGH, 0, 1536, 1400,  256);
	MBUS_CONF( 2, true, HIGHEST, 0,  512,  256,   96);
	MBUS_CONF( 3, true,    HIGH, 0,  256,  100,   80);
	MBUS_CONF( 4, true,    HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF( 5, true,    HIGH, 2,  100,   64,   32);
	MBUS_CONF( 6, true,    HIGH, 2,  100,   64,   32);
	MBUS_CONF( 8, true,    HIGH, 0,  256,  128,   64);
	MBUS_CONF(11, true,    HIGH, 0,  256,  128,  100);
	MBUS_CONF(14, true,    HIGH, 0, 1024,  256,   64);
	MBUS_CONF(16, true, HIGHEST, 6, 8192, 2800, 2400);
	MBUS_CONF(21, true, HIGHEST, 6, 2048,  768,  512);
	MBUS_CONF(25, true, HIGHEST, 0,  100,   64,   32);
	MBUS_CONF(26, true,    HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(37, true,    HIGH, 0,  256,  128,   64);
	MBUS_CONF(38, true,    HIGH, 2,  100,   64,   32);
	MBUS_CONF(39, true,    HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(40, true,    HIGH, 2,  100,   64,   32);

	dmb();
}

static void mctl_sys_init(struct dram_para *para)
{
	struct sunxi_ccm_reg * const ccm =
			(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	/* Put all DRAM-related blocks to reset state */
	clrbits_le32(&ccm->mbus_cfg, MBUS_ENABLE);
	clrbits_le32(&ccm->mbus_cfg, MBUS_RESET);
	clrbits_le32(&ccm->dram_gate_reset, BIT(GATE_SHIFT));
	udelay(5);
	clrbits_le32(&ccm->dram_gate_reset, BIT(RESET_SHIFT));
	clrbits_le32(&ccm->pll5_cfg, CCM_PLL5_CTRL_EN);
	clrbits_le32(&ccm->dram_clk_cfg, DRAM_MOD_RESET);

	udelay(5);

	/* Set PLL5 rate to doubled DRAM clock rate */
	writel(CCM_PLL5_CTRL_EN | CCM_PLL5_LOCK_EN | CCM_PLL5_OUT_EN |
	       CCM_PLL5_CTRL_N(para->clk * 2 / 24), &ccm->pll5_cfg);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&ccm->pll5_cfg, CCM_PLL5_LOCK, CCM_PLL5_LOCK);

	/* Configure DRAM mod clock */
	writel(DRAM_CLK_SRC_PLL5, &ccm->dram_clk_cfg);
	setbits_le32(&ccm->dram_clk_cfg, DRAM_CLK_ENABLE);
	setbits_le32(&ccm->dram_clk_cfg, BIT(0) | BIT(1)); // FACTOR_N = 3
	printf("dram_clk_cfg: 0x%04x\n", ccm->dram_clk_cfg);
	writel(BIT(RESET_SHIFT), &ccm->dram_gate_reset);
	udelay(5);
	setbits_le32(&ccm->dram_gate_reset, BIT(GATE_SHIFT));

	// /* Disable all channels */
	// writel(0, &mctl_com->maer0); // ???
	// writel(0, &mctl_com->maer1); // ???
	// writel(0, &mctl_com->maer2); // ???

	/* Configure MBUS and enable DRAM mod reset */
	setbits_le32(&ccm->mbus_cfg, MBUS_RESET);
	setbits_le32(&ccm->mbus_cfg, MBUS_ENABLE);
	setbits_le32(&ccm->dram_clk_cfg, DRAM_MOD_RESET);
	udelay(5);
}

static void mctl_set_addrmap(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	u8 cols = para->cols;
	u8 rows = para->rows;
	u8 ranks = para->ranks;

	if (!para->bus_full_width)
		cols -= 1;

	/* Ranks */
	if (ranks == 2)
		mctl_ctl->addrmap[0] = rows + cols - 3;
	else
		mctl_ctl->addrmap[0] = 0x1F;

	/* Banks, hardcoded to 8 banks now */
	mctl_ctl->addrmap[1] = (cols - 2) | (cols - 2) << 8 | (cols - 2) << 16;

	/* Columns */
	mctl_ctl->addrmap[2] = 0;
	switch (cols) {
	case 7:
		mctl_ctl->addrmap[3] = 0x1F1F1F00;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 8:
		mctl_ctl->addrmap[3] = 0x1F1F0000;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 9:
		mctl_ctl->addrmap[3] = 0x1F000000;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 10:
		mctl_ctl->addrmap[3] = 0;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 11:
		mctl_ctl->addrmap[3] = 0;
		mctl_ctl->addrmap[4] = 0x1F00;
		break;
	case 12:
		mctl_ctl->addrmap[3] = 0;
		mctl_ctl->addrmap[4] = 0;
		break;
	default:
		panic("Unsupported DRAM configuration: column number invalid\n");
	}

	/* Rows */
	mctl_ctl->addrmap[5] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
	switch (rows) {
	case 13:
		mctl_ctl->addrmap[6] = (cols - 3) | 0x0F0F0F00;
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 14:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | 0x0F0F0000;
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 15:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | 0x0F000000;
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 16:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 17:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
		mctl_ctl->addrmap[7] = (cols - 3) | 0x0F00;
		break;
	case 18:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
		mctl_ctl->addrmap[7] = (cols - 3) | ((cols - 3) << 8);
		break;
	default:
		panic("Unsupported DRAM configuration: row number invalid\n");
	}

	/* Bank groups, DDR4 only */
	mctl_ctl->addrmap[8] = 0x3F3F;
}

static void mctl_phy_configure_odt(struct dram_para *para)
{
	u32 val;
	u32 dram_ca_dri, dram_dx_dri, dram_dx_odt, dram_tpr1;

	switch (para->type) {
	case SUNXI_DRAM_TYPE_DDR3: // TODO
		dram_ca_dri = 0;
		dram_dx_dri = 0;
		dram_tpr1 = 0;
		dram_dx_odt = 0;
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		dram_ca_dri = 0x1919;
		dram_dx_dri = 0x0c0c0c0c;
		dram_tpr1 = 0x0;
		dram_dx_odt = 0x06060606;
		break;
	case SUNXI_DRAM_TYPE_DDR4: // TODO
		dram_ca_dri = 0;
		dram_dx_dri = 0;
		dram_tpr1 = 0;
		dram_dx_odt = 0;
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		dram_ca_dri = 0x0e0e;
		dram_dx_dri = 0x0d0d0d0d;
		dram_tpr1 = 0x04040404;
		dram_dx_odt = 0x07070707;
		break;
	}

	writel(dram_dx_dri & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x388);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x388);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x38c);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		if((dram_tpr1 & 0x1f1f1f1f) != 0)
			writel(dram_tpr1 & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x38c);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x38c);
	}

	writel((dram_dx_dri >> 8) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x3c8);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x3c8);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3cc);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		if((dram_tpr1 & 0x1f1f1f1f) != 0)
			writel((dram_tpr1 >> 8) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x3cc);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x3cc);
	}

	writel((dram_dx_dri >> 16) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x408);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x408);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x40c);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		if((dram_tpr1 & 0x1f1f1f1f) != 0)
			writel((dram_tpr1 >> 16) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x40c);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x40c);
	}

	writel((dram_dx_dri >> 24) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x448);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x448);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x44c);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		if((dram_tpr1 & 0x1f1f1f1f) != 0)
			writel((dram_tpr1 >> 24) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x44c);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x44c);
	}

	writel(dram_ca_dri & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x340);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x340);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x344);

	writel((dram_ca_dri >> 8) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x348);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x348);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x34c);

	val = dram_dx_odt & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x380);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x380);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x384);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x384);
	}

	val = (dram_dx_odt >> 8) & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3c0);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x3c0);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3c4);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x3c4);
	}

	val = (dram_dx_odt >> 16) & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x400);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x400);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x404);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x404);
	}

	val = (dram_dx_odt >> 24) & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x440);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x440);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x444);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x444);
	}
}

static void mctl_phy_ca_bit_delay_compensation(struct dram_para *para) {
	u32 *ptr;
	u32 i, a, b, c, d;
	u32 dram_tpr10, dram_tpr0;

	switch (para->type) {
	case SUNXI_DRAM_TYPE_DDR3: // TODO
		dram_tpr10 = 0;
		dram_tpr0 = 0;
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		dram_tpr10 = 0x002f876b;
		dram_tpr0 = 0x0;
		break;
	case SUNXI_DRAM_TYPE_DDR4: // TODO
		dram_tpr10 = 0;
		dram_tpr0 = 0;
		break;
	case SUNXI_DRAM_TYPE_LPDDR4: // TODO
		dram_tpr10 = 0;
		dram_tpr0 = 0;
		break;
	}

	if (dram_tpr10 >= 0) { // Sorry for direct copy from decompiler
		dram_tpr0 = ((32 * dram_tpr10) & 0x1E00) | ((dram_tpr10 << 9) & 0x1E0000) | ((2 * dram_tpr10) & 0x1E) | ((dram_tpr10 << 13) & 0x1E000000);
		if(dram_tpr10 >> 29)
			dram_tpr0 *= 2;
	}

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x780);
	for (i = 0; i < 32; i++) {
		writel((dram_tpr0 >> 8) & 0x3F, ptr);
		ptr += 4;
	}

	a = dram_tpr0 & 0x3f;
	b = dram_tpr0 & 0x3f;
	c = (dram_tpr0 >> 16) & 0x3f;
	d = (dram_tpr0 >> 24) & 0x3f;

	switch (readl(SUNXI_SID_BASE)) { // Seems like allwinner fab factory change
	case 0x800:
	case 0x2400:
		switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR3:
			writel(a, SUNXI_DRAM_PHY0_BASE + 0x7dc);
			writel(b, SUNXI_DRAM_PHY0_BASE + 0x7e0);
			writel(c, SUNXI_DRAM_PHY0_BASE + 0x7e4);
			writel(d, SUNXI_DRAM_PHY0_BASE + 0x2388); // ??? WTF
			break;
		case SUNXI_DRAM_TYPE_LPDDR4:
			writel(a, SUNXI_DRAM_PHY0_BASE + 0x7dc);
			writel(b, SUNXI_DRAM_PHY0_BASE + 0x7e0);
			writel(c, SUNXI_DRAM_PHY0_BASE + 0x7e4);
			writel(d, SUNXI_DRAM_PHY0_BASE + 0x790);
			break;
		default:
			break;
		}
		break;
	default:
		switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR3:
			writel(a, SUNXI_DRAM_PHY0_BASE + 0x7dc);
			writel(b, SUNXI_DRAM_PHY0_BASE + 0x7e0);
			writel(c, SUNXI_DRAM_PHY0_BASE + 0x7b8);
			writel(d, SUNXI_DRAM_PHY0_BASE + 0x784);
			break;
		case SUNXI_DRAM_TYPE_LPDDR3:
			writel(a, SUNXI_DRAM_PHY0_BASE + 0x7dc);
			writel(b, SUNXI_DRAM_PHY0_BASE + 0x7e0);
			writel(c, SUNXI_DRAM_PHY0_BASE + 0x788);
			writel(d, SUNXI_DRAM_PHY0_BASE + 0x790);
			break;
		case SUNXI_DRAM_TYPE_DDR4:
			writel(a, SUNXI_DRAM_PHY0_BASE + 0x7dc);
			writel(b, SUNXI_DRAM_PHY0_BASE + 0x7e0);
			writel(c, SUNXI_DRAM_PHY0_BASE + 0x784);
			break;
		case SUNXI_DRAM_TYPE_LPDDR4:
			writel(a, SUNXI_DRAM_PHY0_BASE + 0x7dc);
			writel(b, SUNXI_DRAM_PHY0_BASE + 0x7e0);
			writel(c, SUNXI_DRAM_PHY0_BASE + 0x790);
			writel(d, SUNXI_DRAM_PHY0_BASE + 0x78c);
			break;
		}
		break;
	}
}

static bool mctl_phy_write_leveling(struct dram_para *para)
{
	bool result = true;
	u32 val;

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0, 0x80);
	writel(4, SUNXI_DRAM_PHY0_BASE + 0xc);
	writel(0x40, SUNXI_DRAM_PHY0_BASE + 0x10);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

	if (para->bus_full_width)
		val = 0xf;
	else
		val = 3;

	printf("line: %d\n", __LINE__);
	mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0x188), val, val);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

	val = readl(SUNXI_DRAM_PHY0_BASE + 0x258);
	if (val == 0 || val == 0x3f)
		result = false;
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x25c);
	if (val == 0 || val == 0x3f)
		result = false;
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x318);
	if (val == 0 || val == 0x3f)
		result = false;
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x31c);
	if (val == 0 || val == 0x3f)
		result = false;

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0);

	if (para->ranks == 2) {
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0, 0x40);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

		if (para->bus_full_width)
			val = 0xf;
		else
			val = 3;

		printf("line: %d\n", __LINE__);
		mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0x188), val, val);

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0);

	return result;
}

static bool mctl_phy_read_calibration(struct dram_para *para)
{
	bool result = true;
	u32 val, tmp;

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x20);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

	if (para->bus_full_width)
		val = 0xf;
	else
		val = 3;

	while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val) {
		if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20) {
			result = false;
			break;
		}
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

	if (para->ranks == 2) {
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x10);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

		while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val) {
			if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20) {
				result = false;
				break;
			}
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

	val = readl(SUNXI_DRAM_PHY0_BASE + 0x274) & 7;
	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x26c) & 7;
	if (val < tmp)
		val = tmp;
	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x32c) & 7;
	if (val < tmp)
		val = tmp;
	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x334) & 7;
	if (val < tmp)
		val = tmp;
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x38, 0x7, (val + 2) & 7);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 4, 0x20);

	return result;
}

static bool mctl_phy_read_training(struct dram_para *para)
{
	u32 val1, val2, *ptr1, *ptr2;
	bool result = true;
	int i;

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3, 2);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x804, 0x3f, 0xf);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x808, 0x3f, 0xf);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0xa04, 0x3f, 0xf);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0xa08, 0x3f, 0xf);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 6);
	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 1);

	printf("line: %d\n", __LINE__);
	mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0x840), 0xc, 0xc);
	if (readl(SUNXI_DRAM_PHY0_BASE + 0x840) & 3)
		result = false;

	if (para->bus_full_width) {
		printf("line: %d\n", __LINE__);
		mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0xa40), 0xc, 0xc);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0xa40) & 3)
			result = false;
	}

	ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x898);
	ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x850);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}
	ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x8bc);
	ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x874);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}

	if (para->bus_full_width) {
		ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xa98);
		ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xa50);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}

		ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xabc);
		ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xa74);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 3);

	if (para->ranks == 2) {
		/* maybe last parameter should be 1? */
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3, 2);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 6);
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 1);

		printf("line: %d\n", __LINE__);
		mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0x840), 0xc, 0xc);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0x840) & 3)
			result = false;

		if (para->bus_full_width) {
			printf("line: %d\n", __LINE__);
			mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0xa40), 0xc, 0xc);
			if (readl(SUNXI_DRAM_PHY0_BASE + 0xa40) & 3)
				result = false;
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 3);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3);

	return result;
}

static bool mctl_phy_write_training(struct dram_para *para)
{
	u32 val1, val2, *ptr1, *ptr2;
	bool result = true;
	int i;

	writel(0, SUNXI_DRAM_PHY0_BASE + 0x134);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x138);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x19c);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x1a0);

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc, 8);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);
	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x20);

	printf("line: %d\n", __LINE__);
	mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0x8e0), 3, 3);
	if (readl(SUNXI_DRAM_PHY0_BASE + 0x8e0) & 0xc)
		result = false;

	if (para->bus_full_width) {
		printf("line: %d\n", __LINE__);
		mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0xae0), 3, 3);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0xae0) & 0xc)
			result = false;
	}

	ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x938);
	ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x8f0);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}
	ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x95c);
	ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x914);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}

	if (para->bus_full_width) {
		ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xb38);
		ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xaf0);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}
		ptr1 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xb5c);
		ptr2 = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xb14);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x60);

	if (para->ranks == 2) {
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc, 4);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x20);

		printf("line: %d\n", __LINE__);
		mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0x8e0), 3, 3);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0x8e0) & 0xc)
			result = false;

		if (para->bus_full_width) {
			printf("line: %d\n", __LINE__);
			mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0xae0), 3, 3);
			if (readl(SUNXI_DRAM_PHY0_BASE + 0xae0) & 0xc)
				result = false;
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x60);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc);

	return result;
}

static bool mctl_phy_dx_bit_delay_compensation(struct dram_para *para)
{
	u32 *ptr;
	int i;

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);
	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 8);
	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x484);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x16, ptr);
		writel_relaxed(0x16, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x4d0);
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x590);
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x4cc);
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x58c);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x4d8);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x1a, ptr);
		writel_relaxed(0x1a, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x524);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x5e4);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x520);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x5e0);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x604);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x1a, ptr);
		writel_relaxed(0x1a, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x650);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x710);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x64c);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x70c);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x658);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x1a, ptr);
		writel_relaxed(0x1a, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x6a4);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x764);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x6a0);
	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x760);

	dmb();

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);

	/* second part */
	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);
	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 4);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x480);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x10, ptr);
		writel_relaxed(0x10, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x528);
	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x5e8);
	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x4c8);
	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x588);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x4d4);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x12, ptr);
		writel_relaxed(0x12, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x52c);
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x5ec);
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x51c);
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x5dc);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x600);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x12, ptr);
		writel_relaxed(0x12, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x6a8);
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x768);
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x648);
	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x708);

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0x654);
	for (i = 0; i < 9; i++) {
		writel_relaxed(0x14, ptr);
		writel_relaxed(0x14, ptr + 0x30);
		ptr += 2;
	}
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x6ac);
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x76c);
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x69c);
	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x75c);

	dmb();

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);

	return true;
}

static unsigned char phy_init_ddr3_a[] = {
	0x0C, 0x08, 0x19, 0x18, 0x10, 0x06, 0x0A, 0x03, 
	0x0E, 0x00, 0x0B, 0x05, 0x09, 0x1A, 0x04, 0x13, 
	0x16, 0x11, 0x01, 0x15, 0x0D, 0x07, 0x12, 0x17, 
	0x14, 0x02, 0x0F
	};
static unsigned char phy_init_ddr4_a[] = {
	0x19, 0x1A, 0x04, 0x12, 0x09, 0x06, 0x08, 0x0A, 
	0x16, 0x17, 0x18, 0x0F, 0x0C, 0x13, 0x02, 0x05, 
	0x01, 0x11, 0x0E, 0x00, 0x0B, 0x07, 0x03, 0x14, 
	0x15, 0x0D, 0x10
	};
static unsigned char phy_init_lpddr3_a[] = {
	0x08, 0x03, 0x02, 0x00, 0x18, 0x19, 0x09, 0x01, 
	0x06, 0x17, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x04, 
	0x05, 0x07, 0x1A
	};
static unsigned char phy_init_lpddr4_a[] = {
	0x01, 0x05, 0x02, 0x00, 0x19, 0x03, 0x06, 0x07, 
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 
	0x18, 0x04, 0x1A
	};

static unsigned char phy_init_ddr3_b[] = {
	0x03, 0x19, 0x18, 0x02, 0x10, 0x15, 0x16, 0x07, 
	0x06, 0x0E, 0x05, 0x08, 0x0D, 0x04, 0x17, 0x1A, 
	0x13, 0x11, 0x12, 0x14, 0x00, 0x01, 0xC, 0x0A, 
	0x09, 0x0B, 0x0F
	};
static unsigned char phy_init_ddr4_b[] = {
	0x13, 0x17, 0xE, 0x01, 0x06, 0x12, 0x14, 0x07, 
	0x09, 0x02, 0x0F, 0x00, 0x0D, 0x05, 0x16, 0x0C, 
	0x0A, 0x11, 0x04, 0x03, 0x18, 0x15, 0x08, 0x10, 
	0x0B, 0x19, 0x1A
	};
static unsigned char phy_init_lpddr3_b[] = {
	0x05, 0x06, 0x17, 0x02, 0x19, 0x18, 0x04, 0x07, 
	0x03, 0x01, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x08, 
	0x09, 0x00, 0x1A
	};
static unsigned char phy_init_lpddr4_b[] = {
	0x01, 0x03, 0x02, 0x19, 0x17, 0x00, 0x06, 0x07, 
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x04, 
	0x18, 0x05, 0x1A
	};

static bool mctl_phy_init(struct dram_para *para)
{
	struct sunxi_prcm_reg *const prcm =
		(struct sunxi_prcm_reg *)SUNXI_PRCM_BASE;
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	u32 val, *ptr;
	u8 *phy_init;
	int i;

	/* begin phy_para_config */
	clrbits_le32(&prcm->sys_pwroff_gating, BIT(4));

	if(para->type == SUNXI_DRAM_TYPE_LPDDR4)
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x4, BIT(7));

	if (para->bus_full_width)
		val = 0xf;
	else
		val = 3;
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x3c, 0xf, val);

	switch (para->type) {
	case SUNXI_DRAM_TYPE_DDR3:
		val = 13;
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		val = 14;
		break;
	case SUNXI_DRAM_TYPE_DDR4:
		val = 13;
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		val = 20;
		// libdram check tpr13 BIT(28)
		/*
		if(tpr13 & BIT(28))
			val = 22
		*/
		break;
	}

	writel(val, SUNXI_DRAM_PHY0_BASE + 0x14);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x35c);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x368);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x374);

	writel(0, SUNXI_DRAM_PHY0_BASE + 0x18);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x360);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x36c);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x378);

	switch (para->type) {
	case SUNXI_DRAM_TYPE_DDR3:
		val = 9;
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		val = 8;
		break;
	case SUNXI_DRAM_TYPE_DDR4:
		val = 10;
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		val = 10;
		break;
	}

	writel(val, SUNXI_DRAM_PHY0_BASE + 0x1c);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x364);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x370);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x37c);

	/* begin mctl_phy_set_address_remapping */
	switch (readl(SUNXI_SID_BASE)) {
	case 0x800:
	case 0x2400:
		switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR3:
			phy_init = phy_init_ddr3_a;
			break;
		case SUNXI_DRAM_TYPE_LPDDR3:
			phy_init = phy_init_lpddr3_a;
			break;
		case SUNXI_DRAM_TYPE_DDR4:
			phy_init = phy_init_ddr4_a;
			break;
		case SUNXI_DRAM_TYPE_LPDDR4:
			phy_init = phy_init_lpddr4_a;
			break;
		}
		break;
	default:
		switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR3:
			phy_init = phy_init_ddr3_b;
			break;
		case SUNXI_DRAM_TYPE_LPDDR3:
			phy_init = phy_init_lpddr3_b;
			break;
		case SUNXI_DRAM_TYPE_DDR4:
			phy_init = phy_init_ddr4_b;
			break;
		case SUNXI_DRAM_TYPE_LPDDR4:
			phy_init = phy_init_lpddr4_b;
			break;
		}
		break;
	}

	ptr = (u32*)(SUNXI_DRAM_PHY0_BASE + 0xc0);
	for (i = 0; i < 27; i++)
		writel(phy_init[i], &ptr[i]);
	/* end mctl_phy_set_address_remapping */

	/* begin mctl_phy_vref_config */
	writel(0x80, SUNXI_DRAM_PHY0_BASE + 0x3dc);
	writel(0x80, SUNXI_DRAM_PHY0_BASE + 0x45c);
	/* end mctl_phy_vref_config */

	/* begin mctl_drive_odt_config */
	if (IS_ENABLED(CONFIG_DRAM_ODT_EN))
		mctl_phy_configure_odt(para);
	/* end mctl_drive_odt_config */

	if (IS_ENABLED(DRAM_SUN50I_A133_CA_BIT_DELAY_COMPENSATION)) {
		/* begin mctl_phy_ca_bit_delay_compensation */
		mctl_phy_ca_bit_delay_compensation(para);
		/* end mctl_phy_ca_bit_delay_compensation */
	}

	val = readl(SUNXI_DRAM_PHY0_BASE + 4) & 0xFFFFFFF8;

	switch (para->type) {
	case SUNXI_DRAM_TYPE_DDR3:
		val |= 2;
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		val |= 3;
		break;
	case SUNXI_DRAM_TYPE_DDR4:
		val |= 4;
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		val |= 5;
		break;
	}
	writel(val | 8, SUNXI_DRAM_PHY0_BASE + 4);

	if (para->clk <= 672)
		writel(0xf, SUNXI_DRAM_PHY0_BASE + 0x20);
	if (para->clk > 500) {
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x144, BIT(7));
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 0xe0);
	} else {
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x144, BIT(7));
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 0xe0, 0x20);
	}

	clrbits_le32(&mctl_com->unk_0x008, BIT(9));
	udelay(1);
	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 8);

	printf("line: %d\n", __LINE__);
	mctl_await_completion((u32*)(SUNXI_DRAM_PHY0_BASE + 0x180), 4, 4);

	if (IS_ENABLED(DRAM_SUN50I_A133_DELAY_ON_PHY_CONFIG)) {
		udelay(1000);
	}

	writel(0x37, SUNXI_DRAM_PHY0_BASE + 0x58);
	setbits_le32(&prcm->sys_pwroff_gating, BIT(4));
	/* end phy_para_config */

	/* begin mctl_dfi_init */
	setbits_le32(&mctl_com->maer0, 0x100);

	setbits_le32(&mctl_ctl->dfimisc, 1);
	setbits_le32(&mctl_ctl->dfimisc, 0x20);
	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);

	clrbits_le32(&mctl_ctl->dfimisc, 0x20);
	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->dfistat, 1, 1);

	clrbits_le32(&mctl_ctl->pwrctl, 0x20);
	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->statr, 3, 1);

	if (IS_ENABLED(DRAM_SUN50I_A133_DELAY_ON_PHY_CONFIG)) {
		udelay(200);
	}

	clrbits_le32(&mctl_ctl->dfimisc, 1);

	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	//OK

	switch (para->type) {
	case SUNXI_DRAM_TYPE_DDR3:
		writel(0x1f14, &mctl_ctl->mrctrl1); // dram_mr0
		writel(0x80000030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(4, &mctl_ctl->mrctrl1); // dram_mr1
		writel(0x80001030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(0x20, &mctl_ctl->mrctrl1); // dram_mr2
		writel(0x80002030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(0, &mctl_ctl->mrctrl1); // dram_mr3
		writel(0x80003030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		writel(0xc3 | 0x100, &mctl_ctl->mrctrl1); // dram_mr1
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(0x6 | 0x200, &mctl_ctl->mrctrl1); // dram_mr2
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(0x2 | 0x300, &mctl_ctl->mrctrl1); // dram_mr3
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(0x0 | 0xb00, &mctl_ctl->mrctrl1); // dram_mr11
		writel(0x80000030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
		break;
	case SUNXI_DRAM_TYPE_DDR4: // TODO
		break;
	case SUNXI_DRAM_TYPE_LPDDR4: // TODO
		break;
	}

	writel(0, SUNXI_DRAM_PHY0_BASE + 0x54);

	/* end mctl_dfi_init */

	writel(0, &mctl_ctl->swctl);
	clrbits_le32(&mctl_ctl->rfshctl3, 1);
	writel(1, &mctl_ctl->swctl);
	// OK

	if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_WRITE_LEVELING)) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_write_leveling(para))
				break;
		if (i == 5) {
			debug("write leveling failed!\n");
			return false;
		}
	}

	if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_READ_CALIBRATION)) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_read_calibration(para))
				break;
		if (i == 5) {
			debug("read calibration failed!\n");
			return false;
		}
	}

	if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_READ_TRAINING)) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_read_training(para))
				break;
		if (i == 5) {
			debug("read training failed!\n");
			return false;
		}
	}

	if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_WRITE_TRAINING)) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_write_training(para))
				break;
		if (i == 5) {
			debug("write training failed!\n");
			return false;
		}
	}

	if (IS_ENABLED(DRAM_SUN50I_A133_DX_BIT_DELAY_COMPENSATION))
		mctl_phy_dx_bit_delay_compensation(para);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 4);

	return true;
}

static bool mctl_ctrl_init(struct dram_para *para) // mctl_channel_init
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	u32 reg_val;

	clrsetbits_le32(&mctl_com->unk_0x008, BIT(25) | BIT(24) | BIT(9), BIT(25) | BIT(9)); // CLEAR AND SET SAME BIT ???

	setbits_le32(&mctl_com->maer0, BIT(15));

	/* begin mctl_com_set_bus_config */
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		(*((int *)0x03102ea8)) |= BIT(0); // NSI Register ??
	}
	clrsetbits_le32(&mctl_ctl->sched[0], 0xff00, 0x3000);
	// Allwinner vendor libdram check tpr13 BIT(28) here, since A133 dont have such case in LPDDR3 & LPDDR4, just ignore it
	/*
	if (tpr13 & BIT(28)) {
		clrsetbits_le32(&mctl_ctl->sched[0], 0xf, BIT(0));
	}
	*/
	/* end mctl_com_set_bus_config */

	writel(0, &mctl_ctl->hwlpctl);

	/* begin mctl_com_init */

	/* begin mctl_com_set_controller_config */
	switch(para->type) {
	case SUNXI_DRAM_TYPE_DDR3:
		reg_val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_DDR3; // v1 = 0x40001
		break;

	case SUNXI_DRAM_TYPE_DDR4: // unk_35C8+4*(0+234)
		reg_val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_DDR4; // v1 = 0x40010
		break;

	case SUNXI_DRAM_TYPE_LPDDR3: // unk_35C8+4*(3+234)
		reg_val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_LPDDR3; // v1 = 0x40008
		break;
	
	case SUNXI_DRAM_TYPE_LPDDR4: // unk_35C8+4*(4+234)
		reg_val = MSTR_BURST_LENGTH(16) | MSTR_DEVICETYPE_LPDDR4; // v1 = 0x800020
		break;
	}
	reg_val |=  MSTR_ACTIVE_RANKS(para->ranks); //((((dram_para2 >> 11) & 6) + 1) << 24)
	if (para->bus_full_width)
		reg_val |= MSTR_BUSWIDTH_FULL; // (result->dram_para2 << 12) & 0x1000
	else
		reg_val |= MSTR_BUSWIDTH_HALF;
	writel(BIT(31) | BIT(30) | reg_val, &mctl_ctl->mstr); // ( | 0xC0000000 )
	/* end mctl_com_set_controller_config */

	if (para->type == SUNXI_DRAM_TYPE_DDR4) {
		/* begin mctl_com_set_controller_geardown_mode */
		// DDR4 geardown mode: tpr13 BIT(30), since we dont have a DDR4 fex, just leave it here
		/*
		if (tpr13 & BIT(30)) {
			setbits_le32(&mctl_ctl->mstr, MSTR_DEVICETYPE_DDR3);
		}
		*/
		/* end mctl_com_set_controller_geardown_mode */
	}

	if (para->type <= SUNXI_DRAM_TYPE_DDR4) { // NOT LPDDR
		/* begin mctl_com_set_controller_2T_mode */
		// check tpr13 BIT(5), since we dont have a DDR3 || DDR4 board on A133 ,just leave it here
		/*
		if (mctl_ctl->mstr & BIT(11) || tpr13 & BIT(5)) {
			clrsetbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
		}
		*/
		setbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
		/* end mctl_com_set_controller_2T_mode */
	}
	
	/* begin mctl_com_set_controller_odt */
	if (para->ranks == 2)
		writel(0x0303, &mctl_ctl->odtmap);
	else
		writel(0x0201, &mctl_ctl->odtmap);
	
	switch (para->type)
	{
		case SUNXI_DRAM_TYPE_DDR3:
			reg_val = 0x06000400;
			break;

		case SUNXI_DRAM_TYPE_DDR4:
			// later~
			break;

		case SUNXI_DRAM_TYPE_LPDDR3:
			if(para->clk >= 400)
				reg_val = ((7 * para->clk / 2000 + 7) << 24) | 0x400 | ((4 - 7 * para->clk / 2000) << 16);
			else
				reg_val = ((7 * para->clk / 2000 + 7) << 24) | 0x400 | ((3 - 7 * para->clk / 2000) << 16);
			break;

		case SUNXI_DRAM_TYPE_LPDDR4:
			reg_val = 0x04000400;
			break;
	}
	writel(reg_val, &mctl_ctl->odtcfg);
	writel(reg_val, &mctl_ctl->unk_0x2240);
	writel(reg_val, &mctl_ctl->unk_0x3240);
	writel(reg_val, &mctl_ctl->unk_0x4240);
	/* end mctl_com_set_controller_odt */

	/* begin mctl_com_set_controller_address_map */
	mctl_set_addrmap(para);
	/* end mctl_com_set_controller_address_map */

	printf("line: %d\n", __LINE__);
	/* begin mctl_com_set_channel_timing */
	mctl_set_timing_params(para);
	/* end mctl_com_set_channel_timing */
	printf("line: %d\n", __LINE__);

	writel(0, &mctl_ctl->pwrctl);

	/* begin mctl_com_set_controller_update */
	setbits_le32(&mctl_ctl->dfiupd[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->zqctl[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x2180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x3180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x4180, BIT(31) | BIT(30));
	/* end mctl_com_set_controller_update */

	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR4) {
		/* begin mctl_com_set_controller_dbi */
		// tpr13 BIT(29), still missing fex so leave it alone
		/*
		if (tpr13 & BIT(29))
			setbits_le32(&mctl_ctl->dbictl, BIT(2));
		*/
		/* end mctl_com_set_controller_dbi */
	}

	/* begin mctl_com_set_controller_before_phy */

	/* begin mctl_com_set_controller_refresh */
	// libdram hardcoded 1
	setbits_le32(&mctl_ctl->rfshctl3, BIT(0));
	/* end mctl_com_set_controller_refresh */

	clrbits_le32(&mctl_ctl->dfimisc, BIT(0));
	writel(0x20, &mctl_ctl->pwrctl);
	/* end mctl_com_set_controller_before_phy */

	/* end mctl_com_init */

	/* begin mctl_phy_init */

	/* begin mctl_phy_cold_reset */
	clrsetbits_le32(&mctl_com->unk_0x008, BIT(24) | BIT(9), BIT(9));
	udelay(1);
	setbits_le32(&mctl_com->unk_0x008, BIT(24));
	/* end mctl_phy_cold_reset */

	/* begin ddrphy_phyinit_C_initPhyConfig */
	if (!mctl_phy_init(para))
		return false;
	/* end ddrphy_phyinit_C_initPhyConfig */

	/* end mctl_phy_init */

	/* begin mctl_com_set_controller_after_phy */
	writel(0, &mctl_ctl->swctl);
	
	/* begin mctl_com_set_controller_refresh */
	// libdram hardcoded 0
	clrbits_le32(&mctl_ctl->rfshctl3, BIT(0));
	/* end mctl_com_set_controller_refresh */

	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	/* end mctl_com_set_controller_after_phy */

	(*((int *)0x07022004)) = 0x00007177;

	return true;
}

static bool mctl_core_init(struct dram_para *para)
{
	mctl_sys_init(para);

	return mctl_ctrl_init(para);
}

static void mctl_auto_detect_rank_width(struct dram_para *para)
{
	/* this is minimum size that it's supported */
	para->cols = 8;
	para->rows = 13;

	/*
	 * Strategy here is to test most demanding combination first and least
	 * demanding last, otherwise HW might not be fully utilized. For
	 * example, half bus width and rank = 1 combination would also work
	 * on HW with full bus width and rank = 2, but only 1/4 RAM would be
	 * visible.
	 */

	debug("testing 32-bit width, rank = 2\n");
	para->bus_full_width = 1;
	para->ranks = 2;
	if (mctl_core_init(para))
		return;

	debug("testing 32-bit width, rank = 1\n");
	para->bus_full_width = 1;
	para->ranks = 1;
	if (mctl_core_init(para))
		return;

	debug("testing 16-bit width, rank = 2\n");
	para->bus_full_width = 0;
	para->ranks = 2;
	if (mctl_core_init(para))
		return;

	debug("testing 16-bit width, rank = 1\n");
	para->bus_full_width = 0;
	para->ranks = 1;
	if (mctl_core_init(para))
		return;

	panic("This DRAM setup is currently not supported.\n");
}

static void mctl_auto_detect_dram_size(struct dram_para *para)
{
	/* detect row address bits */
	para->cols = 8;
	para->rows = 18;
	mctl_core_init(para);

	for (para->rows = 13; para->rows < 18; para->rows++) {
		/* 8 banks, 8 bit per byte and 16/32 bit width */
		if (mctl_mem_matches((1 << (para->rows + para->cols +
					    4 + para->bus_full_width))))
			break;
	}

	/* detect column address bits */
	para->cols = 11;
	mctl_core_init(para);

	for (para->cols = 8; para->cols < 11; para->cols++) {
		/* 8 bits per byte and 16/32 bit width */
		if (mctl_mem_matches(1 << (para->cols + 1 +
					   para->bus_full_width)))
			break;
	}
}

static unsigned long mctl_calc_size(struct dram_para *para)
{
	u8 width = para->bus_full_width ? 4 : 2;

	/* 8 banks */
	return (1ULL << (para->cols + para->rows + 3)) * width * para->ranks;
}

unsigned long sunxi_dram_init(void)
{
	struct sunxi_prcm_reg *const prcm =
		(struct sunxi_prcm_reg *)SUNXI_PRCM_BASE;
	struct dram_para para = {
		.clk = CONFIG_DRAM_CLK,
		.type = SUNXI_DRAM_TYPE_LPDDR3,
	};
	unsigned long size;

	setbits_le32(&prcm->res_cal_ctrl, BIT(8));
	clrbits_le32(&prcm->ohms240, 0x3f);

	mctl_auto_detect_rank_width(&para);
	mctl_auto_detect_dram_size(&para);

	mctl_core_init(&para);

	size = mctl_calc_size(&para);

	mctl_set_master_priority();

	return size;
};
