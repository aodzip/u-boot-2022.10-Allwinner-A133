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

#include "dram_timings/ida_defs.h"

#define debug printf

#ifndef _REG
#define _REG(x) (*((volatile unsigned int *)(x)))
#endif

enum
{
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
	struct sunxi_mctl_com_reg *const mctl_com =
		(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	const u32 cfg0 = ((bwlimit ? (1 << 0) : 0) | (priority ? (1 << 1) : 0) | ((qos & 0x3) << 2) | ((waittime & 0xf) << 4) | ((acs & 0xff) << 8) | (bwl0 << 16));
	const u32 cfg1 = ((u32)bwl2 << 16) | (bwl1 & 0xffff);

	debug("MBUS port %d cfg0 %08x cfg1 %08x\n", port, cfg0, cfg1);
	writel_relaxed(cfg0, &mctl_com->master[port].cfg0);
	writel_relaxed(cfg1, &mctl_com->master[port].cfg1);
}

#define MBUS_CONF(port, bwlimit, qos, acs, bwl0, bwl1, bwl2) \
	mbus_configure_port(port, bwlimit, false,                \
						MBUS_QOS_##qos, 0, acs, bwl0, bwl1, bwl2)

static void mctl_set_master_priority(void)
{
	struct sunxi_mctl_com_reg *const mctl_com =
		(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	/* enable bandwidth limit windows and set windows size 1us */
	writel(399, &mctl_com->tmr);
	writel(BIT(16), &mctl_com->bwcr);

	MBUS_CONF(0, true, HIGHEST, 0, 256, 128, 100);
	MBUS_CONF(1, true, HIGH, 0, 1536, 1400, 256);
	MBUS_CONF(2, true, HIGHEST, 0, 512, 256, 96);
	MBUS_CONF(3, true, HIGH, 0, 256, 100, 80);
	MBUS_CONF(4, true, HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(5, true, HIGH, 2, 100, 64, 32);
	MBUS_CONF(6, true, HIGH, 2, 100, 64, 32);
	MBUS_CONF(8, true, HIGH, 0, 256, 128, 64);
	MBUS_CONF(11, true, HIGH, 0, 256, 128, 100);
	MBUS_CONF(14, true, HIGH, 0, 1024, 256, 64);
	MBUS_CONF(16, true, HIGHEST, 6, 8192, 2800, 2400);
	MBUS_CONF(21, true, HIGHEST, 6, 2048, 768, 512);
	MBUS_CONF(25, true, HIGHEST, 0, 100, 64, 32);
	MBUS_CONF(26, true, HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(37, true, HIGH, 0, 256, 128, 64);
	MBUS_CONF(38, true, HIGH, 2, 100, 64, 32);
	MBUS_CONF(39, true, HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(40, true, HIGH, 2, 100, 64, 32);

	dmb();
}

static void ccm_set_pll_ddr0_sccg(struct dram_para *para) // TODO
{
	unsigned int v1; // r2

	switch (((unsigned int)para->tpr13 >> 20) & 7)
	{
	case 0u:
		goto LABEL_4;
	case 1u:
		v1 = 0xE486CCCC;
		goto LABEL_3;
	case 2u:
		v1 = 0xE9069999;
		goto LABEL_3;
	case 3u:
		v1 = 0xED866666;
		goto LABEL_3;
	case 5u:
		v1 = 0xF5860000;
		goto LABEL_3;
	default:
		v1 = 0xF2063333;
	LABEL_3:
		_REG(0x3001110) = v1;
	LABEL_4:
		_REG(0x3001010) |= 0x1000000u;
	}
}

static void mctl_sys_init(struct dram_para *para)
{
	struct sunxi_ccm_reg *const ccm =
		(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;

	/* Put all DRAM-related blocks to reset state */
	clrbits_le32(&ccm->mbus_cfg, MBUS_ENABLE);
	clrbits_le32(&ccm->mbus_cfg, MBUS_RESET);
	clrbits_le32(&ccm->dram_gate_reset, BIT(GATE_SHIFT));
	clrbits_le32(&ccm->dram_gate_reset, BIT(RESET_SHIFT));
	clrbits_le32(&ccm->pll5_cfg, CCM_PLL5_CTRL_EN);
	clrbits_le32(&ccm->dram_clk_cfg, DRAM_MOD_RESET);

	udelay(5);

	ccm_set_pll_ddr0_sccg(para);
	clrsetbits_le32(&ccm->pll5_cfg, 0xff03, CCM_PLL5_CTRL_EN | CCM_PLL5_LOCK_EN | CCM_PLL5_OUT_EN | CCM_PLL5_CTRL_N(para->clk * 2 / 24));
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&ccm->pll5_cfg, CCM_PLL5_LOCK, CCM_PLL5_LOCK);

	/* Configure DRAM mod clock */
	clrbits_le32(&ccm->dram_clk_cfg, 0x3000000);
	clrbits_le32(&ccm->dram_clk_cfg, 0x800001F);
	setbits_le32(&ccm->dram_clk_cfg, DRAM_CLK_ENABLE);
	setbits_le32(&ccm->dram_clk_cfg, BIT(0) | BIT(1)); // FACTOR_N = 3
	printf("dram_clk_cfg: 0x%04x\n", ccm->dram_clk_cfg);
	writel(BIT(RESET_SHIFT), &ccm->dram_gate_reset);
	setbits_le32(&ccm->dram_gate_reset, BIT(GATE_SHIFT));

	/* Configure MBUS and enable DRAM mod reset */
	setbits_le32(&ccm->mbus_cfg, MBUS_RESET);
	setbits_le32(&ccm->mbus_cfg, MBUS_ENABLE);
	setbits_le32(&ccm->dram_clk_cfg, DRAM_MOD_RESET);
	udelay(5);
}

// TODO
static void mctl_com_set_controller_address_map(struct dram_para *result)
{
	unsigned int dram_para1; // r3
	int v2;					 // r1
	int v3;					 // r4
	int v4;					 // r6
	int v5;					 // r3
	int v6;					 // r9
	int v7;					 // r5
	int v8;					 // r2
	int v9;					 // lr
	int v10;				 // r5
	int v11;				 // r2
	int v12;				 // r2
	int v13;				 // r2
	int v14;				 // r3
	int v15;				 // r5
	int v16;				 // r3
	int v17;				 // r1
	int v18;				 // r2
	int v19;				 // r12
	int v20;				 // r8
	int v21;				 // r5
	int v22;				 // r2
	int v23;				 // r2
	int v24;				 // r7
	bool v25;				 // zf

	dram_para1 = result->para1;
	v2 = (dram_para1 >> 12) & 3;
	v3 = dram_para1 & 0xF;
	v4 = (unsigned __int8)(dram_para1 >> 4);
	v5 = (unsigned __int16)dram_para1 >> 14;
	v6 = v5 << 16;
	v7 = v5 << 8;
	v8 = v5 << 24;
	if (result->para2 << 28)
		--v3;
	v9 = v7 | v6;
	_REG(0x4820208) = v7 | v6 | v8;
	switch (v3)
	{
	case 8:
		v10 = v5 | 0x1F1F0000 | v7;
		goto LABEL_5;
	case 9:
		v10 = v5 | 0x1F000000 | v7 | v6;
	LABEL_5:
		_REG(0x482020C) = v10;
		goto LABEL_6;
	case 10:
		_REG(0x482020C) = v8 | v5 | v9;
	LABEL_6:
		v11 = 7967;
		goto LABEL_7;
	case 11:
		_REG(0x482020C) = v8 | v5 | v9;
		v11 = v5 | 0x1F00;
	LABEL_7:
		_REG(0x4820210) = v11;
		break;
	default:
		v21 = v7 | v5;
		_REG(0x482020C) = v6 | v8 | v21;
		_REG(0x4820210) = v21;
		break;
	}
	if (v5 == 2)
	{
		v12 = 257;
	}
	else
	{
		v12 = 16191;
		if (v5 == 1)
			v12 = 16129;
	}
	_REG(0x4820220) = v12;
	v13 = v5 - 2 + v3;
	v14 = v5 + v2;
	v15 = v13 << 8;
	if (v2 != 3)
		v13 |= 0x3F0000u;
	v16 = v14 + v3;
	if (v2 == 3)
		v15 |= v13 << 16;
	v17 = 75629080;
	_REG(0x4820204) = v13 | v15;
	v18 = v16 - 6;
	v19 = (v16 - 6) | ((v16 - 6) << 8);
	v20 = v19 | ((v16 - 6) << 16);
	_REG(0x4820214) = v20 | ((v16 - 6) << 24);
	switch (v4)
	{
	case 14:
		v22 = v19 | 0xF0F0000;
		goto LABEL_22;
	case 15:
		v24 = HIWORD(result->tpr13) & 7;
		if (v24 == 1)
		{
			v25 = v3 == 11;
		}
		else
		{
			if (v24 != 2)
				goto LABEL_37;
			v25 = v3 == 10;
		}
		if (v25)
		{
			_REG(0x4820218) = v18 | 0xF000000 | ((v16 - 5) << 8) | ((v16 - 5) << 16);
			v22 = v16 + 7;
			goto LABEL_34;
		}
	LABEL_37:
		v22 = v20 | 0xF000000;
		goto LABEL_22;
	case 16:
		if ((HIWORD(result->tpr13) & 7) == 1 && v3 == 10)
		{
			_REG(0x4820218) = ((v16 - 5) << 24) | ((v16 - 5) << 16) | v19;
			v22 = v16 + 8;
		LABEL_34:
			v17 = 75629056;
		LABEL_22:
			*(_DWORD *)v17 = v22;
		}
		else
		{
			_REG(0x4820218) = v20 | ((v16 - 6) << 24);
		}
		v23 = 3855;
	LABEL_24:
		_REG(0x482021C) = v23;
	LABEL_25:
		if ((result->para2 & 0x1000) != 0)
		{
			if ((HIWORD(result->tpr13) & 7u) - 1 > 2)
				_REG(0x4820200) = v4 - 6 + v16;
		}
		else
		{
			_REG(0x4820200) = 31;
		}
		return;
	case 17:
		_REG(0x4820218) = v20 | ((v16 - 6) << 24);
		v23 = v18 | 0xF00;
		goto LABEL_24;
	default:
		_REG(0x4820218) = v20 | ((v16 - 6) << 24);
		_REG(0x482021C) = (v16 - 6) | ((v16 - 6) << 8);
		goto LABEL_25;
	}
}

static void mctl_phy_vref_config(struct dram_para *para)
{
	u32 val;

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3: // TODO\
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		val = (para->tpr6 >> 16) & 0xFF;
		if (!val)
			val = 0x80;
		break;
	case SUNXI_DRAM_TYPE_DDR4: // TODO
		break;
	case SUNXI_DRAM_TYPE_LPDDR4: // TODO
		break;
	}

	writel(val, SUNXI_DRAM_PHY0_BASE + 0x3dc);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x45c);
}

static void mctl_phy_configure_odt(struct dram_para *para)
{
	u32 val;

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

static void mctl_phy_ca_bit_delay_compensation(struct dram_para *para)
{
	u32 *ptr;
	u32 i, a, b, c, d;

	if (para->tpr10 >= 0)
	{ // Sorry for direct copy from decompiler
		para->tpr0 = ((32 * para->tpr10) & 0x1E00) | ((para->tpr10 << 9) & 0x1E0000) | ((2 * para->tpr10) & 0x1E) | ((para->tpr10 << 13) & 0x1E000000);
		if (para->tpr10 >> 29)
			para->tpr0 *= 2;
	}

	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x780);
	for (i = 0; i < 32; i++)
	{
		writel((para->tpr0 >> 8) & 0x3F, ptr++);
	}

	a = para->tpr0 & 0x3f;
	b = para->tpr0 & 0x3f;
	c = (para->tpr0 >> 16) & 0x3f;
	d = (para->tpr0 >> 24) & 0x3f;

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

static void mctl_dfi_init(struct dram_para *para)
{
	struct sunxi_mctl_com_reg *const mctl_com =
		(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

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
	printf("line: %d\n", __LINE__);

	clrbits_le32(&mctl_ctl->pwrctl, 0x20);
	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->statr, 3, 1);

	if ((para->tpr13 & 0x10) == 0)
	{
		udelay(200);
	}

	clrbits_le32(&mctl_ctl->dfimisc, 1);

	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		writel(para->mr0, &mctl_ctl->mrctrl1); // dram_mr0
		writel(0x80000030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr1, &mctl_ctl->mrctrl1); // dram_mr1
		writel(0x80001030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr2, &mctl_ctl->mrctrl1); // dram_mr2
		writel(0x80002030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr3, &mctl_ctl->mrctrl1); // dram_mr3
		writel(0x80003030, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		writel(para->mr1 | 0x100, &mctl_ctl->mrctrl1); // dram_mr1
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr2 | 0x200, &mctl_ctl->mrctrl1); // dram_mr2
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr3 | 0x300, &mctl_ctl->mrctrl1); // dram_mr3
		writel(0x800000f0, &mctl_ctl->mrctrl0);
		printf("line: %d\n", __LINE__);
		mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

		writel(para->mr11 | 0xb00, &mctl_ctl->mrctrl1); // dram_mr11
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
}

// static bool mctl_phy_write_leveling(struct dram_para *para)
// {
// 	bool result = true;
// 	u32 val;

// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0, 0x80);
// 	writel(4, SUNXI_DRAM_PHY0_BASE + 0xc);
// 	writel(0x40, SUNXI_DRAM_PHY0_BASE + 0x10);

// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

// 	if (para->bus_full_width)
// 		val = 0xf;
// 	else
// 		val = 3;

// 	printf("line: %d\n", __LINE__);
// 	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x188), val, val);

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

// 	val = readl(SUNXI_DRAM_PHY0_BASE + 0x258);
// 	if (val == 0 || val == 0x3f)
// 		result = false;
// 	val = readl(SUNXI_DRAM_PHY0_BASE + 0x25c);
// 	if (val == 0 || val == 0x3f)
// 		result = false;
// 	val = readl(SUNXI_DRAM_PHY0_BASE + 0x318);
// 	if (val == 0 || val == 0x3f)
// 		result = false;
// 	val = readl(SUNXI_DRAM_PHY0_BASE + 0x31c);
// 	if (val == 0 || val == 0x3f)
// 		result = false;

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0);

// 	if (para->ranks == 2)
// 	{
// 		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0, 0x40);

// 		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

// 		if (para->bus_full_width)
// 			val = 0xf;
// 		else
// 			val = 3;

// 		printf("line: %d\n", __LINE__);
// 		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x188), val, val);

// 		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);
// 	}

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0);

// 	return result;
// }

// static bool mctl_phy_read_calibration(struct dram_para *para)
// {
// 	bool result = true;
// 	u32 val, tmp;

// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x20);

// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

// 	if (para->bus_full_width)
// 		val = 0xf;
// 	else
// 		val = 3;

// 	while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val)
// 	{
// 		if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20)
// 		{
// 			result = false;
// 			break;
// 		}
// 	}

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

// 	if (para->ranks == 2)
// 	{
// 		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x10);

// 		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

// 		while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val)
// 		{
// 			if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20)
// 			{
// 				result = false;
// 				break;
// 			}
// 		}

// 		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);
// 	}

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

// 	val = readl(SUNXI_DRAM_PHY0_BASE + 0x274) & 7;
// 	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x26c) & 7;
// 	if (val < tmp)
// 		val = tmp;
// 	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x32c) & 7;
// 	if (val < tmp)
// 		val = tmp;
// 	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x334) & 7;
// 	if (val < tmp)
// 		val = tmp;
// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x38, 0x7, (val + 2) & 7);

// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 4, 0x20);

// 	return result;
// }

// static bool mctl_phy_read_training(struct dram_para *para)
// {
// 	u32 val1, val2, *ptr1, *ptr2;
// 	bool result = true;
// 	int i;

// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3, 2);
// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x804, 0x3f, 0xf);
// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x808, 0x3f, 0xf);
// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0xa04, 0x3f, 0xf);
// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0xa08, 0x3f, 0xf);

// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 6);
// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 1);

// 	printf("line: %d\n", __LINE__);
// 	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x840), 0xc, 0xc);
// 	if (readl(SUNXI_DRAM_PHY0_BASE + 0x840) & 3)
// 		result = false;

// 	if (para->bus_full_width)
// 	{
// 		printf("line: %d\n", __LINE__);
// 		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa40), 0xc, 0xc);
// 		if (readl(SUNXI_DRAM_PHY0_BASE + 0xa40) & 3)
// 			result = false;
// 	}

// 	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x898);
// 	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x850);
// 	for (i = 0; i < 9; i++)
// 	{
// 		val1 = readl(&ptr1[i]);
// 		val2 = readl(&ptr2[i]);
// 		if (val1 - val2 <= 6)
// 			result = false;
// 	}
// 	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8bc);
// 	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x874);
// 	for (i = 0; i < 9; i++)
// 	{
// 		val1 = readl(&ptr1[i]);
// 		val2 = readl(&ptr2[i]);
// 		if (val1 - val2 <= 6)
// 			result = false;
// 	}

// 	if (para->bus_full_width)
// 	{
// 		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa98);
// 		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa50);
// 		for (i = 0; i < 9; i++)
// 		{
// 			val1 = readl(&ptr1[i]);
// 			val2 = readl(&ptr2[i]);
// 			if (val1 - val2 <= 6)
// 				result = false;
// 		}

// 		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xabc);
// 		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa74);
// 		for (i = 0; i < 9; i++)
// 		{
// 			val1 = readl(&ptr1[i]);
// 			val2 = readl(&ptr2[i]);
// 			if (val1 - val2 <= 6)
// 				result = false;
// 		}
// 	}

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 3);

// 	if (para->ranks == 2)
// 	{
// 		/* maybe last parameter should be 1? */
// 		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3, 2);

// 		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 6);
// 		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 1);

// 		printf("line: %d\n", __LINE__);
// 		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x840), 0xc, 0xc);
// 		if (readl(SUNXI_DRAM_PHY0_BASE + 0x840) & 3)
// 			result = false;

// 		if (para->bus_full_width)
// 		{
// 			printf("line: %d\n", __LINE__);
// 			mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa40), 0xc, 0xc);
// 			if (readl(SUNXI_DRAM_PHY0_BASE + 0xa40) & 3)
// 				result = false;
// 		}

// 		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 3);
// 	}

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3);

// 	return result;
// }

// static bool mctl_phy_write_training(struct dram_para *para)
// {
// 	u32 val1, val2, *ptr1, *ptr2;
// 	bool result = true;
// 	int i;

// 	writel(0, SUNXI_DRAM_PHY0_BASE + 0x134);
// 	writel(0, SUNXI_DRAM_PHY0_BASE + 0x138);
// 	writel(0, SUNXI_DRAM_PHY0_BASE + 0x19c);
// 	writel(0, SUNXI_DRAM_PHY0_BASE + 0x1a0);

// 	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc, 8);

// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);
// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x20);

// 	printf("line: %d\n", __LINE__);
// 	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8e0), 3, 3);
// 	if (readl(SUNXI_DRAM_PHY0_BASE + 0x8e0) & 0xc)
// 		result = false;

// 	if (para->bus_full_width)
// 	{
// 		printf("line: %d\n", __LINE__);
// 		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xae0), 3, 3);
// 		if (readl(SUNXI_DRAM_PHY0_BASE + 0xae0) & 0xc)
// 			result = false;
// 	}

// 	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x938);
// 	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8f0);
// 	for (i = 0; i < 9; i++)
// 	{
// 		val1 = readl(&ptr1[i]);
// 		val2 = readl(&ptr2[i]);
// 		if (val1 - val2 <= 6)
// 			result = false;
// 	}
// 	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x95c);
// 	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x914);
// 	for (i = 0; i < 9; i++)
// 	{
// 		val1 = readl(&ptr1[i]);
// 		val2 = readl(&ptr2[i]);
// 		if (val1 - val2 <= 6)
// 			result = false;
// 	}

// 	if (para->bus_full_width)
// 	{
// 		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xb38);
// 		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xaf0);
// 		for (i = 0; i < 9; i++)
// 		{
// 			val1 = readl(&ptr1[i]);
// 			val2 = readl(&ptr2[i]);
// 			if (val1 - val2 <= 6)
// 				result = false;
// 		}
// 		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xb5c);
// 		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xb14);
// 		for (i = 0; i < 9; i++)
// 		{
// 			val1 = readl(&ptr1[i]);
// 			val2 = readl(&ptr2[i]);
// 			if (val1 - val2 <= 6)
// 				result = false;
// 		}
// 	}

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x60);

// 	if (para->ranks == 2)
// 	{
// 		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc, 4);

// 		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);
// 		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x20);

// 		printf("line: %d\n", __LINE__);
// 		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8e0), 3, 3);
// 		if (readl(SUNXI_DRAM_PHY0_BASE + 0x8e0) & 0xc)
// 			result = false;

// 		if (para->bus_full_width)
// 		{
// 			printf("line: %d\n", __LINE__);
// 			mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xae0), 3, 3);
// 			if (readl(SUNXI_DRAM_PHY0_BASE + 0xae0) & 0xc)
// 				result = false;
// 		}

// 		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x60);
// 	}

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc);

// 	return result;
// }

// static bool mctl_phy_dx_bit_delay_compensation(struct dram_para *para)
// {
// 	u32 *ptr;
// 	int i;

// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);
// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 8);
// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x484);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x16, ptr);
// 		writel_relaxed(0x16, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x4d0);
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x590);
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x4cc);
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x58c);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x4d8);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x1a, ptr);
// 		writel_relaxed(0x1a, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x524);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x5e4);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x520);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x5e0);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x604);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x1a, ptr);
// 		writel_relaxed(0x1a, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x650);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x710);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x64c);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x70c);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x658);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x1a, ptr);
// 		writel_relaxed(0x1a, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x6a4);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x764);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x6a0);
// 	writel_relaxed(0x1e, SUNXI_DRAM_PHY0_BASE + 0x760);

// 	dmb();

// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);

// 	/* second part */
// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);
// 	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 4);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x480);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x10, ptr);
// 		writel_relaxed(0x10, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x528);
// 	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x5e8);
// 	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x4c8);
// 	writel_relaxed(0x18, SUNXI_DRAM_PHY0_BASE + 0x588);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x4d4);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x12, ptr);
// 		writel_relaxed(0x12, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x52c);
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x5ec);
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x51c);
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x5dc);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x600);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x12, ptr);
// 		writel_relaxed(0x12, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x6a8);
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x768);
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x648);
// 	writel_relaxed(0x1a, SUNXI_DRAM_PHY0_BASE + 0x708);

// 	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x654);
// 	for (i = 0; i < 9; i++)
// 	{
// 		writel_relaxed(0x14, ptr);
// 		writel_relaxed(0x14, ptr + 0x30);
// 		ptr += 2;
// 	}
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x6ac);
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x76c);
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x69c);
// 	writel_relaxed(0x1c, SUNXI_DRAM_PHY0_BASE + 0x75c);

// 	dmb();

// 	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);

// 	return true;
// }

static const unsigned char phy_init_ddr3_a[] = {
	0x0C, 0x08, 0x19, 0x18, 0x10, 0x06, 0x0A, 0x03,
	0x0E, 0x00, 0x0B, 0x05, 0x09, 0x1A, 0x04, 0x13,
	0x16, 0x11, 0x01, 0x15, 0x0D, 0x07, 0x12, 0x17,
	0x14, 0x02, 0x0F};
static const unsigned char phy_init_ddr4_a[] = {
	0x19, 0x1A, 0x04, 0x12, 0x09, 0x06, 0x08, 0x0A,
	0x16, 0x17, 0x18, 0x0F, 0x0C, 0x13, 0x02, 0x05,
	0x01, 0x11, 0x0E, 0x00, 0x0B, 0x07, 0x03, 0x14,
	0x15, 0x0D, 0x10};
static const unsigned char phy_init_lpddr3_a[] = {
	0x08, 0x03, 0x02, 0x00, 0x18, 0x19, 0x09, 0x01,
	0x06, 0x17, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x04,
	0x05, 0x07, 0x1A};
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
static const unsigned char phy_init_ddr4_b[] = {
	0x13, 0x17, 0xE, 0x01, 0x06, 0x12, 0x14, 0x07,
	0x09, 0x02, 0x0F, 0x00, 0x0D, 0x05, 0x16, 0x0C,
	0x0A, 0x11, 0x04, 0x03, 0x18, 0x15, 0x08, 0x10,
	0x0B, 0x19, 0x1A};
static const unsigned char phy_init_lpddr3_b[] = {
	0x05, 0x06, 0x17, 0x02, 0x19, 0x18, 0x04, 0x07,
	0x03, 0x01, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x08,
	0x09, 0x00, 0x1A};
static const unsigned char phy_init_lpddr4_b[] = {
	0x01, 0x03, 0x02, 0x19, 0x17, 0x00, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x04,
	0x18, 0x05, 0x1A};

static void mctl_phy_set_addreess_remapping(struct dram_para *para)
{
	int i;
	u32 *ptr;
	const u8 *phy_init;

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

	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xc0);
	for (i = 0; i < 27; i++)
		writel(phy_init[i], ptr++);
}

static void phy_para_config(struct dram_para *para)
{
	struct sunxi_prcm_reg *const prcm =
		(struct sunxi_prcm_reg *)SUNXI_PRCM_BASE;
	struct sunxi_mctl_com_reg *const mctl_com =
		(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	u32 val;

	clrbits_le32(&prcm->sys_pwroff_gating, 0x10);

	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x4, 0x08);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3: // TODO
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		if ((para->para2 & 1) != 0)
			val = 3;
		else
			val = 0xf;
		break;
	case SUNXI_DRAM_TYPE_DDR4: // TODO
		break;
	case SUNXI_DRAM_TYPE_LPDDR4: // TODO
		break;
	}
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

	mctl_phy_set_addreess_remapping(para);
	mctl_phy_vref_config(para);
	mctl_phy_configure_odt(para);

	if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_CA_BIT_DELAY_COMPENSATION))
	{
		mctl_phy_ca_bit_delay_compensation(para);
	}

	val = readl(SUNXI_DRAM_PHY0_BASE + 4) & 0xFFFFFFF8;

	switch (para->type)
	{
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
	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x180), 4, 4);

	if (IS_ENABLED(DRAM_SUN50I_A133_DELAY_ON_PHY_CONFIG))
	{
		udelay(1000);
	}

	writel(0x37, SUNXI_DRAM_PHY0_BASE + 0x58);
	setbits_le32(&prcm->sys_pwroff_gating, 0x10);
}

static bool ddrphy_phyinit_C_initPhyConfig(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	int i;

	phy_para_config(para);

	mctl_dfi_init(para);

	writel(0, &mctl_ctl->swctl);
	clrbits_le32(&mctl_ctl->rfshctl3, 1);
	writel(1, &mctl_ctl->swctl);

	// if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_WRITE_LEVELING))
	// {
	// 	for (i = 0; i < 5; i++)
	// 		if (mctl_phy_write_leveling(para))
	// 			break;
	// 	if (i == 5)
	// 	{
	// 		debug("write leveling failed!\n");
	// 		return false;
	// 	}
	// }

	// if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_READ_CALIBRATION))
	// {
	// 	for (i = 0; i < 5; i++)
	// 		if (mctl_phy_read_calibration(para))
	// 			break;
	// 	if (i == 5)
	// 	{
	// 		debug("read calibration failed!\n");
	// 		return false;
	// 	}
	// }

	// if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_READ_TRAINING))
	// {
	// 	for (i = 0; i < 5; i++)
	// 		if (mctl_phy_read_training(para))
	// 			break;
	// 	if (i == 5)
	// 	{
	// 		debug("read training failed!\n");
	// 		return false;
	// 	}
	// }

	// if (IS_ENABLED(CONFIG_DRAM_SUN50I_A133_WRITE_TRAINING))
	// {
	// 	for (i = 0; i < 5; i++)
	// 		if (mctl_phy_write_training(para))
	// 			break;
	// 	if (i == 5)
	// 	{
	// 		debug("write training failed!\n");
	// 		return false;
	// 	}
	// }

	// if (IS_ENABLED(DRAM_SUN50I_A133_DX_BIT_DELAY_COMPENSATION))
	// 	mctl_phy_dx_bit_delay_compensation(para);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 4);

	return true;
}

static void mctl_com_set_bus_config(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		(*((int *)0x03102ea8)) |= 0x1; // NSI Register ??
	}
	clrsetbits_le32(&mctl_ctl->sched[0], 0xff00, 0x3000);
	if ((para->tpr13 & 0x10000000) != 0)
	{
		clrsetbits_le32(&mctl_ctl->sched[0], 0xf, 0x1);
	}
}

static void mctl_com_set_controller_config(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	u32 val;

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_DDR3; // v1 = 0x40001
		break;

	case SUNXI_DRAM_TYPE_DDR4:							   // unk_35C8+4*(0+234)
		val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_DDR4; // v1 = 0x40010
		break;

	case SUNXI_DRAM_TYPE_LPDDR3:							 // unk_35C8+4*(3+234)
		val = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_LPDDR3; // v1 = 0x40008
		break;

	case SUNXI_DRAM_TYPE_LPDDR4:							  // unk_35C8+4*(4+234)
		val = MSTR_BURST_LENGTH(16) | MSTR_DEVICETYPE_LPDDR4; // v1 = 0x800020
		break;
	}
	val |= MSTR_ACTIVE_RANKS(((para->para2 >> 11) & 6) + 1); //((((dram_para2 >> 11) & 6) + 1) << 24)
	if ((para->para2 << 12) & 0x1000)						 // (result->dram_para2 << 12) & 0x1000
		val |= MSTR_BUSWIDTH_HALF;
	else
		val |= MSTR_BUSWIDTH_FULL;
	writel(BIT(31) | BIT(30) | val, &mctl_ctl->mstr); // ( | 0xC0000000 )
}

static void mctl_com_set_controller_geardown_mode(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	if (para->tpr13 & BIT(30))
	{
		setbits_le32(&mctl_ctl->mstr, MSTR_DEVICETYPE_DDR3);
	}
}

static void mctl_com_set_controller_2T_mode(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	if (mctl_ctl->mstr & BIT(11) || para->tpr13 & BIT(5))
	{
		clrbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
	}
	setbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
}

static void mctl_com_set_controller_odt(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	u32 val;

	if ((para->para2 & 0x1000) == 0)
		writel(0x0201, &mctl_ctl->odtmap);
	else
		writel(0x0303, &mctl_ctl->odtmap);

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3:
		val = 0x06000400;
		break;

	case SUNXI_DRAM_TYPE_DDR4: // TODO
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

static void mctl_com_set_controller_update(void)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	setbits_le32(&mctl_ctl->dfiupd[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->zqctl[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x2180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x3180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x4180, BIT(31) | BIT(30));
}

static void mctl_com_set_controller_dbi(struct dram_para *para)
{
	// tpr13 BIT(29), still missing fex so leave it alone
	/*
	if (tpr13 & BIT(29))
		setbits_le32(&mctl_ctl->dbictl, BIT(2));
	*/
}

static void mctl_com_set_controller_refresh(int val)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	clrsetbits_le32(&mctl_ctl->rfshctl3, BIT(0), val & BIT(0));
}

static void mctl_com_set_controller_before_phy(void)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	mctl_com_set_controller_refresh(1);
	clrbits_le32(&mctl_ctl->dfimisc, 1);
	writel(0x20, &mctl_ctl->pwrctl);
}

static void mctl_com_init(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	mctl_com_set_controller_config(para);

	if (para->type == SUNXI_DRAM_TYPE_DDR4)
	{
		mctl_com_set_controller_geardown_mode(para);
	}

	if (para->type <= SUNXI_DRAM_TYPE_DDR4)
	{
		mctl_com_set_controller_2T_mode(para);
	}

	mctl_com_set_controller_odt(para);
	mctl_com_set_controller_address_map(para);

	printf("line: %d\n", __LINE__);
	/* begin mctl_com_set_channel_timing */
	mctl_set_timing_params(para);
	/* end mctl_com_set_channel_timing */
	printf("line: %d\n", __LINE__);

	writel(0, &mctl_ctl->pwrctl);

	mctl_com_set_controller_update();

	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		mctl_com_set_controller_dbi(para);
	}

	mctl_com_set_controller_before_phy();
}

static void mctl_phy_cold_reset(struct dram_para *para)
{
	struct sunxi_mctl_com_reg *const mctl_com =
		(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	clrsetbits_le32(&mctl_com->unk_0x008, BIT(24) | BIT(9), BIT(9));
	udelay(1);
	setbits_le32(&mctl_com->unk_0x008, BIT(24));
}

static bool mctl_phy_init(struct dram_para *para)
{
	mctl_phy_cold_reset(para);
	return ddrphy_phyinit_C_initPhyConfig(para);
}

static void mctl_com_set_controller_after_phy(void)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	writel(0, &mctl_ctl->swctl);
	mctl_com_set_controller_refresh(0);
	writel(1, &mctl_ctl->swctl);
	printf("line: %d\n", __LINE__);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
}

static bool mctl_channel_init(struct dram_para *para)
{
	struct sunxi_mctl_com_reg *const mctl_com =
		(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	bool ret;

	clrsetbits_le32(&mctl_com->unk_0x008, 0x3000200, 0x2000200); // CLEAR AND SET SAME BIT ???
	setbits_le32(&mctl_com->maer0, 0x8000);

	mctl_com_set_bus_config(para);

	writel(0, &mctl_ctl->hwlpctl);

	mctl_com_init(para);
	ret = mctl_phy_init(para);
	mctl_com_set_controller_after_phy();

	(*((int *)0x07022004)) = 0x00007177;

	return ret;
}

static bool mctl_core_init(struct dram_para *para)
{
	mctl_sys_init(para);
	return mctl_channel_init(para);
}

static unsigned long mctl_calc_size(struct dram_para *para)
{
	// u8 width = para->bus_full_width ? 4 : 2;

	// /* 8 banks */
	// return (1ULL << (para->cols + para->rows + 3)) * width * para->ranks;
}

unsigned long sunxi_dram_init(void)
{
	struct dram_para para = {
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
	unsigned long size;

	(*((int *)0x3000160)) |= 0x100;
	(*((int *)0x3000168)) &= 0xffffffc0;

	printf("DRAM CLK =%d MHZ\n", para.clk);
	printf("DRAM Type =%d (3:DDR3,4:DDR4,7:LPDDR3,8:LPDDR4)\n", para.type);

	mctl_core_init(&para);

	size = mctl_calc_size(&para);

	mctl_set_master_priority();

	return size;
};
