// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Distributed Switch Architecture loopback driver
 *
 * Copyright (C) 2016, Florian Fainelli <f.fainelli@gmail.com>
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/export.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/if_bridge.h>
#include <linux/mii.h>

//#include "dsa.h" //fron include/net/dsa.h
//#include "dsa_pdata.h"
#include "dsa_loop.h"
#include "dsa2.h"

//#define AX_DSA_LOOP_PHY_READ
//#define PORT_MTU_ENABLE

/*------------------------ /linux/dsa/loop.h -----------------------------*/
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

struct dsa_loop_port {
	struct dsa_loop_mib_entry mib[__DSA_LOOP_CNT_MAX];
	u16 pvid;
	int mtu;
};
#define DSA_LOOP_VLANS	5

struct dsa_loop_priv {
	struct mii_bus	*bus;
	unsigned int	port_base;
	struct dsa_loop_vlan vlans[DSA_LOOP_VLANS];
	struct net_device *netdev;
	struct dsa_loop_port ports[DSA_MAX_PORTS];
};
/*-----------------------------------------------------------*/

static struct dsa_loop_mib_entry dsa_loop_mibs[] = {
	[DSA_LOOP_PHY_READ_OK]	= { "phy_read_ok", },
	[DSA_LOOP_PHY_READ_ERR]	= { "phy_read_err", },
	[DSA_LOOP_PHY_WRITE_OK] = { "phy_write_ok", },
	[DSA_LOOP_PHY_WRITE_ERR] = { "phy_write_err", },
};
#if 0
static struct phy_device *phydevs[PHY_MAX_ADDR];
#endif
enum dsa_loop_devlink_resource_id {
	DSA_LOOP_DEVLINK_PARAM_ID_VTU,
};

static u64 dsa_loop_devlink_vtu_get(void *priv)
{
	struct dsa_loop_priv *ps = priv;
	unsigned int i, count = 0;
	struct dsa_loop_vlan *vl;

	for (i = 0; i < ARRAY_SIZE(ps->vlans); i++) {
		vl = &ps->vlans[i];
		if (vl->members)
			count++;
	}
	return count;
}

static int dsa_loop_setup_devlink_resources(struct dsa_switch *ds)
{
	struct devlink_resource_size_params size_params;
	struct dsa_loop_priv *ps = ds->priv;
	int err;
		
	devlink_resource_size_params_init(&size_params, ARRAY_SIZE(ps->vlans),
					  ARRAY_SIZE(ps->vlans),
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	err = dsa_devlink_resource_register(ds, "VTU", ARRAY_SIZE(ps->vlans),
					    DSA_LOOP_DEVLINK_PARAM_ID_VTU,
					    DEVLINK_RESOURCE_ID_PARENT_TOP,
					    &size_params);
	if (err)
		goto out;

	dsa_devlink_resource_occ_get_register(ds,
					      DSA_LOOP_DEVLINK_PARAM_ID_VTU,
					      dsa_loop_devlink_vtu_get, ps);
	
	return 0;

out:
	dsa_devlink_resources_unregister(ds);
	return err;
}

static enum dsa_tag_protocol dsa_loop_get_protocol(struct dsa_switch *ds,
						   int port,
						   enum dsa_tag_protocol mp)
{
	dev_dbg(ds->dev, "%s: port: %d\n", __func__, port);
	return DSA_TAG_PROTO_SDSA;	
}

static int dsa_loop_setup(struct dsa_switch *ds)
{
#if 1
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < ds->num_ports; i++)
		memcpy(ps->ports[i].mib, dsa_loop_mibs,
		       sizeof(dsa_loop_mibs));

	dev_dbg(ds->dev, "%s\n", __func__);
	return dsa_loop_setup_devlink_resources(ds);
#else
	int port;
    for(port = 0; port < ds->num_ports; port++) {
         struct dsa_port *dp = dsa_to_port(ds,port);
         if(port==4)
             dp->type = DSA_PORT_TYPE_CPU;
         else
             dp->type = DSA_PORT_TYPE_USER;
 
 
     }
     return 0;
#endif


}

static void dsa_loop_teardown(struct dsa_switch *ds)
{	
	dsa_devlink_resources_unregister(ds);
}

static int dsa_loop_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS && sset != ETH_SS_PHY_STATS)
		return 0;

	return __DSA_LOOP_CNT_MAX;
}

static void dsa_loop_get_strings(struct dsa_switch *ds, int port,
				 u32 stringset, uint8_t *data)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;
	
	if (stringset != ETH_SS_STATS && stringset != ETH_SS_PHY_STATS)
		return;

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++)
		memcpy(data + i * ETH_GSTRING_LEN,
		       ps->ports[port].mib[i].name, ETH_GSTRING_LEN);
}

static void dsa_loop_get_ethtool_stats(struct dsa_switch *ds, int port,
				       uint64_t *data)
{
	struct dsa_loop_priv *ps = ds->priv;
	unsigned int i;

	for (i = 0; i < __DSA_LOOP_CNT_MAX; i++)
		data[i] = ps->ports[port].mib[i].val;
}

#ifdef AX_DSA_LOOP_PHY_READ
static int ax_dsa_loop_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;
	int phyid, RTL_physr;

	ret = mdiobus_read_nested(bus, ps->port_base + port, regnum);// Reg num = 0x0, 0x1, 0x5, 0xa
	phyid = mdiobus_read_nested(bus, ps->port_base + port, MII_PHYSID1);
	if(phyid == 0x1c) {
		if (regnum == MII_STAT1000) { 
            //link parter annocence had 1G capability but we still check real or not
			if(ret & (LPA_1000FULL | LPA_1000HALF)) { 
				//Check real speed				
				mdiobus_write_nested(bus, ps->port_base + port, 0x1f, 0xa43);//RTL page reg
				msleep(100);
				if(ret < 0)
					dev_dbg(ds->dev, "[%s:%d] port:%d, mdiobus write failed\n",__func__,__LINE__,port);
				
				RTL_physr = mdiobus_read_nested(bus, ps->port_base + port, 0x1a);// read 0x1a at page 0xa43
				mdiobus_write_nested(bus, ps->port_base + port, 0x1f, 0x0);//RTL page reg
				msleep(100);
				//dev_dbg(ds->dev, "[%s:%d] Read port:%d(RTL), RTL_physr:0x%x\n",__func__,__LINE__,port,RTL_physr);
				dev_info(ds->dev,"[%s:%d] Read port:%d(RTL), RTL_physr:0x%x\n",__func__,__LINE__,port,RTL_physr);
				/*bit 5:bit 4
					11:Reserved
					10:1000Mbps
					01:100Mbps
					00:10Mbps
				*/
				if(RTL_physr & 0x20) 
					ret = ret;
				else if(RTL_physr & 0x10) //100M
					ret &= ~(LPA_1000FULL | LPA_1000HALF);
				else {//10M
					ret &= ~(LPA_1000FULL | LPA_1000HALF);
					ret &= ~(LPA_100FULL | LPA_100HALF);
				}
			}
		}
	}
	if (ret < 0)
		ps->ports[port].mib[DSA_LOOP_PHY_READ_ERR].val++;
	else
		ps->ports[port].mib[DSA_LOOP_PHY_READ_OK].val++;

	return ret;

}
#else
static int dsa_loop_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;
	int tmp = regnum;

	/*The RTL PHYSR register address is 0x1A, not 0x12
		cause the link speed error, show the "Downshift ..."
	*/
	if(tmp == 0x12)
		tmp = 0x1a;
			
	//ret = mdiobus_read_nested(bus, ps->port_base + port, regnum);
	ret = mdiobus_read_nested(bus, ps->port_base + port, tmp);
	
	if (ret < 0)
		ps->ports[port].mib[DSA_LOOP_PHY_READ_ERR].val++;
	else
		ps->ports[port].mib[DSA_LOOP_PHY_READ_OK].val++;
	return ret;
}
#endif
static int dsa_loop_phy_write(struct dsa_switch *ds, int port,
			      int regnum, u16 value)
{
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	int ret;
	
	ret = mdiobus_write_nested(bus, ps->port_base + port, regnum, value);
	
	if (ret < 0)
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_ERR].val++;
	else
		ps->ports[port].mib[DSA_LOOP_PHY_WRITE_OK].val++;
	
	return ret;
}

static int dsa_loop_port_bridge_join(struct dsa_switch *ds, int port,
				     struct dsa_bridge bridge,
				     bool *tx_fwd_offload,
				     struct netlink_ext_ack *extack)
{
	dev_dbg(ds->dev, "%s: port: %d, bridge: %s\n",
		__func__, port, bridge.dev->name);
	return 0;
}

static void dsa_loop_port_bridge_leave(struct dsa_switch *ds, int port,
				       struct dsa_bridge bridge)
{
	dev_dbg(ds->dev, "%s: port: %d, bridge: %s\n",
		__func__, port, bridge.dev->name);
}

static void dsa_loop_port_stp_state_set(struct dsa_switch *ds, int port,
					u8 state)
{
	dev_dbg(ds->dev, "%s: port: %d, state: %d\n",
		__func__, port, state);
}

static int dsa_loop_port_vlan_filtering(struct dsa_switch *ds, int port,
					bool vlan_filtering,
					struct netlink_ext_ack *extack)
{
	dev_dbg(ds->dev, "%s: port: %d, vlan_filtering: %d\n",
		__func__, port, vlan_filtering);
	return 0;
}

static int dsa_loop_port_vlan_add(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan,
				  struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct dsa_loop_priv *ps = ds->priv;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;

	if (vlan->vid >= ARRAY_SIZE(ps->vlans))
		return -ERANGE;

	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	vl = &ps->vlans[vlan->vid];

	vl->members |= BIT(port);
	if (untagged)
		vl->untagged |= BIT(port);
	else
		vl->untagged &= ~BIT(port);

	dev_dbg(ds->dev, "%s: port: %d vlan: %d, %stagged, pvid: %d\n",
		__func__, port, vlan->vid, untagged ? "un" : "", pvid);

	if (pvid)
		ps->ports[port].pvid = vlan->vid;

	return 0;
}

static int dsa_loop_port_vlan_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct dsa_loop_priv *ps = ds->priv;
	u16 pvid = ps->ports[port].pvid;
	struct mii_bus *bus = ps->bus;
	struct dsa_loop_vlan *vl;
	
	/* Just do a sleeping operation to make lockdep checks effective */
	mdiobus_read(bus, ps->port_base + port, MII_BMSR);

	vl = &ps->vlans[vlan->vid];

	vl->members &= ~BIT(port);
	if (untagged)
		vl->untagged &= ~BIT(port);

	if (pvid == vlan->vid)
		pvid = 1;

	dev_dbg(ds->dev, "%s: port: %d vlan: %d, %stagged, pvid: %d\n",
		__func__, port, vlan->vid, untagged ? "un" : "", pvid);
	ps->ports[port].pvid = pvid;
	
	return 0;
}
#ifdef PORT_MTU_ENABLE
static int dsa_loop_port_change_mtu(struct dsa_switch *ds, int port,
				    int new_mtu)
{
	struct dsa_loop_priv *priv = ds->priv;
	
	priv->ports[port].mtu = new_mtu;
	
	return 0;
}

static int dsa_loop_port_max_mtu(struct dsa_switch *ds, int port)
{
	return ETH_MAX_MTU;
}
#endif
static void dsa_loop_fixed_link_update(struct dsa_switch *ds, int port,
						struct fixed_phy_status *st)
{
	dev_dbg(ds->dev,"%s\n", __func__);
	st->link = 1;
	st->speed = SPEED_1000;
	st->duplex = DUPLEX_FULL;
	st->pause = 0;
	st->asym_pause = 0;
}

static int dsa_loop_port_enable(struct dsa_switch *ds, int port, struct phy_device *phy)
{
	dev_dbg(ds->dev,"%s: port: %d, ebable\n", 
		__func__,port);
	return 0;
}

static void dsa_loop_port_disable(struct dsa_switch *ds, int port)
{
	dev_dbg(ds->dev,"%s: port: %d, disable\n", 
		__func__,port);
}
#if 1
static void dsa_loop_phylink_get_caps(struct dsa_switch *dsa, int port,
										struct phylink_config *config)
{
	bitmap_fill(config->supported_interfaces, PHY_INTERFACE_MODE_MAX);
	__clear_bit(PHY_INTERFACE_MODE_NA, config->supported_interfaces);	
	config->mac_capabilities = ~0;

}
#endif

static struct dsa_port *ax_preferred_default_local_cpu_port(struct dsa_switch *ds)
{
	struct dsa_port *cpu_dp = dsa_to_port(ds,4);

	if(dsa_port_is_cpu(cpu_dp))
		return cpu_dp;
	
	return NULL;
}

static const struct dsa_switch_ops dsa_loop_driver = {
	.get_tag_protocol	= dsa_loop_get_protocol,
	.setup			= dsa_loop_setup,
	.get_strings		= dsa_loop_get_strings,
	.get_ethtool_stats	= dsa_loop_get_ethtool_stats,
	.get_sset_count		= dsa_loop_get_sset_count,
#ifdef AX_DSA_LOOP_PHY_READ
	.phy_read		= ax_dsa_loop_phy_read,
#else
	.phy_read		= dsa_loop_phy_read,
#endif
	.phy_write		= dsa_loop_phy_write,
	.teardown		= dsa_loop_teardown,
	.get_ethtool_phy_stats	= dsa_loop_get_ethtool_stats,
	.port_bridge_join	= dsa_loop_port_bridge_join,
	.port_bridge_leave	= dsa_loop_port_bridge_leave,
	.port_stp_state_set	= dsa_loop_port_stp_state_set,
	.port_vlan_filtering	= dsa_loop_port_vlan_filtering,
	.port_vlan_add		= dsa_loop_port_vlan_add,
	.port_vlan_del		= dsa_loop_port_vlan_del,
#ifdef PORT_MTU_ENABLE
	.port_change_mtu	= dsa_loop_port_change_mtu,
	.port_max_mtu		= dsa_loop_port_max_mtu,
#endif
	.fixed_link_update  = dsa_loop_fixed_link_update,
	.port_enable		= dsa_loop_port_enable,
	.port_disable	    = dsa_loop_port_disable,
	.phylink_get_caps	= dsa_loop_phylink_get_caps,
	.preferred_default_local_cpu_port = ax_preferred_default_local_cpu_port,
};

static int asix_dsa_loop_drv_probe(struct mdio_device *mdiodev)
{
	struct dsa_loop_pdata *pdata = mdiodev->dev.platform_data;
	struct dsa_loop_priv *ps;
	struct dsa_switch *ds;	
	int ret = 0;

	if (!pdata)
		return -ENODEV;

    dev_info(&mdiodev->dev, "%s: Enable ports: 0x%0x, 20250407\n",pdata->name, pdata->enabled_ports);
	
    ds = devm_kzalloc(&mdiodev->dev, sizeof(struct dsa_switch), GFP_KERNEL);
	
	if (!ds)
		return -ENOMEM;

	ds->dev = &mdiodev->dev;	
	ds->num_ports = 5;	
	ds->ops = &dsa_loop_driver;
	
	ps = devm_kzalloc(&mdiodev->dev, sizeof(struct dsa_loop_priv), GFP_KERNEL);
	
	if (!ps)
		return -ENOMEM;
		
	ps->netdev = dev_get_by_name(&init_net, pdata->netdev);
	if (!ps->netdev)
		return -EPROBE_DEFER;

	pdata->cd.netdev[DSA_LOOP_CPU_PORT] = &ps->netdev->dev;
	ds->priv = ps;
	ds->dev = &mdiodev->dev;
	ps->bus = mdiodev->bus;

	dev_set_drvdata(&mdiodev->dev, ds);	
	
	ret = dsa_register_switch(ds);
	if (!ret)
		dev_info(&mdiodev->dev, "%s: 0x%0x\n",
			 pdata->name, pdata->enabled_ports);
			 
	return ret;
}

static void asix_dsa_loop_drv_remove(struct mdio_device *mdiodev)
{
    struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct dsa_loop_priv *ps;
	
	if (!ds)
		return;

	ps = ds->priv;
	
	dsa_unregister_switch(ds);
	dev_put(ps->netdev);	
}

struct mdio_driver ax_dsa_drv = {
	.mdiodrv.driver	= {		
		.name	= "ax_switch_mdio",
	},
	.probe	= asix_dsa_loop_drv_probe,
	.remove	= asix_dsa_loop_drv_remove,
};

MODULE_LICENSE("GPL");