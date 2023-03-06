// SPDX-License-Identifier: GPL-2.0+
/*
 * sun50iw10p1 libdram reverse engineer version
 *
 * Based on H616 one, which is:
 * (C) Copyright 2020 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 */
#include <stdbool.h>
#include <inttypes.h>
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

static struct sunxi_ccm_reg *const ccm = (struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
static struct sunxi_prcm_reg *const prcm = (struct sunxi_prcm_reg *)SUNXI_PRCM_BASE;
static struct sunxi_mctl_com_reg *const mctl_com = (struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
static struct sunxi_mctl_ctl_reg *const mctl_ctl = (struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

struct dram_para para __section(".data") = {
	.clk = CONFIG_DRAM_CLK,
	.type = SUNXI_DRAM_TYPE_LPDDR3,
	.dx_odt = 0x06060606,
	.dx_dri = 0x0c0c0c0c,
	.ca_dri = 0x1919,
	.para0 = 0x16171411,
	.para1 = 0x30eb,
	.para2 = 0x0000,
	.mr0 = 0x0,
	.mr1 = 0xc3,
	.mr2 = 0x6,
	.mr3 = 0x2,
	.mr4 = 0x0,
	.mr5 = 0x0,
	.mr6 = 0x0,
	.mr11 = 0x0,
	.mr12 = 0x0,
	.mr13 = 0x0,
	.mr14 = 0x0,
	.mr16 = 0x0,
	.mr17 = 0x0,
	.mr22 = 0x0,
	.tpr0 = 0x0,
	.tpr1 = 0x0,
	.tpr2 = 0x0,
	.tpr3 = 0x0,
	.tpr6 = 0x2fb48080,
	.tpr10 = 0x002f876b,
	.tpr11 = 0x10120c05,
	.tpr12 = 0x12121111,
	.tpr13 = 0x60,
	.tpr14 = 0x211e1e22,
};

static struct dram_timing channel_timing __section(".data") = {
	.trtp = 3,
	.unk_4 = 3,
	.trp = 6,
	.tckesr = 3,
	.trcd = 6,
	.trrd = 3,
	.tmod = 6,
	.unk_42 = 4,
	.txsr = 4,
	.txs = 4,
	.unk_66 = 8,
	.unk_69 = 8,
	.unk_50 = 1,
	.unk_63 = 2,
	.tcksre = 4,
	.tcksrx = 4,
	.trd2wr = 4,
	.trasmax = 27,
	.twr2rd = 8,
	.twtp = 12,
	.trfc = 128,
	.trefi = 98,
	.txp = 10,
	.tfaw = 16,
	.tras = 14,
	.trc = 20,
	.tcke = 2,
	.tmrw = 0,
	.tccd = 2,
	.tmrd = 2,
	.tcwl = 3,
	.tcl = 3,
	.unk_43 = 1,
	.unk_44 = 1,
};

static const unsigned char phy_init_ddr3_a[] = {
	0x0C, 0x08, 0x19, 0x18, 0x10, 0x06, 0x0A, 0x03,
	0x0E, 0x00, 0x0B, 0x05, 0x09, 0x1A, 0x04, 0x13,
	0x16, 0x11, 0x01, 0x15, 0x0D, 0x07, 0x12, 0x17,
	0x14, 0x02, 0x0F};
static const unsigned char phy_init_lpddr3_a[] = {
	0x08, 0x03, 0x02, 0x00, 0x18, 0x19, 0x09, 0x01,
	0x06, 0x17, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x04,
	0x05, 0x07, 0x1A};
static const unsigned char phy_init_ddr4_a[] = {
	0x19, 0x1A, 0x04, 0x12, 0x09, 0x06, 0x08, 0x0A,
	0x16, 0x17, 0x18, 0x0F, 0x0C, 0x13, 0x02, 0x05,
	0x01, 0x11, 0x0E, 0x00, 0x0B, 0x07, 0x03, 0x14,
	0x15, 0x0D, 0x10};
static const unsigned char phy_init_lpddr4_a[] = {
	0x01, 0x05, 0x02, 0x00, 0x19, 0x03, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x04, 0x1A};

static const unsigned char phy_init_ddr3_b[] = {
	0x03, 0x19, 0x18, 0x02, 0x10, 0x15, 0x16, 0x07,
	0x06, 0x0E, 0x05, 0x08, 0x0D, 0x04, 0x17, 0x1A,
	0x13, 0x11, 0x12, 0x14, 0x00, 0x01, 0xC, 0x0A,
	0x09, 0x0B, 0x0F};
static const unsigned char phy_init_lpddr3_b[] = {
	0x05, 0x06, 0x17, 0x02, 0x19, 0x18, 0x04, 0x07,
	0x03, 0x01, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x08,
	0x09, 0x00, 0x1A};
static const unsigned char phy_init_ddr4_b[] = {
	0x13, 0x17, 0xE, 0x01, 0x06, 0x12, 0x14, 0x07,
	0x09, 0x02, 0x0F, 0x00, 0x0D, 0x05, 0x16, 0x0C,
	0x0A, 0x11, 0x04, 0x03, 0x18, 0x15, 0x08, 0x10,
	0x0B, 0x19, 0x1A};
static const unsigned char phy_init_lpddr4_b[] = {
	0x01, 0x03, 0x02, 0x19, 0x17, 0x00, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x04,
	0x18, 0x05, 0x1A};

static void libdram_mctl_await_completion(uint32_t *reg, uint32_t mask, uint32_t val)
{
	unsigned long tmo = timer_get_us() + 1000000;

	while ((readl(reg) & mask) != val)
	{
		if (timer_get_us() > tmo)
			panic("Timeout initialising DRAM\n");
	}
}

static int libdram_dramc_simple_wr_test(uint32_t dram_size, uint32_t test_range)
{
	uint32_t *dram_memory = (uint32_t *)CONFIG_SYS_SDRAM_BASE;
	uint32_t step = dram_size * 1024 * 1024 / 8;

	for (unsigned i = 0; i < test_range; i++)
	{
		dram_memory[i] = i + 0x1234567;
		dram_memory[i + step] = i - 0x1234568;
	}

	for (unsigned i = 0; i < test_range; i++)
	{
		uint32_t *ptr;
		if (dram_memory[i] != i + 0x1234567)
		{
			ptr = &dram_memory[i];
			goto fail;
		}
		if (dram_memory[i + step] != i - 0x1234568)
		{
			ptr = &dram_memory[i + step];
			goto fail;
		}
		continue;
	fail:
		debug("DRAM simple test FAIL----- at address %p\n", ptr);
		return 1;
	}

	debug("DRAM simple test OK.\n");
	return 0;
}

static uint32_t libdram_DRAMC_get_dram_size(struct dram_para *para)
{
	uint32_t size_bits, size;

	size_bits = (para->para2 & 0xFFFF) >> 12;
	size_bits += (para->para1 & 0xFFFF) >> 14;
	size_bits += (para->para1 >> 4) & 0xFF;
	size_bits += (para->para1 >> 12) & 3;
	size_bits += para->para1 & 0xF;

	if (para->para2 & 0xF)
		size_bits -= 19;
	else
		size_bits -= 18;

	size = 1 << size_bits;
	if (para->tpr13 & 0x70000)
	{
		if (para->para2 >> 30 != 2)
			size = (3 * size) >> 2;
	}

	return size;
}

static void libdram_ccm_set_pll_ddr0_sccg(struct dram_para *para)
{
	switch ((para->tpr13 >> 20) & 7)
	{
	case 0u:
		break;
	case 1u:
		ccm->pll5_pat = 0xE486CCCC;
		break;
	case 2u:
		ccm->pll5_pat = 0xE486CCCC;
		break;
	case 3u:
		ccm->pll5_pat = 0xE486CCCC;
		break;
	case 5u:
		ccm->pll5_pat = 0xE486CCCC;
		break;
	default:
		ccm->pll5_pat = 0xE486CCCC;
		break;
	}
	ccm->pll5_cfg |= 0x1000000u;
}

static void libdram_mctl_sys_init(struct dram_para *para)
{
	/* Put all DRAM-related blocks to reset state */
	clrbits_le32(&ccm->mbus_cfg, MBUS_ENABLE);
	clrbits_le32(&ccm->mbus_cfg, MBUS_RESET);
	clrbits_le32(&ccm->dram_gate_reset, BIT(GATE_SHIFT));
	clrbits_le32(&ccm->dram_gate_reset, BIT(RESET_SHIFT));
	clrbits_le32(&ccm->pll5_cfg, CCM_PLL5_CTRL_EN);
	clrbits_le32(&ccm->dram_clk_cfg, DRAM_MOD_RESET);

	udelay(5);

	libdram_ccm_set_pll_ddr0_sccg(para);
	clrsetbits_le32(&ccm->pll5_cfg, 0xff03, CCM_PLL5_CTRL_EN | CCM_PLL5_LOCK_EN | CCM_PLL5_OUT_EN | CCM_PLL5_CTRL_N(para->clk * 2 / 24));
	libdram_mctl_await_completion(&ccm->pll5_cfg, CCM_PLL5_LOCK, CCM_PLL5_LOCK);

	/* Configure DRAM mod clock */
	clrbits_le32(&ccm->dram_clk_cfg, 0x3000000);
	clrsetbits_le32(&ccm->dram_clk_cfg, 0x800001F, DRAM_CLK_ENABLE | BIT(0) | BIT(1)); // FACTOR_N = 3
	writel(BIT(RESET_SHIFT), &ccm->dram_gate_reset);
	setbits_le32(&ccm->dram_gate_reset, BIT(GATE_SHIFT));

	/* Configure MBUS and enable DRAM mod reset */
	setbits_le32(&ccm->mbus_cfg, MBUS_RESET);
	setbits_le32(&ccm->mbus_cfg, MBUS_ENABLE);
	setbits_le32(&ccm->dram_clk_cfg, DRAM_MOD_RESET);
	udelay(5);
}

static void libdram_mctl_com_set_bus_config(struct dram_para *para)
{
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		(*((uint32_t *)0x03102ea8)) |= 0x1; // NSI Register ??
	}
	clrsetbits_le32(&mctl_ctl->sched[0], 0xff00, 0x3000);
	if ((para->tpr13 & 0x10000000) != 0)
	{
		clrsetbits_le32(&mctl_ctl->sched[0], 0xf, 0x1);
		debug("MX_SCHED(0x04820250) = %p \n", &mctl_ctl->sched[0]);
	}
}

static void libdram_mctl_com_set_controller_config(struct dram_para *para)
{
	uint32_t val = 0;

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_DDR3;
		break;

	case SUNXI_DRAM_TYPE_DDR4:
		val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_DDR4;
		break;

	case SUNXI_DRAM_TYPE_LPDDR3:
		val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_LPDDR3;
		break;

	case SUNXI_DRAM_TYPE_LPDDR4:
		val = MSTR_BURST_LENGTH(16) | MSTR_DEVICETYPE_LPDDR4;
		break;
	}
	val |= (((para->para2 >> 11) & 6) + 1) << 24;
	val |= (para->para2 << 12) & 0x1000;
	writel(BIT(31) | BIT(30) | val, &mctl_ctl->mstr);
}

static void libdram_mctl_com_set_controller_geardown_mode(struct dram_para *para)
{
	if (para->tpr13 & BIT(30))
	{
		setbits_le32(&mctl_ctl->mstr, MSTR_DEVICETYPE_DDR3);
	}
}

static void libdram_mctl_com_set_controller_2T_mode(struct dram_para *para)
{
	if ((mctl_ctl->mstr & 0x800) != 0 || (para->tpr13 & 0x20) != 0)
	{
		clrbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
	}
	else
	{
		setbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
	}
}

static void libdram_mctl_com_set_controller_odt(struct dram_para *para)
{
	uint32_t val = 0;

	if ((para->para2 & 0x1000) == 0)
		writel(0x0201, &mctl_ctl->odtmap);
	else
		writel(0x0303, &mctl_ctl->odtmap);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		val = 0x06000400;
		break;

	case SUNXI_DRAM_TYPE_DDR4:
		val = ((para->mr4 << 10) & 0x70000) | 0x400 | ((((para->mr4 >> 12) & 1) + 6) << 24);
		break;

	case SUNXI_DRAM_TYPE_LPDDR3:
		if (para->clk >= 400)
			val = ((7 * para->clk / 2000 + 7) << 24) | 0x400 | ((4 - 7 * para->clk / 2000) << 16);
		else
			val = ((7 * para->clk / 2000 + 7) << 24) | 0x400 | ((3 - 7 * para->clk / 2000) << 16);
		break;

	case SUNXI_DRAM_TYPE_LPDDR4:
		val = 0x04000400;
		break;
	}
	writel(val, &mctl_ctl->odtcfg);
	writel(val, &mctl_ctl->unk_0x2240);
	writel(val, &mctl_ctl->unk_0x3240);
	writel(val, &mctl_ctl->unk_0x4240);
}

static void libdram_mctl_com_set_controller_address_map(struct dram_para *para)
{
	uint8_t cols, rows, ranks;
	uint32_t unk_2, unk_5, unk_16;

	cols = para->para1 & 0xF;
	rows = (para->para1 >> 4) & 0xFF;
	ranks = (para->tpr13 >> 16) & 7;

	unk_2 = (para->para1 >> 12) & 3;
	unk_5 = (para->para1 & 0xFFFF) >> 14;

	if (para->para2 << 28)
		cols -= 1;

	/* Columns */
	mctl_ctl->addrmap[2] = (unk_5 << 8) | (unk_5 << 16) | (unk_5 << 24);
	switch (cols)
	{
	case 8:
		mctl_ctl->addrmap[3] = 0x1F1F0000 | unk_5 | (unk_5 << 8);
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;

	case 9:
		mctl_ctl->addrmap[3] = 0x1F000000 | unk_5 | (unk_5 << 8) | (unk_5 << 16);
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;

	case 10:
		mctl_ctl->addrmap[3] = unk_5 | (unk_5 << 8) | (unk_5 << 16) | (unk_5 << 24);
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;

	case 11:
		mctl_ctl->addrmap[3] = unk_5 | (unk_5 << 8) | (unk_5 << 16) | (unk_5 << 24);
		mctl_ctl->addrmap[4] = 0x1F00 | unk_5;
		break;

	default:
		mctl_ctl->addrmap[3] = unk_5 | (unk_5 << 8) | (unk_5 << 16) | (unk_5 << 24);
		mctl_ctl->addrmap[4] = unk_5 | (unk_5 << 8);
		break;
	}

	/* Bank groups */
	switch (unk_5)
	{
	case 1:
		mctl_ctl->addrmap[8] = 0x3f01;
		break;

	case 2:
		mctl_ctl->addrmap[8] = 0x101;
		break;

	default:
		mctl_ctl->addrmap[8] = 0x3f3f;
		break;
	}

	/* Banks */
	if (unk_2 == 3)
	{
		mctl_ctl->addrmap[1] = (unk_5 - 2 + cols) | ((unk_5 - 2 + cols) << 8) | ((unk_5 - 2 + cols) << 16);
	}
	else
	{
		mctl_ctl->addrmap[1] = (unk_5 - 2 + cols) | ((unk_5 - 2 + cols) << 8) | 0x3F0000;
	}

	/* Rows */
	unk_16 = unk_5 + unk_2 + cols;
	mctl_ctl->addrmap[5] = (unk_16 - 6) | ((unk_16 - 6) << 8) | ((unk_16 - 6) << 16) | ((unk_16 - 6) << 24);
	switch (rows)
	{
	case 14:
		mctl_ctl->addrmap[6] = (unk_16 - 6) | ((unk_16 - 6) << 8) | 0x0F0F0000;
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;

	case 15:
		if ((ranks == 1 && cols == 11) || (ranks == 2 && cols == 10))
		{
			mctl_ctl->addrmap[6] = (unk_16 - 6) | ((unk_16 - 5) << 8) | ((unk_16 - 5) << 16) | 0x0F000000;
			mctl_ctl->addrmap[0] = unk_16 + 7;
		}
		else
		{
			mctl_ctl->addrmap[6] = (unk_16 - 6) | ((unk_16 - 6) << 8) | ((unk_16 - 6) << 16) | 0x0F000000;
		}
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;

	case 16:
		if (ranks == 1 && cols == 10)
		{
			mctl_ctl->addrmap[6] = (unk_16 - 6) | ((unk_16 - 6) << 8) | ((unk_16 - 5) << 16) | ((unk_16 - 5) << 24);
			mctl_ctl->addrmap[0] = unk_16 + 8;
		}
		else
		{
			mctl_ctl->addrmap[6] = (unk_16 - 6) | ((unk_16 - 6) << 8) | ((unk_16 - 6) << 16) | ((unk_16 - 6) << 24);
		}
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;

	case 17:
		mctl_ctl->addrmap[6] = (unk_16 - 6) | ((unk_16 - 6) << 8) | ((unk_16 - 6) << 16) | ((unk_16 - 6) << 24);
		mctl_ctl->addrmap[7] = (unk_16 - 6) | 0x0F00;
		break;

	default:
		mctl_ctl->addrmap[6] = (unk_16 - 6) | ((unk_16 - 6) << 8) | ((unk_16 - 6) << 16) | ((unk_16 - 6) << 24);
		mctl_ctl->addrmap[7] = (unk_16 - 6) | ((unk_16 - 6) << 8);
		break;
	}

	if (para->para2 & 0x1000)
	{
		if (ranks < 2)
		{
			mctl_ctl->addrmap[0] = rows - 6 + unk_16;
		}
	}
	else
	{
		mctl_ctl->addrmap[0] = 0x1F;
	}
}

static uint32_t libdram_auto_cal_timing(int a1, int a2)
{
	unsigned int result;

	result = a2 * a1 / 1000;
	if (a2 * a1 % 1000)
		++result;
	return result;
}

static void libdram_mctl_com_set_channel_timing(struct dram_para *para)
{
	uint32_t ctrl_freq;

	ctrl_freq = (((*((uint32_t *)0x3001011)) + 1) * 24) >> 2;

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		channel_timing.tfaw = libdram_auto_cal_timing(50, ctrl_freq);
		if (channel_timing.tfaw < 4)
			channel_timing.tfaw = 4;
		channel_timing.trrd = libdram_auto_cal_timing(10, ctrl_freq);
		if (channel_timing.trrd < 1)
			channel_timing.trrd = 1;
		channel_timing.trcd = libdram_auto_cal_timing(10, ctrl_freq);
		if (channel_timing.trcd < 1)
			channel_timing.trcd = 1;
		channel_timing.trc = libdram_auto_cal_timing(70, ctrl_freq);
		channel_timing.trtp = libdram_auto_cal_timing(8, ctrl_freq);
		if (channel_timing.trtp < 2)
			channel_timing.trtp = 2;
		channel_timing.trp = libdram_auto_cal_timing(27, ctrl_freq);
		channel_timing.tras = libdram_auto_cal_timing(42, ctrl_freq);
		channel_timing.unk_4 = channel_timing.trtp;
		channel_timing.trefi = libdram_auto_cal_timing(3900, ctrl_freq) >> 5;
		channel_timing.trfc = libdram_auto_cal_timing(210, ctrl_freq);
		channel_timing.txp = channel_timing.trtp;
		channel_timing.txsr = libdram_auto_cal_timing(220, ctrl_freq);
		channel_timing.tccd = 2;
		para->mr0 = 0;
		para->mr1 = 0x83;
		para->mr2 = 0x1c;
		channel_timing.tcke = 3;
		channel_timing.twr2rd = channel_timing.unk_4 + 9;
		channel_timing.tcksre = 5;
		channel_timing.tcksrx = 5;
		channel_timing.tckesr = 5;
		channel_timing.trd2wr = 0xd;
		channel_timing.trasmax = 0x18;
		channel_timing.twtp = 0x10;
		channel_timing.tmod = 0xc;
		channel_timing.tmrd = 5;
		channel_timing.tmrw = 5;
		channel_timing.tcwl = 4;
		channel_timing.tcl = 7;
		channel_timing.unk_44 = 6;
		channel_timing.unk_43 = 0xc;
		break;
	case SUNXI_DRAM_TYPE_DDR4:
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		break;
	}

	writel((channel_timing.twtp << 24) | (channel_timing.tfaw << 16) | (channel_timing.trasmax << 8) | channel_timing.tras, &mctl_ctl->dramtmg[0]);
	writel((channel_timing.txp << 16) | (channel_timing.trtp << 8) | channel_timing.trc, &mctl_ctl->dramtmg[1]);
	writel((channel_timing.tcwl << 24) | (channel_timing.tcl << 16) | (channel_timing.trd2wr << 8) | channel_timing.twr2rd, &mctl_ctl->dramtmg[2]);
	writel((channel_timing.tmrw << 20) | (channel_timing.tmrd << 12) | channel_timing.tmod, &mctl_ctl->dramtmg[3]);
	writel((channel_timing.trcd << 24) | (channel_timing.tccd << 16) | (channel_timing.trrd << 8) | channel_timing.trp, &mctl_ctl->dramtmg[4]);
	writel((channel_timing.tcksrx << 24) | (channel_timing.tcksre << 16) | (channel_timing.tckesr << 8) | channel_timing.tcke, &mctl_ctl->dramtmg[5]);
	writel((channel_timing.txp + 2) | 0x02020000, &mctl_ctl->dramtmg[6]);
	writel((channel_timing.unk_42 << 24) | (channel_timing.unk_42 << 16) | 0x1000 | channel_timing.txs, &mctl_ctl->dramtmg[8]);
	writel(channel_timing.unk_69 | (channel_timing.unk_63 << 8) | 0x20000, &mctl_ctl->dramtmg[9]);
	writel(0xE0C05, &mctl_ctl->dramtmg[10]);
	writel(0x440C021C, &mctl_ctl->dramtmg[11]);
	writel(channel_timing.unk_66, &mctl_ctl->dramtmg[12]);
	writel(0xA100002, &mctl_ctl->dramtmg[13]);
	writel(channel_timing.txsr, &mctl_ctl->dramtmg[14]);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		clrbits_le32(&mctl_ctl->init[0], 0xC0000000);
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		clrsetbits_le32(&mctl_ctl->init[0], 0xC3FF0000 | 0xC0000FFF, 0x4F0000 | 0x112);
		break;
	case SUNXI_DRAM_TYPE_DDR4:
		clrbits_le32(&mctl_ctl->init[0], 0xC0000000);
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		clrsetbits_le32(&mctl_ctl->init[0], 0xC0000FFF, 0x3F0);
		break;
	}

	if (para->tpr13 & 8)
	{
		writel(0x420000, &mctl_ctl->init[1]);
	}
	else
	{
		writel(0x1F20000, &mctl_ctl->init[1]);
	}

	clrsetbits_le32(&mctl_ctl->init[2], 0xFF0F, 0xFF0F);
	writel(0, &mctl_ctl->dfimisc);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR4:
		writel(para->mr5 | (para->mr4 << 16), &mctl_ctl->init[6]);
		writel(para->mr6, &mctl_ctl->init[7]);
	case SUNXI_DRAM_TYPE_DDR3:
		writel(para->mr1 | (para->mr0 << 16), &mctl_ctl->init[3]);
		writel(para->mr3 | (para->mr2 << 16), &mctl_ctl->init[4]);
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		writel(para->mr12 | (para->mr11 << 16), &mctl_ctl->init[6]);
		writel(para->mr14 | (para->mr22 << 16), &mctl_ctl->init[7]);
	case SUNXI_DRAM_TYPE_LPDDR3:
		writel(para->mr2 | (para->mr1 << 16), &mctl_ctl->init[3]);
		writel(para->mr3 << 16, &mctl_ctl->init[4]);
		break;
	}

	clrsetbits_le32(&mctl_ctl->rankctl, 0xff0, 0x660);

	if (para->tpr13 & 0x20)
	{
		writel((channel_timing.unk_44) | 0x2000000 | (channel_timing.unk_43 << 16) | 0x808000, &mctl_ctl->dfitmg0);
	}
	else
	{
		writel((channel_timing.unk_44 - 1) | 0x2000000 | ((channel_timing.unk_43 - 1) << 16) | 0x808000, &mctl_ctl->dfitmg0);
	}

	writel(0x100202, &mctl_ctl->dfitmg1);

	writel(channel_timing.trfc | (channel_timing.trefi << 16), &mctl_ctl->rfshtmg);
}

static void libdram_mctl_com_set_controller_update(void)
{
	setbits_le32(&mctl_ctl->dfiupd[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->zqctl[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x2180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x3180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x4180, BIT(31) | BIT(30));
}

static void libdram_mctl_com_set_controller_dbi(struct dram_para *para)
{
	if ((para->tpr13 & 0x20000000) != 0)
		setbits_le32(&mctl_ctl->dbictl, 4);
}

static void libdram_mctl_com_set_controller_refresh(int val)
{
	clrsetbits_le32(&mctl_ctl->rfshctl3, BIT(0), val & BIT(0));
}

static void libdram_mctl_com_set_controller_before_phy(void)
{
	libdram_mctl_com_set_controller_refresh(1);
	clrbits_le32(&mctl_ctl->dfimisc, 1);
	writel(0x20, &mctl_ctl->pwrctl);
}

static void libdram_mctl_com_init(struct dram_para *para)
{
	libdram_mctl_com_set_controller_config(para);

	if (para->type == SUNXI_DRAM_TYPE_DDR4)
	{
		libdram_mctl_com_set_controller_geardown_mode(para);
	}

	if (para->type == SUNXI_DRAM_TYPE_DDR3 || para->type == SUNXI_DRAM_TYPE_DDR4)
	{
		libdram_mctl_com_set_controller_2T_mode(para);
	}

	libdram_mctl_com_set_controller_odt(para);
	// mctl_com_set_controller_address_map(para);
	// debug("external 2: %x, 3: %x, 4:  %x\n", mctl_ctl->addrmap[2], mctl_ctl->addrmap[3], mctl_ctl->addrmap[4]);
	// debug("external 8: %x, 1: %x, 5:  %x\n", mctl_ctl->addrmap[8], mctl_ctl->addrmap[1], mctl_ctl->addrmap[5]);
	// debug("external 6: %x, 7: %x, 0:  %x\n", mctl_ctl->addrmap[6], mctl_ctl->addrmap[7], mctl_ctl->addrmap[0]);
	libdram_mctl_com_set_controller_address_map(para);
	// debug("internal 2: %x, 3: %x, 4:  %x\n", mctl_ctl->addrmap[2], mctl_ctl->addrmap[3], mctl_ctl->addrmap[4]);
	// debug("internal 8: %x, 1: %x, 5:  %x\n", mctl_ctl->addrmap[8], mctl_ctl->addrmap[1], mctl_ctl->addrmap[5]);
	// debug("internal 6: %x, 7: %x, 0:  %x\n", mctl_ctl->addrmap[6], mctl_ctl->addrmap[7], mctl_ctl->addrmap[0]);

	libdram_mctl_com_set_channel_timing(para);
	// for (int i = 0; i < 17; i++)
	// {
	// 	debug("mctl_ctl->dramtmg[%d]: %x\n", i, mctl_ctl->dramtmg[i]);
	// }

	writel(0, &mctl_ctl->pwrctl);

	libdram_mctl_com_set_controller_update();

	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		libdram_mctl_com_set_controller_dbi(para);
	}

	libdram_mctl_com_set_controller_before_phy();
}

static void libdram_mctl_com_set_controller_after_phy(void)
{
	writel(0, &mctl_ctl->swctl);
	libdram_mctl_com_set_controller_refresh(0);
	writel(1, &mctl_ctl->swctl);
	libdram_mctl_await_completion(&mctl_ctl->swstat, 1, 1);
}

static void libdram_mctl_phy_cold_reset(void)
{
	clrsetbits_le32(&mctl_com->unk_0x008, 0x1000200, 0x200);
	udelay(1);
	setbits_le32(&mctl_com->unk_0x008, 0x1000000);
}

static void libdram_mctl_phy_set_address_remapping(struct dram_para *para)
{
	int i;
	uint32_t *ptr;
	const uint8_t *phy_init = NULL;

	switch (readl(SUNXI_SID_BASE))
	{
	case 0x800:
	case 0x2400:
		switch (para->type)
		{
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
		switch (para->type)
		{
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

	ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0xc0);
	for (i = 0; i < 27; i++)
	{
		// debug("val: %u, ptr: %p\n", phy_init[i], ptr);
		writel(phy_init[i], ptr++);
	}
}

static void libdram_mctl_phy_vref_config(struct dram_para *para)
{
	uint32_t val = 0;

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		val = para->tpr6 & 0xFF;
		if (!val)
			val = 0x80;
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		val = (para->tpr6 >> 16) & 0xFF;
		if (!val)
			val = 0x80;
		break;
	case SUNXI_DRAM_TYPE_DDR4:
		val = (para->tpr6 >> 8) & 0xFF;
		if (!val)
			val = 0x80;
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		val = (para->tpr6 >> 24) & 0xFF;
		if (!val)
			val = 0x33;
		break;
	}

	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3dc);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x45c);
}

static void libdram_mctl_drive_odt_config(struct dram_para *para)
{
	uint32_t val;

	writel(para->dx_dri & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x388);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x388);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x38c);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		if ((para->tpr1 & 0x1f1f1f1f) != 0)
			writel(para->tpr1 & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x38c);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x38c);
	}

	writel((para->dx_dri >> 8) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x3c8);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x3c8);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3cc);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		if ((para->tpr1 & 0x1f1f1f1f) != 0)
			writel((para->tpr1 >> 8) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x3cc);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x3cc);
	}

	writel((para->dx_dri >> 16) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x408);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x408);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x40c);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		if ((para->tpr1 & 0x1f1f1f1f) != 0)
			writel((para->tpr1 >> 16) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x40c);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x40c);
	}

	writel((para->dx_dri >> 24) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x448);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x448);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x44c);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		if ((para->tpr1 & 0x1f1f1f1f) != 0)
			writel((para->tpr1 >> 24) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x44c);
		else
			writel(4, SUNXI_DRAM_PHY0_BASE + 0x44c);
	}

	writel(para->ca_dri & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x340);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x340);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x344);

	writel((para->ca_dri >> 8) & 0x1f, SUNXI_DRAM_PHY0_BASE + 0x348);
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x348);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x34c);

	val = para->dx_odt & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x380);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x380);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x384);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x384);
	}

	val = (para->dx_odt >> 8) & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3c0);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x3c0);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3c4);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x3c4);
	}

	val = (para->dx_odt >> 16) & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x400);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x400);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x404);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x404);
	}

	val = (para->dx_odt >> 24) & 0x1f;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x440);
	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR3)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x440);
	}
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x444);
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x444);
	}
}

static void libdram_mctl_phy_ca_bit_delay_compensation(struct dram_para *para)
{
	uint32_t *ptr, tpr0;
	uint32_t i, a, b, c, d;

	if (para->tpr10 >= 0)
	{ // Sorry for direct copy from decompiler
		tpr0 = ((32 * para->tpr10) & 0x1E00) | ((para->tpr10 << 9) & 0x1E0000) | ((2 * para->tpr10) & 0x1E) | ((para->tpr10 << 13) & 0x1E000000);
		if (para->tpr10 >> 29)
			tpr0 *= 2;
	}
	else
	{
		tpr0 = para->tpr0;
	}

	ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x780);
	for (i = 0; i < 32; i++)
	{
		// debug("val: %u, ptr: %p\n", (tpr0 >> 8) & 0x3F, ptr);
		writel((tpr0 >> 8) & 0x3f, ptr++);
	}

	a = tpr0 & 0x3f;
	b = tpr0 & 0x3f;
	c = (tpr0 >> 16) & 0x3f;
	d = (tpr0 >> 24) & 0x3f;

	switch (readl(SUNXI_SID_BASE))
	{ // Seems like allwinner fab factory change
	case 0x800:
	case 0x2400:
		switch (para->type)
		{
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
		switch (para->type)
		{
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

static void libdram_phy_para_config(struct dram_para *para)
{
	uint32_t val;

	clrbits_le32(&prcm->sys_pwroff_gating, 0x10);

	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x4, 0x08);

	if ((para->para2 & 1) != 0)
		val = 3;
	else
		val = 0xf;
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x3c, 0xf, val);

	switch (para->type)
	{
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
		if (para->tpr13 & BIT(28))
			val = 22;
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

	switch (para->type)
	{
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

	libdram_mctl_phy_set_address_remapping(para);
	libdram_mctl_phy_vref_config(para);
	libdram_mctl_drive_odt_config(para);

	if (para->tpr10 >> 16)
		libdram_mctl_phy_ca_bit_delay_compensation(para);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		val = 2;
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		val = 3;
		break;
	case SUNXI_DRAM_TYPE_DDR4:
		val = 4;
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		val = 5;
		break;
	}
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 4, 0x7, val | 8);

	if (para->clk <= 672)
		writel(0xf, SUNXI_DRAM_PHY0_BASE + 0x20);
	if (para->clk > 500)
	{
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x144, 0x80);
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 0xe0);
	}
	else
	{
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x144, 0x80);
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 0xe0, 0x20);
	}

	clrbits_le32(&mctl_com->unk_0x008, 0x200);
	udelay(1);
	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 8);
	libdram_mctl_await_completion((uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x180), 4, 4);

	if ((para->tpr13 & 0x10) == 0)
		udelay(1000);

	writel(0x37, SUNXI_DRAM_PHY0_BASE + 0x58);
	setbits_le32(&prcm->sys_pwroff_gating, 0x10);
}

static void libdram_mctl_dfi_init(struct dram_para *para)
{
	setbits_le32(&mctl_com->maer0, 0x100);

	/*
	LDR             R5, =0x4820320
	ORR.W           R3, R3, #0x100
	STR             R3, [R2]
	MOVS            R3, #0
	STR             R3, [R5]
	*/
	writel(0, &mctl_ctl->swctl);

	setbits_le32(&mctl_ctl->dfimisc, 1);
	setbits_le32(&mctl_ctl->dfimisc, 0x20);
	writel(1, &mctl_ctl->swctl);
	libdram_mctl_await_completion(&mctl_ctl->swstat, 1, 1);

	clrbits_le32(&mctl_ctl->dfimisc, 0x20);
	writel(1, &mctl_ctl->swctl);
	libdram_mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	libdram_mctl_await_completion(&mctl_ctl->dfistat, 1, 1);

	clrbits_le32(&mctl_ctl->pwrctl, 0x20);
	writel(1, &mctl_ctl->swctl);
	libdram_mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	libdram_mctl_await_completion(&mctl_ctl->statr, 3, 1);

	if ((para->tpr13 & 0x10) == 0)
	{
		udelay(200);
	}

	clrbits_le32(&mctl_ctl->dfimisc, 1);

	writel(1, &mctl_ctl->swctl);
	libdram_mctl_await_completion(&mctl_ctl->swstat, 1, 1);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		writel(para->mr0, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr1, &mctl_ctl->mrctrl1);
		writel(0x80001030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr2, &mctl_ctl->mrctrl1);
		writel(0x80002030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr3, &mctl_ctl->mrctrl1);
		writel(0x80003030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
		break;

	case SUNXI_DRAM_TYPE_LPDDR3:
		writel(para->mr1 | 0x100, &mctl_ctl->mrctrl1);
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr2 | 0x200, &mctl_ctl->mrctrl1);
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr3 | 0x300, &mctl_ctl->mrctrl1);
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr11 | 0xb00, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
		break;

	case SUNXI_DRAM_TYPE_DDR4:
		writel(para->mr0, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr1, &mctl_ctl->mrctrl1);
		writel(0x80001030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr2, &mctl_ctl->mrctrl1);
		writel(0x80002030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr3, &mctl_ctl->mrctrl1);
		writel(0x80003030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr4, &mctl_ctl->mrctrl1);
		writel(0x80004030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr5, &mctl_ctl->mrctrl1);
		writel(0x80005030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr6 | 0x80, &mctl_ctl->mrctrl1);
		writel(0x80006030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr6 | 0x80, &mctl_ctl->mrctrl1);
		writel(0x80006030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr6 | 0x80, &mctl_ctl->mrctrl1);
		writel(0x80006030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
		break;

	case SUNXI_DRAM_TYPE_LPDDR4:
		writel(para->mr0, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr1 | 0x100, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr2 | 0x200, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr3 | 0x300, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr4 | 0x400, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr11 | 0xb00, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr12 | 0xc00, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr13 | 0xd00, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr14 | 0xe00, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr22 | 0x1600, &mctl_ctl->mrctrl1);
		writel(0x80000030, &mctl_ctl->mrctrl0);
		libdram_mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
		break;
	}

	writel(0, SUNXI_DRAM_PHY0_BASE + 0x54);
}

static bool libdram_phy_write_leveling(struct dram_para *para)
{
	printf("!!!WARNING!!! libdram_phy_write_leveling: unimplemented\n");
	return true;
}

static bool libdram_phy_read_calibration(struct dram_para *para)
{
	uint32_t val;

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x20);

	if (para->type == SUNXI_DRAM_TYPE_DDR4)
	{
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 54, 0x2);
	}

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

	if (para->para2 & 1)
		val = 3;
	else
		val = 0xf;

	while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val)
	{
		if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20)
		{
			return false;
		}
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

	if ((para->para2 & 0x1000) != 0)
	{
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x10);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

		while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val)
		{
			if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20)
			{
				return false;
			}
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

	return true;
}

static bool libdram_phy_read_training(struct dram_para *para)
{
	printf("!!!WARNING!!! libdram_phy_read_training: unimplemented\n");
	return true;
}

static bool libdram_phy_write_training(struct dram_para *para)
{
	printf("!!!WARNING!!! libdram_phy_write_training: unimplemented\n");
	return true;
}

static bool libdram_mctl_phy_dfs(struct dram_para *para, int clk)
{
	printf("!!!WARNING!!! libdram_mctl_phy_dfs: unimplemented\n");
	return true;
}

static void libdram_mctl_phy_dx_bit_delay_compensation(struct dram_para *para)
{
	int i;
	uint32_t val, *ptr;

	if (para->tpr10 & 0x40000)
	{
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 8);
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);

		if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
			clrbits_le32(SUNXI_DRAM_PHY0_BASE + 4, 0x80);

		val = para->tpr11 & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x484);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = para->para0 & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x4d0);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x590);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x4cc);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x58c);

		val = (para->tpr11 >> 8) & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x4d8);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = (para->para0 >> 8) & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x524);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x5e4);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x520);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x5e0);

		val = (para->tpr11 >> 16) & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x604);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = (para->para0 >> 16) & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x650);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x710);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x64c);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x70c);

		val = (para->tpr11 >> 24) & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x658);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = (para->para0 >> 24) & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x6a4);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x764);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x6a0);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x760);
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);
	}

	if (para->tpr10 & 0x20000)
	{
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 4);

		val = para->tpr12 & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x480);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = para->tpr14 & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x528);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x5e8);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x4c8);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x588);

		val = (para->tpr12 >> 8) & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x4d4);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = (para->tpr14 >> 8) & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x52c);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x5ec);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x51c);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x5dc);

		val = (para->tpr12 >> 16) & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x600);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = (para->tpr14 >> 16) & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x6a8);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x768);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x648);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x708);

		val = (para->tpr12 >> 24) & 0x3F;
		ptr = (uint32_t *)(SUNXI_DRAM_PHY0_BASE + 0x654);
		for (i = 0; i < 9; i++)
		{
			writel(val, ptr);
			writel(val, ptr + 0x30);
			ptr += 2;
		}
		val = (para->tpr14 >> 24) & 0x3F;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x6ac);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x76c);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x69c);
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x75c);
	}

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);
}

static bool libdram_ddrphy_phyinit_C_initPhyConfig(struct dram_para *para)
{
	int i, max_retry, ret;

	libdram_phy_para_config(para);
	libdram_mctl_dfi_init(para);
	writel(0, &mctl_ctl->swctl);
	libdram_mctl_com_set_controller_refresh(0);
	writel(1, &mctl_ctl->swctl);

	if (para->tpr10 & 0x80000)
		max_retry = 5;
	else
		max_retry = 1;

	if (para->tpr10 & 0x100000)
	{
		for (i = 0; i < max_retry; i++)
			if (libdram_phy_write_leveling(para))
				break;

		if (i == max_retry)
		{
			debug("phy_write_leveling failed!\n");
			return false;
		}
	}

	if (para->tpr10 & 0x200000)
	{
		for (i = 0; i < max_retry; i++)
			if (libdram_phy_read_calibration(para))
				break;

		if (i == max_retry)
		{
			debug("phy_read_calibration failed!\n");
			return false;
		}
	}

	if (para->tpr10 & 0x400000)
	{
		for (i = 0; i < max_retry; i++)
			if (libdram_phy_read_training(para))
				break;

		if (i == max_retry)
		{
			debug("phy_read_training failed!\n");
			return false;
		}
	}

	if (para->tpr10 & 0x800000)
	{
		for (i = 0; i < max_retry; i++)
			if (libdram_phy_write_training(para))
				break;

		if (i == max_retry)
		{
			debug("phy_write_training failed!\n");
			return false;
		}
	}

	libdram_mctl_phy_dx_bit_delay_compensation(para);

	ret = true;
	if ((para->tpr13 & 0x805) == 5)
	{
		ret &= libdram_mctl_phy_dfs(para, 1);
		ret &= libdram_mctl_phy_dfs(para, 2);
		ret &= libdram_mctl_phy_dfs(para, 3);
		ret &= libdram_mctl_phy_dfs(para, 0);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);
	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 7);

	return ret;
}

static bool libdram_mctl_phy_init(struct dram_para *para)
{
	libdram_mctl_phy_cold_reset();
	return libdram_ddrphy_phyinit_C_initPhyConfig(para);
}

static bool libdram_mctl_channel_init(struct dram_para *para)
{
	bool ret;

	clrsetbits_le32(&mctl_com->unk_0x008, 0x3000200, 0x2000200); // CLEAR AND SET SAME BIT ???
	setbits_le32(&mctl_com->maer0, 0x8000);

	libdram_mctl_com_set_bus_config(para);

	writel(0, &mctl_ctl->hwlpctl);

	libdram_mctl_com_init(para);
	ret = libdram_mctl_phy_init(para);
	libdram_mctl_com_set_controller_after_phy();

	return ret;
}

static bool libdram_mctl_core_init(struct dram_para *para)
{
	libdram_mctl_sys_init(para);
	return libdram_mctl_channel_init(para);
}

static bool libdram_auto_scan_dram_config(struct dram_para *para)
{
	printf("!!!WARNING!!! libdram_auto_scan_dram_config: unimplemented\n");
	// return auto_scan_dram_config(para);
	return true;
}

static bool libdram_dram_software_training(struct dram_para *para)
{
	printf("!!!WARNING!!! libdram_dram_software_training: unimplemented\n");
	// return dram_software_training(para);
	return true;
}

static uint32_t libdram_init_DRAM(struct dram_para *para)
{
	int tmp_tpr11, tmp_tpr12;
	uint32_t dram_size, actual_dram_size;

	tmp_tpr11 = para->tpr13 & 0x800000;
	tmp_tpr12 = 0;
	if (tmp_tpr11)
	{
		tmp_tpr11 = para->tpr11;
		tmp_tpr12 = para->tpr12;
	}
	debug("DRAM BOOT DRIVE INFO: %s\n", "V0.696");

	(*((uint32_t *)0x3000160)) |= 0x100;
	(*((uint32_t *)0x3000168)) &= 0xffffffc0;

	if ((para->tpr13 & 1) == 0 && !libdram_auto_scan_dram_config(para))
	{
		debug("auto_scan_dram_config: failed\n");
		return false;
	}

	if ((para->tpr13 & 0x800) != 0 && !libdram_dram_software_training(para))
	{
		debug("dram_software_training: failed\n");
		return false;
	}

	debug("DRAM CLK = %d MHZ\n", para->clk);
	debug("DRAM Type = %d (3:DDR3,4:DDR4,7:LPDDR3,8:LPDDR4)\n", para->type);

	if (!libdram_mctl_core_init(para))
	{
		debug("DRAM initial error : 0 !\n");
		return false;
	}

	dram_size = libdram_DRAMC_get_dram_size(para);
	actual_dram_size = (para->para2 >> 16) & 0x3FFF;

	switch (para->para2 >> 30)
	{
	case 3:
		if (actual_dram_size != dram_size)
		{
			debug("DRAM SIZE error! auto_scan_dram_size = %d, actual_dram_size = %d\n", dram_size, actual_dram_size);
			return false;
		}
		break;
	case 2:
		dram_size = actual_dram_size;
		break;
	default:
		para->para2 &= 0xFFFF;
		para->para2 |= dram_size << 16;
		break;
	}
	debug("DRAM SIZE = %d MBytes, para1 = %x, para2 = %x, tpr13 = %x\n", dram_size, para->para1, para->para2, para->tpr13);

	if ((para->tpr13 & 0x1000000) != 0)
		mctl_ctl->pwrctl |= 9;

	if ((para->tpr13 & 0x800000) != 0)
	{
		para->tpr11 = tmp_tpr11;
		para->tpr12 = tmp_tpr12;
	}

	if (libdram_dramc_simple_wr_test(dram_size, 4096))
	{
		if ((para->tpr13 & 0x40) != 0)
			return false;
		if (!libdram_mctl_core_init(para))
		{
			debug("DRAM initial error : 1 !\n");
			return false;
		}
		if (libdram_dramc_simple_wr_test(dram_size, 4096))
			return false;
	}

	return dram_size;
};

unsigned long sunxi_dram_init(void)
{
	return libdram_init_DRAM(&para) * 1024 * 1024;
};
