/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *****************************************************************************/

#ifndef __AX_PTP_H
#define __AX_PTP_H

/* INCLUDE FILE DECLARATIONS */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ptp_clock_kernel.h>

#include "../libxdma.h"
#include "ax_switch.h"

/* NAMING CONSTANT AND TYPE DECLARATIONS */
#define AX_TX_PTPHDR_OFFSET_L3		42
#define AX_RX_PTPHDR_OFFSET_L3		28
#define AX_TX_PTPHDR_OFFSET_L2		14
#define AX_RX_PTPHDR_OFFSET_L2		0
#define AX_ETHTYPE_OFFSET		12
#define AX_IP_PROTO_OFFSET		9
#define AX_UDP_PORT_OFFSET		22
#define AX_PTP_EVENT_PORT_NUM		0x13F

/* EXPORTED SUBPROGRAM SPECIFICATIONS */
void ax_ptp_init(struct net_device *netdev);
void ax_ptp_remove(struct net_device *netdev);

#endif /* End of __AX_PTP_H */
