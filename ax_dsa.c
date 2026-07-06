/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *****************************************************************************/
/*
 * Distributed Switch Architecture loopback driver
 *
 * Copyright (C) 2016, Florian Fainelli <f.fainelli@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/export.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/if_bridge.h>
#include <linux/phy_fixed.h>
#include <net/dsa.h>

#include "libxdma.h"
#include "ax_dsa_priv.h"
#include "ax_dsa.h"
#include "version.h"
/*
#define DRV_MODULE_NAME		"AXM57104_DSA"
#define DRV_MODULE_DESC		"AXM57104 DSA Driver"

static char dsa_version[] =
	DRV_MODULE_DESC " " DRV_MODULE_NAME " v" DRV_MODULE_VERSION "\n";

MODULE_AUTHOR("ASIX");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("Dual BSD/GPL");*/

struct dsa_loop_vlan {
	u16 members;
	u16 untagged;
};

struct dsa_loop_mib_entry {
	char name[ETH_GSTRING_LEN];
	unsigned long val;
};

enum dsa_loop_mib_counters {
	DSA_LOOP_PHY_READ_OK,
	DSA_LOOP_PHY_READ_ERR,
	DSA_LOOP_PHY_WRITE_OK,
	DSA_LOOP_PHY_WRITE_ERR,
	__DSA_LOOP_CNT_MAX,
};

static struct dsa_loop_mib_entry dsa_loop_mibs[] = {
	[DSA_LOOP_PHY_READ_OK]	= { "phy_read_ok", },
	[DSA_LOOP_PHY_READ_ERR]	= { "phy_read_err", },
	[DSA_LOOP_PHY_WRITE_OK] = { "phy_write_ok", },
	[DSA_LOOP_PHY_WRITE_ERR] = { "phy_write_err", },
};

struct dsa_loop_port {
	struct dsa_loop_mib_entry mib[__DSA_LOOP_CNT_MAX];
};

#define DSA_LOOP_VLANS	5

struct dsa_loop_priv {
	struct mii_bus	*bus;
	unsigned int	port_base;
	struct dsa_loop_vlan vlans[DSA_LOOP_VLANS];
	struct net_device *netdev;
	struct dsa_loop_port ports[DSA_MAX_PORTS];
	u16 pvid;
};

/*
 * DSA Ethtool
 */
static void dsa_slave_get_drvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, "dsa", sizeof(drvinfo->driver));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "platform", sizeof(drvinfo->bus_info));
}

static int dsa_slave_get_regs_len(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_regs_len) {
		return ds->ops->get_regs_len(ds, p->dp->index);
	}

	return -EOPNOTSUPP;
}

static void
dsa_slave_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *_p)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_regs) {
		ds->ops->get_regs(ds, p->dp->index, regs, _p);
	}
}

static int dsa_slave_nway_reset(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL) {
		return genphy_restart_aneg(p->phy);
	}

	return -EOPNOTSUPP;
}

static u32 dsa_slave_get_link(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL) {
		genphy_update_link(p->phy);
		return p->phy->link;
	}

	return -EOPNOTSUPP;
}

static int dsa_slave_get_eeprom_len(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->cd && ds->cd->eeprom_len) {
		return ds->cd->eeprom_len;
	}

	if (ds->ops->get_eeprom_len) {
		return ds->ops->get_eeprom_len(ds);
	}

	return 0;
}

static int dsa_slave_get_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_eeprom) {
		return ds->ops->get_eeprom(ds, eeprom, data);
	}

	return -EOPNOTSUPP;
}

static int dsa_slave_set_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->set_eeprom) {
		return ds->ops->set_eeprom(ds, eeprom, data);
	}

	return -EOPNOTSUPP;
}

static void dsa_slave_get_strings(struct net_device *dev,
				  uint32_t stringset, uint8_t *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	struct dsa_switch *ds = dp->ds;

	if (stringset == ETH_SS_STATS) {
		int len = ETH_GSTRING_LEN;

		strncpy(data, "tx_packets", len);
		strncpy(data + len, "tx_bytes", len);
		strncpy(data + 2 * len, "rx_packets", len);
		strncpy(data + 3 * len, "rx_bytes", len);
		if (ds->ops->get_strings) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
			ds->ops->get_strings(ds, dp->index, stringset,
					     data + 4 * len);
#else
			ds->ops->get_strings(ds, dp->index, data + 4 * len);
#endif
		}
	}
}


static void dsa_slave_get_ethtool_stats(struct net_device *dev,
					struct ethtool_stats *stats,
					uint64_t *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	struct dsa_switch *ds = dp->ds;

	data[0] = dev->stats.tx_packets;
	data[1] = dev->stats.tx_bytes;
	data[2] = dev->stats.rx_packets;
	data[3] = dev->stats.rx_bytes;
	if (ds->ops->get_ethtool_stats) {
		ds->ops->get_ethtool_stats(ds, dp->index, data + 4);
	}
}

static int dsa_slave_get_sset_count(struct net_device *dev, int sset)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (sset == ETH_SS_STATS) {
		int count;

		count = 4;
		if (ds->ops->get_sset_count) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
			count += ds->ops->get_sset_count(ds, dp->index, sset);	
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
			count += ds->ops->get_sset_count(ds, dp->index);
#else
			count += ds->ops->get_sset_count(ds);
#endif
		}

		return count;
	}

	return -EOPNOTSUPP;
#else
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_port *cpu_dp = dsa_get_cpu_port(dst);
	struct dsa_switch *ds = cpu_dp->ds;
	int count = 0;

	if (cpu_dp->ethtool_ops.get_sset_count) {
		count += cpu_dp->ethtool_ops.get_sset_count(dev, sset);
	}

	if (sset == ETH_SS_STATS && ds->ops->get_sset_count) {
		count += ds->ops->get_sset_count(ds);
	}

	return count;
#endif
}

static void 
dsa_slave_get_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_wol) {
		ds->ops->get_wol(ds, p->dp->index, w);
	}
}

static int dsa_slave_set_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;
	int ret = -EOPNOTSUPP;

	if (ds->ops->set_wol) {
		ret = ds->ops->set_wol(ds, p->dp->index, w);
	}

	return ret;
}

static int dsa_slave_set_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;
	int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	/* Port's PHY and MAC both need to be EEE capable */
	if (!dev->phydev) {
		return -ENODEV;
	}

	if (!ds->ops->set_mac_eee) {
		return -EOPNOTSUPP;
	}

	ret = ds->ops->set_mac_eee(ds, p->dp->index, e);
	if (ret) {
		return ret;
	}

	if (e->eee_enabled) {
		ret = phy_init_eee(dev->phydev, 0);
		if (ret) {
			return ret;
		}
	}

	return phy_ethtool_set_eee(dev->phydev, e);
#else
	if (!ds->ops->set_eee) {
		return -EOPNOTSUPP;
	}

	ret = ds->ops->set_eee(ds, p->dp->index, p->phy, e);
	if (ret) {
		return ret;
	}

	if (p->phy) {
		ret = phy_ethtool_set_eee(p->phy, e);
	}

	return ret;
#endif
}

static int dsa_slave_get_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;
	int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	/* Port's PHY and MAC both need to be EEE capable */
	if (!dev->phydev) {
		return -ENODEV;
	}

	if (!ds->ops->get_mac_eee) {
		return -EOPNOTSUPP;
	}

	ret = ds->ops->get_mac_eee(ds, p->dp->index, e);
	if (ret) {
		return ret;
	}

	return phy_ethtool_get_eee(dev->phydev, e);
#else
	if (!ds->ops->get_eee) {
		return -EOPNOTSUPP;
	}

	ret = ds->ops->get_eee(ds, p->dp->index, e);
	if (ret) {
		return ret;
	}

	if (p->phy) {
		ret = phy_ethtool_get_eee(p->phy, e);
	}

	return ret;
#endif
}

static int
dsa_slave_get_link_ksettings(struct net_device *dev,
			     struct ethtool_link_ksettings *cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (!p->phy) {
		return -EOPNOTSUPP;
	}

	phy_ethtool_ksettings_get(p->phy, cmd);

	return 0;
}

static int
dsa_slave_set_link_ksettings(struct net_device *dev,
			     const struct ethtool_link_ksettings *cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL) {
		return phy_ethtool_ksettings_set(p->phy, cmd);
	}

	return -EOPNOTSUPP;
}


static int dsa_slave_get_rxnfc(struct net_device *dev,
			       struct ethtool_rxnfc *nfc, u32 *rule_locs)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (!ds->ops->get_rxnfc) {
		return -EOPNOTSUPP;
	}

	return ds->ops->get_rxnfc(ds, p->dp->index, nfc, rule_locs);
}

static int dsa_slave_set_rxnfc(struct net_device *dev,
			       struct ethtool_rxnfc *nfc)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (!ds->ops->set_rxnfc) {
		return -EOPNOTSUPP;
	}

	return ds->ops->set_rxnfc(ds, p->dp->index, nfc);
}

static int dsa_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info)
{
	info->so_timestamping =
			SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_RAW_HARDWARE;

	info->phc_index = 0;

	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) | 
		           (1 << HWTSTAMP_FILTER_ALL);

	return 0;
}

static const struct ethtool_ops dsa_loop_ethtool_ops = {
	.get_drvinfo		= dsa_slave_get_drvinfo,
	.get_regs_len		= dsa_slave_get_regs_len,
	.get_regs		= dsa_slave_get_regs,
	.nway_reset		= dsa_slave_nway_reset,
	.get_link		= dsa_slave_get_link,
	.get_eeprom_len		= dsa_slave_get_eeprom_len,
	.get_eeprom		= dsa_slave_get_eeprom,
	.set_eeprom		= dsa_slave_set_eeprom,
	.get_strings		= dsa_slave_get_strings,
	.get_ethtool_stats	= dsa_slave_get_ethtool_stats,
	.get_sset_count		= dsa_slave_get_sset_count,
	.set_wol		= dsa_slave_set_wol,
	.get_wol		= dsa_slave_get_wol,
	.set_eee		= dsa_slave_set_eee,
	.get_eee		= dsa_slave_get_eee,
	.get_link_ksettings	= dsa_slave_get_link_ksettings,
	.set_link_ksettings	= dsa_slave_set_link_ksettings,
	.get_rxnfc		= dsa_slave_get_rxnfc,
	.set_rxnfc		= dsa_slave_set_rxnfc,
	.get_ts_info            = dsa_get_ts_info,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
static enum dsa_tag_protocol dsa_loop_get_protocol(struct dsa_switch *ds,
						   int port)
#else
static enum dsa_tag_protocol dsa_loop_get_protocol(struct dsa_switch *ds)
#endif
{
	return DSA_TAG_PROTO_DSA;
}

static int dsa_loop_setup(struct dsa_switch *ds)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < ds->num_ports; i++) {
		memcpy(ps->ports[i].mib, dsa_loop_mibs, sizeof(dsa_loop_mibs));
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static int dsa_loop_get_sset_count(struct dsa_switch *ds, int port, int sset)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
static int dsa_loop_get_sset_count(struct dsa_switch *ds, int port)
#else
static int dsa_loop_get_sset_count(struct dsa_switch *ds)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	if (sset != ETH_SS_STATS && sset != ETH_SS_PHY_STATS) {
		return 0;
	}
#endif	
	return __DSA_LOOP_CNT_MAX;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static void dsa_loop_get_strings(struct dsa_switch *ds, int port,
				 u32 stringset, uint8_t *data)
#else
static void dsa_loop_get_strings(struct dsa_switch *ds, int port, uint8_t *data)
#endif
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	if (stringset != ETH_SS_STATS && stringset != ETH_SS_PHY_STATS) {
		return;
	}
#endif

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++) {
		memcpy(data + i * ETH_GSTRING_LEN,
		       ps->ports[port].mib[i].name, ETH_GSTRING_LEN);
	}
}

static void dsa_loop_get_ethtool_stats(struct dsa_switch *ds, int port,
				       uint64_t *data)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++) {
		data[i] = ps->ports[port].mib[i].val;
	}
}

static int dsa_loop_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;

	ASIX_DEBUG("%s\n", __func__);
	udelay(100);
	ret = mdiobus_read_nested(bus, ps->port_base + port, regnum);
	if (ret < 0) {
		ps->ports[port].mib[DSA_LOOP_PHY_READ_ERR].val++;
	} else {
		ps->ports[port].mib[DSA_LOOP_PHY_READ_OK].val++;
	}

	return ret;
}

static int dsa_loop_phy_write(struct dsa_switch *ds, int port,
			      int regnum, u16 value)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;

	ASIX_MSG("%s\n", __func__);

	ret = mdiobus_write_nested(bus, ps->port_base + port, regnum, value);
	if (ret < 0) {
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_ERR].val++;
	} else {
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_OK].val++;
	}

	return ret;
}

static void dsa_loop_fixed_link_update(struct dsa_switch *ds, int port,
				       struct fixed_phy_status *st)
{
	ASIX_MSG("%s\n", __func__);
	st->link = 1;
	st->speed = SPEED_1000;
	st->duplex = 1;
	st->pause = 0;
	st->asym_pause = 0;
}

static int dsa_loop_port_bridge_join(struct dsa_switch *ds, int port,
				     struct net_device *bridge)
{
	ASIX_MSG("%s\n", __func__);
	return 0;
}

static void dsa_loop_port_bridge_leave(struct dsa_switch *ds, int port,
				       struct net_device *bridge)
{
	ASIX_MSG("%s\n", __func__);
}

static void dsa_loop_port_stp_state_set(struct dsa_switch *ds, int port,
					u8 state)
{

}

static int dsa_loop_port_vlan_filtering(struct dsa_switch *ds, int port,
					bool vlan_filtering)
{
	ASIX_MSG("%s\n", __func__);
	return 0;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
static int
dsa_loop_port_vlan_prepare(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_vlan *vlan)
#else
static int dsa_loop_port_vlan_prepare(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_vlan *vlan,
				struct switchdev_trans *trans)
#endif
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;

	ASIX_MSG("%s\n", __func__);
	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	if (vlan->vid_end > DSA_LOOP_VLANS) {
		return -ERANGE;
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
static void dsa_loop_port_vlan_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan)
#else
static void dsa_loop_port_vlan_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct switchdev_trans *trans)
#endif
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;
	u16 vid;

	ASIX_MSG("%s\n", __func__);

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		vl = &ps->vlans[vid];

		vl->members |= BIT(port);
		if (untagged) {
			vl->untagged |= BIT(port);
		} else {
			vl->untagged &= ~BIT(port);
		}
	}

	if (pvid) {
		ps->pvid = vid;
	}
}

static int dsa_loop_port_vlan_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;
	u16 vid, pvid = ps->pvid;

	ASIX_MSG("%s\n", __func__);

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		vl = &ps->vlans[vid];

		vl->members &= ~BIT(port);
		if (untagged) {
			vl->untagged &= ~BIT(port);
		}

		if (pvid == vid) {
			pvid = 1;
		}
	}
	ps->pvid = pvid;

	return 0;
}

static const struct dsa_switch_ops dsa_loop_driver = {
	.get_tag_protocol	= dsa_loop_get_protocol,
	.setup			= dsa_loop_setup,
	.get_strings		= dsa_loop_get_strings,
	.get_ethtool_stats	= dsa_loop_get_ethtool_stats,
	.get_sset_count		= dsa_loop_get_sset_count,
	.phy_read		= dsa_loop_phy_read,
	.phy_write		= dsa_loop_phy_write,
	.fixed_link_update	= dsa_loop_fixed_link_update,
	.port_bridge_join	= dsa_loop_port_bridge_join,
	.port_bridge_leave	= dsa_loop_port_bridge_leave,
	.port_stp_state_set	= dsa_loop_port_stp_state_set,
	.port_vlan_filtering	= dsa_loop_port_vlan_filtering,
	.port_vlan_prepare	= dsa_loop_port_vlan_prepare,
	.port_vlan_add		= dsa_loop_port_vlan_add,
	.port_vlan_del		= dsa_loop_port_vlan_del,
};


static int dsa_loop_drv_probe(struct mdio_device *mdiodev)
{
	struct dsa_loop_pdata *pdata = mdiodev->dev.platform_data;
	struct dsa_loop_priv *ps;
	struct dsa_switch *ds;

	if (!pdata) {
		return -ENODEV;
	}

	dev_info(&mdiodev->dev, "%s: 0x%0x\n",
		 pdata->name, pdata->enabled_ports);

	ds = dsa_switch_alloc(&mdiodev->dev, DSA_MAX_PORTS);
	if (!ds) {
		return -ENOMEM;
	}

	ps = devm_kzalloc(&mdiodev->dev, sizeof(*ps), GFP_KERNEL);
	if (!ps) {
		return -ENOMEM;
	}

	ps->netdev = pdata->netdev;
	if (!ps->netdev) {
		return -EPROBE_DEFER;
	}

	pdata->cd.netdev[DSA_LOOP_CPU_PORT] = &ps->netdev->dev;

	ds->dev = &mdiodev->dev;
	ds->ops = &dsa_loop_driver;
	ds->priv = ps;
	ps->bus = mdiodev->bus;

	dev_set_drvdata(&mdiodev->dev, ds);

	return dsa_register_switch(ds);
}

static void dsa_loop_drv_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);

	dsa_unregister_switch(ds);
}

struct mdio_driver ax_dsa_drv = {
	.mdiodrv.driver	= {
		.name	= "ax_switch_mdio",
	},
	.probe	= dsa_loop_drv_probe,
	.remove	= dsa_loop_drv_remove,
};
