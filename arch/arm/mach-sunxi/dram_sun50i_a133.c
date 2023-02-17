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

// Check OK 20230216 16:30
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
	clrsetbits_le32(&ccm->dram_clk_cfg, 0x800001F, DRAM_CLK_ENABLE | BIT(0) | BIT(1)); // FACTOR_N = 3
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

// Check OK 20230216 16:30
static void mctl_phy_vref_config(struct dram_para *para)
{
	u32 val;

	switch (para->type)
	{
	case SUNXI_DRAM_TYPE_DDR3: // TODO
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

// Check Should OK 20230216 16:30
static void mctl_drive_odt_config(struct dram_para *para)
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

// Check Should OK 20230216 16:30
static void mctl_phy_ca_bit_delay_compensation(struct dram_para *para)
{
	u32 *ptr, tpr0;
	u32 i, a, b, c, d;

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

	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x780);
	for (i = 0; i < 32; i++)
	{
		// printf("val: %u, ptr: %p\n", (tpr0 >> 8) & 0x3F, ptr);
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

static bool phy_read_calibration(struct dram_para *para)
{
	bool result = true;
	u32 val, tmp;

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
			result = false;
			break;
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
				result = false;
				break;
			}
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

	// val = readl(SUNXI_DRAM_PHY0_BASE + 0x274) & 7;
	// tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x26c) & 7;
	// if (val < tmp)
	// 	val = tmp;
	// tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x32c) & 7;
	// if (val < tmp)
	// 	val = tmp;
	// tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x334) & 7;
	// if (val < tmp)
	// 	val = tmp;
	// clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x38, 0x7, (val + 2) & 7);

	// setbits_le32(SUNXI_DRAM_PHY0_BASE + 4, 0x20);

	return result;
}

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

// Check OK 20230216 17:18
static void mctl_phy_set_address_remapping(struct dram_para *para)
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
	{
		// printf("val: %u, ptr: %p\n", phy_init[i], ptr);
		writel((u32)phy_init[i], ptr++);
	}
}

// Check OK 20230216 17:08
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

	mctl_phy_set_address_remapping(para);
	mctl_phy_vref_config(para);
	mctl_drive_odt_config(para);

	if (para->tpr10 >> 16)
		mctl_phy_ca_bit_delay_compensation(para);

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
	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x180), 4, 4);

	if ((para->tpr13 & 0x10) == 0)
		udelay(1000);

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

// Check OK 20230216 16:35
static void mctl_com_set_bus_config(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	if (para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		(*((unsigned int *)0x03102ea8)) |= 0x1; // NSI Register ??
	}
	clrsetbits_le32(&mctl_ctl->sched[0], 0xff00, 0x3000);
	if ((para->tpr13 & 0x10000000) != 0)
	{
		clrsetbits_le32(&mctl_ctl->sched[0], 0xf, 0x1);
	}
}

// Check OK 20230216 16:46
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
	val |= (((para->para2 >> 11) & 6) + 1) << 24;
	val |= (para->para2 << 12) & 0x1000;
	// val |= MSTR_ACTIVE_RANKS(((para->para2 >> 11) & 6) + 1); //((((dram_para2 >> 11) & 6) + 1) << 24)
	// if ((para->para2 << 12) & 0x1000)						 // (result->dram_para2 << 12) & 0x1000
	// 	val |= MSTR_BUSWIDTH_HALF;
	// else
	// 	val |= MSTR_BUSWIDTH_FULL;
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

// Check OK 20230216 16:48
static void mctl_com_set_controller_2T_mode(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	if ((mctl_ctl->mstr & 0x800) != 0 || (para->tpr13 & 0x20) != 0)
	{
		clrbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
	}
	else
	{
		setbits_le32(&mctl_ctl->mstr, MSTR_2TMODE);
	}
}

// Check OK 20230216 16:52
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

// Check OK 20230216 16:54
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

// Check OK 20230216 16:55
static void mctl_com_set_controller_dbi(struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	if ((para->tpr13 & 0x20000000) != 0)
		setbits_le32(&mctl_ctl->dbictl, 4);
}

// Check OK 20230216 16:56
static void mctl_com_set_controller_refresh(int val)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	clrsetbits_le32(&mctl_ctl->rfshctl3, BIT(0), val & BIT(0));
}

// Check OK 20230216 16:56
static void mctl_com_set_controller_before_phy(void)
{
	struct sunxi_mctl_ctl_reg *const mctl_ctl =
		(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	mctl_com_set_controller_refresh(1);
	clrbits_le32(&mctl_ctl->dfimisc, 1);
	writel(0x20, &mctl_ctl->pwrctl);
}

static unsigned int auto_cal_timing(int a1, int a2)
{
	unsigned int v2;	 // r4
	unsigned int v3;	 // r1
	unsigned int result; // r0

	v2 = a2 * a1;
	v3 = a2 * a1 % 1000u;
	result = v2 / 1000;
	if (v3)
		++result;
	return result;
}

struct dram_para *mctl_com_set_channel_timing(struct dram_para *result)
{
	int v1;				  // r7
	struct dram_para *v2; // r11
	unsigned int v3;	  // r5
	unsigned int v4;	  // r8
	unsigned int v5;	  // r9
	unsigned int v6;	  // r6
	_DWORD *v7;			  // r10
	unsigned int tpr2;	  // r1
	int v9;				  // r4
	int v10;			  // r0
	int v11;			  // r3
	int v12;			  // r1
	int v13;			  // r0
	int v14;			  // r0
	int v15;			  // r0
	int v16;			  // r3
	int v17;			  // r0
	int v18;			  // r3
	int v19;			  // r0
	int v20;			  // r3
	int v21;			  // r0
	int v22;			  // r3
	int v23;			  // r0
	int v24;			  // r0
	int v25;			  // r3
	int v26;			  // r0
	int v27;			  // r1
	int mr2;			  // r1
	int v29;			  // r0
	int v30;			  // r0
	int v31;			  // r0
	char v32;			  // r5
	char v33;			  // r0
	int v34;			  // r0
	int mr1;			  // r2
	int v36;			  // r0
	uint32_t type;		  // [sp+0h] [bp-B0h]
	int v38;			  // [sp+4h] [bp-ACh]
	int v39;			  // [sp+8h] [bp-A8h]
	int v40;			  // [sp+Ch] [bp-A4h]
	unsigned int v41;	  // [sp+10h] [bp-A0h]
	int v42;			  // [sp+14h] [bp-9Ch]
	int v43;			  // [sp+18h] [bp-98h]
	int v44;			  // [sp+1Ch] [bp-94h]
	int v45;			  // [sp+20h] [bp-90h]
	int v46;			  // [sp+24h] [bp-8Ch]
	int v47;			  // [sp+28h] [bp-88h]
	int v48;			  // [sp+2Ch] [bp-84h]
	int v49;			  // [sp+30h] [bp-80h]
	char v50;			  // [sp+34h] [bp-7Ch]
	int v51;			  // [sp+38h] [bp-78h]
	int v52;			  // [sp+3Ch] [bp-74h]
	int v53;			  // [sp+40h] [bp-70h]
	int v54;			  // [sp+44h] [bp-6Ch]
	int v55;			  // [sp+48h] [bp-68h]
	int v56;			  // [sp+4Ch] [bp-64h]
	int v57;			  // [sp+50h] [bp-60h]
	int v58;			  // [sp+54h] [bp-5Ch]
	int v59;			  // [sp+58h] [bp-58h]
	int v60;			  // [sp+5Ch] [bp-54h]
	int v61;			  // [sp+60h] [bp-50h]
	int v62;			  // [sp+64h] [bp-4Ch]
	int v63;			  // [sp+68h] [bp-48h]
	unsigned int v64;	  // [sp+6Ch] [bp-44h]
	int tpr13;			  // [sp+70h] [bp-40h]
	int v66;			  // [sp+74h] [bp-3Ch]
	int v67;			  // [sp+78h] [bp-38h]
	int v68;			  // [sp+7Ch] [bp-34h]
	int v69;			  // [sp+80h] [bp-30h]

	v1 = 3;
	v2 = result;
	type = result->type;
	v3 = 3;
	LOBYTE(v4) = 3;
	v5 = 6;
	v55 = 3;
	v47 = 6;
	v38 = 3;
	v46 = 6;
	v64 = 24 * (unsigned __int8)(_REG(0x3001011) + 1);
	v42 = 4;
	v67 = 4;
	v68 = 4;
	v66 = 8;
	v69 = 8;
	v50 = 1;
	v63 = 2;
	v39 = 4;
	v41 = 4;
	v49 = 4;
	v61 = 27;
	v62 = 8;
	v54 = 12;
	v60 = 128;
	v53 = 98;
	v48 = 10;
	v58 = 16;
	v59 = 14;
	v57 = 20;
	v6 = 2;
	v45 = 0;
	v56 = 2;
	v52 = 2;
	v40 = 3;
	v51 = 3;
	v43 = 1;
	v44 = 1;
	while (v1 != -1)
	{
		tpr13 = v2->tpr13;
		if ((tpr13 & 0x805) != 5)
			v1 = 0;
		v7 = (_DWORD *)(tpr13 & 4);
		if ((tpr13 & 4) != 0)
		{
			tpr2 = v2->tpr2;
			switch (v1)
			{
			case 0:
				v7 = 0;
				v9 = v64 / ((v2->tpr2 & 0x1Fu) + 1);
				goto LABEL_8;
			case 1:
				v12 = (tpr2 >> 8) & 0x1F;
				break;
			case 2:
				v12 = HIWORD(tpr2) & 0x1F;
				break;
			default:
				v12 = HIBYTE(tpr2) & 0x1F;
				break;
			}
			v9 = v64 / (v12 + 1);
		LABEL_18:
			v7 = (_DWORD *)((v1 + 1) << 12);
			goto LABEL_8;
		}
		v9 = v64 >> 2;
		if (v1)
			goto LABEL_18;
	LABEL_8:
		switch (type)
		{
		case 3u:
			v58 = (unsigned __int8)auto_cal_timing(50, v9);
			v10 = (unsigned __int8)auto_cal_timing(10, v9);
			if ((unsigned __int8)v10 < 2u)
				v10 = 2;
			v38 = v10;
			v47 = (unsigned __int8)auto_cal_timing(15, v9);
			v57 = (unsigned __int8)auto_cal_timing(53, v9);
			v3 = (unsigned __int8)auto_cal_timing(8, v9);
			if (v3 < 2)
				v3 = 2;
			v59 = (unsigned __int8)auto_cal_timing(38, v9);
			LOBYTE(v4) = v3;
			v53 = (unsigned __int16)(auto_cal_timing(7800, v9) >> 5);
			v60 = (unsigned __int16)auto_cal_timing(350, v9);
			v5 = v47;
			v68 = (unsigned __int8)(auto_cal_timing(360, v9) >> 5);
			v48 = v3;
			goto LABEL_14;
		case 4u:
			v58 = (unsigned __int8)auto_cal_timing(35, v9);
			v13 = (unsigned __int8)auto_cal_timing(8, v9);
			if ((unsigned __int8)v13 < 2u)
				v13 = 2;
			v38 = v13;
			v14 = (unsigned __int8)auto_cal_timing(6, v9);
			if ((unsigned __int8)v14 < 2u)
				v14 = 2;
			v48 = v14;
			v15 = (unsigned __int8)auto_cal_timing(10, v9);
			if ((unsigned __int8)v15 < 8u)
				v15 = 8;
			v66 = v15;
			v47 = (unsigned __int8)auto_cal_timing(15, v9);
			v57 = (unsigned __int8)auto_cal_timing(49, v9);
			v16 = (unsigned __int8)auto_cal_timing(3, v9);
			if (!v16)
				LOBYTE(v16) = 1;
			v50 = v16;
			v59 = (unsigned __int8)auto_cal_timing(34, v9);
			v53 = (unsigned __int16)(auto_cal_timing(7800, v9) >> 5);
			v60 = (unsigned __int16)auto_cal_timing(350, v9);
			LOBYTE(v4) = v38;
			v68 = (unsigned __int8)(auto_cal_timing(360, v9) >> 5);
			v5 = v47;
			v63 = v48;
			v11 = 3;
			goto LABEL_34;
		case 7u:
			v17 = (unsigned __int8)auto_cal_timing(50, v9);
			if ((unsigned __int8)v17 < 4u)
				v17 = 4;
			v58 = v17;
			v18 = (unsigned __int8)auto_cal_timing(10, v9);
			if (!v18)
				v18 = 1;
			v38 = v18;
			v19 = (unsigned __int8)auto_cal_timing(24, v9);
			if ((unsigned __int8)v19 < 2u)
				v19 = 2;
			v47 = v19;
			v57 = (unsigned __int8)auto_cal_timing(70, v9);
			v3 = (unsigned __int8)auto_cal_timing(8, v9);
			if (v3 < 2)
				v3 = 2;
			v5 = (unsigned __int8)auto_cal_timing(27, v9);
			v59 = (unsigned __int8)auto_cal_timing(42, v9);
			LOBYTE(v4) = v3;
			v53 = (unsigned __int16)(auto_cal_timing(3900, v9) >> 5);
			v60 = (unsigned __int16)auto_cal_timing(210, v9);
			v48 = v3;
			v67 = (unsigned __int8)auto_cal_timing(220, v9);
		LABEL_14:
			v11 = 2;
		LABEL_34:
			v56 = v11;
			break;
		case 8u:
			v58 = (unsigned __int8)auto_cal_timing(40, v9);
			v4 = (unsigned __int8)auto_cal_timing(10, v9);
			v20 = v4;
			if (v4 < 2)
				v20 = 2;
			v38 = v20;
			v21 = (unsigned __int8)auto_cal_timing(18, v9);
			if ((unsigned __int8)v21 < 2u)
				v21 = 2;
			v47 = v21;
			v57 = (unsigned __int8)auto_cal_timing(65, v9);
			v3 = (unsigned __int8)auto_cal_timing(8, v9);
			v22 = v3;
			if (v3 < 2)
				v22 = 2;
			v48 = v22;
			if ((tpr13 & 0x10000000) != 0)
				v4 = (unsigned __int8)auto_cal_timing(12, v9);
			if (v4 < 4)
				LOBYTE(v4) = 4;
			if (v3 < 4)
				v3 = 4;
			v5 = (unsigned __int8)auto_cal_timing(21, v9);
			v59 = (unsigned __int8)auto_cal_timing(42, v9);
			v53 = (unsigned __int16)(auto_cal_timing(3904, v9) >> 5);
			v60 = (unsigned __int16)auto_cal_timing(280, v9);
			v67 = (unsigned __int8)auto_cal_timing(290, v9);
			v11 = 4;
			goto LABEL_34;
		}
		switch (type)
		{
		case 3u:
			LOBYTE(v6) = auto_cal_timing(8, v9);
			v41 = (unsigned __int8)auto_cal_timing(10, v9);
			if (v41 <= 2)
			{
				v6 = 6;
			}
			else
			{
				v6 = (unsigned __int8)v6;
				if ((unsigned __int8)v6 < 2u)
					v6 = 2;
			}
			v55 = (unsigned __int8)(v6 + 1);
			mr2 = v2->mr2;
			v61 = (unsigned __int8)(v9 / 15);
			v2->mr0 = 0x1F14;
			v2->mr2 = mr2 & 0xFFFFFFC7 | 0x20;
			v2->mr3 = 0;
			if ((int)(v3 + v5) <= 8)
				v3 = (unsigned __int8)(9 - v5);
			v62 = (unsigned __int8)(v4 + 7);
			v39 = v41;
			v49 = 5;
			v54 = 14;
			v46 = 12;
			goto LABEL_72;
		case 4u:
			v23 = (unsigned __int8)auto_cal_timing(15, v9);
			if ((unsigned __int8)v23 < 0xCu)
				v23 = 12;
			v46 = v23;
			v6 = (unsigned __int8)auto_cal_timing(5, v9);
			if (v6 < 2)
				v6 = 2;
			v24 = (unsigned __int8)auto_cal_timing(10, v9);
			if ((unsigned __int8)v24 < 3u)
				v24 = 3;
			v55 = (unsigned __int8)(v6 + 1);
			v41 = v24;
			v42 = (unsigned __int8)(auto_cal_timing(170, v9) >> 5);
			v61 = (unsigned __int8)(auto_cal_timing(70200, v9) >> 10);
			if (v5 > 4)
				v3 = 4;
			else
				v3 = 9 - v5;
			if (v5 <= 4)
				v3 = (unsigned __int8)v3;
			v2->mr2 = v2->mr2 & 0xFFFFFFC7 | 8;
			v2->mr0 = 1312;
			v62 = (unsigned __int8)(v4 + 7);
			v69 = (unsigned __int8)(v50 + 7);
			v39 = v41;
			v49 = 5;
			v54 = 14;
		LABEL_72:
			v52 = 4;
			v45 = 0;
			v40 = 5;
			v51 = 7;
			v44 = 6;
			v25 = 10;
			goto LABEL_73;
		case 7u:
			v2->mr1 = 131;
			v2->mr2 = 28;
			v2->mr0 = 0;
			v6 = 3;
			v62 = (unsigned __int8)(v4 + 9);
			v39 = 5;
			v41 = 5;
			v55 = 5;
			v49 = 13;
			v61 = 24;
			v54 = 16;
			v46 = 12;
			v52 = 5;
			v45 = 5;
			v40 = 4;
			v51 = 7;
			v44 = 6;
			v25 = 12;
			goto LABEL_73;
		case 8u:
			v29 = (unsigned __int8)auto_cal_timing(14, v9);
			if ((unsigned __int8)v29 < 5u)
				v29 = 5;
			v45 = v29;
			v6 = (unsigned __int8)auto_cal_timing(15, v9);
			if (v6 < 2)
				v6 = 2;
			v30 = (unsigned __int8)auto_cal_timing(2, v9);
			if ((unsigned __int8)v30 < 2u)
				v30 = 2;
			v41 = v30;
			v31 = (unsigned __int8)auto_cal_timing(5, v9);
			if ((unsigned __int8)v31 < 2u)
				v31 = 2;
			v39 = v31;
			v61 = (unsigned __int8)((unsigned int)(9 * v53) >> 5);
			v32 = auto_cal_timing(4, v9) + 17;
			v33 = auto_cal_timing(1, v9);
			v2->mr1 = 52;
			v2->mr2 = 27;
			v49 = (unsigned __int8)(v32 - v33);
			v55 = v6;
			v3 = 4;
			v62 = (unsigned __int8)(v4 + 14);
			v52 = v45;
			v54 = 24;
			v46 = 12;
			v40 = 5;
			if ((tpr13 & 0x10000000) != 0)
			{
				v51 = 11;
				v44 = 5;
				v25 = 19;
			}
			else
			{
				v51 = 10;
				v44 = 5;
				v25 = 17;
			}
		LABEL_73:
			v43 = v25;
			break;
		default:
			break;
		}
		v7[0x1208040] = v59 | (v58 << 16) | (v54 << 24) | (v61 << 8);
		v7[0x1208041] = v57 | (v48 << 16) | (v3 << 8);
		v7[0x1208042] = (v51 << 16) | (v40 << 24) | v62 | (v49 << 8);
		v7[0x1208043] = (v52 << 12) | (v45 << 20) | v46;
		v7[0x1208044] = (v56 << 16) | (v47 << 24) | v5 | (v38 << 8);
		v7[0x1208045] = (v39 << 16) | (v41 << 24) | v6 | (v55 << 8);
		v7[0x1208046] = (v48 + 2) | 0x2020000;
		v7[0x1208048] = v68 | 0x1000 | (v42 << 24) | (v42 << 16);
		v7[0x1208049] = v69 | (v63 << 8) | 0x20000;
		v7[0x120804A] = 0xE0C05;
		v7[0x120804B] = 0x440C021C;
		v7[0x120804C] = v66;
		v7[0x120804D] = 0xA100002;
		v7[0x120804E] = v67;
		if (type == 7)
		{
			v26 = _REG(0x48200D0) & 0x3C00FFFF | 0x4F0000;
		LABEL_76:
			v27 = v26 & 0x3FFFF000 | 0x112;
			goto LABEL_98;
		}
		if (type != 8)
		{
			v26 = _REG(0x48200D0) & 0x3FFFFFFF;
			goto LABEL_76;
		}
		v27 = _REG(0x48200D0) & 0x3FFFF000 | 0x3F0;
	LABEL_98:
		_REG(0x48200D0) = v27;
		if ((v2->tpr13 & 8) != 0)
			v34 = 0x420000;
		else
			v34 = 0x1F20000;
		_REG(0x48200D4) = v34;
		_REG(0x48200D8) = _REG(0x48200D8) & 0xFFFF00F0 | 0xD05;
		_REG(0x48201B0) = 0;
		mr1 = v2->mr1;
		if (type - 6 > 2)
		{
			v7[0x1208037] = mr1 | (v2->mr0 << 16);
			v7[0x1208038] = v2->mr3 | (v2->mr2 << 16);
			if (type == 4)
			{
				v7[0x120803A] = v2->mr5 | (v2->mr4 << 16);
				v7[0x120803B] = v2->mr6;
			}
		}
		else
		{
			v7[0x1208037] = v2->mr2 | (mr1 << 16);
			v7[0x1208038] = v2->mr3 << 16;
			if (type == 8)
			{
				v7[0x120803A] = v2->mr12 | (v2->mr11 << 16);
				v7[0x120803B] = v2->mr14 | (v2->mr22 << 16);
			}
		}
		v7[18907197] = v7[18907197] & 0xFFFFF00F | 0x660;
		if ((v2->tpr13 & 0x20) != 0)
			v36 = v44 | 0x2000000 | (v43 << 16);
		else
			v36 = (v44 - 1) | 0x2000000 | ((v43 - 1) << 16);
		--v1;
		v7[0x1208064] = v36 | 0x808000;
		v7[0x1208065] = 1049090;
		result = (struct dram_para *)0x4820064;
		v7[0x1208019] = v60 | (v53 << 16);
	}
	return result;
}

// Check OK 20230216 16:37
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
	mctl_com_set_channel_timing(para);
	printf("line: %d\n", __LINE__);

	writel(0, &mctl_ctl->pwrctl);

	mctl_com_set_controller_update();

	if (para->type == SUNXI_DRAM_TYPE_DDR4 || para->type == SUNXI_DRAM_TYPE_LPDDR4)
	{
		mctl_com_set_controller_dbi(para);
	}

	mctl_com_set_controller_before_phy();
}

// Check OK 20230216 16:58
static void mctl_phy_cold_reset(void)
{
	struct sunxi_mctl_com_reg *const mctl_com =
		(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	clrsetbits_le32(&mctl_com->unk_0x008, 0x1000200, 0x200);
	udelay(1);
	setbits_le32(&mctl_com->unk_0x008, 0x1000000);
}

// Check OK 20230216 16:57
static bool mctl_phy_init(struct dram_para *para)
{
	mctl_phy_cold_reset();
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

// Check OK 20230216 16:33
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

// Check OK 20230216 16:25
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

// TODO
bool auto_scan_dram_rank_width(struct dram_para *a1)
{
	int tpr10;		  // r7
	int tpr13;		  // r8
	__int64 v4;		  // kr00_8
	int v5;			  // r3
	unsigned int v6;  // r5
	int para2;		  // r2
	int v8;			  // r10
	int calibration;  // r0
	unsigned int v11; // r5
	int v12;		  // r0
	unsigned int v13; // r5
	int v14;		  // r0
	unsigned int i;	  // r5
	int v16;		  // r0

	tpr10 = a1->tpr10;
	tpr13 = a1->tpr13;
	v4 = *(_QWORD *)&a1->para1;
	a1->tpr10 = tpr10 | 0x10000000;
	a1->tpr13 = tpr13 | 1;
	if (a1->type == 4)
		v5 = 24759;
	else
		v5 = 8375;
	a1->para1 = v5;
	a1->para2 = 4096;
	v6 = mctl_core_init(a1);
	if (v6)
		goto LABEL_5;
	if ((a1->tpr13 & 2) != 0)
	{
		do
		{
			calibration = phy_read_calibration((int)a1);
			if (calibration == 1)
				v6 = 10;
			++v6;
		} while (v6 <= 9);
		if (calibration)
		{
		LABEL_5:
			printf("[AUTO DEBUG]32bit,2 ranks training success!\n");
		LABEL_6:
			para2 = a1->para2;
			v8 = 1;
			a1->tpr13 = tpr13;
			a1->para1 = v4;
			a1->tpr10 = tpr10;
			a1->para2 = (HIWORD(HIDWORD(v4)) << 16) | para2;
			return v8;
		}
	}
	a1->para2 = 0;
	v11 = mctl_core_init(a1);
	if (v11)
		goto LABEL_14;
	if ((a1->tpr13 & 2) != 0)
	{
		do
		{
			v12 = phy_read_calibration((int)a1);
			if (v12 == 1)
				v11 = 10;
			++v11;
		} while (v11 <= 9);
		if (v12)
		{
		LABEL_14:
			printf("[AUTO DEBUG]32bit,1 ranks training success!\n");
			goto LABEL_6;
		}
	}
	a1->para2 = 4097;
	v13 = mctl_core_init(a1);
	if (v13)
		goto LABEL_21;
	if ((a1->tpr13 & 2) != 0)
	{
		do
		{
			v14 = phy_read_calibration((int)a1);
			if (v14 == 1)
				v13 = 10;
			++v13;
		} while (v13 <= 9);
		if (v14)
		{
		LABEL_21:
			printf("[AUTO DEBUG]16 bit,2 ranks training success!\n");
			goto LABEL_6;
		}
	}
	a1->para2 = 1;
	v8 = mctl_core_init(a1);
	if (v8)
		goto LABEL_28;
	if ((a1->tpr13 & 2) != 0)
	{
		for (i = 0; i <= 9; ++i)
		{
			v16 = phy_read_calibration((int)a1);
			if (v16 == 1)
				i = 10;
		}
		if (v16)
		{
		LABEL_28:
			printf("[AUTO DEBUG]16 bit,1 ranks training success!\n");
			goto LABEL_6;
		}
	}
	return v8;
}

bool auto_scan_dram_size(struct dram_para *a1)
{
	int tpr10;			// r6
	unsigned int para1; // r5
	int v4;				// r3
	__int64 v5;			// kr00_8
	__int16 v6;			// r9
	int result;			// r0
	int v8;				// r2
	int i;				// r3
	int v10;			// r1
	int v11;			// r3
	int v12;			// r1
	int v13;			// r2
	int v14;			// r8
	__int16 v15;		// r2
	int k;				// r7
	int v17;			// r3
	int v18;			// r1
	int v19;			// r12
	int v20;			// r2
	int v21;			// r1
	int v22;			// r3
	int v23;			// r0
	int v24;			// r10
	int v25;			// r3
	__int64 v26;		// kr08_8
	char v27;			// r11
	int v28;			// r2
	int m;				// r3
	int v30;			// r1
	int n;				// r1
	int v32;			// r2
	int v33;			// r3
	int v34;			// r12
	int v35;			// r3
	int j;				// r1
	int v37;			// r2

	tpr10 = a1->tpr10;
	para1 = a1->para1;
	a1->tpr10 = tpr10 | 0x10000000;
	if (a1->type == 4)
		v4 = 0xB0EB;
	else
		v4 = 0x30EB;
	a1->para1 = v4;
	v5 = *(_QWORD *)&a1->para1;
	if ((v5 & 0xF00000000LL) != 0)
		v6 = 1;
	else
		v6 = 2;
	if (!mctl_core_init(a1))
		return 0;
	v8 = 0x40000000;
	for (i = 0; i != 16; ++i)
	{
		if ((i & 1) != 0)
			v10 = v8;
		else
			v10 = ~v8;
		*(_DWORD *)v8 = v10;
		v8 += 4;
		dsb();
	}
	v11 = 0x40000000;
	v12 = 0;
	while (1)
	{
		v13 = (v12 & 1) != 0 ? v11 : ~v11;
		if (v13 != *(_DWORD *)(v11 + 64))
			break;
		++v12;
		v11 += 4;
		if (v12 == 16)
		{
			v14 = 1;
			goto LABEL_20;
		}
	}
	v35 = 0x40000000;
	for (j = 0; j != 16; ++j)
	{
		if ((j & 1) != 0)
			v37 = v35;
		else
			v37 = ~v35;
		if (v37 != *(_DWORD *)(v35 + 128))
			break;
		v35 += 4;
	}
	v14 = a1->type == 4 ? 2 : 0;
LABEL_20:
	v15 = ((unsigned __int16)v5 >> 14) + v6;
	for (k = 7; k != 11; ++k)
	{
		v17 = 0x40000000;
		v18 = 0;
		while (1)
		{
			v19 = (v18 & 1) != 0 ? v17 : ~v17;
			if (*(_DWORD *)((1 << (v15 + k)) + v17) != v19)
				break;
			++v18;
			v17 += 4;
			if (v18 == 16)
				goto LABEL_27;
		}
	}
LABEL_27:
	v20 = 1 << (v15 + 13);
	v21 = 0x40000000;
	v22 = 0;
	while (1)
	{
		v23 = (v22 & 1) != 0 ? v21 : ~v21;
		if (*(_DWORD *)(v20 + v21) != v23)
			break;
		++v22;
		v21 += 4;
		if (v22 == 16)
		{
			v24 = 2;
			goto LABEL_34;
		}
	}
	v24 = 3;
LABEL_34:
	v25 = a1->type == 4 ? 24856 : 8472;
	a1->para1 = v25;
	v26 = *(_QWORD *)&a1->para1;
	v27 = (v26 & 0xF00000000LL) != 0 ? 1 : 2;
	if (!mctl_core_init(a1))
		return 0;
	v28 = 0x40000000;
	for (m = 0; m != 16; ++m)
	{
		if ((m & 1) != 0)
			v30 = v28;
		else
			v30 = ~v28;
		*(_DWORD *)v28 = v30;
		v28 += 4;
		dsb();
	}
	for (n = 12; n != 17; ++n)
	{
		v32 = 0x40000000;
		v33 = 0;
		while (1)
		{
			v34 = (v33 & 1) != 0 ? v32 : ~v32;
			if (*(_DWORD *)((1 << (((unsigned __int16)v26 >> 14) + 10 + v27 + n)) + v32) != v34)
				break;
			++v33;
			v32 += 4;
			if (v33 == 16)
				goto LABEL_53;
		}
	}
LABEL_53:
	a1->tpr10 = tpr10;
	result = 1;
	a1->para1 = (HIWORD(para1) << 16) | (v14 << 14) | k | (v24 << 12) | (16 * n);
	return result;
}

unsigned int DRAMC_get_dram_size(struct dram_para *a1)
{
	unsigned int para2;	 // r1
	unsigned int v3;	 // r2
	char v4;			 // r2
	unsigned int result; // r0

	para2 = a1->para2;
	v3 = ((unsigned __int16)para2 >> 12) + ((unsigned __int16)a1->para1 >> 14) + (unsigned __int8)((unsigned int)a1->para1 >> 4) + (((unsigned int)a1->para1 >> 12) & 3) + (a1->para1 & 0xF);
	if ((para2 & 0xF) != 0)
		v4 = v3 - 19;
	else
		v4 = v3 - 18;
	result = 1 << v4;
	if (HIWORD(a1->tpr13) << 29)
	{
		if (para2 >> 30 != 2)
			return (3 * result) >> 2;
	}
	return result;
}

bool auto_scan_dram_config(struct dram_para *a1)
{
	int tpr13;				// r3
	unsigned int clk;		// r6
	int para0;				// r5
	int tpr12;				// r8
	int tpr11;				// r9
	int tpr14;				// r7
	int v9;					// r3
	unsigned int dram_size; // r0
	int v11;				// r1

	tpr13 = a1->tpr13;
	clk = a1->clk;
	if ((tpr13 & 0x1000) != 0 && clk > 0x168)
		a1->clk = 360;
	para0 = tpr13 & 0x2000000;
	if ((tpr13 & 0x2000000) != 0)
	{
		para0 = a1->para0;
		a1->para0 = 0x14151A1C;
		tpr12 = a1->tpr12;
		tpr11 = a1->tpr11;
		tpr14 = a1->tpr14;
		a1->tpr11 = 0xE131619;
		a1->tpr12 = 0x18171817;
		a1->tpr14 = 0x2A28282B;
	}
	else
	{
		tpr14 = 0;
		tpr12 = 0;
		tpr11 = 0;
	}
	if ((tpr13 & 0x4000) == 0 && !auto_scan_dram_rank_width(a1) || !auto_scan_dram_size(a1))
		return 0;
	v9 = a1->tpr13;
	if ((v9 & 0x8000) == 0)
		a1->tpr13 = v9 | 0x6001;
	if ((a1->tpr13 & 0x80000) != 0)
	{
		if (!mctl_core_init(a1))
			return 0;
		dram_size = DRAMC_get_dram_size(a1);
		a1->tpr13 &= ~0x80000u;
		if (dram_size == 4096)
		{
			_REG(0xA0000000) = 0xA0A0A0A0;
			dsb();
			if (_REG(0xA0000000) != 0xA0A0A0A0)
			{
				a1->tpr13 |= 0x10000u;
				printf("[AUTO DEBUG]3GB autoscan enable,dram_tpr13 = %x\n");
			}
			goto LABEL_19;
		}
		if (dram_size == 2048)
		{
			_REG(0x70000000) = 0x70707070;
			dsb();
			if (_REG(0x70000000) == 0x70707070)
			{
				_REG(0xA0000000) = 0xA0A0A0A0;
				_REG(0x80000000) = 0x80808080;
				udelay(1);
				if (_REG(0xA0000000) == 0xA0A0A0A0)
					goto LABEL_19;
				v11 = a1->tpr13 | 0x50000;
			}
			else
			{
				v11 = a1->tpr13 | 0x20000;
			}
			a1->tpr13 = v11;
			printf("[AUTO DEBUG]1.5GB autoscan enable,dram_tpr13 = %x\n");
		}
	}
LABEL_19:
	if ((a1->tpr13 & 0x2000000) != 0)
	{
		if ((a1->para2 & 0x1000) != 0)
		{
			a1->para0 = para0;
			a1->tpr11 = tpr11;
			a1->tpr12 = tpr12;
			a1->tpr14 = tpr14;
		}
		else
		{
			a1->para0 = a1->mr17;
			a1->tpr11 = a1->tpr1;
			a1->tpr12 = a1->tpr2;
			a1->tpr14 = a1->mr22;
		}
	}
	a1->clk = clk;
	return 1;
}

bool dram_software_training(struct dram_para *a1)
{
	printf("dram_software_training\n");
	return false;
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

	if ((para.tpr13 & 1) == 0)
	{
		printf("l: %d\n", __LINE__);
		if (!auto_scan_dram_config(&para))
		{
			printf("auto_scan_dram_config: failed\n");
			return 0;
		}
	}

	if ((para.tpr13 & 0x800) != 0)
	{
		if (!dram_software_training(&para))
		{
			printf("dram_software_training: failed\n");
			return 0;
		}
	}

	printf("DRAM CLK =%d MHZ\n", para.clk);
	printf("DRAM Type =%d (3:DDR3,4:DDR4,7:LPDDR3,8:LPDDR4)\n", para.type);

	mctl_core_init(&para);

	size = mctl_calc_size(&para);

	mctl_set_master_priority();

	return size;
};
