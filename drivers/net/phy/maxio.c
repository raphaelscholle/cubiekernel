// SPDX-License-Identifier: GPL-2.0+
/* drivers/net/phy/maxio.c
 *
 * Driver for Maxio PHYs
 */
#include <linux/bitops.h>
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mdio.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/version.h>

#define MAXIO_PHY_VER			"v1.8.1.13"
#define MAXIO_PAGE_SELECT		0x1f
#define MAXIO_MAE0621A_WORK_STATUS_REG	0x1d
#define MAXIO_MAE0621A_CLK_MODE_REG	0x02

#define MAXIO_PHYSR_P_A43  (0X1A)
#define MAXIO_PHY_LINK     (1<<2)
#define MAXIO_PHY_DUPLEX   (1<<3)
#define MAXIO_PHY_SPEED    (3<<4)
#define MAXIO_PHY_1000M	   (0X20)
#define MAXIO_PHY_100M     (0X10)
#define MAXIO_PHY_10M      (0X00)

#define AUTONEG_COMPLETED_INT_EN	(0x8)
#define LINKOK				(0x4)
#define AUTONEG_COMPLETED		(0x8)

#define PHY_READ(a,b)		phy_read((a),(b))
#define PHY_WRITE(a,b,c)	phy_write((a),(b),(c))

int maxio_read_paged(struct phy_device *phydev, int page, u32 regnum)
{
	int ret = 0, oldpage;

	oldpage = PHY_READ(phydev, MAXIO_PAGE_SELECT);
	if (oldpage >= 0) {
		PHY_WRITE(phydev, MAXIO_PAGE_SELECT, page);
		ret = PHY_READ(phydev, regnum);
	}
	PHY_WRITE(phydev, MAXIO_PAGE_SELECT, oldpage);

	return ret;
}

int maxio_write_paged(struct phy_device *phydev, int page, u32 regnum, u16 val)
{
	int ret = 0, oldpage;

	oldpage = PHY_READ(phydev, MAXIO_PAGE_SELECT);
	if (oldpage >= 0) {
		PHY_WRITE(phydev, MAXIO_PAGE_SELECT, page);
		ret = PHY_WRITE(phydev, regnum, val);
	}

	PHY_WRITE(phydev, MAXIO_PAGE_SELECT, oldpage);

	return ret;
}

int maxio_adcc_check(struct phy_device *phydev)
{
	int ret = 0;
	int adcvalue;
	u32 regval;
	int i;

	maxio_write_paged(phydev, 0xd96, 0x2, 0x1fff );
	maxio_write_paged(phydev, 0xd96, 0x2, 0x1000 );

	for (i = 0; i < 4; i++) {
		regval = 0xf908 + i * 0x100;
		maxio_write_paged(phydev, 0xd8f, 0xb, regval );
		adcvalue = maxio_read_paged(phydev, 0xd92, 0xb);
		if (adcvalue & 0x1ff) {
			 continue;
		} else {
			ret = -1;
			break;
		}
	}

	return ret;
}

int maxio_self_check(struct phy_device *phydev, int checknum)
{
	int ret = 0;
	int i;

	for (i = 0; i < checknum; i++) {
		ret = maxio_adcc_check(phydev);
		if (0 == ret) {
			printk("MAE0621A READY\n");
			break;
		} else {
			maxio_write_paged(phydev, 0x0, 0x0, 0x1940 );
			PHY_WRITE(phydev, MAXIO_PAGE_SELECT, 0x0);
			msleep(10);
			maxio_write_paged(phydev, 0x0, 0x0, 0x1140 );
			maxio_write_paged(phydev, 0x0, 0x0, 0x9140 );
		}
	}

	maxio_write_paged(phydev, 0xd96, 0x2, 0xfff );
	maxio_write_paged(phydev, 0x0, 0x0, 0x9140 );
	PHY_WRITE(phydev, MAXIO_PAGE_SELECT, 0x0);

	return ret;
}

static int maxio_mae0621a_resume(struct phy_device *phydev)
{
	int ret;

	ret = genphy_resume(phydev);

	ret |= PHY_WRITE(phydev, MII_BMCR, BMCR_RESET | PHY_READ(phydev, MII_BMCR));
	msleep(20);

	return ret;
}

static int maxio_mae0621a_suspend(struct phy_device *phydev)
{
	int ret = 0;

	ret = genphy_suspend(phydev);
	ret |= PHY_WRITE(phydev, MAXIO_PAGE_SELECT ,0);

	return ret;
}

int maxio_restart_aneg(struct phy_device *phydev)
{
	int ctl = PHY_READ(phydev, MII_BMCR);

	if (ctl < 0)
		return ctl;

	ctl |= BMCR_ANENABLE | BMCR_ANRESTART;

	ctl &= ~BMCR_ISOLATE;

	return PHY_WRITE(phydev, MII_BMCR, ctl);
}

static void phy_resolve_aneg_linkmode_maxio(struct phy_device *phydev){

	int physr_p_a43 = maxio_read_paged(phydev,0xa43,MAXIO_PHYSR_P_A43);

	if ((physr_p_a43&MAXIO_PHY_SPEED) == MAXIO_PHY_1000M) {
		phydev->speed = SPEED_1000;
	} else if ((physr_p_a43&MAXIO_PHY_SPEED) == MAXIO_PHY_100M) {
		phydev->speed = SPEED_100;
	} else if ((physr_p_a43&MAXIO_PHY_SPEED) == MAXIO_PHY_10M) {
		phydev->speed = SPEED_10;
	}

	if (physr_p_a43 & MAXIO_PHY_DUPLEX) {
		phydev->duplex = DUPLEX_FULL;
	} else {
		phydev->duplex = DUPLEX_HALF;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
	genphy_read_lpa(phydev);
#else
	if (phydev->duplex == DUPLEX_FULL) {
		int lpa = PHY_READ(phydev, MII_LPA);
		phydev->pause = (lpa & LPA_PAUSE_CAP) ? 1 : 0;
		phydev->asym_pause = (lpa & LPA_PAUSE_ASYM) ? 1 : 0;
	}
#endif
}

void phy_resolve_link_compatibility_maxio(struct phy_device *phydev)
{
	int *paras = (int *)phydev->priv;
	int maxio_an_times = paras[0];
	int link_stable = paras[1];
	int iner, physr, insr;
	iner = maxio_read_paged(phydev, 0xa42, 0x12);
	if (iner&AUTONEG_COMPLETED_INT_EN) {
		physr = maxio_read_paged(phydev, 0xa43, 0x1a);
		if (physr & LINKOK) {
			insr = maxio_read_paged(phydev, 0xa43, 0x1d);
			if ((insr & AUTONEG_COMPLETED) == 0 && (link_stable == 0)) {
				if (maxio_an_times < 4 ) {
					maxio_restart_aneg(phydev);
					phydev->link = 0;
					maxio_an_times++;
				} else if (maxio_an_times == 4) {
					link_stable = 1;
				}
			} else if (insr & AUTONEG_COMPLETED) {
				maxio_an_times = 0;
				link_stable = 1;
			}

			if (link_stable == 1)
				maxio_an_times = 0;
		} else
			link_stable = 0;
	}

	paras[0] = maxio_an_times;
	paras[1] = link_stable;
}

static int maxio_mae0621a_status(struct phy_device *phydev)
{
	int err, old_link = phydev->link;

	err = genphy_update_link(phydev);
	if (err)
		return err;

	if (phydev->autoneg == AUTONEG_ENABLE && old_link && phydev->link)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	phy_resolve_aneg_linkmode_maxio(phydev);

	if (phydev->autoneg == AUTONEG_ENABLE)
		phy_resolve_link_compatibility_maxio(phydev);

	return 0;
}

static void maxio_mae0621a_remove(struct phy_device *phydev)
{
	printk("maxio driver remove\r\n");
	if (phydev->priv != NULL) {
		kfree(phydev->priv);
	}
	phydev->priv=NULL;
}

static int maxio_mae0621a_config_init(struct phy_device *phydev)
{
	int ret = 0;

	printk("MAXIO_PHY_VER: %s \n",MAXIO_PHY_VER);

	ret |= maxio_write_paged(phydev, 0xda0, 0x10, 0xc13);
	ret |= maxio_write_paged(phydev, 0x0, 0xd, 0x7);
	ret |= maxio_write_paged(phydev, 0x0, 0xe, 0x3c);
	ret |= maxio_write_paged(phydev, 0x0, 0xd, 0x4007);
	ret |= maxio_write_paged(phydev, 0x0, 0xe, 0x0);
	ret |= maxio_write_paged(phydev, 0xd96, 0x13, 0x7bc);
	ret |= maxio_write_paged(phydev, 0xd8f, 0x8, 0x2500);
	ret |= maxio_write_paged(phydev, 0xd90, 0x2, 0x1555);
	ret |= maxio_write_paged(phydev, 0xd90, 0x5, 0x2b15);
	ret |= maxio_write_paged(phydev, 0xd92, 0x14, 0xa);
	ret |= maxio_write_paged(phydev, 0xd91, 0x7, 0x5b00);
	ret |= maxio_write_paged(phydev, 0xd8f, 0x0, 0x300);
	ret |= maxio_write_paged(phydev, 0xd92, 0xa, 0x8506);
	ret |= maxio_write_paged(phydev, 0xd91, 0x6, 0x6870);
	ret |= maxio_write_paged(phydev, 0xd91, 0x1, 0x940);
	ret |= maxio_write_paged(phydev, 0xda0, 0x13, 0x1303);
	ret |= maxio_write_paged(phydev, 0xd97, 0xc, 0x177);
	ret |= maxio_write_paged(phydev, 0xd97, 0xb, 0x9a9);
	ret |= maxio_write_paged(phydev, 0xa42, 0x12, 0x28);
	ret |= maxio_write_paged(phydev, 0x0, 0x4, 0xde1);
	ret |= maxio_write_paged(phydev, 0x0, 0x0, 0x9140);

	PHY_WRITE(phydev, MAXIO_PAGE_SELECT, 0x0);

	ret |= maxio_self_check(phydev,50);
	msleep(100);

	return 0;
}

static int maxio_mae0621a_probe(struct phy_device *phydev)
{
	int *paras;
	paras = kzalloc( 2 * sizeof(int), GFP_KERNEL);
	if (!paras)
		return -ENOMEM;

	phydev->priv = paras;

	printk("maxio_mae0621a_probe clkmode(oscillator) PHY_ID: 0x%x\n", phydev->phy_id);

	PHY_WRITE(phydev, MAXIO_PAGE_SELECT, 0x0);
	mdelay(100);

	return 0;
}

static int maxio_mae0621aq3ci_probe(struct phy_device *phydev)
{
	int *paras;
	paras = kzalloc( 2 * sizeof(int), GFP_KERNEL);
	if(!paras)
		return -ENOMEM;

	phydev->priv = paras;

	printk("maxio_mae0621aQ3C probe PHY_ID: 0x%x\n", phydev->phy_id);

	return 0;
}

static int maxio_mae0621aq3ci_config_init(struct phy_device *phydev)
{
	int ret = 0;
	printk("MAXIO_PHY_VER: %s \n",MAXIO_PHY_VER);

	ret |= maxio_write_paged(phydev, 0xa43, 0x19, 0x823);
	ret |= maxio_write_paged(phydev, 0xdab, 0x17, 0xC13);
	ret |= maxio_write_paged(phydev, 0xd96, 0x15, 0xc08a);
	ret |= maxio_write_paged(phydev, 0xda4, 0x12, 0x7bc);
	ret |= maxio_write_paged(phydev, 0xd8f, 0x16, 0x2500);
	ret |= maxio_write_paged(phydev, 0xd90, 0x16, 0x1555);
	ret |= maxio_write_paged(phydev, 0xd92, 0x11, 0x2b15);
	ret |= maxio_write_paged(phydev, 0xd96, 0x16, 0x4010);
	ret |= maxio_write_paged(phydev, 0xda5, 0x11, 0x4a12);
	ret |= maxio_write_paged(phydev, 0xda5, 0x12, 0x4a12);
	ret |= maxio_write_paged(phydev, 0xd99, 0x16, 0xa);
	ret |= maxio_write_paged(phydev, 0xd95, 0x13, 0x5b00);
	ret |= maxio_write_paged(phydev, 0xd8f, 0x10, 0x300);
	ret |= maxio_write_paged(phydev, 0xd98, 0x17, 0x8506);
	ret |= maxio_write_paged(phydev, 0xd95, 0x12, 0x6870);
	ret |= maxio_write_paged(phydev, 0xd93, 0x15, 0x940);
	ret |= maxio_write_paged(phydev, 0xdad, 0x12, 0x303);   // TXCST OFF
	ret |= maxio_write_paged(phydev, 0xdad, 0x13, 0x50d);   // IO DS=1
	ret |= maxio_write_paged(phydev, 0xdad, 0x14, 0xd05);
	ret |= maxio_write_paged(phydev, 0xdad, 0x15, 0x505);
	ret |= maxio_write_paged(phydev, 0xdad, 0x17, 0x1);
	ret |= maxio_write_paged(phydev, 0xda8, 0x11, 0x177);
	ret |= maxio_write_paged(phydev, 0xda8, 0x10, 0x9a9);
	ret |= maxio_write_paged(phydev, 0xda8, 0x12, 0x868);
	ret |= maxio_write_paged(phydev, 0xa42, 0x12, 0x28);
	ret |= maxio_write_paged(phydev, 0x0, 0x4, 0xde1);
	ret |= maxio_write_paged(phydev, 0x0, 0x0, 0x9140);

	ret |= PHY_WRITE(phydev, MAXIO_PAGE_SELECT, 0);

	return 0;
}

static int maxio_mae0621aq3ci_resume(struct phy_device *phydev)
{
	int ret = 0;
	ret = genphy_resume(phydev);
	ret |= maxio_write_paged(phydev, 0xdaa, 0x17, 0x1001 );
	ret |= maxio_write_paged(phydev, 0xdab, 0x15, 0x0 );
	ret |= PHY_WRITE(phydev, MAXIO_PAGE_SELECT, 0);

	return ret;
}

static int maxio_mae0621aq3ci_suspend(struct phy_device *phydev)
{
	int ret = 0;

	ret = maxio_write_paged(phydev, 0xdaa, 0x17, 0x1011 );
	ret |= maxio_write_paged(phydev, 0xdab, 0x15, 0x5550 );
	ret |= PHY_WRITE(phydev, MAXIO_PAGE_SELECT, 0);

	ret |= genphy_suspend(phydev);

	ret |= PHY_WRITE(phydev, MAXIO_PAGE_SELECT ,0);

	return ret;
}

static struct phy_driver maxio_nc_drvs[] = {
	{
		.phy_id = 0x7b744411,
		.phy_id_mask = 0x7fffffff,
		.name = "MAE0621A-Q2C Gigabit Ethernet",
		.features = PHY_GBIT_FEATURES,
		.probe = maxio_mae0621a_probe,
		.config_init	= maxio_mae0621a_config_init,
		.config_aneg = &genphy_config_aneg,
		.read_status = maxio_mae0621a_status,
		.suspend = maxio_mae0621a_suspend,
		.resume = maxio_mae0621a_resume,
		.remove = maxio_mae0621a_remove,
	},
	{
		.phy_id = 0x7b744412,
		.phy_id_mask = 0x7fffffff,
		.name = "MAE0621A/B-Q3C(I) Gigabit Ethernet",
		.features = PHY_GBIT_FEATURES ,
		.probe = maxio_mae0621aq3ci_probe,
		.config_aneg = &genphy_config_aneg,
		.config_init = maxio_mae0621aq3ci_config_init,
		.read_status = &maxio_mae0621a_status,
		.suspend = maxio_mae0621aq3ci_suspend,
		.resume = maxio_mae0621aq3ci_resume,
		.remove = maxio_mae0621a_remove,
	},
};

module_phy_driver(maxio_nc_drvs);
static struct mdio_device_id __maybe_unused maxio_nc_tbl[] = {
	{ 0x7b744411, 0x7fffffff },
	{ 0x7b744412, 0x7fffffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, maxio_nc_tbl);

MODULE_DESCRIPTION("Maxio PHY driver");
MODULE_AUTHOR("Zhao Yang");
MODULE_LICENSE("GPL");
