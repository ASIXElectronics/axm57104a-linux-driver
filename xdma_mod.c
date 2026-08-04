/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *     This is unpublished proprietary source code of ASIX Electronic
 *     Corporation
 *
 *     The copyright notice above does not evidence any actual or intended
 *     publication of such source code.
 *****************************************************************************/
/*
 * This file is part of the Xilinx DMA IP Core driver for Linux
 *
 * Copyright (c) 2016-present,  Xilinx, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#define pr_fmt(fmt)     KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/aer.h>
#include <linux/delay.h>
#include <linux/if_vlan.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/if_arp.h>

#include <linux/phy.h>//Wales

/* include early, to verify it depends only on the headers above */
#include "libxdma_api.h"
#include "libxdma.h"
#include "xdma_mod.h"
#include "version.h"
#include "ioctl.h"
#include "switch/ax_ptp.h"
#include "switch/ax_switch.h"
#include "DSA/dsa_loop.h"

#define DRV_MODULE_NAME		"AXM57104A"
#define DRV_MODULE_DESC		"ASIX PCIe NIC Driver"
#define DELAY_TIME_US	60
static char nic_version[] =
	DRV_MODULE_DESC " " DRV_MODULE_NAME " v" DRV_MODULE_VERSION "\n";

MODULE_AUTHOR("ASIX");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("Dual BSD/GPL");

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(VID_ASIX, 0x7104), },
	{0,}
};


MODULE_DEVICE_TABLE(pci, pci_ids);

extern struct mdio_driver ax_dsa_drv;
static void __ax_restart_autoneg
(struct ax_private *ax_local, int port, int eth_restart);

static int xdma_desc_flag;
static int *core_version;

struct net_device *netdev_primary;

//#define ENABLE_AX_REMOVE_MDIO_DEV_FUN
#define ax_write_register(v, mem) iowrite32(v, mem)

static u32 ax_read_register(void *iomem)//add static for kernel 6.8
{
	return ioread32(iomem);
}

/*
 * MDIO BUS
 */
struct mdio_transfer {
	struct mii_bus *bus;
	int 	phy_id;
	int 	regnum;
	u16 	val;
	u8 	mdio_bus;
};

int ax_mdio_device_bus_match(struct device *dev, struct device_driver *drv);
void ax_get_dsa_mac (struct ax_private *ax_local);
void ioctl_signature (struct ax_private *ax_local, IOCTL *ioctl);
void ioctl_read_reg (struct ax_private *ax_local, IOCTL *ioctl);
void ioctl_write_reg (struct ax_private *ax_local, IOCTL *ioctl);
void ioctl_get_bar_base_addr (struct ax_private *ax_local, IOCTL *ioctl);
void ioctl_clean_FIFO (struct ax_private *ax_local, IOCTL *ioctl);
int ax_net_change_mtu(struct net_device *dev, int new_mtu);

static int ax_mdio_read(struct mdio_transfer *data)
{
	struct ax_private *ax_dev = (struct ax_private *)data->bus->priv;
	struct xdma_dev *xdev = ax_dev->xdev;
	unsigned long flags;
	int *mdio_ctrl = (int *)(xdev->bar[2] + MDIO_CTRL);
	int *mdio_read = (int *)(xdev->bar[2] + MDIO_READ_DATA);
	int reg_add, phy_add;
	u32 value = 0, ctrl = 0;
	u8 mdio_bus = data->mdio_bus;
	u8 i;

	spin_lock_irqsave(&ax_dev->mdio_lock, flags);
	phy_add = data->phy_id;
	reg_add = data->regnum;

	ctrl = (phy_add << 27)|(reg_add << 22)|(1 << 21)|(mdio_bus & 0x7F);
	ax_write_register(ctrl, mdio_ctrl);
	
	ctrl |= (1 << 20);
	ax_write_register(ctrl, mdio_ctrl);

	for (i = 0; i < 10; i++) {
		udelay(DELAY_TIME_US);
		value = 0;
		ctrl = ax_read_register(mdio_ctrl);
		if ((ctrl & (1 << 20)) == 0) {
			value = ax_read_register(mdio_read);
            break;
		}
	}
	spin_unlock_irqrestore(&ax_dev->mdio_lock, flags);
	return (value & 0xFFFF);
}

static int ax_mdio_bus_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct mdio_transfer data;
	data.bus = bus;
/* Bescause the dsa default phy address allocates 0 to 3 and RTL8211 can't
 * use the phy address 0, the phy_id needs to add 1 to match the RTL8211 phy
 * address. This example, RTL8211 phy address set 1 to 4. */
	
	data.phy_id = phy_id+PHY_ID_SHIFT;	
	data.regnum = regnum;
	data.val = 0;
	data.mdio_bus = 0;
	return ax_mdio_read(&data);
	
}

static int ax_mdio_write(struct mdio_transfer *data)
{
	struct ax_private *ax_dev = (struct ax_private *)data->bus->priv;
	struct xdma_dev *xdev = ax_dev->xdev;
	unsigned long flags;
	int *mdio_ctrl = (int *)(xdev->bar[2] + MDIO_CTRL);
	int *mdio_write = (int *)(xdev->bar[2] + MDIO_WRITE_DATA);
	int reg_add, phy_add;
	u32 value = 0, ctrl = 0;
	u8 mdio_bus = data->mdio_bus;
	u8 i;

	spin_lock_irqsave(&ax_dev->mdio_lock, flags);
	phy_add = data->phy_id;
	reg_add = data->regnum;

	ctrl = (phy_add << 27) | (reg_add << 22) | (mdio_bus & 0x7F);
	value = data->val;
	ax_write_register(value, mdio_write);
	ax_write_register(ctrl, mdio_ctrl);
	
	ctrl |= (1 << 20);
	ax_write_register(ctrl, mdio_ctrl);

	for (i = 0; i < 10; i++) {
		udelay(DELAY_TIME_US);
		ctrl = 0;
		ctrl = ax_read_register(mdio_ctrl);
		if ((ctrl & (1 << 20)) == 0) {
			break;
		}
	}
	spin_unlock_irqrestore(&ax_dev->mdio_lock, flags);
	return 0;
}

static int ax_mdio_bus_write(struct mii_bus *bus, int phy_id, int regnum,
			     u16 val)
{
	struct mdio_transfer data;
	data.bus = bus;
/* Bescause the dsa default phy address allocates 0 to 3 and RTL8211 can't
 * use the phy address 0, the phy_id needs to add 1 to match the RTL8211 phy
 * address. This example, RTL8211 phy address set 1 to 4. */
	data.phy_id = phy_id+PHY_ID_SHIFT;
	data.regnum = regnum;
	data.val = val;
	data.mdio_bus = 0;
	return 0;
}

static int ax_init_mdio(struct ax_private *ax_local)
{
	struct mii_bus *mdio;
	int ret;
	
	mdio = mdiobus_alloc();
		
	if (!mdio) {
		netdev_err(ax_local->dev, "Could not allocate MDIO bus\n");
		return -ENOMEM;
	}

	ax_local->mdio = mdio;

	mdio->priv = (void *)ax_local;
	mdio->read = &(ax_mdio_bus_read);
	mdio->write = &(ax_mdio_bus_write);
	mdio->name = "AXM57104A MDIO Bus";
	mdio->phy_mask = 0xFFFFFFFF;
	
	snprintf(mdio->id, MII_BUS_ID_SIZE, "%s-mii", "asix");

	ret = mdiobus_register(mdio);
	if (ret) {
		pr_err("Could not register MDIO bus\n");
		goto mfree;
	}

	pr_info("registered mdio bus %s", mdio->id);
	return 0;
mfree:
	mdiobus_free(mdio);
	ax_local->mdio = NULL;
	return ret;
}

static void ax_remove_mdio(struct ax_private *ax_local)
{
	netdev_info(ax_local->dev,
		    "deregistered mdio bus %s\n", ax_local->mdio->id);
		
	mdiobus_unregister(ax_local->mdio);
	mdiobus_free(ax_local->mdio);
}

/*
 * MDIO device
 */

static struct dsa_loop_pdata ax_dsa_pdata = {
	.cd = {
		.port_names[0] = NULL,
		.port_names[1] = NULL,
		.port_names[2] = NULL,
		.port_names[3] = NULL,
		.port_names[DSA_LOOP_CPU_PORT] = "cpu",		
	},
	.name = "AXM57104A DSA driver",
	//.enabled_ports = DSA_LOOP_NUM_PORTS,	
	.enabled_ports = 5,	
	.netdev = NULL,
};

static const struct mdio_board_info bdinfo = {	
	.modalias = "ax_switch_mdio",
	.mdio_addr = 31,	
	.platform_data = &ax_dsa_pdata,
};

int ax_mdio_device_bus_match(struct device *dev, struct device_driver *drv)
{
	struct mdio_device *mdiodev = to_mdio_device(dev);
	struct mdio_driver *mdiodrv = to_mdio_driver(drv);
	
	if (mdiodrv->mdiodrv.flags & MDIO_DEVICE_IS_PHY) {
		return 0;
	}		
	return strcmp(mdiodev->modalias, drv->name) == 0;
	
}
#ifdef ENABLE_AX_REMOVE_MDIO_DEV_FUN
static int ax_remove_mdio_dev(struct ax_private *ax_local)
{
	struct mdio_device *mdiodev;
	int ret = 0;
	printk("[%s:%d] pass\n",__func__,__LINE__);
	if (ax_dsa_pdata.netdev) {
		mdiodev = ax_local->mdiodev;
		if (mdiodev == NULL) {
			pr_err("mdio_device_create failed");
			return -ENOMEM;
		}

		mdio_device_free(mdiodev);
	} else {
		ret = -ENODEV;
	}
	printk("[%s:%d] pass\n",__func__,__LINE__);
	return ret;
}
#endif
static int ax_create_mdio_dev(struct ax_private *ax_local)
{
	struct mdio_device *mdiodev;
	int ret = 0;

	ax_dsa_pdata.netdev = (const char *)ax_local->dev->name ;
	if (ax_dsa_pdata.netdev) {

		mdiodev = mdio_device_create(ax_local->mdio, 31);
		if (mdiodev == NULL) {
			pr_err("mdio_device_create failed");
			return -ENOMEM;
		}
		strncpy(mdiodev->modalias, bdinfo.modalias
			, sizeof(mdiodev->modalias));

		mdiodev->bus_match = ax_mdio_device_bus_match;
		mdiodev->dev.platform_data = (void *)&ax_dsa_pdata;

		ret = mdio_device_register(mdiodev);
		if (ret) {
			pr_err("mdio_device_register failed");
			mdio_device_free(mdiodev);
		} else {
			ax_local->mdiodev = mdiodev;
		}
	} else {
		ret = -ENODEV;
	}

	return ret;
}

/*
 * I2C operation
 */
struct i2c_transfer {
	struct ax_private *ax_dev;
	u8 		length;
	u8	 	i2c_bus;
	u8 		dev_addr;
	u8		buf_addr;
	u8 		buf[8];
};

static void _ax_i2c_read(struct i2c_transfer *data)
{
	struct ax_private *ax_dev = data->ax_dev;
	struct xdma_dev *xdev = ax_dev->xdev;
	int *reg32_c = (int *)(xdev->bar[2] + I2C_MASTER_CTRL);
	int *reg32_d_l = (int *)(xdev->bar[2] + I2C_READ_DATA_LO);
	int *reg32_d_h = (int *)(xdev->bar[2] + I2C_READ_DATA_HI);
	u32 value = 0;

	ax_write_register(data->buf_addr, reg32_c);
	value = (1 << 12) | (data->i2c_bus << 8) | (data->dev_addr << 1);
	ax_write_register(value, reg32_c);
	value |= (1 << 0);
	ax_write_register(value, reg32_c);
	mdelay(2);

	value = (1 << 20) | (data->length << 12) | (data->i2c_bus << 8) |
		(data->dev_addr << 1);
	ax_write_register(value, reg32_c);
	value |= (1 << 0);
	ax_write_register(value, reg32_c);
	mdelay(2);
	value = ax_read_register(reg32_d_l);
	memcpy(data->buf, &value, 4);
	value = ax_read_register(reg32_d_h);
	memcpy(data->buf + 4, &value, 4);
}

static void ax_i2c_read(struct ax_private *ax_dev, int length,
			unsigned char b_addr, unsigned char *buf)
{
	struct i2c_transfer i2c;
	int i = 0, j;

	do {
		i2c.ax_dev = ax_dev;
		i2c.length = length & 0x1F;
		i2c.i2c_bus = 0;
		i2c.dev_addr = 0x50;
		i2c.buf_addr = b_addr & 0xFF;
		_ax_i2c_read(&i2c);

		if ((i2c.buf[0] == VENDOR_OUI_0)
			&& (i2c.buf[1] == VENDOR_OUI_1)
			&& (i2c.buf[2] == VENDOR_OUI_2)) {

			for (j = 0; j < length; j++) {
				buf[j] = i2c.buf[j];
			}
			break;
		} else if (i >= CHECK_MAC_COUNT) {
			ASIX_MSG("[%s] EEPROM MAC address not valid",
					__FUNCTION__);
			
			eth_random_addr(buf);// Rand mac address by kernel 
			buf[0] = VENDOR_OUI_0;
			buf[1] = VENDOR_OUI_1;
			buf[2] = VENDOR_OUI_2;			
			break;
		}
	} while (++i);
}

/*
 * Phy setting
 */
static void ax_init_phy_led(struct ax_private *ax_local)
{
	struct ax_switch *pSwitch = &ax_local->axswitch;
	struct mdio_transfer data;
	u8 i;
	data.bus = ax_local->mdio;
	for (i = 1; i < pSwitch->number_of_ports; i++) {	
		data.phy_id = i;
		data.mdio_bus = 0;

		data.regnum = 0x1F;	// Set page
		data.val = 0x0D04;
		ax_mdio_write(&data);
		msleep(100);

		data.regnum = 0x10;	// Set LED
		data.val = 0x36B;	// LED0: Link LED1: Active
		ax_mdio_write(&data);
		msleep(100);

		data.regnum = 0x1F;	// Set page
		data.val = 0x0;
		ax_mdio_write(&data);
		msleep(100);
	}
}

static void ax_phy_greeneth(struct ax_private *ax_local, unsigned char enable)
{
	struct ax_switch *pSwitch = &ax_local->axswitch;
	struct mdio_transfer data;
	u16 reg = (enable) ? 0xd73f : 0x573f;
	u8 i;

	data.bus = ax_local->mdio;
	for (i = 1; i < pSwitch->number_of_ports; i++) {	
		data.phy_id = i;
		data.mdio_bus = 0;
		data.regnum = 0x1F;	// Set page
		data.val = 0xa43;
		ax_mdio_write(&data);

		data.regnum = 27;
		data.val = 0x8011;
		ax_mdio_write(&data);

		data.regnum = 28;
		data.val = reg;
		ax_mdio_write(&data);

		data.regnum = 0x1F;	// Set page
		data.val = 0x0;
		ax_mdio_write(&data);
	}
}

static void ax_phy_EEE(struct ax_private *ax_local, unsigned char enable)
{
	struct ax_switch *pSwitch = &ax_local->axswitch;
	struct mdio_transfer data;
	u8 i;

	data.bus = ax_local->mdio;
	for (i = 1; i < pSwitch->number_of_ports; i++) {	
		u16 reg;
		data.phy_id = i;
		data.mdio_bus = 0;
		data.regnum = 0x1F;	// Set page
		data.val = 0xa43;
		ax_mdio_write(&data);

		data.regnum = 0x19;
		data.val = 0;
		reg = ax_mdio_read(&data);

		data.regnum = 0x19;
		if (enable) {
			data.val = reg | (1 << 5);
		} else {
			data.val = reg & ~(1 << 5);
		}
		ax_mdio_write(&data);

		data.regnum = 0x1F;	// Set page
		data.val = 0x0;
		ax_mdio_write(&data);
	}
}

static void ax_phy_PTP(struct ax_private *ax_local, unsigned char enable)
{
	struct ax_switch *pSwitch = &ax_local->axswitch;
	struct mdio_transfer data;
	u8 i;

	data.bus = ax_local->mdio;
	for (i = 1; i < pSwitch->number_of_ports; i++) {	
		u16 reg;
		data.phy_id = i;
		data.mdio_bus = 0;
		data.regnum = 0x1F;	// Set page
		data.val = 0xe40;
		ax_mdio_write(&data);

		data.regnum = 0x10;
		data.val = 0;
		reg = ax_mdio_read(&data);

		data.regnum = 0x10;
		if (enable) {
			data.val = reg | 0x3F;
		} else {
			data.val = reg & ~(0x3F);
		}
		ax_mdio_write(&data);

		data.regnum = 0x1F;	// Set page
		data.val = 0x0;
		ax_mdio_write(&data);
	}
}

static void ax_phy_serdes(struct ax_private *ax_local, unsigned char enable)
{
	struct ax_switch *pSwitch = &ax_local->axswitch;
	struct mdio_transfer data;
	u8 i;
	u16 reg;

	data.bus = ax_local->mdio;
	for (i = 1; i < pSwitch->number_of_ports; i++) {	
		data.phy_id = i;
		data.mdio_bus = 0;
		data.regnum = 0x1F;	// Set page
		data.val = 0x0;
		ax_mdio_write(&data);

		data.regnum = 0x0;
		data.val = 0x9200;
		ax_mdio_write(&data);

		data.mdio_bus = 0;
		data.regnum = 0x1F;	// Set page
		data.val = 0xA43;
		ax_mdio_write(&data);

		data.regnum = 0x19;
		data.val = 0x0;
		reg = ax_mdio_read(&data);

		data.regnum = 0x19;
		data.val = reg & 0xFFFFFFFE;
		ax_mdio_write(&data);

		data.mdio_bus = 0;
		data.regnum = 0x1F;	// Set page
		data.val = 0x0;
		ax_mdio_write(&data);

		data.regnum = 0x0;
		data.val = 0x9200;
		ax_mdio_write(&data);
	}
}
/*
 * Phy Timer Polling
 */
static void __ax_phy_timer(struct timer_list *t)
{
	struct ax_private *ax_local = from_timer(ax_local, t, ax_phy_timer);
	struct mdio_transfer data;
	struct ax_ethphy_status phy_status;
	int status;
	int adv;
	int lpa;
	int lpagb = 0;
	int common_adv;
	int common_adv_gb = 0;
	u8 i;

	data.bus = ax_local->mdio;
	data.mdio_bus = 0x0;
	
	for (i = 1; i < (ax_local->axswitch.number_of_ports); i++) {	
		data.phy_id = i;
		/* link */
		data.regnum = MII_BMSR;
		data.val = 0;
		status = ax_mdio_read(&data);
		if (status < 0) {
			continue;
		}
		if (status & 0x4) {			
			if (ax_local->phy_status[i - 1].link == 1) {				
				continue;
			}
			ax_local->phy_status[i - 1].link = 1;
		} else {			
			if (ax_local->phy_status[i - 1].link == 1) {
				memset(&ax_local->phy_status[i - 1], 0,
					sizeof(ax_local->phy_status[i - 1]));				
			}
			continue;
		}

		memset(&phy_status, 0, sizeof(phy_status));
		phy_status.link = 1;
		
		/* 1G */
		data.regnum = MII_STAT1000;
		data.val = 0;
		lpagb = ax_mdio_read(&data);
		if (lpagb < 0) {
			continue;
		}
		data.regnum = MII_CTRL1000;
		data.val = 0;
		adv = ax_mdio_read(&data);

		if (adv < 0) {
			continue;
		}
		common_adv_gb = lpagb & (adv << 2);		
		/* 100M/10M */
		data.regnum = MII_LPA;
		data.val = 0;
		lpa = ax_mdio_read(&data);

		if (lpa < 0) {
			continue;
		}
		data.regnum = MII_ADVERTISE;
		data.val = 0;
		adv = ax_mdio_read(&data);

		if (adv < 0) {
			continue;
		}
		common_adv = lpa & adv;
		
		phy_status.speed = SPEED_10;
		phy_status.duplex = DUPLEX_HALF;
		phy_status.pause = 0;
		phy_status.asym_pause = 0;
		if (common_adv_gb & (LPA_1000FULL | LPA_1000HALF)) {
			phy_status.speed = SPEED_1000;

			if (common_adv_gb & LPA_1000FULL) {
				phy_status.duplex = DUPLEX_FULL;
			}

		}
		else if (common_adv & (LPA_100FULL | LPA_100HALF)) 
		{
			phy_status.speed = SPEED_100;

			if (common_adv & LPA_100FULL) {
				phy_status.duplex = DUPLEX_FULL;
			}
		}

		else 
		{
			if (common_adv & LPA_10FULL) {
				phy_status.duplex = DUPLEX_FULL;
			}
		}

		if (phy_status.duplex == DUPLEX_FULL) {
			phy_status.pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
			phy_status.asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
		}

		if (memcmp(&phy_status, &ax_local->phy_status[i - 1],
						sizeof(phy_status)) != 0) {
			printk("__ax_restart_autoneg %d", i);
			memcpy(&ax_local->phy_status[i - 1],
			       &phy_status,
			       sizeof(phy_status));
			ax_local->phy_status[i - 1].link = 1;
			__ax_restart_autoneg(ax_local, i, 0);
		}
	}
	
	mod_timer(&ax_local->ax_phy_timer, jiffies + msecs_to_jiffies(PHY_POLLING_TIMER));
}

static void ax_phy_timer_init(struct ax_private *ax_local)
{
	timer_setup(&ax_local->ax_phy_timer, __ax_phy_timer, 0);

	mod_timer(&ax_local->ax_phy_timer,  jiffies + msecs_to_jiffies(PHY_POLLING_TIMER));
}

/*
 * Ethtool operations
 */

static void ax_get_drvinfo(struct net_device *dev,
			   struct ethtool_drvinfo *info)
{
	struct ax_private *ax_local = (struct ax_private *)netdev_priv(dev);	
	strcpy (info->driver, DRV_MODULE_NAME);
	strcpy (info->version, DRV_MODULE_VERSION);
	strcpy (info->bus_info, pci_name(ax_local->pdev));	
}

static int ax_get_regs_len(struct net_device *dev)
{
	return -EOPNOTSUPP;
}

static int ax_get_link_ksettings(struct net_device *netdev,
				 struct ethtool_link_ksettings *cmd)
{
	cmd->base.speed = SPEED_1000;
	cmd->base.autoneg = AUTONEG_DISABLE;
	cmd->base.duplex = DUPLEX_FULL;
	return 0;
}

static int ax_set_link_ksettings(struct net_device *netdev,
				 const struct ethtool_link_ksettings *cmd)
{
	return -EOPNOTSUPP;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
static int ax_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	ecmd->supported = SUPPORTED_TP | SUPPORTED_1000baseT_Full;
	ecmd->autoneg = AUTONEG_DISABLE;
	ecmd->duplex = DUPLEX_FULL;
	return 0;
}

static int ax_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	return -EOPNOTSUPP;
}
#endif

static void
__ax_restart_autoneg (struct ax_private *ax_local, int port, int eth_restart)
{
	struct mdio_transfer data;
	u16 reg;
	data.bus = ax_local->mdio;
	if (eth_restart) {
		/* RTL */
		data.phy_id = port;
		data.mdio_bus = 0;	
		data.regnum = MII_BMCR;
		data.val = 0x0;
		reg = ax_mdio_read(&data);
		data.val = reg | BMCR_ANRESTART;
		ax_mdio_write(&data);
	}
}

static void ax_restart_autoneg (struct ax_private *ax_local)
{
	struct ax_switch *pSwitch = &ax_local->axswitch;
	u8 i;

	for (i = 1; i < pSwitch->number_of_ports; i++) {	
		__ax_restart_autoneg(ax_local, i, 1);
	}
}

static int ax_nway_reset(struct net_device *dev)
{
	return 0;
}

static int ax_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info)
{
	struct ax_private *ax_local = (struct ax_private *)netdev_priv(dev);

	info->phc_index = 0;
	info->so_timestamping =
			SOF_TIMESTAMPING_TX_SOFTWARE |
			SOF_TIMESTAMPING_RX_SOFTWARE |
			SOF_TIMESTAMPING_SOFTWARE |
			SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_RAW_HARDWARE;

	if (ax_local->axswitch.ax_ptp_clocks->ptp_clock) {
		info->phc_index =
		ptp_clock_index(ax_local->axswitch.ax_ptp_clocks->ptp_clock);
	} else {
		info->phc_index = -1;
	}

	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_ALL);

	return 0;
}
#if 0
static u32 ax_get_link(struct net_device *netdev)
{
	return ethtool_op_get_link(netdev) ? 1 : 0;
}
#endif
const struct ethtool_ops ax_ethtool_ops = {
	.get_drvinfo		= ax_get_drvinfo,
	.get_regs_len		= ax_get_regs_len,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
	.get_settings		= ax_get_settings,
	.set_settings		= ax_set_settings,
#endif
	.nway_reset		= ax_nway_reset,
	.get_ts_info            = ax_get_ts_info,
	.get_link_ksettings 	= ax_get_link_ksettings,
	.set_link_ksettings 	= ax_set_link_ksettings,
	//.get_link		= ax_get_link,	
	.get_link		= ethtool_op_get_link,
};

/*
 * Network Interface
 */

void ax_get_dsa_mac (struct ax_private *ax_local)
{
	struct ax_dsa_data *ax_dsa = &ax_local->ax_dsa;
	struct xdma_dev *xdev = ax_local->xdev;
	u32 i;

	for (i = 0; i < 8; i++) {
		int *offset;
		u32 value;
		u8 temp;
		offset = (int *)(xdev->bar[2] + DSA_OFFSET + (i * 4));
		value = ioread32(offset);
		temp = (i / 2);
		if (i % 2 == 0) { // HI
			ax_dsa->dsa_mac[temp][1] = value & 0xFF;
			ax_dsa->dsa_mac[temp][0] = (value >> 8) & 0xFF;
		} else {
			ax_dsa->dsa_mac[temp][5] = value & 0xFF;
			ax_dsa->dsa_mac[temp][4] = (value >> 8) & 0xFF;
			ax_dsa->dsa_mac[temp][3] = (value >> 16) & 0xFF;
			ax_dsa->dsa_mac[temp][2] = (value >> 24) & 0xFF;
		}
	}
}
static int
ax_set_domain_number_table(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct domain_number_table_data domain_number_table;
	struct ax_private *ax_local = (struct ax_private *)netdev_priv(dev);

	memset(&domain_number_table, 0, sizeof(struct domain_number_table_data));
	if (copy_from_user(&domain_number_table,
			(struct domain_number_table_data *)ifr->ifr_data,
				sizeof(struct domain_number_table_data))) {
		return -1;
	}

	ax_local->axswitch.domain_number_table = domain_number_table;	
	return 0;
}

static int
ax_get_sync_tx_timestamp(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ioctl_timestamp_data  timestamp_data;
	CAPTURE_DATA ts_d;
	struct ax_private *ax_local = (struct ax_private *)netdev_priv(dev);
	int i, domain_index = -1;
	
	memset(&timestamp_data, 0, sizeof(struct ioctl_timestamp_data));
	if (copy_from_user(&timestamp_data,
				(struct ioctl_timestamp_data *)ifr->ifr_data,
				sizeof(struct ioctl_timestamp_data))) {
		return -1;
	}

	ts_d.port_id = timestamp_data.port_id;
	ts_d.msg_type = timestamp_data.msg_type;
	ts_d.sequence_id = timestamp_data.seq_id;
	ts_d.domain_number = timestamp_data.domain_number;
	if (ax_tsq_find_item_1(&ax_local->axswitch.tx_queue, &ts_d) < 0) {
		return -1;
	}
	for (i = 0; i < ax_local->axswitch.number_of_domains; i++) {
			if (ts_d.domain_number == 
		ax_local->axswitch.domain_number_table.domain_number_map[i]) {
				domain_index = i + 1;
				break;
			}
	}
	if (domain_index == -1) {
		printk(KERN_ERR "ax_get_sync_tx_timestamp: Domain index for Domain number %d not found\n", ts_d.domain_number);
		return -1;
	}
	timestamp_data.timestamp = (ts_d.timestamp_h[domain_index] * NS_PER_SEC)
				+ ts_d.timestamp_l[domain_index];

	if (copy_to_user((struct ioctl_timestamp_data *)ifr->ifr_data,
				&timestamp_data,
				sizeof(struct ioctl_timestamp_data))) {
		return -1;
	}
	
	return 0;
}

static int
ax_get_sync_rx_timestamp(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ioctl_timestamp_data  timestamp_data;
	CAPTURE_DATA ts_d;
	struct ax_private *ax_local = (struct ax_private *)netdev_priv(dev);
	int i, domain_index = -1;
	
	memset(&timestamp_data, 0, sizeof(struct ioctl_timestamp_data));
	if (copy_from_user(&timestamp_data,
				(struct ioctl_timestamp_data *)ifr->ifr_data,
				sizeof(struct ioctl_timestamp_data))) {
		printk("Copy from user failed ------\n");
		return -1;
	}

	ts_d.port_id = timestamp_data.port_id;
	ts_d.msg_type = timestamp_data.msg_type;
	ts_d.sequence_id = timestamp_data.seq_id;
	ts_d.domain_number = timestamp_data.domain_number;

	if (ax_tsq_find_item_1(&ax_local->axswitch.rx_queue, &ts_d) < 0) {
		return -1;
	}

	for (i = 0; i < (ax_local->axswitch.number_of_domains); i++) {
			if (ts_d.domain_number == 
		ax_local->axswitch.domain_number_table.domain_number_map[i]) {
				domain_index = i + 1;
				break;
			}
	}
	if (domain_index == -1) {
		printk(KERN_ERR "AX_GET_SYNC_RX_TIMESTAMP: Domain number for Domain index %d not found\n", ts_d.domain_number);
		return -1;
	}

	timestamp_data.timestamp = (ts_d.timestamp_h[domain_index] * NS_PER_SEC)
				+ ts_d.timestamp_l[domain_index];

	if (copy_to_user((struct ioctl_timestamp_data *)ifr->ifr_data,
				&timestamp_data,
				sizeof(struct ioctl_timestamp_data))) {
		return -1;
	}
	
	return 0;
}

static int
ax_gptp_set_ptp_mode(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ax_private *ax_local = (struct ax_private*)netdev_priv(dev);
	if (copy_from_user(&ax_local->axswitch.ptp_mode, (int *)ifr->ifr_data, sizeof(int))) {
		printk(KERN_ERR "AX_GPTP_IOCTL_SET_PTP_MODE: Error copying from user in AX_GPTP_IOCTL_SET_PTP_MODE\n");
		return -1;
	}

	return 0;
}
static int ax_get_hwtstamp_config(struct net_device *dev, struct ifreq *ifr)
{
	return -EOPNOTSUPP; //FPGA v2301
}

static int ax_set_hwtstamp_config(struct net_device *dev, struct ifreq *ifr)
{
	struct hwtstamp_config config;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config))) {
		return -EFAULT;
	}

	/* reserved for future extensions */
	if (config.flags) {
		return -EINVAL;
	}

	if ((config.tx_type != HWTSTAMP_TX_OFF) &&
	    (config.tx_type != HWTSTAMP_TX_ON)) {
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	config.tx_type = HWTSTAMP_TX_ON;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;

}

void ioctl_signature (struct ax_private *ax_local, IOCTL *ioctl)
{
	memcpy (ioctl->Gid, ASIX_GID, 8);
	ioctl->Status = AX_STATUS_SUCCESS;
}

void ioctl_read_reg (struct ax_private *ax_local, IOCTL *ioctl)
{
	PAX_MEM_REG Cmd = &ioctl->CmdData.Mem_register;
	struct xdma_dev *xdev = ax_local->xdev;
	int *offset;
	
	ioctl->Status = AX_STATUS_SUCCESS;

	switch (Cmd->bar) {
	case BAR0:
	case BAR1:
	case BAR2:
		offset = (int *)(xdev->bar[Cmd->bar] + Cmd->offset);
		break;
	default:
		ioctl->Status = AX_STATUS_FAILURE;
		return;
	}
	Cmd->value = ioread32(offset);
}

void ioctl_write_reg (struct ax_private *ax_local, IOCTL *ioctl)
{
	PAX_MEM_REG Cmd = &ioctl->CmdData.Mem_register;
	struct xdma_dev *xdev = ax_local->xdev;
	int *offset;

	ioctl->Status = AX_STATUS_SUCCESS;

	switch (Cmd->bar) {
	case BAR0:
	case BAR1:
	case BAR2:
		offset = (int *)(xdev->bar[Cmd->bar] + Cmd->offset);
		break;
	default:
		ioctl->Status = AX_STATUS_FAILURE;
		return;
	}
	iowrite32((u32)Cmd->value, offset);

	if ((Cmd->bar == BAR2) && ((Cmd->offset & 0x0F00) == DSA_OFFSET)) {
		ax_get_dsa_mac(ax_local);
	}
}

void ioctl_get_bar_base_addr (struct ax_private *ax_local, IOCTL *ioctl)
{
	PAX_BAR_BASE Cmd = &ioctl->CmdData.Bar_base_addr;
	struct xdma_dev *xdev = ax_local->xdev;
	
	ioctl->Status = AX_STATUS_SUCCESS;
	Cmd->bar_base_addr = 0xFFFFFFFF;

	switch (Cmd->bar) {
	case BAR0:
	case BAR1:
	case BAR2:
		Cmd->bar_base_addr = (uint64_t) xdev->phy_bar[Cmd->bar];
		break;
	default:
		ioctl->Status = AX_STATUS_FAILURE;
		break;
	}
}

void ioctl_clean_FIFO (struct ax_private *ax_local, IOCTL *ioctl)
{
#if 1
	
	int *p_timestamping_ctrl = (int *)(ax_local->xdev->bar[2] + PTP_TIMESTAMP_CTRL); 
	int *rx_FIFO = (int *)(ax_local->xdev->bar[2] + PTP_RX_MESSAGE_INFO);
	while ((*p_timestamping_ctrl & 0x1) != 0) {
		ax_read_register(rx_FIFO);
	}
#else
	int i = 0;
	u32 port = 0;

	int *p_ptp_port_id = (int *)(ax_local->xdev->bar[2] + PTP_RX_PORT);

	int *rx_FIFO = (int *)(ax_local->xdev->bar[2] + PTP_RX_CLOCK_ID_HI);

	for (port = 0; port < 4; port++) {
		ax_write_register(port, p_ptp_port_id);
		for (i = 0; i < FIFO_COUNT ; i++) {
			ax_read_register(rx_FIFO);
		}
	}

#endif
	ioctl->Status = AX_STATUS_SUCCESS;
}

typedef void (*IOCTL_TABLE)(struct ax_private *ax_local, IOCTL *ioctl);

IOCTL_TABLE ioctl_fun_table[] = {

	/* AX_IOCTL_SIGNATURE		0 */
	ioctl_signature,
	/* AX_IOCTL_READ_REG		1 */
	ioctl_read_reg,
	/* AX_IOCTL_WRITE_REG		2 */
	ioctl_write_reg,
	/* AX_IOCTL_GET_BAR_BASE	3 */
	ioctl_get_bar_base_addr,
	/* AX_IOCTL_CLEAN_FIFO      4 */
    ioctl_clean_FIFO,
};

static int ax_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	switch (cmd) {
		case SIOCSHWTSTAMP:
			return ax_set_hwtstamp_config(dev, rq);
		break;
		case SIOCGHWTSTAMP:			
			return ax_get_hwtstamp_config(dev, rq);
		break;
		default:
			pr_err(" [%s:%d] No support command:0x%x\n",__func__,__LINE__,cmd);
			return -1;
		break;
	}
}

static int ax_net_privioctl(struct net_device *dev, struct ifreq *rq, void __user *data,int cmd)
{
	struct ax_private *ax_local = netdev_priv(dev);
	int rc = 0;
	void __user *uptr = (void __user *)rq->ifr_data;
	IOCTL ioctl;

	if (!netif_running(dev)) {	
		return -EINVAL;
	}

	switch (cmd) {
		case IOCTL_SEND_CMD:		
			if (copy_from_user (&ioctl, uptr, sizeof (ioctl)))
				return -EFAULT;
			
			if (ioctl.Opcode > sizeof (ioctl_fun_table) / 
				sizeof (ioctl_fun_table[0])) {
				ioctl.Status = AX_STATUS_NOT_SUPPORT;
				printk("The ioctl %ld doesn't support\n", 
					ioctl.Opcode);
			} else if (ioctl_fun_table[ioctl.Opcode] == NULL) {
				ioctl.Status = AX_STATUS_NOT_SUPPORT;
				printk("The ioctl %ld doesn't impletement\n", 
					ioctl.Opcode);
			} else {
				(*ioctl_fun_table[ioctl.Opcode])(ax_local, &ioctl);
			}
			
			if (copy_to_user (uptr, &ioctl, sizeof (ioctl))) {
				return -EFAULT;
			}
		break;
		case IOCTL_SET_DOMAIN_NUMBER_TABLE:			
			return ax_set_domain_number_table(dev, rq, cmd);
		break;
		case IOCTL_GET_SYNC_TX_TIMESTAMP:			
			return ax_get_sync_tx_timestamp(dev, rq, cmd);
		break;
		case IOCTL_GET_SYNC_RX_TIMESTAMP:			
			return ax_get_sync_rx_timestamp(dev, rq, cmd);
			break;
		case IOCTL_GPTP_SET_PTP_MODE:			
			return ax_gptp_set_ptp_mode(dev, rq, cmd);
			break;
		default:
			pr_err(" [%s:%d] No support command:0x%x\n",__func__,__LINE__,cmd);
			return -1;
		break;
	}

	return rc;

}

static void
ax_net_clean_ring(struct net_device *dev)
{
#ifndef RX_SKB_COPY
	struct ax_private	*ax_local = netdev_priv(dev);
	u16			i;
	for (i = 0; i < RX_DESC_NUM; i++) {
		if (ax_local->rx_buffers[i].skb) {
			pci_unmap_single(ax_local->pdev,
					 ax_local->rx_buffers[i].mapping,
					 PKT_BUF_LEN, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(ax_local->rx_buffers[i].skb);
		}
	}
#endif /* End of RX_SKB_COPY */
}

static int ax_net_init_ring(struct net_device *dev)
{
	struct ax_private	*ax_local = netdev_priv(dev);
	struct xdma_dev		*xdev = ax_local->xdev;
	struct xdma_engine 	*engine;
	struct xdma_transfer 	*transfer;
	struct xdma_desc 	*desc_virt;
	dma_addr_t 		result_bus;
#ifdef RX_SKB_COPY
	dma_addr_t 		rx_buff_bus;
#endif /* End of RX_SKB_COPY */
#ifdef TX_SKB_COPY
	dma_addr_t 		tx_buff_bus;
#endif /* End of TX_SKB_COPY */
	int			i, j;

	engine = &xdev->engine_c2h[0];
	transfer = engine->transfer;
	desc_virt = transfer->desc_virt;
	result_bus = engine->cyclic_result_bus;
	rx_buff_bus = engine->rx_buff_bus;
	for (i = 0; i < RX_DESC_NUM; i++) {
#ifdef RX_SKB_COPY
		desc_virt[i].dst_addr_lo = cpu_to_le32(PCI_DMA_L(rx_buff_bus));
		desc_virt[i].dst_addr_hi = cpu_to_le32(PCI_DMA_H(rx_buff_bus));
		rx_buff_bus += sizeof(struct packet_buff);
#else
		ax_local->rx_buffers[i].skb = dev_alloc_skb(PKT_BUF_LEN + 2);
		if (ax_local->rx_buffers[i].skb == NULL) {
			break;
		}
		ax_local->rx_buffers[i].skb->dev = dev;
		skb_reserve(ax_local->rx_buffers[i].skb, 2);
		ax_local->rx_buffers[i].skb->dev = ax_local->dev;
		ax_local->rx_buffers[i].mapping =
					pci_map_single(ax_local->pdev,
					ax_local->rx_buffers[i].skb->data,
					PKT_BUF_LEN,
					PCI_DMA_FROMDEVICE);

		desc_virt[i].dst_addr_lo =
		cpu_to_le32(PCI_DMA_L(ax_local->rx_buffers[i].mapping));

		desc_virt[i].dst_addr_hi =
		cpu_to_le32(PCI_DMA_H(ax_local->rx_buffers[i].mapping));
#endif /* End of RX_SKB_COPY */
		desc_virt[i].src_addr_lo = cpu_to_le32(PCI_DMA_L(result_bus));
		desc_virt[i].src_addr_hi = cpu_to_le32(PCI_DMA_H(result_bus));
		result_bus += sizeof(struct xdma_result);
		desc_virt[i].bytes = cpu_to_le32(PKT_BUF_LEN);
	}

	if (i != RX_DESC_NUM) {
		ax_net_clean_ring(dev);
		return -ENOMEM;
	}

#ifdef TX_SKB_COPY
	for (j = 0; j < 2; j++) {
		engine = &xdev->engine_h2c[j];
		transfer = engine->transfer;
		desc_virt = transfer->desc_virt;
		tx_buff_bus = engine->tx_buff_bus;
		for (i = 0; i < TX_DESC_NUM; i++) {
			desc_virt[i].src_addr_lo =
					cpu_to_le32(PCI_DMA_L(tx_buff_bus));
			desc_virt[i].src_addr_hi =
					cpu_to_le32(PCI_DMA_H(tx_buff_bus));
			tx_buff_bus += sizeof(struct packet_buff);
			result_bus += sizeof(struct xdma_result);
			desc_virt[i].bytes = cpu_to_le32(PKT_BUF_LEN);
		}
	}
#endif /* End of TX_SKB_COPY */
	return 0;
}

static void ax_init_rgmii(struct ax_private *ax_local)
{
	struct ax_switch *pSwitch = &ax_local->axswitch;
	struct mdio_transfer data;
	u8 i,j;
	u8 match_num = 0;
    u8 retry_num = 0;
	u16 bmsr;
	u8 link_status = 0;
	u8 autoneg_comple = 0;
	
	data.bus = ax_local->mdio;
	/*check link status is complete 
	  if not, do
	    a. re-start autoneg
		b. autoneg enable, 
		c. set spd=1G
	*/
	for (i = 1; i < (pSwitch->number_of_ports); i++) {	
		data.phy_id = i;
		data.mdio_bus = 0;
		data.regnum = MII_BMSR;
        bmsr = ax_mdio_read(&data);		
        link_status = bmsr & BMSR_LSTATUS;
		autoneg_comple = bmsr & BMSR_ANEGCOMPLETE;

		bmsr &= (BMSR_LSTATUS | BMSR_ANEGCOMPLETE);
		if (bmsr ==(BMSR_LSTATUS | BMSR_ANEGCOMPLETE)){
			continue;
		}
		else{
			for(j=0;j<3;j++){
				data.phy_id = i;
				data.mdio_bus = 0;
				data.regnum = MII_BMCR;
				data.val = (BMCR_ANRESTART | BMCR_ANENABLE);
				ax_mdio_write(&data);
				msleep(100);

				while (1) {
					u16 bmsr;
					data.regnum = MII_BMSR;
					bmsr = ax_mdio_read(&data);
					bmsr &= (BMSR_LSTATUS | BMSR_ANEGCOMPLETE);				
					if (bmsr ==(BMSR_LSTATUS | BMSR_ANEGCOMPLETE)) {
						match_num++;
					}
					retry_num++;
					if (match_num >= 3 || retry_num >= 5) {
						break;
					}
					msleep(1);
            			}
				if (match_num == 3) {
					break;
				}
			}
			if (retry_num >= 5 && match_num < 3) {
            			pr_info("RGMII Port %d: Autoneg. failed\n", i-1);
        		}

		}
	}

}

static void ax_set_mac_reg(struct ax_private *ax_local, unsigned char *addr)
{
	struct xdma_dev	*xdev = ax_local->xdev;
	int *reg_addr = (int *)(xdev->bar[0] + NODE_ID_1);
	uint32_t reg32 = 0;

	//Node 1
	reg32 = ((((uint32_t)addr[0]) & 0xFF) << 8) |
		(((uint32_t)addr[1]) & 0xFF);
	ax_write_register(reg32, reg_addr);
	//Node 0
	reg_addr = (int *)(xdev->bar[0] + NODE_ID_0);
	reg32 =  ((((uint32_t)addr[2]) & 0xFF) << 24) |
		((((uint32_t)addr[3]) & 0xFF) << 16) |
		((((uint32_t)addr[4]) & 0xFF) << 8) |
		(((uint32_t)addr[5]) & 0xFF);
	ax_write_register(reg32, reg_addr);
}

static int ax_net_open(struct net_device *dev)
{
	struct ax_private *ax_local = netdev_priv(dev);
	struct xdma_dev *xdev = ax_local->xdev;
	int err, i;
	
	xdma_desc_setup(xdev, &xdev->engine_h2c[0], xdma_desc_flag);
	xdma_desc_setup(xdev, &xdev->engine_h2c[1], xdma_desc_flag);
	xdma_desc_setup(xdev, &xdev->engine_c2h[0], xdma_desc_flag);

	if (xdma_desc_flag == 0) {
		xdma_desc_flag = 1;
	}

	err = ax_net_init_ring(dev);

	if (err) {
		pr_err("Cannot allocate dma buffers, aborting.\n");
		return err;
	}

	ax_get_dsa_mac(ax_local);

	for (i = 0; i < XDMA_CHANNEL_NUM_MAX; i++) {
		ax_local->tx_list[i].cur = 0;
		ax_local->tx_list[i].idle = 1;
		ax_local->tx_list[i].cur_pkt_count = 0;
	}
	ax_local->cur_rx = 0;
	ax_local->dirty_rx = 0;
	ax_local->rx_flags = 0;
	ax_local->cur_rx_pkt_count = 0;

	ax_write_register(AX_PLINK, (xdev->bar[0] + GLOBAL_MAC_CONFIG));

	engine_start(&xdev->engine_c2h[0]);

	skb_queue_head_init(&ax_local->tx_timestamp);
	set_bit(__LINK_STATE_START,&dev->state);

#ifdef AX_MQ
	netif_set_real_num_tx_queues(ax_local->dev, ax_local->num_tx_queues);
	netif_tx_start_all_queues(ax_local->dev);
#else
	netif_start_queue(dev);
#endif
#ifdef CONFIG_NAPI
	napi_enable(&ax_local->napi);
#endif /* CONFIG_NAPI */

	return 0;
}

static int ax_net_close(struct net_device *dev)
{
	struct ax_private *ax_local = netdev_priv(dev);
	struct xdma_dev *xdev = ax_local->xdev;

#ifdef CONFIG_NAPI
	napi_disable (&ax_local->napi);
#endif /* CONFIG_NAPI */

#ifdef AX_MQ
	netif_carrier_off(dev);
	netif_tx_stop_all_queues(dev);
#else
	netif_stop_queue(dev);
#endif
	xdma_engine_stop(&xdev->engine_c2h[0]);
	xdma_engine_stop(&xdev->engine_h2c[0]);
	xdma_engine_stop(&xdev->engine_h2c[1]);

	ax_net_clean_ring(dev);
	clear_bit(__LINK_STATE_START,&dev->state);
	return 0;
}

static int
ax_net_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ax_private	*ax_local = netdev_priv(dev);
	struct xdma_dev		*xdev = ax_local->xdev;
	struct xdma_engine 	*engine;
	struct xdma_transfer 	*transfer;
	struct xdma_desc 	*desc_virt;
	struct packet_buff 	*tx_buff;
	struct ax_tx_list	*list;
	u32			trans_length;
	u32			entry;
	unsigned long		flags;
	struct nic_tx_header	*header;
	u32			index;
#ifdef AX_MQ
	//u32			queue = skb_get_queue_mapping(skb);
	u32			queue_index;
#endif
#ifdef ASIX_GPTP_DEBUG
	struct timespec64 tmv_64;
	u64 nanoSec;
#endif
	
	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)  {
		skb_queue_tail(&ax_local->tx_timestamp, skb);
		index = 1;
	} else {
		index = 0;
	}

	list = &ax_local->tx_list[index];
	engine = &xdev->engine_h2c[index];
	transfer = engine->transfer;
	desc_virt = transfer->desc_virt;
	tx_buff = engine->tx_buff;

	spin_lock_irqsave(&ax_local->lock, flags);

	if (skb->len < 60 && skb_tailroom(skb)) {
		skb_put(skb, 60 - skb->len);
	}
	
	entry = list->cur;

	header = (struct nic_tx_header *)tx_buff[entry].data;

	header->length = (u16)skb->len;

	memcpy((tx_buff[entry].data + sizeof(struct nic_tx_header)),
	       skb->data, skb->len);
#if 0
  	printk("Tx:");
       	print_hex_dump(KERN_DEBUG,"",DUMP_PREFIX_NONE,16,1,skb->data,skb->len,0);
       	printk("\r\n");

#endif
#ifdef ASIX_GPTP_DEBUG
	if((skb->data[12] == 0xdc)&&skb->data[13] == 0xdc)
	{
		if((skb->data[22]&0xf)==0x2)
		{
			ktime_get_real_ts64(&tmv_64);
                        nanoSec = ((u64)tmv_64.tv_sec) * 1000000000 + (u64)tmv_64.tv_nsec;
			printk("%s,T1, msg_type: %d, Kernel Time: %lld",__func__,skb->data[22]&0xf,nanoSec);
		}
	}
#endif
	trans_length = ALIGN_8_BYTES(skb->len + sizeof(struct nic_tx_header));
	desc_virt[entry].bytes = cpu_to_le32(trans_length);

	skb_tx_timestamp(skb);
	
	netif_trans_update(dev);
	list->cur_pkt_count++;


	if (list->idle == 1) {
		desc_virt[entry].control =
			cpu_to_le32(DESC_MAGIC |
			XDMA_DESC_EOP |
			XDMA_DESC_STOPPED);
		engine_start(engine);

		list->idle = 0;
		transfer->current_list = (transfer->current_list + 1)
					% DESC_LIST_NUM;
		list->cur = (transfer->current_list *
				(TX_DESC_NUM / DESC_LIST_NUM));
		list->cur_pkt_count = 0;
	} else {
		desc_virt[entry].control = cpu_to_le32(DESC_MAGIC);
		list->cur = (entry + 1) % TX_DESC_NUM;

		if (list->cur == 0 ||
		    list->cur == (TX_DESC_NUM / DESC_LIST_NUM)) {
#ifdef AX_MQ
			for (queue_index = 0;
				queue_index < MAX_TX_QUEUE;
				queue_index++) {
					netif_stop_subqueue(dev, queue_index);
			}
#else
			netif_stop_queue(dev);
#endif
			ax_local->stop_queue_channel = index;
		}
	}

	spin_unlock_irqrestore(&ax_local->lock, flags);

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		dev_kfree_skb_irq(skb);
	}

	return 0;
} /* End of ax_pci_start_xmit() */

static void xpdev_free(struct xdma_pci_dev *xpdev)
{
	struct xdma_dev *xdev = xpdev->xdev;

	xpdev->xdev = NULL;
	xdma_device_close(xpdev->pdev, xdev);

	kfree(xpdev);
}
//
static struct xdma_pci_dev *pcidev_alloc(struct pci_dev *pdev)
{
	struct xdma_pci_dev *pcidev = kmalloc(sizeof(*pcidev), GFP_KERNEL);

	if (!pcidev) {
		return NULL;
	}

	pcidev->magic = MAGIC_DEVICE;
	pcidev->pdev = pdev;
	pcidev->user_max = MAX_USER_IRQ;
	pcidev->h2c_channel_max = XDMA_CHANNEL_NUM_MAX;
	pcidev->c2h_channel_max = 1;//XDMA_CHANNEL_NUM_MAX;

	return pcidev;
}

int ax_net_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu <= 0 || new_mtu > 1500) {
		return -EINVAL;
	}
	return 0;
}

static void ax_net_set_multicast(struct net_device *dev)
{
	struct ax_private *ax_local = netdev_priv(dev);
	struct xdma_dev	*xdev = ax_local->xdev;
	uint32_t rx_ctl = AX_RX_CTL_AB;
	u8 multi_filter[AX_MCAST_FILTER_SIZE];
	int *reg_addr;
	int mc_count;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
	mc_count = net->mc_count;
#else
	mc_count = netdev_mc_count(dev);
#endif

	if (dev->flags & IFF_PROMISC) {
		rx_ctl |= AX_RX_CTL_PRO;
	} else if (dev->flags & IFF_ALLMULTI || mc_count > AX_MAX_MCAST) {
		rx_ctl |= AX_RX_CTL_AMALL;
	} else if (mc_count == 0) {
		/* just broadcast and directed */
	} else {
		/* We use the 20 byte dev->data
		 * for our 8 byte filter buffer
		 * to avoid allocating memory that
		 * is tricky to free later */
		u32 crc_bits;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
		struct dev_mc_list *mc_list = net->mc_list;
		int i;

		memset(multi_filter, 0, AX_MCAST_FILTER_SIZE);

		/* Build the multicast hash filter. */
		for (i = 0; i < net->mc_count; i++) {
			crc_bits =
			    ether_crc(ETH_ALEN, mc_list->dmi_addr) >> 26;
			multi_filter[crc_bits >> 3] |=  1 << (crc_bits & 7);
			mc_list = mc_list->next;
		}
#else
		struct netdev_hw_addr *ha = NULL;
		memset(multi_filter, 0, AX_MCAST_FILTER_SIZE);
		netdev_for_each_mc_addr(ha, dev) {
			crc_bits = ether_crc(ETH_ALEN, ha->addr) >> 26;
			multi_filter[crc_bits >> 3] |=
				1 << (crc_bits & 7);
		}
#endif
		reg_addr = (int *)(xdev->bar[0] + MULTI_FILTER_ARR_0);
		ax_write_register(*((uint32_t *)multi_filter), reg_addr);
		reg_addr = (int *)(xdev->bar[0] + MULTI_FILTER_ARR_1);
		ax_write_register(*((uint32_t *)(multi_filter + 4)), reg_addr);

		rx_ctl |= AX_RX_CTL_AM;
	}

	reg_addr = (int *)(xdev->bar[0] + RX_CTRL);
	ax_write_register(rx_ctl, reg_addr);
	
}

static struct net_device_stats *ax_net_get_stats(struct net_device *dev)
{
	struct ax_private *ax_local = netdev_priv(dev);
	return &ax_local->net_stats;
}

static int ax_net_set_mac_addr(struct net_device *dev, void *p)
{
	struct ax_private *ax_local = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (netif_running(dev)) {
		return -EBUSY;
	}
	if (!is_valid_ether_addr(addr->sa_data)) {
		return -EADDRNOTAVAIL;
	}

	eth_hw_addr_set(dev, addr->sa_data);
	ax_set_mac_reg(ax_local, addr->sa_data);

	return 0;
} /* End of ax_pci_set_mac_addr() */

/* PROC */
static int ax_proc_show(struct seq_file *seq, void *v)
{
	seq_puts(seq, nic_version);
	return 0;
}

static int ax_proc_show_old(struct seq_file *seq, void *v)
{
	struct ax_private *ax_dev = seq->private;
	seq_puts(seq, ax_dev->ax_netdev_oldname);
	return 0;
}

static int ax_proc_show_new(struct seq_file *seq, void *v)
{
	struct ax_private *ax_dev = seq->private;
	seq_puts(seq, ax_dev->dev->name);
	return 0;
}

static int ax_proc_show_core_version(struct seq_file *seq, void *v)
{
	char str[32];
	snprintf(str, sizeof(str), "%x", *core_version);
	seq_puts(seq, str);
	return 0;
}

static int ax_proc_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 17, 0)
	if (strncmp(file->f_path.dentry->d_iname,
			ASIX_PROC_IFNAME_OLD, 10) == 0) {
		return single_open(file, ax_proc_show_old, inode->i_private);
	} else if (strncmp(file->f_path.dentry->d_iname,
			ASIX_PROC_IFNAME_NEW, 10) == 0) {
		return single_open(file, ax_proc_show_new, inode->i_private);
	} else if (strncmp(file->f_path.dentry->d_iname,
			ASIX_PROC_CORE_VERSION, 10) == 0) {
		return single_open(file,
				ax_proc_show_core_version,
				inode->i_private);
	}
	return single_open(file, ax_proc_show, inode->i_private);
#else
	if (strncmp(file->f_path.dentry->d_iname,
			ASIX_PROC_IFNAME_OLD, 10) == 0) {
		return single_open(file, ax_proc_show_old, PDE_DATA(inode));
	} else if (strncmp(file->f_path.dentry->d_iname,
			ASIX_PROC_IFNAME_NEW, 10) == 0) {
		return single_open(file, ax_proc_show_new, PDE_DATA(inode));
	} else if (strncmp(file->f_path.dentry->d_iname,
			ASIX_PROC_CORE_VERSION, 10) == 0) {
		return single_open(file,
				ax_proc_show_core_version,
				PDE_DATA(inode));
	}
	return single_open(file, ax_proc_show, PDE_DATA(inode));
#endif
}

#ifdef AX_MQ
#ifdef AX_SELECT_QUEUE
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)
static u16 axm57104_select_queue(struct net_device *dev, struct sk_buff *skb,
				 void *accel_priv,
				 select_queue_fallback_t fallback)
#else
static u16 axm57104_select_queue(struct net_device *dev, struct sk_buff *skb,
				 struct net_device *sb_dev)
#endif
{
	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		return PTP_QUEUE_INDEX;
	} else {
		return 0;
	}
}
#endif
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations ax_ifname_fops = {
	.owner   = THIS_MODULE,
	.open    = ax_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};
#else
static const struct proc_ops ax_ifname_fops = {
	//.owner   = THIS_MODULE,
	.proc_open    = ax_proc_open,
	.proc_read    = seq_read,
	.proc_lseek  = seq_lseek,
	.proc_release = single_release,
};
#endif



static const struct net_device_ops ax_netdev_ops = {
	.ndo_open			= ax_net_open,
	.ndo_stop			= ax_net_close,
	.ndo_start_xmit		= ax_net_start_xmit,
	.ndo_change_mtu		= ax_net_change_mtu,
	.ndo_do_ioctl		= ax_net_ioctl,
	.ndo_eth_ioctl		= ax_net_ioctl,
	.ndo_siocdevprivate = ax_net_privioctl, 
	.ndo_set_rx_mode	= ax_net_set_multicast,
	.ndo_get_stats		= ax_net_get_stats,
	.ndo_set_mac_address= ax_net_set_mac_addr,	
#ifdef AX_MQ
#ifdef AX_SELECT_QUEUE
	.ndo_select_queue       = axm57104_select_queue,
#endif
#endif
};

#ifdef AX_MQ
static void axm57104_netdev_capbility(struct ax_private *ax_local)
{
	ax_local->num_tx_queues = MAX_TX_QUEUE;
}
#endif

#ifdef AX_CONFIG_PM
static int axm57104_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct xdma_pci_dev *pcidev = NULL;
	struct ax_private *ax_local = NULL;
	struct net_device *netdev = NULL;
	unsigned long flags;
	u16 data;

	if (!pdev) {
		ASIX_DEBUG("[%s] pci device not exist",	__FUNCTION__);
		return -ENOTSUPP;
	}

	pcidev = dev_get_drvdata(&pdev->dev);

	if (!pcidev) {
		ASIX_MSG("[%s] xdma pci device not exist", __FUNCTION__);
		return -ENOTSUPP;
	}

	ax_local = pcidev->ax_netdev_priv;

	netdev = ax_local->dev;

	netif_device_detach(netdev);
	netif_stop_queue(netdev);

	del_timer_sync(&ax_local->ax_phy_timer);

	spin_lock_irqsave (&ax_local->lock, flags);

#ifdef CONFIG_NAPI
	netif_napi_del(&ax_local->napi);
#endif
	ax_write_register(0x0, (ax_local->xdev->bar[0] + GLOBAL_MAC_CONFIG));

#ifdef ENABLE_TASKLET
	xdma_user_isr_disable(ax_local->xdev, 0xFE);
#else
	xdma_user_isr_disable(ax_local->xdev, 0xFF);
#endif
	spin_unlock_irqrestore(&ax_local->lock, flags);

	mdio_driver_unregister(&ax_dsa_drv);
	ax_remove_mdio(ax_local);
#ifdef ENABLE_AX_REMOVE_MDIO_DEV_FUN
	ax_remove_mdio_dev(ax_local);
#endif

	xpdev_free(pcidev);
	xdma_desc_flag = 0;

	if (pdev->pm_cap) {
		pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &data);
		pci_write_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, data |
					PCI_PM_CTRL_PME_ENABLE |
					PCI_D3hot);
		pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &data);
	}

	pci_disable_device(pdev);
	pci_save_state(pdev);
	pci_enable_wake(pdev, PCI_D3hot, 1);
	pci_set_power_state(pdev, PCI_D3hot);
	netdev->operstate=IF_OPER_DOWN;
	return 0;
}

static int axm57104_resume (struct pci_dev *pdev)
{
	struct xdma_pci_dev *pcidev = NULL;
	struct ax_private *ax_local = NULL;
	struct net_device *netdev = NULL;
	int ret;
	void *hndl;
	struct xdma_dev *xdev;
	unsigned char dev_addr[6] = { 0x00, 0x0E, 0xC6, 0x00, 0x39, 0x00 };
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 10)
	u32 pci_pm_state = PCI_D0;
#endif
	u16 data;
	int i;
	
	if (!pdev) {
		ASIX_MSG("[%s] pci dev not exist", __FUNCTION__);
		return -ENOTSUPP;
	}
	pci_set_power_state(pdev, PCI_D0);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 10)
	pci_restore_state(pdev, &pci_pm_state);
#else
	pci_restore_state(pdev);
#endif
	pci_enable_wake(pdev, PCI_D0, 0);


	if (pci_enable_device(pdev) < 0) {
		ASIX_MSG("pci_enable_device failed, ""disabling device");
		return -EINVAL;
	}
	pci_set_master(pdev);

	if (pdev->pm_cap) {
		pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &data);
		pci_write_config_word(pdev,
		pdev->pm_cap + PCI_PM_CTRL, data & (~PCI_PM_CTRL_PME_ENABLE));
		pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &data);
	}

	pcidev = pcidev_alloc(pdev);
	if (!pcidev) {
		ASIX_MSG("[%s] allocate memory for pcidev fail", __FUNCTION__);
		return -ENOMEM;
	}

	hndl = xdma_device_open(DRV_MODULE_NAME, pdev, &pcidev->user_max,
	&pcidev->h2c_channel_max, &pcidev->c2h_channel_max);

	if (!hndl) {
		ASIX_MSG("[%s] xdma_device_open fail", 	__FUNCTION__);
		return -EINVAL;
	}

	if (!pcidev->h2c_channel_max && !pcidev->c2h_channel_max) {
		ASIX_MSG("[%s] NO engine found!", __FUNCTION__);
		return -EINVAL;
	}

	if (pcidev->user_max) {
		u32 mask = (1 << (pcidev->user_max + 1)) - 1;
#ifdef ENABLE_TASKLET
		mask &= 0xFFFE;
#endif
		ret = xdma_user_isr_enable(hndl, mask);
		if (ret) {
			ASIX_MSG("[%s]xdma user isr enable fail", __FUNCTION__);
			goto err_out;
		}
	}

	/* make sure no duplicate */
	xdev = xdev_find_by_pdev(pdev);
	if (!xdev) {
		ASIX_MSG("xdev not found!");
		return -EINVAL;
	}
	BUG_ON(hndl != xdev);

	pcidev->xdev = hndl;

	dev_set_drvdata(&pdev->dev, pcidev);

	netdev = netdev_primary;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	ax_local = netdev_priv(netdev);

	if(ax_tsn_init_switch(&ax_local->axswitch, &ax_local->txrx_timestamp_lock)!=0)
	{
		pr_err("TSN switch capture queue initial fail");
		goto err_out;
	}
	ax_local->dev = netdev;
	ax_local->pdev = pdev;
	ax_local->xdev = pcidev->xdev;

	pcidev->ax_netdev_priv = ax_local;

	spin_lock_init (&ax_local->mdio_lock);
	spin_lock_init (&ax_local->lock);
	spin_lock_init (&ax_local->rx_lock);
	spin_lock_init (&ax_local->txrx_timestamp_lock);

	netdev->irq = pdev->irq;

	ax_i2c_read(ax_local, 6, 0, dev_addr);
	memcpy(netdev->dev_addr, dev_addr, ETH_ALEN);
	ax_set_mac_reg(ax_local, dev_addr);

	ax_local->axswitch.pMemBase = pcidev->xdev->bar[2];
	ax_local->axswitch.ptestMemBase = pcidev->xdev->bar[0];

#ifdef CONFIG_NAPI
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 0, 0)
	netif_napi_add(netdev, &ax_local->napi, ax_net_poll);
#else
	netif_napi_add(netdev, &ax_local->napi, ax_net_poll, 64);
#endif
#endif

	for (i = 0; i < DSA_LOOP_NUM_PORTS; i++) {
		snprintf(ax_local->ax_dsa_port_names[i], 24,
			 "%s.%d", netdev->name, (u8)i);
		ax_dsa_pdata.cd.port_names[i] = ax_local->ax_dsa_port_names[i];
	}
	snprintf(ax_local->ax_netdev_oldname, 24, "%s", netdev->name);

	netif_device_attach(netdev);

	/* MDIO Bus */
	ax_init_mdio(ax_local);

	/* Create mdio_device */
	ax_create_mdio_dev(ax_local);

	/* Init PHC */
	ax_ptp_init(netdev);
	xdma_user_isr_register(xdev, pdev);
#ifdef ENABLE_TASKLET
	xdma_user_isr_enable(xdev, 0xFE);
#else
	xdma_user_isr_enable(xdev, 0xFF);
#endif

	ax_init_rgmii(ax_local);
	/* Init RTL PHY*/
	ax_init_phy_led(ax_local);
	ax_phy_greeneth(ax_local, 0);
	ax_phy_EEE(ax_local, 0);
	ax_phy_PTP(ax_local, 0);
	ax_phy_serdes(ax_local, 0);
	ax_restart_autoneg(ax_local);

	ax_phy_timer_init(ax_local);
	/* Register MDIO dev driver */
	mdio_driver_register(&ax_dsa_drv);
	netdev->operstate=IF_OPER_UP;
	return 0;

err_out:
	if (pcidev->xdev->got_regions) {
		pci_release_regions(pdev);
	}

	if (!pcidev->xdev->regions_in_use) {
		pci_disable_device(pdev);
	}

	xpdev_free(pcidev);
	if (ax_local->dev) {
		free_netdev(ax_local->dev);
	}
	if (ax_local->proc_dir) {
		remove_proc_entry(ASIX_PROC_DIR, init_net.proc_net);
	}
	if (ax_local->pron_ifname_new) {
		proc_remove(ax_local->pron_ifname_new);
	}
	if (ax_local->pron_ifname_old) {
		proc_remove(ax_local->pron_ifname_old);
	}
	if (ax_local->proc_core_version) {
		proc_remove(ax_local->proc_core_version);
	}
	return -1;
}
#endif

static int axm57104_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *netdev = NULL;
	struct ax_private *ax_local = NULL;
	struct xdma_pci_dev *pcidev = NULL;
	struct xdma_dev *xdev;
	int rv = 0, i;
	void *hndl;
	unsigned char dev_addr[6] = { 0x00, 0x0E, 0xC6, 0x00, 0x39, 0x00 };
	int *core_ver_add = NULL, *switch_ver_add = NULL;
	u32 switch_ver = 0, core_ver;

	pcidev = pcidev_alloc(pdev);
	if (!pcidev) {
		return -ENOMEM;
	}

	hndl = xdma_device_open(DRV_MODULE_NAME, pdev, &pcidev->user_max,
			&pcidev->h2c_channel_max, &pcidev->c2h_channel_max);
	if (!hndl) {
		return -EINVAL;
	}

	if (!pcidev->h2c_channel_max && !pcidev->c2h_channel_max) {
		ASIX_MSG("engine not found!\n");
		return -EINVAL;
	}

	if (pcidev->user_max) {
		u32 mask = (1 << (pcidev->user_max + 1)) - 1;
#ifdef ENABLE_TASKLET
		mask &= 0xFFFE;
#endif
		rv = xdma_user_isr_enable(hndl, mask);
		if (rv) {
			goto err_out;
		}
	}

	/* make sure no duplicate */
	xdev = xdev_find_by_pdev(pdev);
	if (!xdev) {
		pr_warn("NO xdev found!\n");
		return -EINVAL;
	}
	BUG_ON(hndl != xdev);
	pr_info("%s xdma%d, pdev 0x%p, xdev 0x%p, 0x%p, usr %d, ch %d,%d\n",
		dev_name(&pdev->dev), xdev->idx, pdev, pcidev, xdev,
		pcidev->user_max, pcidev->h2c_channel_max,
		pcidev->c2h_channel_max);

	pcidev->xdev = hndl;	
	xdma_desc_flag = 0;

#ifdef AX_MQ
	netdev = alloc_etherdev_mq(sizeof(struct ax_private), MAX_TX_QUEUE);
#else
	netdev = alloc_etherdev(sizeof(struct ax_private));	
	if (!netdev) {
		ASIX_MSG("Etherdev alloc failed.\n");
		goto err_out;
		rv = -ENOMEM;
	}
#endif
	
	dev_set_drvdata(&pdev->dev, pcidev);//0512
	SET_NETDEV_DEV(netdev, &pdev->dev);
	
	netdev_primary = netdev;
	ax_local = netdev_priv(netdev);

	netdev->netdev_ops = &ax_netdev_ops;
	netdev->ethtool_ops = &ax_ethtool_ops;
	
	if(ax_tsn_init_switch(&ax_local->axswitch, &ax_local->txrx_timestamp_lock)!=0)
	{
		pr_err("TSN switch capture queue initial fail");
		goto err_out;
	}
	ax_local->dev = netdev;
	ax_local->pdev = pdev;
	ax_local->xdev = pcidev->xdev;

#ifdef AX_MQ
	axm57104_netdev_capbility(ax_local);
#endif
	pcidev->ax_netdev_priv = ax_local;

	spin_lock_init (&ax_local->mdio_lock);
	spin_lock_init (&ax_local->lock);
	spin_lock_init (&ax_local->rx_lock);
	spin_lock_init (&ax_local->txrx_timestamp_lock);

	netdev->irq = pdev->irq;
	
	ax_i2c_read(ax_local, 6, 0, dev_addr);

	ax_local->axswitch.pMemBase = pcidev->xdev->bar[2];
	ax_local->axswitch.ptestMemBase = pcidev->xdev->bar[0];
	core_ver_add = (int *)(pcidev->xdev->bar[2] + CORE_VERSION);
	switch_ver_add = (int *)(pcidev->xdev->bar[0] + DRIVER_VERSION);

	core_version = core_ver_add;
#ifdef CONFIG_NAPI
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 0, 0)
	netif_napi_add(netdev, &ax_local->napi, ax_net_poll);
#else
	netif_napi_add(netdev, &ax_local->napi, ax_net_poll, 64);
#endif
#endif /* CONFIG_NAPI */

	eth_hw_addr_set(netdev,dev_addr);

	if(!is_valid_ether_addr(netdev->dev_addr)){
		dev_err(&ax_local->pdev->dev, "Invalid MAC Address\n");
		//err = -EIO;
		goto err_out;
	}

	ax_set_mac_reg(ax_local, dev_addr);

	if (register_netdev(netdev) != 0) {
		//ASIX_DEBUG("Cannot register net device, aborting.\n");
		printk("Cannot register net device, aborting.\n");
		goto err_out1;
	}

	for (i = 0; i < DSA_LOOP_NUM_PORTS; i++) {
		snprintf(ax_local->ax_dsa_port_names[i], 24,
			 "%s.%d", netdev->name, (u8)i);
		ax_dsa_pdata.cd.port_names[i] = ax_local->ax_dsa_port_names[i];		
	}
	snprintf(ax_local->ax_netdev_oldname, 24, "%s", netdev->name);
	

	/* MDIO Bus */
	ax_init_mdio(ax_local);
	/* Create mdio_device */
	ax_create_mdio_dev(ax_local);	
	/* Init PHC */
	ax_ptp_init(netdev);
	
	xdma_user_isr_register(xdev, pdev);
#ifdef ENABLE_TASKLET
	xdma_user_isr_enable(xdev, 0xFE);
#else
	xdma_user_isr_enable(xdev, 0xFF);
#endif

	ax_init_rgmii(ax_local);
	ax_init_phy_led(ax_local);
	ax_phy_greeneth(ax_local, 0);
	ax_phy_EEE(ax_local, 0);
	ax_phy_PTP(ax_local, 0);
	ax_phy_serdes(ax_local, 0);
	ax_restart_autoneg(ax_local);
	ax_phy_timer_init(ax_local);
	/* Register MDIO dev driver */
	mdio_driver_register(&ax_dsa_drv);	

	ax_local->proc_dir = proc_mkdir(ASIX_PROC_DIR, init_net.proc_net);
	if (!ax_local->proc_dir) {
		pr_warn("cannot create /proc/net/%s\n", ASIX_PROC_DIR);
		rv = -ENODEV;
		goto err_out1;
	}
	ax_local->pron_ifname_old = proc_create_data(ASIX_PROC_IFNAME_OLD,
						     0666,
						     ax_local->proc_dir,
						     &ax_ifname_fops,
						     ax_local);
	if (ax_local->pron_ifname_old == NULL) {
		pr_err("cannot create %s procfs entry\n"
						, ASIX_PROC_IFNAME_OLD);
		rv = -EINVAL;
		goto err_out1;
	}
	ax_local->pron_ifname_new = proc_create_data(ASIX_PROC_IFNAME_NEW,
						     0666,
						     ax_local->proc_dir,
						     &ax_ifname_fops,
						     ax_local);
	if (ax_local->pron_ifname_new == NULL) {
		pr_err("cannot create %s procfs entry\n"
						, ASIX_PROC_IFNAME_NEW);
		rv = -EINVAL;
		goto err_out1;
	}

	ax_local->proc_core_version = proc_create_data(ASIX_PROC_CORE_VERSION,
							0666,
							ax_local->proc_dir,
							&ax_ifname_fops,
							ax_local);
	if (ax_local->proc_core_version == NULL) {
		pr_err("cannot create %s procfs entry\n"
						, ASIX_PROC_CORE_VERSION);
		rv = -EINVAL;
		goto err_out1;
	}
	switch_ver = ax_read_register(switch_ver_add);
	core_ver = ax_read_register(core_ver_add);

	ASIX_MSG("AXM57104A Driver version : %s", DRV_MODULE_VERSION);
	ASIX_MSG("AXM57104A NIC version : %x", switch_ver);
	ASIX_MSG("AXM57104A Core version : %x", core_ver);

	/* Add at 2023.8.18*/	
	netif_carrier_off(netdev);
	netif_carrier_on(netdev);
	netdev->operstate = IF_OPER_UP;
	return 0;
err_out1:
#ifdef CONFIG_NAPI
	netif_napi_del(&ax_local->napi);
#endif
err_out:
	pr_err("pdev 0x%p, err %d.\n", pdev, rv);
	if (pcidev->xdev->got_regions) {
		pci_release_regions(pdev);
	}

	if (!pcidev->xdev->regions_in_use) {
		pci_disable_device(pdev);
	}

	xpdev_free(pcidev);
	if (ax_local->dev) {
		free_netdev(ax_local->dev);
	}
	if (ax_local->proc_dir) {
		remove_proc_entry(ASIX_PROC_DIR, init_net.proc_net);
	}
	if (ax_local->pron_ifname_new) {
		proc_remove(ax_local->pron_ifname_new);
	}
	if (ax_local->pron_ifname_old) {
		proc_remove(ax_local->pron_ifname_old);
	}
	if (ax_local->proc_core_version) {
		proc_remove(ax_local->proc_core_version);
	}
	return rv;
}

static void axm57104_remove(struct pci_dev *pdev)
{
	struct xdma_pci_dev *pcidev;
	struct ax_private *ax_local;	
	if (!pdev) {
		return;
	}

	pcidev = dev_get_drvdata(&pdev->dev);	
	if (!pcidev) {
		return;
	}

	ax_local = pcidev->ax_netdev_priv;
	del_timer_sync(&ax_local->ax_phy_timer);
	if (ax_local->pron_ifname_new) {
		proc_remove(ax_local->pron_ifname_new);
	}
	if (ax_local->pron_ifname_old) {
		proc_remove(ax_local->pron_ifname_old);
	}
	if (ax_local->proc_core_version) {
		proc_remove(ax_local->proc_core_version);
	}
	if (ax_local->proc_dir) {
		remove_proc_entry(ASIX_PROC_DIR, init_net.proc_net);
	}
#ifdef CONFIG_NAPI
	netif_napi_del(&ax_local->napi);
#endif

#ifdef ENABLE_TASKLET
	xdma_user_isr_disable(ax_local->xdev, 0xFE);
#else
	xdma_user_isr_disable(ax_local->xdev, 0xFF);
#endif

	ax_ptp_remove(ax_local->dev);
	mdio_driver_unregister(&ax_dsa_drv);
	ax_remove_mdio(ax_local);
#ifdef ENABLE_AX_REMOVE_MDIO_DEV_FUN
	ax_remove_mdio_dev(ax_local);/*Wales 20250408 change */
#endif
	unregister_netdev(ax_local->dev);
	free_netdev(ax_local->dev);
	xpdev_free(pcidev);
	dev_set_drvdata(&pdev->dev, NULL);
}

/*
 * PCIe Operation
 */

static pci_ers_result_t axpci_error_detected(struct pci_dev *pdev,
					pci_channel_state_t state)
{
	struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
		break;
	case pci_channel_io_frozen:
		pr_warn("dev 0x%p,0x%p, \
			frozen state error, \
			reset controller\n",
			pdev, xpdev);
		xdma_device_offline(pdev, xpdev->xdev);
		pci_disable_device(pdev);
		return PCI_ERS_RESULT_NEED_RESET;
		break;
	case pci_channel_io_perm_failure:
		pr_warn("dev 0x%p,0x%p, \
			failure state error, \
			req. disconnect\n",
			pdev, xpdev);

		return PCI_ERS_RESULT_DISCONNECT;
		break;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t axpci_slot_reset(struct pci_dev *pdev)
{
	struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	if (pci_enable_device_mem(pdev)) {
		pr_info("0x%p failed to renable after slot reset\n", xpdev);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);
	xdma_device_online(pdev, xpdev->xdev);

	return PCI_ERS_RESULT_RECOVERED;
}

static void axpci_error_resume(struct pci_dev *pdev)
{
	struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p.\n", pdev, xpdev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
	pci_cleanup_aer_uncorrect_error_status(pdev);
#else
	pci_aer_clear_nonfatal_status(pdev);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
static void axpci_reset_prepare(struct pci_dev *pdev)
{
	struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p.\n", pdev, xpdev);
	xdma_device_offline(pdev, xpdev->xdev);
}

static void axpci_reset_done(struct pci_dev *pdev)
{
	struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p.\n", pdev, xpdev);
	xdma_device_online(pdev, xpdev->xdev);
}

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static void axpci_reset_notify(struct pci_dev *pdev, bool prepare)
{
	struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p, prepare %d.\n", pdev, xpdev, prepare);

	if (prepare) {
		xdma_device_offline(pdev, xpdev->xdev);
	} else {
		xdma_device_online(pdev, xpdev->xdev);
	}
}
#endif

static const struct pci_error_handlers xdma_err_handler = {
	.error_detected	= axpci_error_detected,
	.slot_reset	= axpci_slot_reset,
	.resume		= axpci_error_resume,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	.reset_prepare	= axpci_reset_prepare,
	.reset_done	= axpci_reset_done,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	.reset_notify	= axpci_reset_notify,
#endif
};

static struct pci_driver axpci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= pci_ids,
	.probe		= axm57104_probe,
	.remove		= axm57104_remove,
	.err_handler	= &xdma_err_handler,
#ifdef AX_CONFIG_PM
	.suspend	= axm57104_suspend,
	.resume		= axm57104_resume,
#endif
};

/*
 * Kernel Module Initial
 */
static int __init axm57104_init(void)
{
	pr_info(DRV_MODULE_NAME ": Driver Install");
	return pci_register_driver(&axpci_driver);
}
static void __exit axm57104_exit(void)
{
	/* unregister this driver from the PCI bus driver */
	pr_info(DRV_MODULE_NAME ": Driver Uninstall");
	pci_unregister_driver(&axpci_driver);
}

module_init(axm57104_init);
module_exit(axm57104_exit);
